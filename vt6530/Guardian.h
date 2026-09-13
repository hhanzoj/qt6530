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
#ifndef _guardian_h
#define _guardian_h

#include <spl/debug.h>
#include <spl/Memory.h>
#include <spl/Exception.h>
#include <spl/StringBuffer.h>
#include <spl/term/Telnet.h>
#include <vt6530/Keys.h>
#include <vt6530/Mode.h>
#include <vt6530/TextDisplay.h>
#include <vt6530/TermEventListener.h>


#define SIZE_ELEM 100

/**
 *  Class to assist in command parsing
 */
class InputElementBuffer : public IMemoryValidate
{
	char *m_elements[SIZE_ELEM];
	int m_pos;

public:
	InputElementBuffer();
	~InputElementBuffer();

	int Size();
	void AddElement(const char *str);
	void AddElement(char c);
	char *ElementAt(int index);
	void Clear();

#if defined(DEBUG) || defined(_DEBUG)
	void CheckMem() const;
	void ValidateMem() const;
#endif
};

/**
 *	Tandem Guardian operating environment terminal
 *  command interpreter.
 */
class Guardian : public Mode
{	
	InputElementBuffer m_strStack;

	/* accumulate characters for sending to the display */
	StringBuffer m_accum;
	
	StringBuffer m_keyBuffer;
	
	StringBuffer m_blockBuf;

	/* the keyboard handler */
	Keys *m_keys;

	/* the socket IO and telnet line protocol handler */
	Telnet *m_telnet;
	
	/* the abstract display.  characters are stored here,
	 * but doesn't actually render the text on-screen     */
	TextDisplay *m_display;
	
	/* The command interpreter is a state machine -- this is the state */
	int m_state;

	/* from Vt6530 */
	Vector<Vt6530EventListener *> *m_listeners;

public:
	
	Guardian
	(
		Vector<Vt6530EventListener *> *listeners, 
		TextDisplay *display, 
		Keys *keys, 
		Telnet *telnet
	);

	virtual ~Guardian();

	/*
	 *  Process incoming text from the host.
	 */
	void ProcessRemoteString(const char *inp, const int len);
	
	/*
	 *  Process text in local edit mode.
	 */
	void ExecLocalCommand(const char cmd);

	virtual bool IsConvMode();

	void DispatchResetLine();

	/**
	 *  The host has completed transmision and is
	 *  now waiting for input.  This can be used
	 *  to buffer keystrokes until the screen is
	 *  fully m_displayed.
	 */
	void DispatchEnquire();

#if defined(DEBUG) || defined(_DEBUG)
	void CheckMem() const;
	void ValidateMem() const;
#endif
};


#endif
