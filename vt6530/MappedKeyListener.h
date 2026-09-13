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
#ifndef _mappedkeylistener_h
#define _mappedkeylistener_h

#include <spl/StringBuffer.h>

class MappedKeyListener
{
public:
	virtual void KeyMappedKey(const char *s, int len) = 0;
	virtual void KeyCommand(const char c) = 0;
	virtual int KeyGetPage() = 0;
	virtual int KeyGetCursorX() = 0;
	virtual int KeyGetCursorY() = 0;
	virtual void KeyGetStartFieldASCII(StringBuffer *sb) = 0;
};

#endif
