#include <spl/debug.h>
#include <vt6530/SharedProtocol.h>
#include <vt6530/Keys.h>

void SharedProtocol::WriteBuffer(Page *page, const char *text)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	Write(&page->m_bufferPos, page, page->GetWriteAttr() | page->GetPriorAttr(), text);
}

void SharedProtocol::WriteCursor(Page *page, const char *text)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	Write(&(page->m_cursorPos), page, page->GetWriteAttr() | page->GetPriorAttr(), text);
}

void SharedProtocol::ArrowDown(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT_PTR(cursor);

	page->GetCell(cursor)->SetDirty(true);
	cursor->m_row++;
	cursor->AdjustRow();
	page->GetCell(cursor)->SetDirty(true);
}

void SharedProtocol::ArrowUp(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT_MEM(cursor, sizeof(Cursor));

	page->GetCell(cursor)->SetDirty(true);
	cursor->m_row--;
	cursor->AdjustRow();
	page->GetCell(cursor)->SetDirty(true);
}

void SharedProtocol::ArrowLeft(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT_PTR(cursor);

	page->GetCell(cursor)->SetDirty(true);
	cursor->m_column--;
	cursor->AdjustCol();
	page->GetCell(cursor)->SetDirty(true);
}

void SharedProtocol::ArrowRight(Page *page, Cursor *cursor)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT_PTR(cursor);

	page->GetCell(cursor)->SetDirty(true);
	cursor->m_column++;
	cursor->AdjustCol();
	page->GetCell(cursor)->SetDirty(true);
}

void SharedProtocol::Home(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
}

void SharedProtocol::End(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
}

void SharedProtocol::ValidateCursorPos(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
}

/*  FASE 04 -- insertar y borrar caracter.
 *
 *  Los dos cuerpos estaban vacios desde 2007, asi que las teclas Insertar y
 *  Suprimir no hacian nada. Se nota en cuanto se corrige un texto con TEDIT.
 *
 *  Fuera de modo protegido el limite es la linea: se corre desde el cursor
 *  hasta el final de la fila y el ultimo caracter se pierde o se rellena con
 *  un espacio, segun el caso. En modo protegido el limite es el campo, y de
 *  eso se encarga ProtectPage, que redefine las dos.                       */
void SharedProtocol::InsertChar(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	const int fila = page->m_cursorPos.m_row;
	const int desde = page->m_cursorPos.m_column;
	const int hasta = page->GetNumColumns();

	for (int c = hasta - 1; c > desde; c--)
	{
		page->GetCell(c, fila)->Set(page->GetCell(c - 1, fila));
		page->GetCell(c, fila)->SetDirty(true);
	}
	page->GetCell(desde, fila)->Set(' ');
	page->GetCell(desde, fila)->SetDirty(true);
}

void SharedProtocol::DeleteChar(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	const int fila = page->m_cursorPos.m_row;
	const int desde = page->m_cursorPos.m_column;
	const int hasta = page->GetNumColumns();

	for (int c = desde; c < hasta - 1; c++)
	{
		page->GetCell(c, fila)->Set(page->GetCell(c + 1, fila));
		page->GetCell(c, fila)->SetDirty(true);
	}
	page->GetCell(hasta - 1, fila)->Set(' ');
	page->GetCell(hasta - 1, fila)->SetDirty(true);
}

void SharedProtocol::Backspace(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	page->m_cursorPos.m_column--;
	page->m_cursorPos.AdjustCol();
	page->GetCell(&page->m_cursorPos)->Set(' ');

	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void SharedProtocol::Tab(Page *page, int inc)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
}

void SharedProtocol::CarageReturn(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	page->m_cursorPos.m_column = 0;
	page->GetCell(&page->m_cursorPos)->SetDirty(true);

	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void SharedProtocol::Linefeed(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	
	if (page->m_cursorPos.m_row == page->GetNumRows()-1)
	{
		page->ScrollPageUp();
	}
	else
	{
		page->m_cursorPos.m_row++;
	}
	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void SharedProtocol::CursorRight(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_bufferPos)->SetDirty(true);
	if (++page->m_cursorPos.m_column >= page->GetNumColumns())
	{
		page->m_cursorPos.m_column = 0;
		if (page->m_cursorPos.m_row == page->GetNumRows()-1)
		{
			page->ScrollPageUp();
		}
		else
		{
			page->m_cursorPos.m_row++;
			page->m_cursorPos.AdjustRow();
		}
	}
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void SharedProtocol::CursorLeft(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->m_cursorPos.m_column--;
	if (page->m_cursorPos.m_column < 0)
	{
		page->m_cursorPos.m_column = 0;
		if (page->m_cursorPos.m_row == 0)
		{
			page->m_cursorPos.m_row = page->GetNumRows() -1;
		}
		else
		{
			page->m_cursorPos.m_row--;
		}
	}
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}
	
void SharedProtocol::CursorUp(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	page->m_cursorPos.m_row++;
	page->m_cursorPos.AdjustRow();
	page->GetCell(&page->m_cursorPos)->SetDirty(true);

	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void SharedProtocol::CursorDown(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	if (page->m_cursorPos.m_row == page->GetNumRows()-1)
	{
		page->ScrollPageUp();
	}
	else
	{
		page->m_cursorPos.m_row++;
		page->GetCell(&page->m_cursorPos)->SetDirty(true);
	}
	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void SharedProtocol::ClearToEOL(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
}

void SharedProtocol::ClearBlock(Page *page, int startRow, int startCol, int endRow, int endCol)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
}

void SharedProtocol::ReadBuffer(StringBuffer *sb, Page *page, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol)
{
}

void SharedProtocol::SetCursor(Page *page, int row, int col)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	page->GetCell(&page->m_cursorPos)->SetDirty(true);
	page->m_cursorPos.m_row = row;
	page->m_cursorPos.m_column = col;
	page->GetCell(&page->m_cursorPos)->SetDirty(true);

	ASSERT(page->m_cursorPos.m_row < page->GetNumRows());
	ASSERT(page->m_cursorPos.m_column < page->GetNumColumns());
}

void SharedProtocol::ClearToEOP(Page *page)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();

	for (int c = page->m_cursorPos.m_column; c < page->GetNumColumns(); c++)
	{
		page->GetCell(c, page->m_cursorPos.m_row)->Clear(CHAR_CELL_DIRTY | page->GetWriteAttr() | page->GetPriorAttr());
	}
	for (int r = page->m_cursorPos.m_row+1; r < page->GetNumRows(); r++)
	{
		for (int c = 0; c < page->GetNumColumns(); c++)
		{
			page->GetCell(c, r)->Clear(CHAR_CELL_DIRTY | page->GetWriteAttr() | page->GetPriorAttr());
		}
	}
}

void SharedProtocol::WriteChar (Page *page, Cursor *cursor, int c)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT_PTR(cursor);
	ASSERT(cursor->m_row < page->GetNumRows());
	ASSERT(cursor->m_column < page->GetNumColumns());

	switch ((char)(c & MASK_CHAR))
	{
		case '\t':
			Tab(page, 1);
			break;
		case '\r':
			Linefeed(page);
			break;
		case '\n':
			CarageReturn(page);
			break;;
		case '\b':
			Backspace(page);
			break;
		case (char)11:
			CursorUp(page);
			break;
		case SPC_DEL:
			DeleteChar(page);
			break;
		case SPC_DOWN:
			ArrowDown(page, cursor);
			break;
		case SPC_END:
			End(page);
			break;
		case SPC_HOME:
			Home(page);
			break;
		case SPC_INS:
			InsertChar(page);
			break;
		case SPC_LEFT:
			ArrowLeft(page, cursor);
			break;
		case SPC_PGDN:
			break;
		case SPC_PGUP:
			break;
		case SPC_PRINTSCR:
			break;
		case SPC_RIGHT:
			ArrowRight(page, cursor);
			break;
		case SPC_UP:
			ArrowUp(page, cursor);
			break;
		default:
			if (page->GetCell(cursor)->IsKeyUpshift())
			{
				page->GetCell(cursor)->Set(c & MASK_CHAR, (c & ~MASK_CHAR) | page->GetWriteAttr() | page->GetPriorAttr() | page->GetCell(cursor)->GetAttributes());
			}
			else
			{
				/*  FASE 03: faltaba GetWriteAttr().
				 *
				 *  La rama de al lado (IsKeyUpshift) si lo aplica, y
				 *  ProtectPage::WriteChar tambien. Solo esta se lo salteaba,
				 *  y es la que usa el modo conversacional para TODO: lo que
				 *  escribe el host y el eco local de lo tecleado.
				 *
				 *  Consecuencia: ESC 6 no tenia efecto en conversacional ni
				 *  aunque llegara bien decodificado. Los dos defectos se
				 *  tapaban entre si.                                      */
				page->GetCell(cursor)->Set(c, page->GetCell(cursor)->GetAttributes() | CHAR_CELL_DIRTY | page->GetWriteAttr() | page->GetPriorAttr());
			}
			cursor->m_column++;
			cursor->AdjustCol();
			page->GetCell(cursor)->SetDirty(true);
			break;
	}
	ValidateCursorPos(page);
}

void SharedProtocol::Write(Cursor *pos, Page *page, int attribute, const char *text)
{
	ASSERT_MEM(page, sizeof(Page));
	page->ValidateMem();
	ASSERT(attribute == 0 || (attribute & MASK_CHAR) == 0);

	int len = strlen(text);
	for (int x = 0; x < len; x++)
	{
		//page->mem[pos.row][pos.column] = text.charAt(x) | attribute | CHAR_CELL_DIRTY;
		//c = text.charAt(x);
		//System.out.print((char)(c & MASK_CHAR));		
		//c |= attribute;
		//System.out.print((char)(c & MASK_CHAR));
		WriteChar(page, pos, text[x] | attribute);
		if (pos->m_column == page->GetNumColumns()-1)
		{
			return;
		}
	}
	ASSERT(pos->m_row < page->GetNumRows());
	ASSERT(pos->m_column < page->GetNumColumns());
}

