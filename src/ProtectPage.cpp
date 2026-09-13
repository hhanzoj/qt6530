#include <spl/debug.h>
#include <vt6530/ProtectPage.h>

void ProtectPage::Home(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
}

void ProtectPage::End(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
}

/** 0x09
 * PROTECT
 *  Move to the first position of the next 
 *  unprotected field.
 */
void ProtectPage::Tab(Page *page, int inc)
{
	ASSERT_MEM(page, sizeof(Page));

	int newX = page->ScanForNextField(page->m_cursorPos.m_column, page->m_cursorPos.m_row, inc);
	if (newX >= 0)
	{
		newX = page->ScanForUnprotectField(newX, page->m_cursorPos.m_row, inc);
		if (newX >= 0)
		{
			page->m_cursorPos.m_column = newX;
			return;
		}
	}
	int y = page->m_cursorPos.m_row+inc;

	/*  FASE 03, dos defectos en este bucle, los dos solo hacia atras:
	 *
	 *  1. el contador avanzaba con "qpr += inc". Con inc = -1 se va a
	 *     negativo y la condicion qpr < GetNumRows() nunca falla: si no hay
	 *     ningun campo desprotegido, el bucle no termina.
	 *
	 *  2. la fila se barria desde la columna 0 incluso hacia atras, y
	 *     ScanForUnprotectField con c == 0 e inc < 0 envuelve a la ULTIMA
	 *     columna de esa misma fila. De ahi que la flecha arriba terminara
	 *     en la columna 80.
	 *
	 *  Hacia atras hay que arrancar por el final de la fila.             */
	const int primeraCol = (inc > 0) ? 0 : page->GetNumColumns() - 1;
	for (int qpr = 0; qpr < page->GetNumRows(); qpr++)
	{
		if (inc > 0)
		{
			if (y >= page->GetNumRows())
			{
				y = 0;
			}
		}
		else
		{
			if (y < 0)
			{
				y = page->GetNumRows()-1;
			}
		}
		ASSERT(y < page->GetNumRows());
		newX = page->ScanForUnprotectField(primeraCol, y, inc);
		if (newX >= 0)
		{
			page->m_cursorPos.m_column = newX;
			page->m_cursorPos.m_row = y;
			ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
			ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
			return;
		}
		//y++;
		y += inc;
	}
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void ProtectPage::ClearToEOL(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	int x = page->m_bufferPos.m_column;
	int y = page->m_bufferPos.m_row;
	int attr = page->GetCell(x, y)->GetAttributes();
	
	if (page->GetCell(x, y)->IsStartField())
	{
		// clear the video attributes for the field
		attr = VID_NORMAL;
		page->GetCell(x, y)->ClearVideoAttribs();
		x++;
	}
	attr |= CHAR_CELL_DIRTY /*| (int)' '*/;
	while (x < page->GetNumColumns() && (!page->GetCell(x, y)->IsStartField()))
	{
		page->GetCell(x, y)->Clear(attr);
		x++;
	}
	if (x != page->GetNumColumns()) //((page->mem[y][x] & CHAR_START_FIELD) != 0)
	{
		return;
	}
	for (y = y+1; y < page->GetNumRows(); y++)
	{
		x = 0;
		while (x < page->GetNumColumns() && (!page->GetCell(x, y)->IsStartField()))
		{
			page->GetCell(x, y)->Clear(attr);
			x++;
		}
		if (x < page->GetNumColumns())
		{
			if (page->GetCell(x, y)->IsStartField())
			{
				break;
			}
		}
	}
	ASSERT(page->m_bufferPos.m_row < page->GetNumRows());
	ASSERT(page->m_bufferPos.m_column < page->GetNumColumns());
}

void ProtectPage::ClearToEOP(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	Cursor *cursor = &page->m_bufferPos;
	
	ASSERT_PTR(cursor);

	int attr = page->GetWriteAttr() | page->GetPriorAttr();
	
	for (int c = cursor->m_column; c < page->GetNumColumns(); c++)
	{
		PageCell *cell = page->GetCell(c, cursor->m_row);
		if (cell->IsUnprotect())
		{
			cell->Clear (CHAR_CELL_DIRTY | attr | DAT_UNPROTECT);
		}
	}
	for (int r = cursor->m_row+1; r < page->GetNumRows(); r++)
	{
		for (int c = 0; c < page->GetNumColumns(); c++)
		{
			PageCell *cell = page->GetCell(c, r);
			if (cell->IsUnprotect())
			{
				cell->Clear(CHAR_CELL_DIRTY | attr | DAT_UNPROTECT);
			}
		}
	}
}

void ProtectPage::CursorLeft(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	
	if (page->m_cursorPos.m_column == 0)
	{
		if (page->m_cursorPos.m_row == 0)
		{
			page->m_cursorPos.m_row = page->GetNumRows()-1;
		}
		else
		{
			page->m_cursorPos.m_row--;
			page->m_cursorPos.AdjustRow();
		}
		page->m_cursorPos.m_column = page->GetNumColumns()-1;
	}
	else
	{
		page->m_cursorPos.m_column--;
		page->m_cursorPos.AdjustCol();
	}
	if (!page->GetCell(&page->m_cursorPos)->IsUnprotect())
	{
		Tab(page, -1);
	}
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void ProtectPage::ReadBuffer(StringBuffer *accum, Page *page, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	bool writeCr = false;
	StringBuffer sb;
	
	ASSERT(startRow >= 0 && endRow < page->GetNumRows());
	ASSERT(startCol >= 0 && endCol < page->GetNumColumns());
	
	accum->SetLength(0);
	
	for (int y = startRow; y <= endRow; y++)
	{
		bool fieldStarted = false;
		
		for (int x = startCol; x < endCol; x++)
		{
			if (page->GetCell(x, y)->IsStartField())
			{
				if ( fieldStarted )
				{
					sb.Trim();
					if (sb.Length() > 0)
					{
						accum->Append(sb.GetChars());
					}
					sb.SetLength(0);
					writeCr = false;
				}
				if ( (page->GetCell(x+1, y)->AsInt() & reqMask) != 0)
				{
					fieldStarted = true;
					accum->Append((char)17);
					accum->Append((char)(y + 0x20));
					accum->Append((char)(x + 0x21));
				}
				else
				{
					fieldStarted = false;
				}
			}
			else if ((page->GetCell(x, y)->AsInt() & (reqMask)) != 0 && fieldStarted)
			{
				sb.Append(page->GetCell(x, y)->Get());
				writeCr = true;
			}
			else if (fieldStarted)
			{
				sb.Trim();
				if (sb.Length() > 0)
				{
					accum->Append(sb.GetChars());
				}
				sb.SetLength(0);
				writeCr = false;
				fieldStarted = false;
			}
		}
		if (writeCr == true)
		{
			sb.Trim();
			if (sb.Length() > 0)
			{
				accum->Append(sb.GetChars());
			}
			writeCr = false;
			sb.SetLength(0);
		}
	}
	accum->Append((char)4);
}

void ProtectPage::WriteChar(Page *page, Cursor *bufferPos, int c)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT(bufferPos->m_row < page->GetNumRows());
	ASSERT(bufferPos->m_column < page->GetNumColumns());

	if ( (c & 0xFF) < 32)
	{
		SharedProtocol::WriteChar(page, bufferPos, c);
		return;
	}
	PageCell *cell = page->GetCell(bufferPos);

	/*  FASE 03 -- aca habia un ASSERT(!cell->IsStartField()).
	 *
	 *  La suposicion era que el host nunca escribe un caracter encima de
	 *  una celda de inicio de campo. VIEWSYS lo hace todo el tiempo: en
	 *  cada refresco reescribe la pantalla entera y el cursor de escritura
	 *  pasa por celdas que ya son inicio de campo. Con la GUI conectada a
	 *  rci3 la asercion inundaba la consola varias veces por segundo.
	 *
	 *  El dibujo sale bien igual, porque el SetAttributes de abajo hace OR
	 *  con los atributos que ya tenia la celda y conserva CHAR_START_FIELD.
	 *  Queda una pregunta abierta para el manual: si escribir encima de un
	 *  inicio de campo deberia BORRAR esa marca en vez de conservarla. Con
	 *  lo observado no se puede decidir, y conservarla es lo que produce la
	 *  pantalla correcta.                                                 */
	cell->Set(0xFF & c);
	cell->SetAttributes( (c & ~0xFF) | cell->GetAttributes() | page->GetWriteAttr() | CHAR_CELL_DIRTY | page->GetPriorAttr() );
	
	bufferPos->m_column++;
	bufferPos->AdjustCol();
	
	page->GetCell(bufferPos)->SetDirty(true);
}

/*  FASE 04 -- insertar y borrar caracter en modo protegido.
 *
 *  Igual que en SharedProtocol pero el limite es el CAMPO, no la fila: se
 *  corre hasta la celda anterior al proximo inicio de campo, o hasta el
 *  final de la fila si no hay otro. Correr mas alla pisaria el campo de al
 *  lado, que en una pantalla de formulario es de otra cosa.
 *
 *  El caracter que entra o el hueco que queda toman los atributos de la
 *  celda vecina, para que no aparezca un espacio con otro video en el medio
 *  de un campo.                                                            */
static int FinDelCampo(Page *page, int fila, int desde)
{
	const int cols = page->GetNumColumns();
	for (int c = desde + 1; c < cols; c++)
	{
		if (page->GetCell(c, fila)->IsStartField())
		{
			return c;          /* exclusivo: el inicio de campo no se toca */
		}
	}
	return cols;
}

void ProtectPage::InsertChar(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	const int fila  = page->m_cursorPos.m_row;
	const int desde = page->m_cursorPos.m_column;
	const int hasta = FinDelCampo(page, fila, desde);

	for (int c = hasta - 1; c > desde; c--)
	{
		page->GetCell(c, fila)->Set(page->GetCell(c - 1, fila));
		page->GetCell(c, fila)->SetDirty(true);
	}
	page->GetCell(desde, fila)->Set(' ');
	page->GetCell(desde, fila)->SetDirty(true);
}

void ProtectPage::DeleteChar(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	const int fila  = page->m_cursorPos.m_row;
	const int desde = page->m_cursorPos.m_column;
	const int hasta = FinDelCampo(page, fila, desde);

	for (int c = desde; c < hasta - 1; c++)
	{
		page->GetCell(c, fila)->Set(page->GetCell(c + 1, fila));
		page->GetCell(c, fila)->SetDirty(true);
	}
	if (hasta - 1 >= desde)
	{
		page->GetCell(hasta - 1, fila)->Set(' ');
		page->GetCell(hasta - 1, fila)->SetDirty(true);
	}
}

void ProtectPage::Linefeed(Page *page)
{
	Tab(page, 1);
}

void ProtectPage::ValidateCursorPos(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
	
	if ( !page->GetCell(&page->m_cursorPos)->IsUnprotect() )
	{
		Tab(page, 1);
	}
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void ProtectPage::SetCursor(Page *page, int row, int col)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	page->m_cursorPos.m_row = row;
	page->m_cursorPos.m_column = col;
	ValidateCursorPos(page);
	page->GetCell(&page->m_cursorPos)->SetDirty(true);

	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void ProtectPage::ClearBlock(Page *page, int startRow, int startCol, int endRow, int endCol)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	int value = CHAR_CELL_DIRTY | ' ';
	int mask = MASK_FIELD ^ CHAR_START_FIELD;
	PageCell *cell;

	for (int y = startRow; y <= endRow; y++)
	{
		for (int x = startCol; x <= endCol; x++)
		{
			cell = page->GetCell(x, y);
			cell->Clear( cell->GetAttributes() & mask | CHAR_CELL_DIRTY );
		}
	}
	ValidateCursorPos(page);
}

/*  FASE 03 -- las flechas verticales en modo protegido.
 *
 *  Antes eran, literalmente, Tab(page, 1) y Tab(page, -1): no bajaban ni
 *  subian, saltaban al campo desprotegido siguiente o anterior y ademas
 *  perdian la columna. En una pantalla como la de TEDIT, donde cada linea
 *  es un campo, "abajo" parecia funcionar de casualidad -- caia en la
 *  primera posicion del campo de abajo -- y "arriba" no funcionaba porque
 *  el Tab hacia atras estaba roto (ver el comentario de Tab).
 *
 *  Lo que hace ahora: se mueve una fila en la direccion pedida
 *  CONSERVANDO la columna, y si la celda de destino esta protegida sigue
 *  buscando en esa misma direccion. Si en ninguna fila la columna esta
 *  disponible, recien ahi cae en el Tab de antes.
 *
 *  ATENCION: esto es lo razonable y lo que hace usable a TEDIT, pero NO
 *  esta confirmado contra el manual. La otra lectura posible es que en un
 *  6530 la flecha vertical deba ir siempre al campo de arriba o abajo sin
 *  conservar la columna, que es lo que hacia el codigo original. Si
 *  aparece el manual y dice eso, se vuelve.                              */
static void MoverVertical(ProtectPage *self, Page *page, Cursor *cursor,
                          int inc, void (*tab)(ProtectPage *, Page *, int))
{
	const int filas = page->GetNumRows();
	const int col   = cursor->m_column;

	page->GetCell(cursor)->SetDirty(true);

	int y = cursor->m_row;
	for (int intentos = 0; intentos < filas; intentos++)
	{
		y += inc;
		if (y >= filas) y = 0;
		if (y < 0)      y = filas - 1;

		if (page->GetCell(col, y)->IsUnprotect())
		{
			cursor->m_row = y;
			page->GetCell(cursor)->SetDirty(true);
			return;
		}
	}

	/*  Ninguna fila tiene esa columna desprotegida: se cae al comportamiento
	 *  de siempre, que al menos deja el cursor en un campo escribible. */
	tab(self, page, inc);
	page->GetCell(&page->m_cursorPos)->SetDirty(true);
}

static void LlamarTab(ProtectPage *self, Page *page, int inc)
{
	self->Tab(page, inc);
}

void ProtectPage::ArrowDown(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	MoverVertical(this, page, cursor, 1, LlamarTab);
}

void ProtectPage::ArrowUp(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	MoverVertical(this, page, cursor, -1, LlamarTab);
}

void ProtectPage::ArrowLeft(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(cursor)->SetDirty(true);
	cursor->m_column--;
	cursor->AdjustCol();
	if (!page->GetCell(cursor)->IsUnprotect())
	{
		Tab(page, -1);
	}
	page->GetCell(cursor)->SetDirty(true);
}

void ProtectPage::ArrowRight(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(cursor)->SetDirty(true);
	cursor->m_column++;
	cursor->AdjustCol();
	if (!page->GetCell(cursor)->IsUnprotect())
	{
		Tab(page, 1);
	}
	page->GetCell(cursor)->SetDirty(true);
}

void UnprotectPage::Tab(int inc)
{
	/** 0x09
	 * 
	 * UNPROTECT MODE
	 *  Move the the next tab stop on the row.  If the
	 *  cursor is past the last tab stop, move to 
	 *  column 1 of the next row.
	 */
}

void UnprotectPage::ClearToEOL(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	for (int x = page->m_cursorPos.m_column; x < page->GetNumColumns(); x++)
	{
		page->GetCell(x, page->m_cursorPos.m_row)->ClearTo(' ');
	}
}

void UnprotectPage::ReadBuffer(StringBuffer *accum, Page *page, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	for (int y = startRow-1; y < endRow-1; y++)
	{
		for (int x = startCol-1; x < endCol-1; x++)
		{
			accum->Append( page->GetCell(x, y)->Get() );
		}
		accum->Append((char)13);
	}
}
