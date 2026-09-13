/*
 *   This file is part of the Standard Portable Library (SPL).
 *
 *   SPL is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Foobar is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with SPL.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _page_h
#define _page_h

#include <string.h>
#include <spl/Memory.h>
#include <spl/collection/Vector.h>

#include "Attributes.h"
#include "Cursor.h"
#include "PageProtocol.h"

struct CellAttributes
{
	unsigned m_vidNormal : 1;
	unsigned m_vidBlinking : 1;
	unsigned m_vidReverse : 1;
	unsigned m_vidInvis : 1;
	unsigned m_vidUnderline : 1;

	unsigned m_datMdt : 1;
	unsigned m_datDataType : 3;
	unsigned m_datAutotab : 1;
	unsigned m_datUnprotect : 1;

	unsigned m_keyUpshift : 1;
	unsigned m_keyKbOnly : 1;
	unsigned m_keyAidOnly : 1;
	unsigned m_keyEither : 1;

	unsigned m_charStartField : 1;
	unsigned m_charDirtyCell : 1;
	unsigned m_charColor : 3;
};

class PageCell
{
protected:
	char m_ch;

	union
	{
		struct CellAttributes m_attribs;
		int m_bits;
	};
public:
	PageCell();
	~PageCell();

	inline char Get( )
	{
		return m_ch;
	}

	inline void Set( char ch )
	{
		m_ch = ch;
		m_attribs.m_charDirtyCell = 1;
	}

	inline void Set (PageCell *cell)
	{
		m_ch = cell->m_ch;
		m_bits = cell->m_bits;
		m_attribs.m_charDirtyCell = 1;
	}

	inline void Set( char ch, int attribs )
	{
		Set(ch);
		SetAttributes(attribs);
	}

	void SetAttributes( int attribs );

	inline int GetAttributes()
	{
		return m_bits << 8;
	}

	inline int AsInt()
	{
		return GetAttributes() | (int)m_ch;
	}

	inline void Clear()
	{
		m_ch = ' ';
		m_bits = 0;
		m_attribs.m_vidNormal = true;
	}

	inline void Clear( int attributes )
	{
		m_ch = ' ';
		SetAttributes(attributes);
	}

	void ClearVideoAttribs();

	/* ClearTo doesn't set the dirty flag */
	inline void ClearTo( char ch )
	{
		m_ch = ch;
		m_bits = 0;
		m_attribs.m_vidNormal = 1;
	}

	inline void SetDirty(bool val) { m_attribs.m_charDirtyCell = ((val)?1:0); }
	inline bool IsDirty() { return m_attribs.m_charDirtyCell != 0; }

	inline void SetNormal(bool val) { m_attribs.m_vidNormal = ((val)?1:0); }
	inline bool IsNormal() { return m_attribs.m_vidNormal != 0; }

	inline void SetBlinking(bool val) { m_attribs.m_vidBlinking = ((val)?1:0); }
	inline bool IsBlinking() { return m_attribs.m_vidBlinking != 0; }

	inline void SetReverse(bool val) { m_attribs.m_vidReverse = ((val)?1:0); }
	inline bool IsReverse() { return m_attribs.m_vidReverse != 0; }

	inline void SetInvis(bool val) { m_attribs.m_vidInvis = ((val)?1:0); }
	inline bool IsInvis() { return m_attribs.m_vidInvis != 0; }

	inline void SetUnderline(bool val) { m_attribs.m_vidUnderline = ((val)?1:0); }
	inline bool IsUnderline() { return m_attribs.m_vidUnderline != 0; }

	inline void SetUnprotect(bool val) { m_attribs.m_datUnprotect = ((val)?1:0); }
	inline bool IsUnprotect() { return m_attribs.m_datUnprotect != 0; }

	inline void SetStartField(bool val) { m_attribs.m_charStartField = ((val)?1:0); }
	inline bool IsStartField() { return m_attribs.m_charStartField != 0; }

	inline void SetMDT(bool val) { m_attribs.m_datMdt = ((val)?1:0); }
	inline bool IsMDT() { return m_attribs.m_datMdt != 0; }

	inline void SetKeyUpshift(bool val) { m_attribs.m_keyUpshift = ((val)?1:0); }
	inline bool IsKeyUpshift() { return m_attribs.m_keyUpshift != 0; }
};

/*
 *  In protect mode, there's 4 pages (or buffers) to send
 *  commands to.
 */
class Page : public IMemoryValidate
{	
protected:
	char m_chbuf[2];
	
	int m_numRows, m_numColumns;

	int m_writeAttr;		// These attributes can be set and then effect
	int m_priorAttr;		// all subsequent writes.
	
	int m_insertMode;
	bool m_cursorBlock;

	PageCell *m_cells;
	Vector<PageCell *> m_fields;
	Vector<PageCell *> m_unprotectFields;

public:

	Cursor m_cursorPos;
	Cursor m_bufferPos;

	Page(int numRows, int numCols);

	virtual ~Page();
	
	void Init();

	inline int GetNumRows()
	{
		return m_numRows;
	}

	inline int GetNumColumns()
	{
		return m_numColumns;
	}

	inline PageCell *GetCell(int x, int y)
	{
		ASSERT( x < m_numColumns && y < m_numRows );
		return &m_cells[y * m_numColumns + x];
	}

	inline PageCell *GetCell(Cursor *cursor)
	{
		return GetCell(cursor->m_column, cursor->m_row);
	}

	void WriteBuffer(PageProtocol *mode, const char *text);
	
	void WriteCursor(PageProtocol *mode, const char *text);

	void WriteCursorLocal(PageProtocol *mode, const char *text);
	
	void CarageReturn(PageProtocol *mode);
	
	void SetCursor(PageProtocol *mode, int row, int col);
	
	void SetBuffer(int row, int col);
	
	inline void SetVideoPriorCondition(int attr)
	{
		m_priorAttr = attr;
	}
	
	inline void SetWriteAttribute(int attr)
	{
		m_writeAttr = attr;
	}
	
	inline int GetWriteAttr()
	{
		return m_writeAttr;
	}

	inline int GetPriorAttr()
	{
		return m_priorAttr;
	}

	void InsertChar(PageProtocol *mode);
	
	inline void SetInsertMode(int mode)
	{
		m_insertMode = mode;
	}

	void DeleteChar(PageProtocol *mode);
	
	void Tab(PageProtocol *mode, int inc);
	
	void Backspace(PageProtocol *mode);
	
	void CursorUp(PageProtocol *mode);
	
	void CursorDown(PageProtocol *mode);
	
	void CursorLeft(PageProtocol *mode);
	
	void CursorRight(PageProtocol *mode);
	
	void ClearPage();
	
	inline void ClearToEOP(PageProtocol *mode)
	{
		ASSERT_PTR(mode);
		mode->ClearToEOP(this);
	}
	
	inline void ClearToEOL(PageProtocol *mode)
	{
		ASSERT_PTR(mode);
		mode->ClearToEOL(this);
	}
	
	inline void ClearBlock(PageProtocol *mode, int startRow, int startCol, int endRow, int endCol)
	{
		ASSERT_PTR(mode);
		mode->ClearBlock(this, startRow, startCol, endRow, endCol);
	}
	
	void WriteField(int c);
	
	inline void ReadBuffer(StringBuffer *out, PageProtocol *mode, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol)
	{
		ASSERT_PTR(mode);
		mode->ReadBuffer(out, this, reqMask, forbidMask, startRow, startCol, endRow, endCol);
	}
	
	void ResetMDTs();
	
	void GetStartFieldASCII(StringBuffer *);

	/*void paint(PaintSurface *ps, char *statusLine);*/

	void ForceDirty();
	
	void ScrollPageUp();

	/** Corren el contenido de las filas hacia abajo o hacia arriba desde
	 *  row, dejando una fila en blanco. No tocan la estructura de campos. */
	void InsertLine(int row);
	void DeleteLine(int row);

	int ScanForNextField(int c, int r, int inc);
	
	int ScanForUnprotectField(int c, int r, int inc);

	inline int GetFieldCount() { return m_fields.Count(); }
	inline int GetUnprotectFieldCount() { return m_unprotectFields.Count(); }
	inline PageCell *GetFieldStart(int x) { return (x >= m_fields.Count()) ? NULL : m_fields.ElementAt(x); }
	inline PageCell *GetUnprotectFieldStart(int x) { return (x >= m_unprotectFields.Count()) ? NULL : m_unprotectFields.ElementAt(x); }

	/*inline void setColors(PaintSurface *ps, int ch, COLORREF fgcolor, COLORREF bgcolor, COLORREF fgbright, HBRUSH foreground, HBRUSH background, HPEN pen, HPEN revpen, int r, int c, int charWidth, int charHeight)
	{
		if ( ((ch & VID_REVERSE) != 0) && ((ch & VID_BLINKING) != 0) ) 
		{ 
			//HBRUSH color = bg; 
			//bg = fg;
			//fg = color;
			ps->setBkColor(fgbright);
			ps->setTextColor(bgcolor);
			ps->setPen(revpen);
			ps->fillRect(c * charWidth, r * charHeight, charWidth, charHeight, foreground);
		}
		else if ((ch & VID_REVERSE) != 0)
		{
			ps->setBkColor(fgcolor);
			ps->setTextColor(bgcolor);
			ps->setPen(revpen);
			ps->fillRect(c * charWidth, r * charHeight, charWidth, charHeight, foreground);
		}
		else if ((ch & VID_BLINKING) != 0)
		{
			ps->setBkColor(bgcolor);
			ps->setTextColor(fgbright);	
			ps->setPen(pen);
			ps->fillRect(c * charWidth, r * charHeight, charWidth, charHeight, background);
		}
		else
		{
			ps->setBkColor(bgcolor);
			ps->setTextColor(fgcolor);
			ps->fillRect(c * charWidth, r * charHeight, charWidth, charHeight, background);
		}
	}*/

#if defined(DEBUG) || defined(_DEBUG)
	void CheckMem() const;
	void ValidateMem() const;
#endif
};


#endif
