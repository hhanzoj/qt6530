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
#ifndef _text_display_h
#define _text_display_h

#include <spl/Memory.h>
#include <spl/Color.h>
#include <spl/StringBuffer.h>
#include <vt6530/Attributes.h>
#include <vt6530/Page.h>
//#include "PaintSurface.h"
#include <vt6530/PageProtocol.h>
#include <vt6530/SharedProtocol.h>
#include <vt6530/ProtectPage.h>


class TextDisplay : public IMemoryValidate
{
	StringBuffer *m_statusLine;
	
	//int m_charWidth;      /* current width of a char */
	//int m_charHeight;      /* current height of a char */
	//int m_charDescent;      /* base line descent */	
	
	Page *m_displayPage;
	Page *m_writePage;
	Page **m_pages;
	int m_numPages;
	
	bool m_echoOn;
	bool m_blockMode;
	bool m_protectMode;
	
	bool m_requiresRepaint;
	
	PageProtocol *m_ppprotectMode;
	PageProtocol *m_ppunProtectMode;
	PageProtocol *m_ppconvMode;

	PageProtocol *m_ppRemote;
	
	bool m_keysLocked;
	
	int m_numRows, m_numColumns;

	Color m_foreground;
	Color m_background;

public:
	
	TextDisplay(int pageCount, int cols, int rows);
	virtual ~TextDisplay();

	inline void SetForeGroundColor(Color color)
	{
		m_foreground = color;
	}

	inline void SetBackGroundColor(Color color)
	{
		m_background = color;
	}

	inline Color *GetForeGroundColor()
	{
		return &m_foreground;
	}

	inline Color *GetBackGroundColor()
	{
		return &m_background;
	}

	inline bool NeedsRepaint()
	{
		return m_requiresRepaint;
	}
	
	inline void SetRePaint(bool val)
	{
		m_requiresRepaint = val;
	}
	
	inline void WriteBuffer(char *text)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->WriteBuffer(m_ppRemote, text);
	}
	
	inline void WriteDisplay(char *text)
	{
		ASSERT_MEM(m_displayPage, sizeof(Page));
		m_displayPage->WriteCursor(m_ppRemote, text);
		m_requiresRepaint = true;
	}
	
	void WriteLocal(char *text);
	
	void EchoDisplay(const char *text);
	
		/** ESC W
	 *  1.  Clear all pages to blanks
	 *  2.  Set video prior condition to NORMAL for all pages
	 *  3.  Select page 1
	 *  4.  Display page 1
	 *  5.  Set the buffer addess to (1,1) for all pages
	 *  6.  Set the cursor address to (1,1) for all pages
	 *  7.  Lock the keyboard
	 *  8.  Clear the status line display
	 *  9.  Reset insert mode
	 * 10.  Initialize datatype table
	 * 11.  Disable local line editing
	 */
	void SetProtectMode();
	
	
	/** ESC X
	 *  1.  Clear all pages to blanks
	 *  2.  Set the video prior conditiion registers to NORMAL for all pages
	 *  3.  Select page 1
	 *  4.  Display page 1
	 *  5.  Set the buffer address to (1,1) for all pages
	 *  6.  Set the cursor address to (1,1) for all pages
	 *  7.  Lock the keyboard
	 *  8.  Clear the status line
	 *  9.  Reset insert mode
	 * 10.  Enable local line editing
	 * 11.  Clear all horizontal tab stops
	 */
	void ExitProtectMode();
	
	inline void SetKeysLocked()
	{
		m_keysLocked = true;
	}
	
	inline void SetKeysUnlocked()
	{
		m_keysLocked = false;
	}
	
	/** ESC :
	 */
	inline void SetPage(int page)
	{
		ASSERT_MEM(m_pages, (m_numPages + 2) * sizeof(Page *));
		ASSERT_MEM(m_pages[page], sizeof(Page));

		//if (page >= numPages)
		//{
		//	return;
		//}
		m_writePage = m_pages[page];
	}
	
	/** 0x07
	 */
	inline void Bell()
	{
		// ding, ding, ding
	}
	
	/** 0x08
	 */
	inline void Backspace()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));

		m_writePage->Backspace(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** 0x09
	 */
	inline void Tab()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));

		m_writePage->Tab(m_ppRemote, 1);
		m_requiresRepaint = true;
	}	
	
	/** 0x0A
	 */
	inline void Linefeed()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));

		m_writePage->CursorDown(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** 0x0D
	 */
	inline void CarageReturn()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));

		m_writePage->CarageReturn(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** ESC J
	 */
	inline void SetCursorRowCol(int row, int col)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		ASSERT(row < m_numRows && col < m_numColumns);
		ASSERT(row >= 0 && col >= 0);

		m_writePage->SetCursor(m_ppRemote, row, col);
		m_requiresRepaint = true;
	}
	
	inline void SetBufferRowCol(int row, int col)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		ASSERT(row < m_numRows && col < m_numColumns);
		ASSERT(row >= 0 && col >= 0);

		m_writePage->SetBuffer(row, col);
		m_requiresRepaint = true;
	}

	inline void SetVideoPriorCondition(int attr)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->SetVideoPriorCondition(attr);
	}

	inline void SetInsertMode(int mode)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->SetInsertMode( mode );
	}
		
	/** ESC 0
	 */
	inline void PrintScreen()
	{
	}
	
	/** ESC 1
	 * 
	 *  Set a tab at the current cursor location
	 */
	void SetTab();
	
	/** ESC 2
	 * 
	 *  Clear the tab at the current cursor location
	 */
	void ClearTab();
	
	/** ESC 3
	 */
	void ClearAllTabs();
		
	/** ESC i
	 */
	inline void Backtab()
	{
		ASSERT_MEM(m_displayPage, sizeof(Page));
		m_displayPage->Tab(m_ppRemote, -1);
		m_requiresRepaint = true;
	}
	
	/** ESC 6
	 * 
	 *  All subsuquent writes use this attribute
	 */
	inline void SetWriteAttribute(int attr)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		ASSERT ( (attr & MASK_CHAR) == 0);
		m_writePage->SetWriteAttribute(attr);
	}

	/** ESC 7
	 *  Not sure what this is supposed to do
	 */
	inline void SetPriorWriteAttribute(int attr)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		ASSERT ( (attr & MASK_CHAR) == 0);
		m_writePage->SetWriteAttribute(attr);
	}
	
	/** ESC ! or ESC ' '
	 */
	void SetDisplayPage(int page);
	
	/** ESC A
	 */
	inline void MoveCursorUp()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->CursorUp(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** ESC C
	 */
	inline void MoveCursorRight()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->CursorRight(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** ESC H
	 */
	inline void Home()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->SetCursor(m_ppRemote, 0, 0);
		m_requiresRepaint = true;
	}
	
	/** ESC F
	 */
	inline void End()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->SetCursor(m_ppRemote, m_numRows-1, 0);
		m_requiresRepaint = true;
	}
	
	/** ESC I
	 */
	inline void ClearPage()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->SetWriteAttribute(VID_NORMAL);
		m_writePage->SetVideoPriorCondition(VID_NORMAL);
		m_writePage->ClearPage();
		m_requiresRepaint = true;
	}
	
	/** ESC J
	 */
	inline void ClearToEnd()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->ClearToEOP(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** ESC I
	 */
	inline void ClearBlock(int startRow, int startCol, int endRow, int endCol)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->ClearBlock(m_ppRemote, startRow, startCol, endRow, endCol);
		m_requiresRepaint = true;
	}
	
	/** ESC K
	 *  In block mode, erase the field.  In
	 *  conversation mode, clear to end of line
	 */
	inline void ClearEOL()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->ClearToEOL(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** 0x1D
	 */
	inline void StartField(int videoAttr, int dataAttr)
	{
		WriteField(DecodeVideoAttrs(videoAttr) | DecodeDataAttrs(dataAttr) | ' ');
	}
	
	/** ESC [
	 */
	inline void StartField(int videoAttr, int dataAttr, int keyAttr)
	{
		WriteField(DecodeVideoAttrs(videoAttr) | DecodeDataAttrs(dataAttr) | DecodeKeyAttrs(keyAttr) | ' ');
	}

	void ReadBufferAllMdt(StringBuffer *out, int startRow, int startCol, int endRow, int endCol)
	{
		ReadBuffer(out, DAT_MDT, 0, startRow, startCol, endRow, endCol);
	}
	
	inline void ReadBufferAllIgnoreMdt(StringBuffer *out, int startRow, int startCol, int endRow, int endCol)
	{
		ReadBuffer(out, 0, 0, startRow, startCol, endRow, endCol);
	}
	
	/** ESC - <
	 * 
	 * PROTECT MODE
	 *  Read all the unprotected fields in the block
	 *
	 * UNPROTECT MODE 
	 *  Return raw characters in the block
	 */
	inline void ReadBufferUnprotectIgnoreMdt(StringBuffer *out, int startRow, int startCol, int endRow, int endCol)
	{
		ReadBuffer(out, DAT_UNPROTECT, 0, startRow, startCol, endRow, endCol);
	}
	
	inline void ReadBufferUnprotect(StringBuffer *out, int startRow, int startCol, int endRow, int endCol)
	{
		ReadBuffer(out, DAT_UNPROTECT | DAT_MDT, 0, startRow, startCol, endRow, endCol);
	}

	/** ESC ]
	 * 
	 *  Read all the fields in the block (protected and unprotected)
	 */
	inline void ReadFieldsAll(StringBuffer *out, int startRow, int startCol, int endRow, int endCol)
	{
		ReadBuffer(out, 0, 0, startRow, startCol, endRow, endCol);
	}

	/** ESC >
	 *  reset all modified data tags for unprotected fields
	 */
	inline void ResetMdt()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->ResetMDTs();
	}
	
	/** ESC O
	 */
	inline void InsertChar()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->InsertChar(m_ppRemote);
		m_requiresRepaint = true;
	}
		
	/** ESC M
	 */			
	inline void SetModeBlock()
	{
		m_blockMode = true;
		ExitProtectMode();
		m_ppRemote = m_ppunProtectMode;
	}
	
	void SetModeConv();

	inline bool IsBlockMode() { return m_blockMode || m_protectMode; }

	/** ESC p
	 */
	inline void SetPageCount(int count)
	{
		m_numPages = count;
	}
	
	/** ESC q
	 */
	void Init();
	
	void WriteStatus(const char *msg);

	/** ESC o
	 *  
	 */
	void WriteMessage(const char *msg);

	void InitDataTypeTable();
	
	inline int GetNumColumns()
	{
		return m_numColumns;
	}
	
	inline int GetNumRows()
	{
		return m_numRows;
	}
	
	int GetCurrentPage();

	inline Page *GetDisplayPage()
	{
		return m_displayPage;
	}
	
	inline int GetCursorCol()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		return m_writePage->m_cursorPos.m_column + 1;
	}
	
	inline int GetCursorRow()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		return m_writePage->m_cursorPos.m_row + 1;
	}	

	inline int GetBufferCol()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		return m_writePage->m_bufferPos.m_column + 1;
	}
	
	inline int GetBufferRow()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		return m_writePage->m_bufferPos.m_row + 1;
	}
	
	inline bool GetProtectMode()
	{
		return m_protectMode;
	}

	/*  ---- UNICA edicion al nucleo en la fase 0 ----
	 *  m_statusLine era privado y sin accesor: el front-end de consola
	 *  nunca llego a pintar la linea 25. La fase 02 la necesita para el
	 *  widget, y las pruebas para comprobar que Insert() la hace crecer
	 *  (defecto 07 de la auditoria). Accesor de solo lectura, sin efectos. */
	inline const StringBuffer *GetStatusLine() const
	{
		return m_statusLine;
	}

	inline bool GetBlockMode()
	{
		return m_blockMode;
	}
	
	/*  FASE 03: el flag existia desde 2007 pero nadie lo leia y nadie lo
	 *  apagaba. Ahora Guardian::ExecLocalCommand lo consulta antes de
	 *  dibujar lo tecleado: cuando el host hace el eco (telnet WILL ECHO)
	 *  el terminal no tiene que hacerlo, y ese es justamente el mecanismo
	 *  por el que una clave no aparece en pantalla -- el host deja de
	 *  devolverla.                                                        */
	inline bool GetEchoOn() const
	{
		return m_echoOn;
	}

	inline void SetEchoOn()
	{
		m_echoOn = true;
	}
	
	inline void SetEchoOff()
	{
		m_echoOn = false;
	}

	/** ESC A
	 */
	inline void CursorUp()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->CursorUp(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** 0x0A
	 */
	inline void CursorDown()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->CursorDown(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** ESC C
	 */
	inline void CursorRight()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->CursorRight(m_ppRemote);
		m_requiresRepaint = true;
	}
	
	/** ESC L
	 */
	/*  FASE 04: los dos cuerpos estaban vacios, asi que ESC L y ESC M no
	 *  hacian nada -- ni desde el host ni desde el teclado.              */
	inline void LineDown()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->InsertLine(m_writePage->m_cursorPos.m_row);
		m_requiresRepaint = true;
	}

	/** ESC M
	 */
	inline void DeleteLine()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->DeleteLine(m_writePage->m_cursorPos.m_row);
		m_requiresRepaint = true;
	}

	/** ESC O
	 * 
	 *  Insert a space
	 */
	inline void Insert()
	{
		m_requiresRepaint = true;
	}
		
	inline void GetStartFieldASCII(StringBuffer *sb)
	{
		ASSERT_MEM(m_displayPage, sizeof(Page));
		m_displayPage->GetStartFieldASCII(sb);
	}
		
	/** ESC P
	 * Delete a character at a given position on the screen.
	 * All characters right to the position will be moved one to the left.
	 */
	inline void DeleteChar()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->DeleteChar(m_ppRemote);
		m_requiresRepaint = true;
	}		
		
	/*void paint(PaintSurface *ps)
	{
		ASSERT_MEMdisplayPage, sizeof(Page));
		displayPage->paint(ps, *statusLine);
		requiresRepaint = false;
	}*/
	
	void DumpScreen(StringBuffer *pw);
	
	void DumpAttibutes(StringBuffer *pw);

	/**
	 *  Get the 'index'nth field on the screen.
	 *  The first field is index ZERO.  If the
	 *  index is larger than the number of field,
	 *  an empty string is returned.
	 */
	void GetField(int index, StringBuffer *accum);
	
	/**
	 *  Get the video, data, and key attributes for a
	 *  field.
	 */
	int GetFieldAttributes(int index);

	/**
	 *  Get the text in the field at the cursor
	 *  position.
	 */
	void GetCurrentField(StringBuffer *accum);
	
	/**
	 *  Get the 'index'nth unprotected field on 
	 *  the screen.  The first field is index 
	 *  ZERO.  If the index is larger than the 
	 *  number of field, an empty string is 
	 *  returned.
	 */
	void GetUnprotectField(int index, StringBuffer *accum);
	
	/**
	 *  Write text into the 'index'nth 
	 *  unprotected field on the screen.  The 
	 *  first field is index ZERO.  If the 
	 *  index is larger than the number of field, 
	 *  the request is ignored.
	 */
	void SetField(int index, char *text);
	
	/**
	 *  Returns true if the 'index'nth unprotected
	 *  field has its MDT set. The first field is 
	 *  index ZERO.  If the index is larger than 
	 *  the  number of fields, false is returned.
	 */
	bool IsFieldChanged(int index);

	/**
	 *  Get a full line of display text.  
	 */
	void GetLine(int lineNumber, StringBuffer *line);
	
	/**
	 *  Set the cursor at the start if the 
	 *  'index'nth unprotected field on the screen.  
	 *  The first field is index ZERO.  If the 
	 *  index is larger than the number of field, 
	 *  the request is ignored.
	 */
	void CursorToField(int index);
	
	void ToHTML(Color *fg, Color *bg, StringBuffer *);

	void GetSubString(int row, int col, int len, StringBuffer *sb);

	void GetText(StringBuffer *sb);

#if defined(DEBUG) || defined(_DEBUG)
	void CheckMem() const;
	void ValidateMem() const;
#endif

private:

	inline void ReadBuffer(StringBuffer *out, int reqMask, int forbidMask, int startRow, int startCol, int endRow, int endCol)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->ReadBuffer(out, m_ppRemote, reqMask, forbidMask, startRow, startCol, endRow, endCol);
	}
	
	/** 0x08
	 * PROTECT MODE
	 *  Move to the start of the field.  If the cursor
	 *  is already at the start, move the first position
	 *  of the previous unprotected field.
	 *
	 * If the new cursor position is protected,
	 * move to the last position of the previous
	 * unprotected field.
	 *
	 * UNPROTECT MODE
	 *  Move to previous tab.  If no prev tab exists
	 *  on the current row, move to first column.  If
	 *  already on first column, move to last tab on 
	 *  previous row.  If the cursor is in (1,1), move
	 *  to the right most tab of the last row
	 */
	inline void CursorLeft()
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->CursorLeft(m_ppRemote);
		m_requiresRepaint = true;
	}

	inline void WriteField(int c)
	{
		ASSERT_MEM(m_writePage, sizeof(Page));
		m_writePage->WriteField(c);
	}

	void ClearAll();

	int DecodeKeyAttrs(int attr);
	int DecodeDataAttrs(int attr);	

public:
	/*  FASE 03: era privado, y hacia falta afuera. Guardian lo necesita
	 *  para ESC 6 y ESC 7: convierte el byte de atributos del cable en los
	 *  flags VID_* (bit3 = invisible, que es como se oculta una clave).
	 *  El camino de inicio de campo ya lo usaba desde adentro.           */
	int DecodeVideoAttrs(int attr);
};

#endif
