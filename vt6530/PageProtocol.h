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
#ifndef _page_protocol_h
#define _page_protocol_h

#include <vt6530/Attributes.h>
#include <vt6530/Cursor.h>
#include <spl/StringBuffer.h>

class Page;

#define MODE_DISPLAY = 0
#define MODE_PROTECT = 1
#define MODE_UNPROTECT = 2


class PageProtocol
{
public:
	virtual void WriteBuffer(Page *page, const char *text) = 0;
	virtual void WriteCursor(Page *page, const char *text) = 0;
	virtual void WriteChar(Page *page, Cursor *cursor, int c) = 0;
	virtual void InsertChar(Page *page) = 0;
	virtual void DeleteChar(Page *page) = 0;
	virtual void Backspace(Page *page) = 0;
	virtual void Tab(Page *page, int inc) = 0;
	virtual void CarageReturn(Page *page) = 0;
	virtual void Linefeed(Page *page) = 0;
	virtual void Home(Page *page) = 0;
	virtual void End(Page *page) = 0;
	
	virtual void ValidateCursorPos(Page *page) = 0;
	virtual void SetCursor(Page *page, int row, int col) = 0;
	virtual void CursorLeft(Page *page) = 0;
	virtual void CursorRight(Page *page) = 0;
	virtual void CursorDown(Page *page) = 0;
	virtual void CursorUp(Page *page) = 0;

	virtual void ClearToEOL(Page *page) = 0;
	virtual void ClearToEOP(Page *page) = 0;
	virtual void ClearBlock(Page *page, int startRow, int startCol, int endRow, int endCol) = 0;
	
	virtual void ReadBuffer(StringBuffer *out, Page *page, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol) = 0;
};

#endif
