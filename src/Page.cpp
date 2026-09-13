#include <spl/debug.h>
#include <vt6530/Page.h>

PageCell::PageCell() 
{ 
	memset(this, 0, sizeof(PageCell)); 
}

PageCell::~PageCell() 
{
}

void PageCell::SetAttributes( int attribs )
{
	m_bits = attribs >> 8;
	ASSERT( (m_attribs.m_vidNormal != 0) == ((attribs & VID_NORMAL) != 0) );
	ASSERT( (m_attribs.m_vidBlinking != 0) == ((attribs & VID_BLINKING) != 0) );
	ASSERT( (m_attribs.m_vidReverse != 0) == ((attribs & VID_REVERSE) != 0) );
	ASSERT( (m_attribs.m_vidInvis != 0) == ((attribs & VID_INVIS) != 0) );
	ASSERT( (m_attribs.m_vidUnderline != 0) == ((attribs & VID_UNDERLINE) != 0) );
	ASSERT( (m_attribs.m_datMdt != 0) == ((attribs & DAT_MDT) != 0) );
	ASSERT( (m_attribs.m_datAutotab != 0) == ((attribs & DAT_AUTOTAB) != 0) );
	ASSERT( (m_attribs.m_datUnprotect != 0) == ((attribs & DAT_UNPROTECT) != 0) );
	ASSERT( (m_attribs.m_keyUpshift != 0) == ((attribs & KEY_UPSHIFT) != 0) );
	ASSERT( (m_attribs.m_keyKbOnly != 0) == ((attribs & KEY_KB_ONLY) != 0) );
	ASSERT( (m_attribs.m_charStartField != 0) == ((attribs & CHAR_START_FIELD) != 0) );
	ASSERT( (m_attribs.m_charDirtyCell != 0) == ((attribs & CHAR_CELL_DIRTY) != 0) );
}

void PageCell::ClearVideoAttribs()
{
	m_attribs.m_vidNormal = true;
	m_attribs.m_vidBlinking = 0;
	m_attribs.m_vidReverse = 0;
	m_attribs.m_vidInvis = 0;
	m_attribs.m_vidUnderline = 0;
	m_attribs.m_charDirtyCell = 1;
}


Page::Page(int numRows, int numCols)
	: m_cursorPos(numRows, numCols), 
	m_bufferPos(numRows, numCols),
	m_fields(),
	m_unprotectFields()
{
	ASSERT(numRows > 0 && numCols > 0);
	m_numColumns = numCols;
	m_numRows = numRows;

	if ( NULL == (m_cells = new PageCell[m_numRows * m_numColumns]) )
	{
		throw OutOfMemoryException();
	}
	memset(m_cells, 0, sizeof(PageCell) * m_numRows * m_numColumns);
	//m_mem = new int *[numRows];
	//for (int x = 0; x < m_numRows; x++)
	//{
	//	m_mem[x] = new int[m_numColumns];
	//}

	m_chbuf[1] = '\0';

	m_writeAttr = VID_NORMAL;
	m_priorAttr = VID_NORMAL;
	m_insertMode = INSERT_INSERT;
	
	m_cursorBlock = true;

	Init();
}

Page::~Page()
{
	//ASSERT_MEM(m_mem, m_numRows*sizeof(int *));
	ASSERT_MEM(m_cells, sizeof(PageCell) * m_numRows * m_numColumns);

	//for (int x = 0; x < m_numRows; x++)
	//{
	//	ASSERT_MEM(m_mem[x], m_numColumns*sizeof(int));
	//	delete[] m_mem[x];
	//}
	//delete[] m_mem;
	delete[] m_cells;
}

void Page::Init()
{	
	ASSERT_MEM(m_cells, sizeof(PageCell) * m_numRows * m_numColumns);

	m_writeAttr = VID_NORMAL;
	m_priorAttr = VID_NORMAL;
	m_cursorPos.Clear();
	m_bufferPos.Clear();
	m_fields.Clear();
	m_unprotectFields.Clear();

	for (int x = 0; x < m_numRows; x++)
	{
		for (int q = 0; q < m_numColumns; q++)
		{
			PageCell *cell = GetCell(q, x);
			cell->ClearTo(' ');// = (int)' ' | CHAR_CELL_DIRTY | m_writeAttr;
			cell->SetDirty(true);
			cell->SetNormal(true);
		}
	}
}

void Page::InsertChar(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->InsertChar(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::DeleteChar(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->DeleteChar(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::Tab(PageProtocol *mode, int inc)
{
	ASSERT_PTR(mode);
	mode->Tab(this, inc);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::Backspace(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->Backspace(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::CursorUp(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->CursorUp(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::CursorDown(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->CursorDown(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::CursorLeft(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->CursorLeft(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::CursorRight(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->CursorRight(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::ClearPage()
{
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
	
	ASSERT_MEM(m_cells, m_numRows*m_numColumns*sizeof(PageCell));

	for (int r = 0; r < m_numRows * m_numColumns; r++)
	{
		m_cells[r].Clear();
		m_cells[r].SetAttributes(m_writeAttr | m_priorAttr);
	}
}

void Page::WriteBuffer(PageProtocol *mode, const char *text)
{
	ASSERT_PTR(mode);
	mode->WriteBuffer(this, text);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::WriteCursor(PageProtocol *mode, const char *text)
{
	ASSERT_PTR(mode);
	mode->WriteCursor(this, text);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::WriteCursorLocal(PageProtocol *mode, const char *text)
{
	ASSERT_PTR(mode);

	int len = strlen(text);

	for (int x = 0; x < len; x++)
	{
		mode->WriteChar(this, &m_cursorPos, text[x] | DAT_MDT);
	}
}

void Page::CarageReturn(PageProtocol *mode)
{
	ASSERT_PTR(mode);
	mode->CarageReturn(this);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::SetCursor(PageProtocol *mode, int row, int col)
{
	ASSERT_PTR(mode);
	mode->SetCursor(this, row, col);
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::SetBuffer(int row, int col)
{
	m_bufferPos.m_row = row;
	m_bufferPos.m_column = col;
	ASSERT(m_bufferPos.m_row < m_numRows);
	ASSERT(m_bufferPos.m_column < m_numColumns);
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

void Page::WriteField(int c)
{
	ValidateMem();

	// mark the field
	PageCell *cell = GetCell(&m_bufferPos);
	m_fields.Add( cell );
	cell->Set(c & MASK_CHAR);
	cell->SetAttributes((c & MASK_FIELD) | CHAR_START_FIELD | CHAR_CELL_DIRTY | m_writeAttr | m_priorAttr);
	if ( cell->IsUnprotect() )
	{
		m_unprotectFields.Add( cell );
	}
	// The field start char is not updatable.  For unprotect fields, the char to the
	// right is the first updatable char.
	cell->SetUnprotect(false);

	m_bufferPos.m_column++;
	m_bufferPos.AdjustCol();
			
	int col;
	
	// detect any previous field on the line and extend
	// its attributes upto this one
	for (col = m_bufferPos.m_column; col < m_numColumns; col++)
	{
		if ( GetCell(col, m_bufferPos.m_row)->IsStartField() )
		{
			return;
		}
		GetCell(col, m_bufferPos.m_row)->SetAttributes( (c & MASK_FIELD) | CHAR_CELL_DIRTY | m_writeAttr | m_priorAttr );
		ASSERT( !GetCell(col, m_bufferPos.m_row)->IsStartField() );
	}
	for (int y = m_bufferPos.m_row + 1; y < m_numRows; y++)
	{
		for (int x = 0; x < m_numColumns; x++)
		{
			PageCell *cell = GetCell(x, y);
			if (cell->IsStartField())
			{
				return;
			}
			cell->SetAttributes((c & MASK_FIELD) | CHAR_CELL_DIRTY | m_writeAttr | m_priorAttr);
			ASSERT( !cell->IsStartField() );
		}
	}
}

void Page::ResetMDTs()
{
	ValidateMem();

	for (int r = 0; r < m_numRows * m_numColumns; r++)
	{
		m_cells[r].SetMDT(false);
	}
}

void Page::GetStartFieldASCII(StringBuffer *buf)
{
	ValidateMem();

	for (int r = 0; r < m_numRows; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			int c2 = c+1;
			int r2 = r;
			if (c2 >= m_numColumns)
			{
				c2 = 0;
				r2++;
				if (r2 >= m_numRows)
				{
					r2 = 0;
				}
			}
			if (GetCell(c, r)->IsStartField() && GetCell(c2, r2)->IsUnprotect())
			{
				buf->Append((char)(r + 0x20));
				buf->Append((char)(c + 0x21));
				return;
			}
		}
	}
	buf->Append("  ");
}

void Page::ForceDirty()
{
	ValidateMem();
	
	for (int r = 0; r < m_numRows * m_numColumns; r++)
	{
		m_cells[r].SetDirty(true);
	}
}

void Page::ScrollPageUp()
{
	ValidateMem();

	for (int r = 0; r < m_numRows -1; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			GetCell(c, r)->Set(GetCell(c, r+1));
		}
	}
	for (int c = 0; c < m_numColumns; c++)
	{
		GetCell(c, m_numRows-1)->Set(' ');
		GetCell(c, m_numRows-1)->SetAttributes(m_writeAttr | m_priorAttr);
	}
	ASSERT(m_cursorPos.m_row < m_numRows);
	ASSERT(m_cursorPos.m_column < m_numColumns);
}

/*  FASE 04 -- insertar y borrar linea.
 *
 *  Mueven el CONTENIDO de las filas, no su estructura de campos. Es una
 *  decision deliberada: los inicios de campo estan ademas en m_fields y
 *  m_unprotectFields como punteros a celda, asi que correr los atributos de
 *  campo dejaria esas listas apuntando a cualquier lado. El reparto de
 *  campos lo define el host; lo que la tecla mueve es el texto.
 *
 *  Para una pantalla como la de TEDIT, donde cada fila es un campo igual al
 *  de al lado, el resultado es indistinguible de mover la fila entera.
 *
 *  ATENCION: no esta contrastado con el manual. Es lo que hace falta para
 *  que Ctrl+Insertar y Ctrl+Suprimir sirvan en un editor, y lo que no puede
 *  romper nada.                                                            */
void Page::InsertLine(int row)
{
	ValidateMem();
	if (row < 0 || row >= m_numRows) return;

	for (int r = m_numRows - 1; r > row; r--)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			GetCell(c, r)->Set(GetCell(c, r - 1)->Get());
		}
	}
	for (int c = 0; c < m_numColumns; c++)
	{
		GetCell(c, row)->Set(' ');
	}
}

void Page::DeleteLine(int row)
{
	ValidateMem();
	if (row < 0 || row >= m_numRows) return;

	for (int r = row; r < m_numRows - 1; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			GetCell(c, r)->Set(GetCell(c, r + 1)->Get());
		}
	}
	for (int c = 0; c < m_numColumns; c++)
	{
		GetCell(c, m_numRows - 1)->Set(' ');
	}
}

int Page::ScanForNextField(int c, int r, int inc)
{
	ASSERT(r < m_numRows);
	ASSERT(c < m_numColumns);
	ValidateMem();

	if (inc > 0)
	{
		for (int x = c; x < m_numColumns; x++)
		{
			if (GetCell(x, r)->IsStartField())
			{
				ASSERT(!GetCell(x, r)->IsUnprotect());
				return x;
			}
		}
	}
	else
	{
		if (c == 0)
		{
			c = m_numColumns-1;
			if (r == 0)
			{
				r = m_numRows-1;
			}
			else
			{
				r--;
			}
		}
		for (int x = c; x >= 0; x--)
		{
			if (GetCell(x, r)->IsStartField())
			{
				ASSERT(!GetCell(x, r)->IsUnprotect());
				return x;
			}
		}
	}
	return -1;
}

int Page::ScanForUnprotectField(int c, int r, int inc)
{
	ASSERT(r < m_numRows);
	ASSERT(c < m_numColumns);
	ValidateMem();

	if (inc > 0)
	{
		for (int x = c; x < m_numColumns; x++)
		{
			if (GetCell(x, r)->IsUnprotect())
			{
				ASSERT(!GetCell(x, r)->IsStartField());
				return x;
			}
		}
	}
	else
	{
		if (c == 0)
		{
			c = m_numColumns-1;
			if (r == 0)
			{
				r = m_numRows-1;
			}
			else
			{
				r--;
			}
		}
		for (int x = c; x >= 0; x--)
		{
			if (GetCell(x, r)->IsUnprotect())
			{
				ASSERT(!GetCell(x, r)->IsStartField());
				return x;
			}
		}			
	}
	return -1;
}

#if 0
static COLORREF brighter(COLORREF c)
{
	int r = GetRValue(c);
	r += (int)(r * .1F);
	int g = GetGValue(c);
	g += (int)(g * .1F);
	int b = GetBValue(c);
	b += (int)(b * .1F);
	return RGB((r>0xFF)?0xFF:r, (g>0xFF)?0xFF:g, (b>0xFF)?0xFF:b);
}

void Page::paint(PaintSurface *ps, char *statusLine)
{
	ASSERT_MEM(mem, numRows*sizeof(int *));

	COLORREF fgcolor = ps->getForeGroundColor();
	COLORREF bgcolor = ps->getBackGroundColor();
	COLORREF fgbright = brighter(fgcolor);
	HBRUSH foreground = CreateSolidBrush(fgcolor);
	HPEN pen = CreatePen(PS_SOLID, 1, ps->getForeGroundColor());
	HPEN revpen = CreatePen(PS_SOLID, 1, bgcolor);
	HBRUSH background = CreateSolidBrush(bgcolor);

	int charWidth = ps->getFontWidth();
	int charDescent = ps->getFontDescent();
	int charHeight = ps->getFontHeight()/*+charDescent*/;
	
	//HBRUSH fg, bg;
	boolean allClean = true;
	
	HPEN oldpen = ps->setPen(pen);
	ps->setPaintMode();

	for (int r = 0; r < numRows; r++)
	{
		ASSERT_MEM(mem[r], numColumns*sizeof(int));

		for(int c = 0; c < numColumns; c++)
		{
			int ch = mem[r][c];
			if ( (ch & CHAR_CELL_DIRTY) == 0)
			{
				continue;
			}
			mem[r][c] &= ~CHAR_CELL_DIRTY;
			allClean = false;
			//fg = foreground;
			//bg = background;

			setColors(ps, ch, fgcolor, bgcolor, fgbright, foreground, background, pen, revpen, r, c, charWidth, charHeight);
			// clear the part of the screen we want to change (fill rectangle)
			//ps->fillRect(c * charWidth, r * charHeight, charWidth, charHeight, bg);
			
			// draw the characters
			if ( (ch & VID_INVIS) == 0)
			{
				chbuf[0] = (char)(ch & MASK_CHAR);
				if (chbuf[0] != ' ')
				{
					ps->drawBytes(chbuf, 0, 1, c * charWidth, (r+1) * charHeight /*- charDescent*/);
				}
			}
			if(((ch & VID_UNDERLINE) != 0) && ((ch & CHAR_START_FIELD) == 0))
			{
				int liney = (r+1) * charHeight - charDescent/3;
				int linex = c * charWidth;
				ps->drawLine(linex, liney, linex + charWidth, liney);
			}
		}
	}
	if (allClean)
	{
		for (int y = 0; y < numRows; y++)
		{
			ASSERT_MEM(mem[y], numColumns*sizeof(int));
			for (int x = 0; x < numColumns; x++)
			{
				mem[y][x] |= CHAR_CELL_DIRTY;
			}
		}
		paint(ps, statusLine);
		return;
	}
	else
	{
		// draw cursor
		ps->setPaintXorMode();

		if (cursorBlock)
		{
			int ch = mem[cursorPos.row][cursorPos.column];
			//ps->fillRect( cursorPos.column * charWidth, 
			//			cursorPos.row * charHeight,
			//			charWidth, charHeight, foreground);
			setColors(ps, ch, bgcolor, fgcolor, bgcolor, background, foreground, revpen, pen, cursorPos.row, cursorPos.column, charWidth, charHeight);

			chbuf[0] = (char)(ch & MASK_CHAR);
			if (chbuf[0] != ' ')
			{
				ps->drawBytes(chbuf, 0, 1, cursorPos.column * charWidth, (cursorPos.row+1) * charHeight /*- charDescent*/);
			}
		}
		else
		{
			ps->drawLine(cursorPos.column * charWidth ,
					   (cursorPos.row) * charHeight + charHeight,
						cursorPos.column * charWidth + charWidth,
					   (cursorPos.row) * charHeight + charHeight); 
		}
		ps->setPaintMode();
		
		// draw the status line
		ps->setBkColor(bgcolor);
		ps->setTextColor(fgcolor);
		ps->fillRect(0, numRows * charHeight, charWidth*numColumns, charHeight, background);
		
		ASSERT(strlen(statusLine) >= numColumns);
		for (int x = 0; x < numColumns; x++)
		{
			int c = statusLine[x];
			if (c == 27)
			{
				int attr = statusLine[++x];
				c = statusLine[++x];
			}
			chbuf[0] = (char)(c & MASK_CHAR);
			ps->drawBytes(chbuf, 0, 1, x*charWidth, (numRows+1) * charHeight /*- charDescent*/);			
		}
	}
	ps->setPen( oldpen );
	DeleteObject( foreground );
	DeleteObject( pen );
	DeleteObject( revpen );
	DeleteObject( background );
}
#endif

#if defined(DEBUG) || defined(_DEBUG)
void Page::CheckMem() const
{
	DEBUG_NOTE_MEM_ALLOCATION(m_cells);
	m_fields.CheckMem();
	m_unprotectFields.CheckMem();
	//for (int x = 0; x < m_numRows; x++)
	//{
	//	DEBUG_NOTE_MEM_ALLOCATION(m_mem[x]);
	//}
}

void Page::ValidateMem() const
{
	ASSERT_MEM(m_cells, sizeof(PageCell) * m_numRows * m_numColumns);
	m_fields.ValidateMem();
	m_unprotectFields.ValidateMem();
	//ASSERT_MEM(m_mem, m_numRows*sizeof(int *));
	//for (int x = 0; x < m_numRows; x++)
	//{
	//	ASSERT_MEM(m_mem[x], m_numColumns*sizeof(int));
	//}
}
#endif
