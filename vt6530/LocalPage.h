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
#ifndef _local_page_h
#define _local_page_h

#include "Attributes.h"
#include "SharedProtocol.h"
#include "Page.h"

class LocalPage : public SharedProtocol
{
	void WriteCursor(Page *page, char *text)
	{
		int len = strlen(text);
		for (int x = 0; x < len; x++)
		{
			WriteChar(page, text[x]);
		}
	}

	void WriteChar(Page *page, int c)
	{	
		SharedProtocol::WriteChar(page, &page->m_cursorPos, c | DAT_MDT);
	}
};

#endif
