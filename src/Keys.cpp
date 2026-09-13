#include <spl/debug.h>
#include <vt6530/Keys.h>
#include <spl/StringBuffer.h>

Keys::Keys()
{
	CheckTableSizes();

	SetKeySet(KEYS_CONV);
	m_ignoreKeys = false;
	m_protectMode = false;
	m_enterKeyOn = true;
	m_sendCursorWithFn = false;
	m_pressedKey = ' ';
	m_pressedWhen = 0;
}

Keys::~Keys()
{
}

void Keys::NotifyListener(const char b)
{
	if (! m_ignoreKeys)
	{
		m_listener->KeyCommand(b);
	}
}

void Keys::NotifyListener(const char *str, int len)
{
	if (! m_ignoreKeys)
	{
		m_listener->KeyMappedKey(str, len);
	}
}

void Keys::SetProtectMode()
{
	LockKeyboard();
	m_protectMode = true;
	// disable local line editing
}

void Keys::ExitProtectMode()
{
	UnlockKeyboard();
	m_protectMode = false;
	// enable local line editing
}

void Keys::SetKeySet(int keySet)
{
	if (keySet == KEYS_ANSI)
	{
		m_plain = ansiChar;
		m_crtl = ansiCharCtl;
		m_alt = ansiCharAlt;
		m_shift = ansiCharShift;
		m_localCmd = ansiLocal;
		m_localCmdCtl = ansiLocalCtl;
		m_localCmdAlt = ansiLocalAlt;
		m_sendCursorWithFn = false;
		m_protectMode = false;
		m_enterKeyOn = true;
	}
	else if (keySet == KEYS_CONV)
	{
		m_plain = convChar;
		m_crtl = convCharCtl;
		m_alt = convCharAlt;
		m_shift = convCharShift;
		m_localCmd = convLocal;
		m_localCmdCtl = convLocalCtl;
		m_localCmdAlt = convLocalAlt;
		m_sendCursorWithFn = true;
		m_protectMode = false;
		m_enterKeyOn = true;
	}
	else if (keySet == KEYS_BLOCK)
	{
		m_plain = blockChar;
		m_crtl = blockCharCtl;
		m_alt = blockCharAlt;
		m_shift = blockCharShift;
		m_localCmd = blockLocal;
		m_localCmdCtl = blockLocalCtl;
		m_localCmdAlt = blockLocalAlt;
		m_sendCursorWithFn = true;
		m_protectMode = false;
		m_enterKeyOn = true;
	}
}

void Keys::KeyReleased( int keycode, bool shift, bool ctrl, bool alt )
{
	bool fn = false;
	switch (keycode)
	{
		case SPC_F1:
			m_pressedKey = SPC_F1;
			fn = true;
			break;
		case SPC_F2:
			m_pressedKey = SPC_F2;
			fn = true;
			break;
		case SPC_F3:
			m_pressedKey = SPC_F3;
			fn = true;
			break;
		case SPC_F4:
			m_pressedKey = SPC_F4;
			fn = true;
			break;
		case SPC_F5:
			m_pressedKey = SPC_F5;
			fn = true;
			break;
		case SPC_F6:
			m_pressedKey = SPC_F6;
			fn = true;
			break;
		case SPC_F7:
			m_pressedKey = SPC_F7;
			fn = true;
			break;
		case SPC_F8:
			m_pressedKey = SPC_F8;
			fn = true;
			break;
		case SPC_F9:
			m_pressedKey = SPC_F9;
			fn = true;
			break;
		case SPC_F10:
			m_pressedKey = SPC_F10;
			fn = true;
			break;
		case SPC_F11:
			m_pressedKey = SPC_F11;
			fn = true;
			break;
		case SPC_F12:
			m_pressedKey = SPC_F12;
			fn = true;
			break;
		/* FASE 03: las cuatro que faltaban. */
		case SPC_F13:
			m_pressedKey = SPC_F13;
			fn = true;
			break;
		case SPC_F14:
			m_pressedKey = SPC_F14;
			fn = true;
			break;
		case SPC_F15:
			m_pressedKey = SPC_F15;
			fn = true;
			break;
		case SPC_F16:
			m_pressedKey = SPC_F16;
			fn = true;
			break;
		/*  FASE 03 -- teclas de navegacion.
		 *
		 *  Aca estaba el defecto. Los doce casos de abajo llamaban a
		 *  KeyAction(false, keycode, ...) SIN asignar m_pressedKey, y
		 *  KeyAction descarta su parametro keycode: indexa las tablas con
		 *  m_pressedKey. O sea que una flecha reenviaba lo que hubiera
		 *  mapeado la tecla anterior. El autor habia dejado la asignacion
		 *  escrita y comentada en cada caso ("//keycode = SPC_HOME;"), pero
		 *  sobre la variable equivocada: keycode es un parametro por valor
		 *  y KeyAction no lo mira.
		 *
		 *  Que hace falta que llegue: m_localCmd[i] vale i para todos estos
		 *  indices, asi que el codigo SPC_ viaja como byte crudo hasta
		 *  SharedProtocol::WriteChar, que hace switch justo sobre esas
		 *  constantes (case SPC_UP -> ArrowUp, y asi). La cadena entera ya
		 *  estaba, solo faltaba el primer eslabon.                        */
		case SPC_HOME:
		case SPC_INS:
		case SPC_DEL:
		case SPC_DOWN:
		case SPC_END:
		case SPC_LEFT:
		case SPC_PGDN:
		case SPC_PGUP:
		case SPC_PRINTSCR:
		case SPC_RIGHT:
		case SPC_UP:
		case SPC_SCROLLOCK:
			m_pressedKey = keycode;
			KeyAction(false, keycode, shift, ctrl, alt);
			break;
	}
	if (fn)
	{
		KeyAction(fn, keycode, shift, ctrl, alt);
	}
}

void Keys::KeyAction(bool fn, int keycode, bool shift, bool ctrl, bool alt)
{
	StringBuffer sb;

	/*  FASE 03. Antes esta funcion ignoraba su parametro keycode y usaba
	 *  siempre m_pressedKey, que solo las teclas de funcion asignaban.
	 *  Ahora el parametro manda, que es lo que hacia KeyTyped a mano.    */
	m_pressedKey = keycode;

	/*  Y de paso, el limite. Las tablas de cadenas llegan hasta el indice
	 *  127; tres de las de comandos locales (ansiLocal y sus variantes)
	 *  solo tenian 120 entradas, asi que un indice de 120 a 127 leia
	 *  fuera del arreglo. Se rellenaron a 128 mas abajo, y aca se corta
	 *  todo lo que quede afuera: hasta la fase 03 un caracter acentuado
	 *  (0xF1 para la enie) indexaba m_plain[241] y le hacia strlen a lo
	 *  que hubiera ahi.
	 *
	 *  Arriba de 127 se manda el byte tal cual: el tipo de terminal que
	 *  negociamos es tn6530-8, de ocho bits. Es lo razonable, pero no
	 *  esta confirmado contra el manual ni contra una captura -- si
	 *  aparece evidencia de otra cosa, se cambia aca.                    */
	if (keycode < 0)
	{
		return;
	}
	if (keycode > 127)
	{
		if (keycode <= 255)
		{
			const char byte8 = (char)(unsigned char)keycode;
			NotifyListener(&byte8, 1);
		}
		return;
	}
	if (ctrl)
	{
		if (fn)
		{
			if (m_sendCursorWithFn)
			{
				if (m_protectMode)
				{
					return;						
				}
				else
				{
					return;
				}
			}
		}
		if (m_localCmdCtl[m_pressedKey] != 0)
		{
			NotifyListener(m_localCmdCtl[m_pressedKey]);
			return;
		}
		NotifyListener(m_crtl[m_pressedKey], strlen(m_crtl[m_pressedKey]));
	}
	else if (shift)
	{
		if (fn)
		{
			if (m_sendCursorWithFn)
			{
				if (m_protectMode)
				{
					sb.Append((char)1);
					sb.Append(shiftFn[m_pressedKey]);
					sb.Append((char)(m_listener->KeyGetPage() + 0x20));
					m_listener->KeyGetStartFieldASCII(&sb);
					sb.Append(((char)3));
					sb.Append(((char)0));
					NotifyListener(sb.GetChars(), 7);
					return;						
				}
				else
				{
					sb.Append((char)1);
					sb.Append(shiftFn[m_pressedKey]);
					sb.Append((char)(m_listener->KeyGetCursorX() + 0x20));
					sb.Append((char)(m_listener->KeyGetCursorY() + 0x20));
					sb.Append(((char)13));
					NotifyListener(sb.GetChars(), 5);
					return;
				}
			}
		}
		if (m_localCmd[m_pressedKey] != 0)
		{
			NotifyListener(m_localCmd[m_pressedKey]);
			return;
		}
		NotifyListener(this->m_shift[m_pressedKey], strlen(this->m_shift[m_pressedKey]));
	}
	else if (alt)
	{
		if (fn)
		{
			if (m_sendCursorWithFn)
			{
				if (m_protectMode)
				{
					return;						
				}
				else
				{
					return;
				}
			}
		}
		if (m_localCmdAlt[m_pressedKey] != 0)
		{
			NotifyListener(m_localCmdAlt[m_pressedKey]);
			return;
		}
		NotifyListener(this->m_alt[m_pressedKey], strlen(this->m_alt[m_pressedKey]));
	}
	else
	{
		if (fn)
		{
			if (m_sendCursorWithFn)
			{
				if (m_protectMode)
				{
				//	notifyListener((char)1 + plainFn[pressedKey] + (char)(listener.getPage() + 0x20) + listener.getStartFieldASCII() + ((char)3) + "" + ((char)0));
				//	return;						
				//}
				//else
				//{
				//	notifyListener((char)1 + plainFn[pressedKey] + (char)(listener.getCursorX() + 0x20) + ""   + (char)(listener.getCursorY() + 0x20) + "" + ((char)13));
				//	return;
					sb.Append((char)1);
					sb.Append(plainFn[m_pressedKey]);
					sb.Append((char)(m_listener->KeyGetPage() + 0x20));
					m_listener->KeyGetStartFieldASCII(&sb);
					sb.Append(((char)3));
					sb.Append(((char)0));
					NotifyListener(sb.GetChars(), 7);
					return;						
				}
				else
				{
					sb.Append((char)1);
					sb.Append(plainFn[m_pressedKey]);
					sb.Append((char)(m_listener->KeyGetCursorX() + 0x20));
					sb.Append((char)(m_listener->KeyGetCursorY() + 0x20));
					sb.Append(((char)13));
					NotifyListener(sb.GetChars(), 5);
					return;
				}
			}
		}
		if (m_pressedKey == 13 && m_enterKeyOn && m_protectMode)
		{
			sb.Append((char)1);
			sb.Append("V");
			sb.Append((char)(m_listener->KeyGetPage() + 0x20));
			sb.Append((char)(m_listener->KeyGetCursorX() + 0x20));
			sb.Append((char)(m_listener->KeyGetCursorY() + 0x20));
			sb.Append((char)3);
			sb.Append((char)0);
			NotifyListener(sb.GetChars(), 7);

			/*  FASE 04: faltaba este return.
			 *
			 *  En modo protegido Enter es una tecla AID: transmite y le cede
			 *  el turno al host, que decide donde queda el cursor. Sin el
			 *  return se caia ademas al comando local, que hacia un Tab al
			 *  campo siguiente. O sea que el cursor bajaba DOS veces: una
			 *  nuestra y otra del host al contestar el AID. Se ve enseguida
			 *  en TEDIT.
			 *
			 *  Todos los demas caminos AID -- las teclas de funcion -- ya
			 *  devolvian aca mismo; este era el unico que seguia de largo. */
			return;
		}
		if (m_localCmd[m_pressedKey] != 0)
		{
			NotifyListener(m_localCmd[m_pressedKey]);
			return;
		}
		if ( ! fn )
		{
			NotifyListener(m_plain[m_pressedKey], strlen(m_plain[m_pressedKey]));
		}
	}
}

const char *Keys::ansiChar[] = {
					TDM_ESC "@", TDM_ESC "A", TDM_ESC "B", TDM_ESC "C", TDM_ESC "D",
					TDM_ESC "E", TDM_ESC "F", TDM_ESC "G", "\b", "\t", 
					"\r", TDM_ESC "H", TDM_ESC "I", "\n", TDM_ESC "J",
					TDM_ESC "[K", "0", TDM_ESC "[U", TDM_ESC "[H", TDM_ESC "[\24H", 
					TDM_ESC "[@", TDM_ESC "[P", "", TDM_ESC "[A", TDM_ESC "[B", 
					TDM_ESC "[D", TDM_ESC "[C", TDM_ESC, "\0", "\0", 
					"\30", "\31", " ", "!", "\"", 
					"#", "$", "%", "&", "\"", "(", ")", "*", "+", ",", 
					"-", ".", "/", "0", "1", "2", "3", "4", "5", "6", 
					"7", "8", "9", ":", ";", "<", "=", ">", "?", "@", 
					"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", 
					"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", 
					"U", "V", "W", "X", "Y", "Z", "[", "\\", "]", "^", 
					"_", "`", "a", "b", "c", "d", "e", "f", "g", "h", 
					"i", "j", "k", "l", "m", "n", "o", "p", "q", "r", 
					"s", "t", "u", "v", "w", "x", "y", "z", "{", "|", 
					"}", "~",
					/* FASE 03: el indice 127 (DEL). Faltaba en las dos tablas
					 * ANSI -- 127 entradas contra 128 en las otras cuatro --
					 * asi que tecleando un DEL con el juego ANSI se leia
					 * fuera del arreglo. */
					"\177"
					};

const char *Keys::ansiCharCtl[] = {
					"", "", "", "", "", "", "", "", "", 
					  "", "", "", "", "", "", "", "", "",
					"", "", "", "", "", "", "", "","", 
					"", "", "", "","", " ", "", "","", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "\0", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "\0", "\1", "\2", "\3", "\4", "\5", "\6", 
					"\7", "", "\t", "\10", "\11", "\12", "\13", "\14", "\15", "\16", "\17", 
					"\18", "\19", "\20", "\21", "\22", "\23", "\24", "\25", "\26", "\27", "\28", 
					"\29", "\30", "", "", "\1", "\2", "\3", "\4", "\5", "\6", "\7", 
					"", "\t", "\10", "\11", "\12", "\13", "\14", "\15", "\16", "\17", "\18", 
					"\19", "\20", "\21", "\22", "\23", "\24", "\25", "\26", "", "", "", 
					"", "" 
					};
const char *Keys::ansiCharAlt[] = {
					"", "", "", "", "", "", "", "", "", 
					  "", "", "", "", "", "", "", "", "",
					"", "", "", "", "", "", "", "","", 
					"", "", "", "","", " ", "", "","", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "" 
					};
const char *Keys::ansiCharShift[] = {
					TDM_ESC "@", TDM_ESC "A", TDM_ESC "B", TDM_ESC "C", TDM_ESC "D",
					TDM_ESC "E", TDM_ESC "F", TDM_ESC "G", "\b", "\t", 
					"\r", TDM_ESC "H", TDM_ESC "I", "\n", TDM_ESC "J",
					TDM_ESC "[K", "0", TDM_ESC "[U", TDM_ESC "[H", TDM_ESC "[\24H", 
					TDM_ESC "[@", TDM_ESC "[P", "", TDM_ESC "[A", TDM_ESC "[B", 
					TDM_ESC "[D", TDM_ESC "[C", TDM_ESC, "\0", "\0", 
					"\30", "\31", " ", "!", "\"", 
					"#", "$", "%", "&", "\"", "(", ")", "*", "+", ",", 
					"-", ".", "/", "0", "1", "2", "3", "4", "5", "6", 
					"7", "8", "9", ":", ";", "<", "=", ">", "?", "@", 
					"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", 
					"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", 
					"U", "V", "W", "X", "Y", "Z", "[", "\\", "]", "^", 
					"_", "`", "a", "b", "c", "d", "e", "f", "g", "h", 
					"i", "j", "k", "l", "m", "n", "o", "p", "q", "r", 
					"s", "t", "u", "v", "w", "x", "y", "z", "{", "|", 
					"}", "~",
					/* FASE 03: el indice 127 (DEL). Faltaba en las dos tablas
					 * ANSI -- 127 entradas contra 128 en las otras cuatro --
					 * asi que tecleando un DEL con el juego ANSI se leia
					 * fuera del arreglo. */
					"\177"
					};
const int Keys::ansiLocal[] = {
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					/* FASE 03: faltaban ocho entradas (120 de 128); un indice
					 * de 120 a 127 leia fuera del arreglo. */
					0,0,0,0,0,0,0,0
					};
const int Keys::ansiLocalCtl[] = {
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					/* FASE 03: faltaban ocho entradas (120 de 128); un indice
					 * de 120 a 127 leia fuera del arreglo. */
					0,0,0,0,0,0,0,0
					};
const int Keys::ansiLocalAlt[] = {
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					/* FASE 03: faltaban ocho entradas (120 de 128); un indice
					 * de 120 a 127 leia fuera del arreglo. */
					0,0,0,0,0,0,0,0
					};

const char *Keys::convChar[] = {
					"@", "A", "B", "C", "D",
					"E", "F", "G",  "\b", "\t",
					"\r", "H", "I", "\n", "J", 
					"K", "\16", "\17", "\0", "\0", 
					"\20", TDM_NAK, "\22", "\23", "\11", "\r", "\b", TDM_ESC, "\t", "\0", 
					"\30", "\31", " ", "!", "\"", "#", "$", "%", "&", "\"", 
					"(", ")", "*", "+", ",", "-", ".", "/", "0", "1", 
					"2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", 
					"=", ">", "?", "@", "A", "B", "C", "D", "E", "F", "G", 
					"H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", 
					"S", "T", "U", "V", "W", "X", "Y", "Z", "[", "\\", "]", 
					"^", "_", "`", "a", "b", "c", "d", "e", "f", "g", "h", 
					"i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", 
					"t", "u", "v", "w", "x", "y", "z", "{", "|", "}", "~", 
					/* FASE 03: era "\127", que en C es octal y da 0x57 (la letra
					 * W). El indice 127 de estas tablas es el DEL. */
					"\177" 
					};
const char *Keys::convCharCtl[] = {
					"", "", "", "", "", "", "", "", "", 
					  "", "", "", "", "", "", "", "", "",
					"", "", "", "", "", "", "", "","", 
					"", "", "", "","", " ", "", "","", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "\0", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "\0", "\1", "\2", "\3", "\4", "\5", "\6", 
					"\7", "", "\t", "\10", "\11", "\12", "\13", "\14", "\15", "\16", "\17", 
					"\18", "\19", "\20", "\21", "\22", "\23", "\24", "\25", "\26", "\27", "\28", 
					"\29", "\30", "", "", "\1", "\2", "\3", "\4", "\5", "\6", "\7", 
					"", "\t", "\10", "\11", "\12", "\13", "\14", "\15", "\16", "\17", "\18", 
					"\19", "\20", "\21", "\22", "\23", "\24", "\25", "\26", "", "", "", 
					"", "" 
					};
const char *Keys::convCharAlt[] = {
					"", "", "", "", "", "", "", "", "", 
					  "", "", "", "", "", "", "", "", "",
					"", "", "", "", "", "", "", "","", 
					"", "", "", "","", " ", "", "","", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "" 
					};
const char *Keys::convCharShift[] = {
					"'", "a", "b", "c", "e",
					"e", "f", "g",  "", "",
					"\r", "h", "i", "\n", "j", 
					"k", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", 
					"", "", " ", "!", "\"", "#", "$", "%", "&", "\"", 
					"(", ")", "*", "+", ",", "-", ".", "/", "0", "1", 
					"2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", 
					"=", ">", "?", "@", "A", "B", "C", "D", "E", "F", "G", 
					"H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", 
					"S", "T", "U", "V", "W", "X", "Y", "Z", "[", "\\", "]", 
					"^", "_", "`", "A", "B", "C", "D", "E", "F", "G", "H", 
					"I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", 
					"T", "U", "V", "W", "X", "Y", "Z", "{", "|", "}", "~", 
					/* FASE 03: era "\127", que en C es octal y da 0x57 (la letra
					 * W). El indice 127 de estas tablas es el DEL. */
					"\177" 
					};
const int Keys::convLocal[] = {
					0,0,0,0,0,0,0,7,8,9,
					10,0,0,13,0,0,16,17,18,19,
					20,21,22,23,24,25,26,27,28,29,
					30,31,32,33,34,35,36,37,38,39,
					40,41,42,43,44,45,46,47,48,49,
					50,51,52,53,54,55,56,57,58,59,
					60,61,62,63,64,65,66,67,68,69,
					70,71,72,73,74,75,76,77,78,79,
					80,81,82,83,84,85,86,87,88,89,
					90,91,92,93,94,95,96,97,98,99,
					100,101,102,103,104,105,106,107,108,109,
					110,111,112,113,114,115,116,117,118,119,
					120,121,122,123,124,125,126,127
					};
/*						0,0,0,0,0,0,0,0,0,9,
					0,0,0,0,0,0,16,17,18,19,
					0,0,0,23,24,0,26,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0
					};*/
const int Keys::convLocalCtl[] = {
					0,0,0,0,0,0,0,0,8,0,
					0,0,0,13,0,0,16,17,18,19,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					/* FASE 03: faltaban ocho entradas (120 de 128); un indice
					 * de 120 a 127 leia fuera del arreglo. */
					0,0,0,0,0,0,0,0
					};
const int Keys::convLocalAlt[] = {
					0,0,0,0,0,0,0,0,8,0,
					0,0,0,13,0,0,0,0,0,19,
					0,0,0,23,24,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,49,
					50,51,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					/* FASE 03: faltaban ocho entradas (120 de 128); un indice
					 * de 120 a 127 leia fuera del arreglo. */
					0,0,0,0,0,0,0,0
					};
const char *Keys::blockChar[] = {
					"@", "A", "B", "C", "D",
					"E", "F", "G",  TDM_BACKSPACE, "\t",
					"\r", "H", "I", "\n", "J", 
					"K", "\16", "\17", "\0", "\0", 
					"\20", TDM_NAK, "\22", "\23", "\11", "\r", TDM_BACKSPACE, TDM_ESC, "\t", "\0", 
					"\30", "\31", " ", "!", "\"", "#", "$", "%", "&", "\"", 
					"(", ")", "*", "+", ",", "-", ".", "/", "0", "1", 
					"2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", 
					"=", ">", "?", "@", "A", "B", "C", "D", "E", "F", "G", 
					"H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", 
					"S", "T", "U", "V", "W", "X", "Y", "Z", "[", "\\", "]", 
					"^", "_", "`", "a", "b", "c", "d", "e", "f", "g", "h", 
					"i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", 
					"t", "u", "v", "w", "x", "y", "z", "{", "|", "}", "~", 
					/* FASE 03: era "\127", que en C es octal y da 0x57 (la letra
					 * W). El indice 127 de estas tablas es el DEL. */
					"\177" 
					};
const char *Keys::blockCharCtl[] = {
					"", "", "", "", "", "", "", "", "", 
					  "", "", "", "", "", "", "", "", "",
					"", "", "", "", "", "", "", "","", 
					"", "", "", "","", " ", "", "","", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "\0", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "\0", "\1", "\2", "\3", "\4", "\5", "\6", 
					"\7", "", "\t", "\10", "\11", "\12", "\13", "\14", "\15", "\16", "\17", 
					"\18", "\19", "\20", "\21", "\22", "\23", "\24", "\25", "\26", "\27", "\28", 
					"\29", "\30", "", "", "\1", "\2", "\3", "\4", "\5", "\6", "\7", 
					"", "\t", "\10", "\11", "\12", "\13", "\14", "\15", "\16", "\17", "\18", 
					"\19", "\20", "\21", "\22", "\23", "\24", "\25", "\26", "", "", "", 
					"", "" 
					};
const char *Keys::blockCharAlt[] = {
					"", "", "", "", "", "", "", "", "", 
					  "", "", "", "", "", "", "", "", "",
					"", "", "", "", "", "", "", "","", 
					"", "", "", "","", " ", "", "","", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", "", 
					"", "" 
					};
const char *Keys::blockCharShift[] = {
					"'", "a", "b", "c", "e",
					"e", "f", "g",  "", "",
					"\r", "h", "i", "\n", "j", 
					"k", "", "", "", "", 
					"", "", "", "", "", "", "", "", "", "", 
					"", "", " ", "!", "\"", "#", "$", "%", "&", "\"", 
					"(", ")", "*", "+", ",", "-", ".", "/", "0", "1", 
					"2", "3", "4", "5", "6", "7", "8", "9", ":", ";", "<", 
					"=", ">", "?", "@", "A", "B", "C", "D", "E", "F", "G", 
					"H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", 
					"S", "T", "U", "V", "W", "X", "Y", "Z", "[", "\\", "]", 
					"^", "_", "`", "A", "B", "C", "D", "E", "F", "G", "H", 
					"I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", 
					"T", "U", "V", "W", "X", "Y", "Z", "{", "|", "}", "~", 
					/* FASE 03: era "\127", que en C es octal y da 0x57 (la letra
					 * W). El indice 127 de estas tablas es el DEL. */
					"\177" 
					};
const int Keys::blockLocal[] = {
					0,0,0,0,0,0,0,7,8,9,
					10,0,0,13,0,0,16,17,18,19,
					20,21,22,23,24,25,26,27,28,29,
					30,31,32,33,34,35,36,37,38,39,
					40,41,42,43,44,45,46,47,48,49,
					50,51,52,53,54,55,56,57,58,59,
					60,61,62,63,64,65,66,67,68,69,
					70,71,72,73,74,75,76,77,78,79,
					80,81,82,83,84,85,86,87,88,89,
					90,91,92,93,94,95,96,97,98,99,
					100,101,102,103,104,105,106,107,108,109,
					110,111,112,113,114,115,116,117,118,119,
					120,121,122,123,124,125,126,127
					};
const int Keys::blockLocalCtl[] = {
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0
					};
const int Keys::blockLocalAlt[] = {
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0,
					0,0,0,0,0,0,0,0,0,0
					};

/*  Indexadas por el codigo SPC_*, no por el numero de tecla: SPC_F9 vale 11,
 *  de ahi los huecos. Las cuatro ultimas entradas son F13..F16, agregadas en
 *  la fase 03 en los indices 30..33.
 *
 *  El codigo de F16 en la fila plana (0x4F) esta CONFIRMADO contra una captura
 *  real de VIEWSYS. Los de F13..F15 y toda la fila con shift se derivan del
 *  patron 0x40+(n-1) y 0x61+(n-2), que cuadra con las doce ya existentes,
 *  pero no estan confirmados contra el host.                                */
const char *Keys::plainFn[] = {
	"@", "A", "B", "C", "D", "E", "F", "G", "", "", "", "H", "I", "", "J", "K",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"L", "M", "N", "O"                       /* F13  F14  F15  F16 */
};
const char *Keys::shiftFn[] = {
	"'", "a", "b", "c", "d", "e", "f", "g", "", "", "", "h", "i", "", "j", "k",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"l", "m", "n", "o"                       /* F13  F14  F15  F16 */
};

/* ------------------------------------------------------------------ */

void Keys::CheckTableSizes()
{
	/*  Las tablas se indexan con el codigo de la tecla, 0 a 127. Que a una
	 *  le falte una entrada no se ve al compilar ni al correr: se lee fuera
	 *  del arreglo. Paso -- ansiChar y ansiCharShift tenian 127 -- asi que
	 *  el tamano queda fijado aca.                                        */
	static_assert(sizeof(ansiChar)      / sizeof(char *) == 128, "ansiChar");
	static_assert(sizeof(ansiCharCtl)   / sizeof(char *) == 128, "ansiCharCtl");
	static_assert(sizeof(ansiCharAlt)   / sizeof(char *) == 128, "ansiCharAlt");
	static_assert(sizeof(ansiCharShift) / sizeof(char *) == 128, "ansiCharShift");
	static_assert(sizeof(convChar)      / sizeof(char *) == 128, "convChar");
	static_assert(sizeof(convCharCtl)   / sizeof(char *) == 128, "convCharCtl");
	static_assert(sizeof(convCharAlt)   / sizeof(char *) == 128, "convCharAlt");
	static_assert(sizeof(convCharShift) / sizeof(char *) == 128, "convCharShift");
	static_assert(sizeof(blockChar)     / sizeof(char *) == 128, "blockChar");
	static_assert(sizeof(blockCharCtl)  / sizeof(char *) == 128, "blockCharCtl");
	static_assert(sizeof(blockCharAlt)  / sizeof(char *) == 128, "blockCharAlt");
	static_assert(sizeof(blockCharShift)/ sizeof(char *) == 128, "blockCharShift");

	static_assert(sizeof(ansiLocal)     / sizeof(int) >= 128, "ansiLocal");
	static_assert(sizeof(ansiLocalCtl)  / sizeof(int) >= 128, "ansiLocalCtl");
	static_assert(sizeof(ansiLocalAlt)  / sizeof(int) >= 128, "ansiLocalAlt");
	static_assert(sizeof(convLocal)     / sizeof(int) >= 128, "convLocal");
	static_assert(sizeof(convLocalCtl)  / sizeof(int) >= 128, "convLocalCtl");
	static_assert(sizeof(convLocalAlt)  / sizeof(int) >= 128, "convLocalAlt");
	static_assert(sizeof(blockLocal)    / sizeof(int) >= 128, "blockLocal");
	static_assert(sizeof(blockLocalCtl) / sizeof(int) >= 128, "blockLocalCtl");
	static_assert(sizeof(blockLocalAlt) / sizeof(int) >= 128, "blockLocalAlt");

	/*  Y las de teclas de funcion, que van por LAST_SPC. */
	static_assert(sizeof(plainFn) / sizeof(char *) == LAST_SPC, "plainFn");
	static_assert(sizeof(shiftFn) / sizeof(char *) == LAST_SPC, "shiftFn");
}
