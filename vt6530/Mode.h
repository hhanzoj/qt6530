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
#ifndef _mode_h
#define _mode_h

#include <spl/debug.h>
#include <spl/Memory.h>

class Mode : public IMemoryValidate
{
public:
	Mode() {}
	virtual ~Mode() {}
	virtual void ProcessRemoteString(const char *inp, const int inplen) = 0;
	virtual void ExecLocalCommand(const char cmd) = 0;
	virtual bool IsConvMode() = 0;

#if defined(DEBUG) || defined(_DEBUG)
	virtual void CheckMem() const = 0;
	virtual void ValidateMem() const = 0;
#endif
};


#endif
