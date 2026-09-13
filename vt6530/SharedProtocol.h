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
#ifndef _shared_protocol_h
#define _shared_protocol_h

#include <spl/types.h>
#include <vt6530/Cursor.h>
#include <vt6530/PageProtocol.h>
#include <vt6530/Page.h>


class SharedProtocol : public PageProtocol
{
public:
	virtual void WriteBuffer(Page *page, const char *text);
	
	virtual void WriteCursor(Page *page, const char *text);

	virtual void WriteChar (Page *page, Cursor *cursor, int c);
	
	virtual void ArrowDown(Page *page, Cursor *cursor);
	
	virtual void ArrowUp(Page *page, Cursor *cursor);

	virtual void ArrowLeft(Page *page, Cursor *cursor);
	
	virtual void ArrowRight(Page *page, Cursor *cursor);

	virtual void Home(Page *page);

	virtual void End(Page *page);

	virtual void ValidateCursorPos(Page *page);
	
	virtual void InsertChar(Page *page);
	
	virtual void DeleteChar(Page *page);
	
	void Backspace(Page *page);
	
	virtual void Tab(Page *page, int inc);
	
	virtual void CarageReturn(Page *page);
	
	virtual void Linefeed(Page *page);
	
	virtual void CursorRight(Page *page);

	virtual void CursorLeft(Page *page);
		
	virtual void CursorUp(Page *page);
	
	virtual void CursorDown(Page *page);
	
	virtual void ClearToEOL(Page *page);
	
	virtual void ClearBlock(Page *page, int startRow, int startCol, int endRow, int endCol);

	virtual void ClearToEOP(Page *page);
	
	virtual void ReadBuffer(StringBuffer *sb, Page *page, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol);

	virtual void SetCursor(Page *page, int row, int col);

protected:
	
	void Write(Cursor *pos, Page *page, int attribute, const char *text);		
};

#endif
