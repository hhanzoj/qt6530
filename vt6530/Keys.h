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
#ifndef _keys_h
#define _keys_h

#include "MappedKeyListener.h"

/*
 *  Tandem key definitions and mappings.
 */
#define KEYS_ANSI  0
#define KEYS_CONV  1
#define KEYS_BLOCK 2
	
#define TDM_SOH  ((char)1)
#define TDM_ENQUIRY  ((char)5)
#define TDM_ACK   ((char)6)
#define TDM_BELL  ((char)7)
#define TDM_BACKSPACE  "\b"
#define TDM_NAK  "\21"
#define TDM_ESC  "\27"
#define TDM_CR   ((char)13)
	
#define SPC_F1  0
#define SPC_F2  1
#define SPC_F3  2
#define SPC_F4  3
#define SPC_F5  4
#define SPC_F6  5
#define SPC_F7  6
#define SPC_F8  7
#define SPC_F9  11
#define SPC_F10  12
#define SPC_F11  14
#define SPC_F12  15
#define SPC_BREAK  16
#define SPC_PGUP  17
#define SPC_PGDN  18
#define SPC_HOME  19
#define SPC_END  20
#define SPC_INS  21
#define SPC_DEL  22
#define SPC_SCROLLOCK  23
#define SPC_UP  24
#define SPC_DOWN  25
#define SPC_LEFT  26
#define SPC_RIGHT  28
#define SPC_PRINTSCR  29

/*  FASE 03 -- F13 a F16.
 *
 *  El 6530 define dieciseis teclas de funcion y esta tabla solo llegaba a
 *  doce. No es teorico: VIEWSYS sale con F16, y una captura real de rci3 trae
 *  la secuencia AID 01 4F 21 20 20 03 00 -- el codigo 0x4F.
 *
 *  Los codigos van 0x40 + (n-1), lo que cuadra con plainFn para F1..F12 y con
 *  ese 0x4F para F16. Se agregan al final en vez de usar los huecos 8, 9, 10 y
 *  13, que pisan los codigos de control ASCII y confundirian a quien llame.  */
#define SPC_F13  30
#define SPC_F14  31
#define SPC_F15  32
#define SPC_F16  33

#define LAST_SPC  34


class Keys
{
	bool m_sendCursorWithFn;
	
	/*  FASE 03: era un char, y char es con signo en x86. Un codigo de
	 *  128 a 255 -- cualquier acentuado de latin-1 -- se guardaba
	 *  negativo y despues indexaba las tablas hacia atras. KeyAction
	 *  ahora acota el rango, y esto deja de ser una trampa.        */
	int m_pressedKey;
	long m_pressedWhen;

	const char **m_plain;
	const char **m_crtl;
	const char **m_alt;
	const char **m_shift;
	
	const int *m_localCmd;
	const int *m_localCmdCtl;
	const int *m_localCmdAlt;
	
	volatile bool m_ignoreKeys;
	bool m_protectMode;
	bool m_enterKeyOn;

	MappedKeyListener *m_listener;
						  
public:

	Keys();
	virtual ~Keys();

	void SetProtectMode();	
	void ExitProtectMode();
	
	inline void SetEnterKeyOn()
	{
		m_enterKeyOn = true;
	}
	
	inline void SetEnterKeyOff()
	{
		m_enterKeyOn = false;
	}
	
	inline void SetListener(MappedKeyListener *listener)
	{
		this->m_listener = listener;
	}
	
	void SetKeySet( int keySet );
	
	//public void setMap(int ch, int modifier, char *out);
	
	inline void KeyPressed( int keycode, bool shift, bool ctrl, bool alt ) 
	{
	}
	
	void KeyReleased( int keycode, bool shift, bool ctrl, bool alt );
	
	inline void KeyTyped(  int keycode, bool shift, bool ctrl, bool alt )
	{
		m_pressedKey = keycode;
		KeyAction(false, keycode, shift, ctrl, alt);
	}
	
	void KeyAction(bool fn, int keycode, bool shift, bool ctrl, bool alt );
	
	inline void LockKeyboard()
	{
		m_ignoreKeys = true;
	}
	
	inline void UnlockKeyboard()
	{
		m_ignoreKeys = false;
	}
	
	inline void SetCrLfOn()
	{
	}
	
	inline void SetCrLfOff()
	{
	}

#if defined(DEBUG) || defined(_DEBUG)
	void CheckMem() const {}
	void ValidateMem() const {}
#else
	inline void CheckMem() const {}
	inline void ValidateMem() const {}
#endif

private:
	
	/** No hace nada en tiempo de ejecucion: solo lleva los static_assert
	 *  del tamano de las tablas, que necesitan verlas ya definidas. */
	static void CheckTableSizes();

	void NotifyListener(const char b);
	
	void NotifyListener(const char *str, int len);

	static const char *ansiChar[];
	static const char *ansiCharCtl[];
	static const char *ansiCharAlt[];
	static const char *ansiCharShift[];
	static const int ansiLocal[];
	static const int ansiLocalCtl[];
	static const int ansiLocalAlt[];
	static const char *convChar[];
	static const char *convCharCtl[];
	static const char *convCharAlt[];
	static const char *convCharShift[];
	static const int convLocal[];
	static const int convLocalCtl[];
	static const int convLocalAlt[];
	static const char *blockChar[];
	static const char *blockCharCtl[];
	static const char *blockCharAlt[];
	static const char *blockCharShift[];
	static const int blockLocal[];
	static const int blockLocalCtl[];
	static const int blockLocalAlt[];
	static const char *shiftFn[];
	static const char *plainFn[];
};

#endif
