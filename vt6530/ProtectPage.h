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
#ifndef _protect_page_h
#define _protect_page_h

#include <vt6530/SharedProtocol.h>
#include <vt6530/Page.h>

class ProtectPage : public SharedProtocol
{
public:
	/*  FASE 03: Tab pasa a ser publico. Las flechas verticales lo usan como
	 *  ultimo recurso desde una funcion auxiliar del .cpp. */
	virtual void Tab(Page *page, int inc);

private:
	virtual void Home(Page *page);

	virtual void End(Page *page);

	virtual void ClearToEOL(Page *page);
	
	virtual void ClearToEOP(Page *page);
	
	virtual void CursorLeft(Page *page);

	virtual void ReadBuffer(StringBuffer *accum, Page *page, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol);

	virtual void InsertChar(Page *page);

	virtual void DeleteChar(Page *page);

	virtual void WriteChar(Page *page, Cursor *bufferPos, int c);

	virtual void Linefeed(Page *page);

	virtual void ValidateCursorPos(Page *page);
	
	virtual void SetCursor(Page *page, int row, int col);

	virtual void ClearBlock(Page *page, int startRow, int startCol, int endRow, int endCol);

	virtual void ArrowDown(Page *page, Cursor *cursor);
	
	virtual void ArrowUp(Page *page, Cursor *cursor);

	virtual void ArrowLeft(Page *page, Cursor *cursor);
	
	virtual void ArrowRight(Page *page, Cursor *cursor);
};

class UnprotectPage : public SharedProtocol
{
	virtual void Tab(int inc);
	
	virtual void ClearToEOL(Page *page);

	virtual void ReadBuffer(StringBuffer *accum, Page *page, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol);
};

#endif
