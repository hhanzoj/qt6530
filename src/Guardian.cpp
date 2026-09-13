#include <ctype.h>
#include <spl/debug.h>
#include <spl/Log.h>
#include <spl/StringBuffer.h>
#include <vt6530/Guardian.h>

#define CHAR_ESC '\27'
#define CHAR_BELL  7
#define CHAR_BKSPACE  8
#define CHAR_HTAB  9
#define CHAR_LF  10
#define CHAR_CR  13

InputElementBuffer::InputElementBuffer()
{
	m_pos = 0;
}

InputElementBuffer::~InputElementBuffer()
{
	Clear();
}

int InputElementBuffer::Size()
{
	return m_pos;
}

void InputElementBuffer::AddElement(const char *str)
{
	char *buf = new char[strlen(str)+1];
	strcpy(buf, str);
	ASSERT(m_pos < SIZE_ELEM);
	m_elements[m_pos++] = buf;
}

void InputElementBuffer::AddElement(char c)
{
	char *buf = new char[2];
	buf[0] = c;
	buf[1] = '\0';
	ASSERT(m_pos < SIZE_ELEM);
	m_elements[m_pos++] = buf;
}

char *InputElementBuffer::ElementAt(int index)
{
	ASSERT(index < SIZE_ELEM);
	ASSERT_PTR(m_elements[index]);
	return m_elements[index];
}

void InputElementBuffer::Clear()
{
	for (int x = 0; x < m_pos; x++)
	{
		/*  FASE 03: era "delete", y los dos AddElement reservan con
		 *  "new char[]". Liberar un arreglo con delete sin corchetes es
		 *  comportamiento indefinido; ASan lo marca como
		 *  alloc-dealloc-mismatch. En la practica sobrevivio quince anos
		 *  porque char no tiene destructor y las dos implementaciones de
		 *  la biblioteca de C++ que uso el autor devolvian el bloque
		 *  igual. No hay razon para seguir apostando a eso.              */
		delete [] m_elements[x];
		m_elements[x] = NULL;
	}
	m_pos = 0;
}

#if defined(DEBUG) || defined(_DEBUG)
void InputElementBuffer::CheckMem() const
{
	for ( int x = 0; x < m_pos; x++ )
	{
		DEBUG_NOTE_MEM_ALLOCATION(m_elements[x]);
	}
}
void InputElementBuffer::ValidateMem() const
{
	for ( int x = 0; x < m_pos; x++ )
	{
		ASSERT_PTR(m_elements[x]);
	}
}
#endif

Guardian::Guardian
(
	Vector<Vt6530EventListener *> *listeners, 
	TextDisplay *display, 
	Keys *keys, 
	Telnet *telnet
) : m_strStack(), m_accum(), m_keyBuffer(), m_blockBuf()
{
	this->m_keys = keys;
	this->m_telnet = telnet;
	this->m_display = display;
	m_state = 0;
	m_listeners = listeners;
}

Guardian::~Guardian()
{
}

bool Guardian::IsConvMode() 
{ 
	return !m_display->IsBlockMode(); 
}

void Guardian::DispatchResetLine()
{
	for (int x = 0; x < m_listeners->Count(); x++)
	{
		m_listeners->ElementAt(x)->Vt6530_OnResetLine();
	}
	ValidateMem();
}

void Guardian::DispatchEnquire()
{
	for (int x = 0; x < m_listeners->Count(); x++)
	{
		m_listeners->ElementAt(x)->Vt6530_OnEnquire();
	}
	ValidateMem();
}

void Guardian::ProcessRemoteString(const char *inp, const int inplen)
{		
	int pos = 0;
	int dataTypeTableCount = 0;

	ValidateMem();

	while (pos < inplen)
	{
		byte ch = (byte)inp[pos++];
		
		switch (m_state)
		{
			case 0:
				if (ch > 31)
				{
					m_accum.Append((char)ch);
					break;
				}
				if (m_accum.Length() > 0)
				{
					if (m_display->GetProtectMode())
					{
						m_display->WriteBuffer(m_accum.GetChars());
					}
					else
					{
						m_display->WriteDisplay(m_accum.GetChars());
					}
					m_accum.SetLength(0);
				}
				switch (ch)
				{
					case 0x00:
						break;
					case 0x01:
						// SOH
						m_state = 5000;
						break;
					case 0x04:
						// reset the line
						m_display->ResetMdt();
						DispatchResetLine();
						break;
					case 0x05:
						// ENQ
						DispatchEnquire();
						break;
					case 0x07:
						// BELL
						m_display->Bell();
						break;
					case 0x08:
						// Backspace
						m_display->Backspace();
						break;
					case 0x09:
						// HTab
						m_display->Tab();
						break;
					case 0x0A:
						// NL
						m_display->Linefeed();
						break;
					case 0x0D:
						// CR
						m_display->CarageReturn();
						break;
					case 0x0E:
						// shift out to G1 character set
						Log::WriteInfo("G1 char set");
						break;
					case 0x0F:
						// Shift in to G0 character set
						Log::WriteInfo("G0 char set");
						break;
					case 0x13:
						// set cursor address
						m_state = 42;
						break;
					case 0x1B:
						// ESC
						m_state = 1;
						break;
					case 0x11:
						// set buffer address (block mode)
						m_state = 56;
						break;
					case 0x1D:
						// start field
						m_state = 59;
						break;
					default:
						Log::WriteError("Unknown command char %d", (int)ch);
						break;
				}
				break;
			case 1:
				// ESC
				switch (ch)
				{
					case '0':
						// print screen
						m_state = 0;
						m_display->PrintScreen();
						break;
					case '1':
						// Set tab at cursor location
						m_state = 0;
						m_display->SetTab();
						break;
					case '2':
						// Clear tab
						m_display->ClearTab();
						m_state = 0;
						break;
					case '3':
						// Clear all tabs
						m_display->ClearAllTabs();
						m_state = 0;
						break;
					case '6':
						// Set video attributes
						m_state = 44;
						break;
					case '7':
						// Set video prior condition register
						m_state = 46;
						break;
					case ';':
						// Display page
						m_state = 48;
						break;
					case '?':
						/*  ESC ? -- leer configuracion del terminal.
						 *
						 *  FASE 03, y es el que colgaba a TEDIT.
						 *
						 *  La respuesta de conversacional terminaba en
						 *  "\13", que en C es OCTAL y da 0x0B, no el CR
						 *  que esperan las otras tres respuestas de
						 *  consulta (ESC ^, ESC _ y ESC a, que usan el 13
						 *  decimal). TEDIT arranca preguntando ESC ^ y
						 *  ESC ?, y se queda esperando el CR de la segunda:
						 *  la pantalla no se dibujaba hasta que otra tecla
						 *  lo destrabara. Es el mismo defecto de escapes
						 *  octales que ya tenia TDM_ESC.
						 *
						 *  Y de paso el otro: la respuesta de modo bloque
						 *  termina en ETX NUL, pero se mandaba con strlen(),
						 *  que corta justo en el NUL. Nunca se enviaba.
						 *  Por eso ahora se arma con largo explicito.     */
						{
							static const char cfg[] =
								"\1!A 2B72C 0D 0E 0F 0G 0H15I 3J 0L 0M 1N 0O "
								"0P 0X 6S 0T 0U 1V 1W 1e 1f 0i 1h10";
							byte resp[sizeof(cfg) + 2];
							int n = 0;
							for (const char *p = cfg; *p != '\0'; p++)
							{
								resp[n++] = (byte)*p;
							}
							if (m_display->GetBlockMode())
							{
								resp[n++] = 3;    /* ETX */
								resp[n++] = 0;    /* NUL */
							}
							else
							{
								resp[n++] = 13;   /* CR  */
							}
							m_telnet->SendRaw(resp, n);
						}
						m_state = 0;
						break;
					case '@':
						// Delay one second
						m_state = 0;
						break;
					case 'A':
						// Cursor up
						m_display->MoveCursorUp();
						m_state = 0;
						break;
					case 'C':
						// Cursor right
						m_display->MoveCursorRight();
						m_state = 0;
						break;
					case 'F':
						// Cursor home down
						m_display->End();
						m_state = 0;
						break;
					case 'H':
						// Cursor home
						m_display->Home();
						m_state = 0;
						break;
					case 'I':
						// Clear memory to spaces
						// NOTE: in protect mode, this gets a block arg
						if (m_display->GetProtectMode())
						{
							m_state = 15000;
							continue;
						}
						m_display->ClearPage();
						m_state = 0;
						break;
					case 'J':
						// Erase to end of page/memory
						m_display->ClearToEnd();
						m_state = 0;
						break;
					case 'K':
						// Erase to end of line/field
						m_display->ClearEOL();
						m_state = 0;
						break;
					case '^':
						// Read terminal status
						if (m_display->GetBlockMode())
						{
							byte status[] = {1, 63, 66, 70, 67, 67, 94, 64, 3, 0, 4};
							m_telnet->SendRaw(status, 11);
						}
						else
						{
							byte status[] = {1, 63, 67, 70, 67, 13}; //67, 94, 64, 3, 0};
							m_telnet->SendRaw(status, 6);
						}
						m_state = 0;
						break;
					case '_':
						// Read firmware revision level
						if (m_display->GetBlockMode())
						{
							Log::WriteInfo("Read firmware revision level");
						}
						else
						{
							byte status[] = {1, 35, 67, 48, 48, 84, 79, 67, 48, 48, 13};
							m_telnet->SendRaw(status, 11);
						}
						m_state = 0;
						break;
					case 'a':
						// Read cursor address
						{
							byte cursorPos[] = {1, '_', '!', 0, 0, 13};
							cursorPos[3] = m_display->GetCursorRow();
							cursorPos[4] = m_display->GetCursorCol();
							m_telnet->SendRaw(cursorPos, 6);
						}
						m_state = 0;
						break;
					case 'b':
						// Unlock keyboard
						m_keys->UnlockKeyboard();
						m_display->SetKeysUnlocked();
						m_state = 10000;
						break;
					case 'c':
						// Lock keyboard
						m_keys->LockKeyboard();
						m_display->SetKeysLocked();
						m_state = 0;
						break;
					case 'd':
						// Simulate function key
						m_keys->LockKeyboard();
						m_state = 50;
						break;
					case 'f':
						// Disconnect modem
						Log::WriteInfo("Disconnect modem");
						m_state = 0;
						break;
					case 'o':
						// Write to message field
						m_state = 52;
						break;
					case 'v':
						// set terminal configuration
						m_state = 54;
						break;
					case 'x':
						//Set IO device configuration
						Log::WriteInfo("Set IO device configuration");
						m_state = 0;
						break;
					case 'y':
						// Read IO device configuration
						Log::WriteInfo("Read IO device configuration");
						m_state = 0;
						break;
					case '{':
						// Write to file or device driver
						Log::WriteInfo("Write to file or device driver");
						m_state = 0;
						break;
					case '}':
						// Write/read to file or device driver
						Log::WriteInfo("Write/read to file or device driver");
						m_state = 0;
						break;
					case '-':
						// extended CSI sequence
						m_state = 3;
						break;
					case ':':
						// select page
						m_state = 66;
						break;
					case '<':
						// read buffer
						//if (ASSERT.debug > 0)
						//	display.dumpScreen(Logger.out);
						m_blockBuf.SetLength(0);
						m_display->ReadBufferUnprotectIgnoreMdt(&m_blockBuf, 0, 0, m_display->GetNumRows()-1, m_display->GetNumColumns()-1);
						m_telnet->SendRaw((byte *)m_blockBuf.GetChars(), m_blockBuf.Length());
						m_blockBuf.SetLength(0);
						m_state = 0;
						break;
					case '=':
						// Read with address
						m_state = 67;
						break;
					case '>':
						// Reset modified data tags
						m_display->ResetMdt();
						m_state = 0;
						break;
					case 'L':
						m_display->LineDown();
						m_state = 0;
						break;
					case 'M':
						m_display->DeleteLine();
						m_state = 0;
						break;
					case 'N':
						// disable local line editing until
						// 1. ESC q
						// 2. Exit block mode
						// 3. protect to nonprotect submode
						Log::WriteInfo("Disable local line editing");
						m_state = 0;
						break;
					case 'O':
						// insert char
						m_display->InsertChar();
						m_state = 0;
						break;
					case 'P':
						// delete char
						m_display->DeleteChar();
						m_state = 0;
						break;
					case 'S':
						// roll up
						Log::WriteInfo("Roll up");
						m_state = 0;
						break;
					case 'T':
						// roll down
						m_state = 0;
						m_display->LineDown();
						break;
					case 'U':
						// page down
						Log::WriteInfo("Page down");
						m_state = 0;
						break;
					case 'V':
						// page up
						Log::WriteInfo("Page up");
						m_state = 0;
						break;
					case 'W':
						//  Enter protect mode
						m_display->SetProtectMode();
						m_keys->SetProtectMode();
						m_state = 0;
						break;
					case 'X':
						// exit protect mode
						m_display->ExitProtectMode();
						m_keys->ExitProtectMode();
						m_state = 0;
						break;
					case '[':
						// start field extended
						m_state = 71;
						break;
					case ']':
						// Read with address all
						if (m_display->GetProtectMode())
						{
							// same as ESC =
							m_state = 67;
							continue;
						}
						m_state = 75;
						break;
					case 'i':
						// back tab
						m_display->Backtab();
						m_state = 0;
						break;
					case 'p':
						// set max page num
						m_state = 81;
						break;
					case 'q':
						// reinitialize
						m_display->Init();
						m_display->SetProtectMode();
						m_display->ExitProtectMode();
						m_state = 10000;
						break;
					case 'r':
						// Define data type table
						m_state = 84;
						break;
					case 'u':
						// define enter key function
						m_state = 82;
						break;
					default:
						Log::WriteInfo("Unknown ESC %d", (int)ch);
						m_state = 0;
						break;
				}
				break;
			case 3:
				if (isdigit(ch))
				{
					m_state = 24;
					m_accum.Append((char)ch);
					continue;
				}
				switch (ch)
				{
					case 'c':
						m_strStack.AddElement("7");
						m_state = 30;
						break;
					case 'e':
						// Get machine name 3-28
						{
							byte name[] = {1, '&', 'j', 'o', 'h', 'n', 13};
							m_telnet->SendRaw(name, 7);
						}
						m_state = 0;
						break;
					case 'V':
						m_state = 39;
						break;
					case 'W':
						// Report Exec code 3-34
						{
							byte code[] = {1, '?', (1<<6) | 1, 'F', 'D', 13};
						}
						m_state = 0;
						break;
					case 'J':
						m_blockBuf.SetLength(0);
						m_display->ReadBufferUnprotect(&m_blockBuf, 0, 0, m_display->GetNumRows()-1, m_display->GetNumColumns()-1);
						m_telnet->SendRaw((byte *)m_blockBuf.GetChars(), m_blockBuf.Length());
						m_state = 0;
						break;
					default:
						Log::WriteInfo("Unknown ESC - %d", (int)ch);
						break;
				}
				break;
			case 39:
				if (ch == CHAR_CR)
				{
					// Execute local program
					Log::WriteInfo("Execute local program %s", m_accum.GetChars());
					m_accum.SetLength(0);
					m_state = 0;
					continue;
				}
				m_accum.Append((char)ch);
				break;
			case 24:
				if (isdigit(ch))
				{
					m_accum.Append((char)ch);
					m_state = 25;
					continue;
				}
				switch (ch)
				{
					case ';':
						m_strStack.AddElement(m_accum.GetChars());
						m_accum.SetLength(0);
						m_state = 34;
						break;
					case 'd':
						// Read string configuration param
						Log::WriteInfo("Read string config param %s", m_accum.GetChars());
						m_accum.SetLength(0);
						m_state = 0;
						break;
					case 'c':
						m_state = 30;
						break;
					default:
						Log::WriteInfo("Unknown ESC-%s %d", m_accum.GetChars(), (int)ch);
						break;
				}
				break;
			case 34:
				if (isdigit(ch))
				{
					m_accum.Append((char)ch);
					continue;
				}
				if (ch != ';')
				{
					Log::WriteInfo("Expected ';' in state 34; Got %d", (int)ch);
				}
				m_strStack.AddElement(m_accum.GetChars());
				m_accum.SetLength(0);
				m_state = 35;
				break;
			case 35:
				if (isdigit(ch))
				{
					m_accum.Append((char)ch);
					continue;
				}
				switch (ch)
				{
					case 'C':
						// set buffer address extended
						m_display->SetBufferRowCol(atoi(m_strStack.ElementAt(0)), atoi(m_accum.GetChars()));
						m_strStack.Clear();
						m_accum.SetLength(0);
						m_state = 0;
						break;
					case 'q':
						m_strStack.AddElement(m_accum.GetChars());
						m_strStack.AddElement("q");
						m_accum.SetLength(0);
						m_state = 36;
						break;
					case 'I':
						// Clear memory to spaces extended
						{
							int sr = m_strStack.ElementAt(0)[0];
							int sc = m_strStack.ElementAt(0)[1];
							int er = m_accum[0];
							int ec = m_accum[1];
							m_accum.SetLength(0);
							m_strStack.Clear();
							m_display->ClearBlock(sr, sc, er, ec);
							m_state = 0;
						}
						break;
					case ';':
						m_strStack.AddElement(m_accum.GetChars());
						m_state = 64;
						break;
					default:
						Log::WriteInfo("Unexpected char in 35: %d", (int)ch);
						break;
				}
				break;
			case 36:
				switch (ch)
				{
					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
					case '5':
					case '6':
					case '7':
					case '8':
					case '9':
					case 'A':
					case 'B':
					case 'C':
					case 'D':
					case 'E':
					case 'F':
						m_strStack.AddElement((char) ch);
						m_state = 37;
						continue;
				}
				m_strStack.AddElement(m_accum.GetChars());
				m_accum.SetLength(0);
				{
					char *p1;
					int p2, p3;

					p1 = m_strStack.ElementAt(0);
					if (p1[0] == '0')
					{
						// reset color map
						Log::WriteInfo("Reset color map");
					}
					else
					{
						Log::WriteInfo("Set color map");
						p2 = atoi(m_strStack.ElementAt(1));
						p3 = atoi(m_strStack.ElementAt(2));
						
						if (m_strStack.ElementAt(3)[0] != 'q')
						{
							Log::WriteInfo("State 36 Error");
						}
						for (int x = 0; x < p3-p2; x++)
						{
							// setColorMap(p2+x, Integer.parseInt((String)strStack.elementAt(x*2+4), 16), Integer.parseInt((String)strStack.elementAt(x*2+5), 16) );
							Log::WriteInfo("SetColorMap(%d, %s, %s);", p2+x, m_strStack.ElementAt(x*2+4), m_strStack.ElementAt(x*2+5));
						}
					}
					m_strStack.Clear();
				}
				break;
			case 37:
				switch (ch)
				{
					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
					case '5':
					case '6':
					case '7':
					case '8':
					case '9':
					case 'A':
					case 'B':
					case 'C':
					case 'D':
					case 'E':
					case 'F':
						m_strStack.AddElement((char) ch);
						m_state = 36;
						continue;
				}
				Log::WriteInfo("Bad hex in state 37: %d", (int)ch);
				break;
			case 30:
				if (ch > 31)
				{
					m_accum.Append((char)ch);
					continue;
				}
				if (ch == 0x12)
				{
					m_strStack.AddElement(m_accum.GetChars());
					m_accum.SetLength(0);
					continue;
				}
				if (ch != CHAR_CR)
				{
					Log::WriteInfo("Expected CR in 30; Got %d", (int)ch);
				}
				m_accum.SetLength(0);
				{
					int count = atoi(m_strStack.ElementAt(0));
					for (int x = 1; x < m_strStack.Size(); x++)
					{
						Log::WriteInfo("Parameter recived: %s", m_strStack.ElementAt(x));
					}
				}
				m_strStack.Clear();
				break;
			case 25:
				if (isdigit(ch))
				{
					m_accum.Append((char)ch);
					continue;
				}
				if (ch == ';')
				{
					m_strStack.AddElement(m_accum.GetChars());
					m_accum.SetLength(0);
					m_state = 27;
					continue;
				}
				Log::WriteInfo("Unexpected char in 25: %d", (int)ch);
				break;
			case 27:
				if (isdigit(ch))
				{
					m_accum.Append((char)ch);
					continue;
				}
				if (ch == 'D')
				{
					// set cursor position
					int curx = m_strStack.ElementAt(0)[0] - 32;
					int cury = m_strStack.ElementAt(1)[0] - 32;
					m_display->SetCursorRowCol(curx, cury);
				}
				else if (ch == 'O')
				{
					// write to AUX
					Log::WriteInfo("Write to AUX");
				}
				else
				{
					Log::WriteInfo("Unexpcted char in 27: %d", (int)ch);
				}
				break;
			case 42:
				m_strStack.AddElement((char) ch);
				m_state = 43;
				break;
			case 43:
				m_display->SetCursorRowCol( m_strStack.ElementAt(0)[0] - 0x20, (int)ch - 0x20);
				m_strStack.Clear();
				m_state = 0;
				break;
			case 44:
				/*  FASE 03. ESC 6 -- atributos de video -- nunca funciono.
				 *
				 *  Pasaba el byte del cable crudo a SetWriteAttribute, que
				 *  arranca con ASSERT((attr & MASK_CHAR) == 0): como el
				 *  byte del cable siempre cae en el byte bajo, la asercion
				 *  se disparaba SIEMPRE y el atributo nunca se aplicaba.
				 *
				 *  El decodificador ya existia: DecodeVideoAttrs, que mapea
				 *  bit0->normal, bit1->parpadeo, bit2->reverso,
				 *  bit3->INVISIBLE, bit4->subrayado. Estaba en uso, pero
				 *  solo por el camino de inicio de campo (WriteField), que
				 *  es el de modo bloque. El de conversacional se lo salteo.
				 *
				 *  El bit 3 importa: es como el 6530 oculta una clave en
				 *  conversacional -- no apagando el eco, sino escribiendo
				 *  invisible. El eco sigue siendo local.                 */
				m_display->SetWriteAttribute(
					m_display->DecodeVideoAttrs((int)ch));
				m_state = 0;
				break;
			case 46:
				/*  FASE 03. ESC 7 tenia el mismo problema de decodificacion
				 *  que ESC 6, y se arregla igual.
				 *
				 *  Queda una duda que NO se toca: TextDisplay::
				 *  SetPriorWriteAttribute llama a SetWriteAttribute, no a
				 *  SetVideoPriorCondition, que existe al lado. Parece un
				 *  copiar y pegar, pero el propio autor dejo escrito "Not
				 *  sure what this is supposed to do", asi que sin el manual
				 *  no hay con que decidir. Se deja como esta.            */
				m_display->SetPriorWriteAttribute(
					m_display->DecodeVideoAttrs((int)ch));
				m_state = 0;
				break;
			case 48:
				m_display->SetDisplayPage(ch - 0x20);
				m_state = 0;
				break;
			case 50:
				{
					byte fnKey[] = {1, ch, 0, 0, 13};
					fnKey[2] = m_display->GetCursorRow();
					fnKey[3] = m_display->GetCursorCol();
					m_telnet->SendRaw(fnKey, 5);
				}
				m_state = 0;
				break;
			case 52:
				/*  ESC o -- linea de mensaje, hasta el CR.
				 *
				 *  FASE 03: TEDIT mete un ESC 6 ADENTRO del mensaje, para
				 *  ponerle atributo de video. Como esto acumulaba todo tal
				 *  cual, la secuencia terminaba dibujada como texto: la
				 *  linea de estado mostraba "<1B>6%1) $DATA01..." en vez de
				 *  "1) $DATA01...".
				 *
				 *  La linea de estado de este emulador es una cadena sin
				 *  atributos, asi que el atributo no se puede aplicar; lo
				 *  que se puede es no ensuciarla. Se consume y se descarta.
				 *  Aplicarlo de verdad es trabajo de la fase 04, junto con
				 *  darle atributos a la linea de estado.                 */
				if (ch == 13)
				{
					m_display->WriteMessage(m_accum.GetChars());
					m_accum.SetLength(0);
					m_state = 0;
					continue;
				}
				if (ch == 0x1B)
				{
					m_state = 5200;
					break;
				}
				m_accum.Append((char)ch);
				break;

			case 5200:
				/*  Un ESC dentro del mensaje. Solo se conocen los de
				 *  atributo de video, que llevan un argumento. Cualquier
				 *  otro se registra y se devuelve al mensaje tal cual, para
				 *  no tragarse en silencio algo que no entendemos.       */
				if (ch == '6' || ch == '7')
				{
					m_state = 5201;
					break;
				}
				Log::WriteInfo("ESC %d dentro de la linea de mensaje", (int)ch);
				m_accum.Append((char)0x1B);
				m_accum.Append((char)ch);
				m_state = 52;
				break;

			case 5201:
				/*  El argumento del atributo. Se descarta. */
				m_state = 52;
				break;
			case 54:
				if (ch == 13)
				{
					/*  FASE 03: leia m_accum[0] sin comprobar que hubiera
					 *  algo. Con el payload de TEDIT entregado byte a byte
					 *  --como lo parte el TCP-- llega un ESC v con el cuerpo
					 *  vacio y esto se iba fuera del arreglo. Lo mismo el
					 *  m_accum[1] de la rama 'M'.                         */
					if (m_accum.Length() == 0)
					{
						Log::WriteInfo("ESC v con cuerpo vacio");
						m_accum.SetLength(0);
						m_state = 0;
						continue;
					}
					switch (m_accum[0])
					{
						case 'A':
							// cursor type
							break;
						case 'F':
							// language
							break;
						case 'G':
							// mode
							break;
						case 'M':
							// enter key mode
							if (m_accum.Length() < 2)
								Log::WriteInfo("ESC v M sin argumento");
							else if (m_accum[1] == '0')
								m_keys->SetEnterKeyOff();
							else
								m_keys->SetEnterKeyOn();
							break;
						case 'T':
							// normal intensity
							break;
						case 'V':
							// character size
							break;
						default:
							Log::WriteInfo("State 54: %s", m_accum.GetChars());
							break;
					}
					m_accum.SetLength(0);
					m_state = 0;
					continue;
				}
				m_accum.Append((char) ch);
				break;
			case 56:
				m_strStack.AddElement((char) ch);
				m_state = 57;
				break;
			case 57:
				m_display->SetBufferRowCol( m_strStack.ElementAt(0)[0] - 0x20, ch - 0x20);
				m_strStack.Clear();
				m_state = 0;
				break;
			case 59:
				m_strStack.AddElement((char) ch);
				m_state = 60;
				break;
			case 60:
				m_display->StartField( m_strStack.ElementAt(0)[0] - 0x20, ch - 0x20);
				m_strStack.Clear();
				m_state = 0;
				break;
			case 64:
				if (isdigit(ch))
				{
					m_accum.Append((char)ch);
					continue;
				}
				switch(ch)
				{
					case 'J':
					case 'K':
						{
							int sr = atoi(m_strStack.ElementAt(0));
							int sc = atoi(m_strStack.ElementAt(1));
							int er = atoi(m_strStack.ElementAt(2));
							int ec = atoi(m_accum.GetChars());
							m_strStack.Clear();
							m_accum.SetLength(0);
							m_blockBuf.SetLength(0);
							m_display->ReadBufferAllIgnoreMdt(&m_blockBuf, sr, sc, er, ec);
							m_telnet->SendRaw((byte *)m_blockBuf.GetChars(), m_blockBuf.Length());
							m_blockBuf.SetLength(0);
						}
						break;
					default:
						Log::WriteInfo("Unexpected char in 64: %d", (int)ch);
						break;
				}
				break;
			case 66:
				m_display->SetPage(ch-0x20);
				m_state = 0;
				break;
			case 67:
				if (m_strStack.Size() == 3)
				{
					int sr = m_strStack.ElementAt(0)[0] - 0x20;
					int sc = m_strStack.ElementAt(1)[0] - 0x20;
					int er = m_strStack.ElementAt(2)[0] - 0x20;
					int ec = ch - 0x20;
					m_blockBuf.SetLength(0);
					m_display->ReadBufferAllMdt(&m_blockBuf, sr, sc, er, ec);
					m_telnet->SendRaw((byte *)m_blockBuf.GetChars(), m_blockBuf.Length());
					m_blockBuf.SetLength(0);
					m_strStack.Clear();
					m_state = 0;
					continue;
				}
				m_strStack.AddElement((char) ch);
				break;
			case 71:
				m_strStack.AddElement((char) ch);
				m_state = 72;
				break;
			case 72:
				m_strStack.AddElement((char) ch);
				m_state = 73;
				break;
			case 73:
				{
					int vidAttr = m_strStack.ElementAt(0)[0] - 0x20;
					int dataAttr = m_strStack.ElementAt(1)[0] - 0x20;
					int keyAttr = ch - 0x20;
					m_display->StartField(vidAttr, dataAttr, keyAttr);
				}
				m_strStack.Clear();
				m_state = 0;
				break;
			case 75:
				m_strStack.AddElement(ch);
				m_state = 76;
				break;
			case 76:
				m_strStack.AddElement(ch);
				m_state = 77;
				break;
			case 77:
				if (ch != ';')
				{
					Log::WriteInfo("Expected ; in 77: %d", (int)ch);
				}
				m_state = 78;
				break;
			case 78:
				m_strStack.AddElement((char) ch);
				m_state = 79;
				break;
			case 79:
				{
					int sr = m_strStack.ElementAt(0)[0] - 0x20;
					int sc = m_strStack.ElementAt(1)[0] - 0x20;
					int er = m_strStack.ElementAt(2)[0] - 0x20;
					int ec = ch - 0x20;
					m_blockBuf.SetLength(0);
					m_display->ReadFieldsAll(&m_blockBuf, sr, sc, er, ec);
					m_telnet->SendRaw((byte *)m_blockBuf.GetChars(), m_blockBuf.Length());
					m_blockBuf.SetLength(0);
				}
				m_strStack.Clear();
				m_state = 0;
				break;
			case 81:
				m_display->SetPageCount(((int)ch) - 0x30);
				m_state = 10000;
				break;
			case 82:
				m_strStack.AddElement((char)(ch-0x20));
				m_state = 83;
				break;
			case 83:
				m_accum.Append((char)ch);
				ch = m_strStack.ElementAt(0)[0] - 1;
				m_strStack.Clear();
				if (ch == 0)
				{
					//keys->setMap(13, 0, accum);
					Log::WriteInfo("keys->setMap(13, 0, accum);");
					m_accum.SetLength(0);
					m_state = 0;
				}
				else
				{
					m_strStack.AddElement((char)ch);
				}
				break;
			case 84:
				// datatype table add ch
				if (++dataTypeTableCount == 96)
				{
					// set data type table
					m_state = 0;
					Log::WriteInfo("Set data type table");
				}
				break;
			/*  FASE 03 -- estados que seguian a ESC p, ESC q y ESC b.
			 *
			 *  Esperan un CR y despues un LF, pero antes NO comprobaban:
			 *  consumieran lo que consumieran avanzaban igual. TELSERV no manda
			 *  ese CR LF -- manda el comando siguiente -- asi que se perdian dos
			 *  bytes despues de cada uno de los tres. En una captura real eso se
			 *  llevo puesto un ESC q entero.
			 *
			 *  Ahora, si el byte no es el esperado, se devuelve al flujo en vez
			 *  de tragarlo: pos-- y de vuelta al estado 0.                     */
			case 10000:
				if (ch == 4)
				{
					m_state = 0;
					continue;
				}
				if (ch != 13)
				{
					Log::WriteInfo("Guardian Expected 13 in 10000: %d; "
					               "se devuelve al flujo", (int)ch);
					m_state = 0;
					pos--;
					continue;
				}
				m_state = 10001;
				break;
			case 10001:
				if (ch == 4)
				{
					m_state = 0;
					continue;
				}
				if (ch != 10)
				{
					Log::WriteInfo("Guardian Expected 10 in 10001: %d; "
					               "se devuelve al flujo", (int)ch);
					m_state = 0;
					pos--;
					continue;
				}
				m_state = 0;
				break;
			case 5000:
				switch (ch)
				{
					case 'A':
						// ANSI terminal mode
						//telnet->setBufferingOff();
						Log::WriteInfo("ANSI MODE");
						break;
					case 'B':
						// BLOCK mode
						m_display->SetModeBlock();
						m_keys->SetKeySet(KEYS_BLOCK);
						//telnet->setBufferingOn();
						Log::WriteInfo("BLOCK MODE");
						break;
					case 'C':
						// Conversational mode
						m_display->SetModeConv();
						m_keys->SetKeySet(KEYS_CONV);
						//telnet->setBufferingOff();
						Log::WriteInfo("CONVERSATIONAL MODE");
						break;
					case '!':
						// send term config?
						m_state = 5050;
						Log::WriteInfo("send term config?");
						break;
					default:
						Log::WriteInfo("Unexpected char in 5000: %d", (int)ch);
						break;
				}
				m_state = 5001;
				break;
			case 5001:
				if (ch != 3) 
				{
					Log::WriteInfo("Expected 3 in 5000: %d", (int)ch);
				}
				m_state = 0;
				break;
			case 5050:
				// accept chars until 3
				if (ch == 3)
				{
					Log::WriteInfo("Send term config? %s", m_accum.GetChars());
					m_accum.SetLength(0);
					m_state = 0;
					continue;
				}
				m_accum.Append((char) ch);
				break;
			case 15000:
				// ESC I (Clear Block)
				if (m_strStack.Size() == 3)
				{
					m_display->ClearBlock(m_strStack.ElementAt(0)[0]-0x20, m_strStack.ElementAt(1)[0]-0x20, m_strStack.ElementAt(2)[0]-0x20, ch - 0x20);
					m_strStack.Clear();
					m_state = 0;
					continue;
				}
				m_strStack.AddElement((char) ch);
				break;
			default:
				Log::WriteInfo("Unknown state %d", (int)m_state);
				break;
		}
	}
	if (m_accum.Length() > 0)
	{
		if (m_display->GetProtectMode())
		{
			m_display->WriteBuffer(m_accum.GetChars());
		}
		else
		{
			m_display->WriteDisplay(m_accum.GetChars());
		}
		m_accum.SetLength(0);
	}
	ValidateMem();
}

/*  Codigos que mueven o editan la pantalla y no son texto. Coinciden con
 *  las constantes SPC_* porque m_localCmd[i] vale i: el codigo de la tecla
 *  viaja crudo hasta SharedProtocol::WriteChar, que hace switch sobre
 *  ellas. Se deja afuera el 27 a proposito -- ahi ESC es un caracter que
 *  el usuario tecleo, no una orden de movimiento.                        */
static bool EsMovimientoLocal(const char cmd)
{
	switch ((unsigned char)cmd)
	{
		case SPC_PGUP:  case SPC_PGDN:  case SPC_HOME:  case SPC_END:
		case SPC_INS:   case SPC_DEL:   case SPC_SCROLLOCK:
		case SPC_UP:    case SPC_DOWN:  case SPC_LEFT:  case SPC_RIGHT:
		case SPC_PRINTSCR:
			return true;
	}
	return false;
}

void Guardian::ExecLocalCommand(const char cmd)
{
	char buf[2];
	buf[0] = cmd;
	buf[1] = '\0';

	ValidateMem();

	/*  FASE 03 -- el eco local.
	 *
	 *  El flag m_echoOn existia desde 2007 y nadie lo leia. Ahora se
	 *  consulta, pero OJO con que lo apaga: atarlo a la negociacion telnet
	 *  (WILL ECHO) fue un error y se revirtio. TELSERV declara WILL ECHO y
	 *  despues no hace el eco caracter por caracter: en TN6530
	 *  conversacional el terminal edita la linea localmente y la manda
	 *  entera al CR -- es lo que hace el m_keyBuffer de aca abajo. Con el
	 *  eco apagado por telnet no se veia NADA de lo tecleado.
	 *
	 *  Como default entonces el eco es local, igual que en 2007. Queda
	 *  pendiente descubrir que lo apaga de verdad en el prompt de la clave;
	 *  el sospechoso es el atributo de video invisible.
	 *
	 *  Dos excepciones a la regla:
	 *    - los codigos de movimiento se ejecutan siempre: son ordenes para
	 *      la pantalla, no eco;
	 *    - en modo bloque el eco es siempre local, porque las teclas no
	 *      salen al host hasta que se manda el bloque.                    */
	const bool movimiento = EsMovimientoLocal(cmd);
	const bool ecoLocal   = m_display->GetEchoOn() || m_display->GetBlockMode();

	if (movimiento || ecoLocal)
	{
		m_display->WriteLocal(buf);
	}

	if (! m_display->GetBlockMode())
	{
		/*  FASE 03 -- caracter por caracter cuando el eco lo hace el host.
		 *
		 *  Si el terminal no dibuja lo tecleado, quien lo dibuja es el host
		 *  devolviendolo. Y para que eso se vea MIENTRAS se teclea y no
		 *  recien al apretar Enter, hay que mandarle cada tecla en el
		 *  momento en vez de acumular la linea.
		 *
		 *  No es un invento: este host negocia WILL ECHO y WILL SGA, que
		 *  juntos son la definicion de telnet caracter por caracter. La
		 *  acumulacion en m_keyBuffer es lo que corresponde cuando el eco
		 *  es local -- el terminal edita la linea y la manda entera.
		 *
		 *  Lo que se gana, y es lo que importa: la clave no se dibuja
		 *  nunca. El host devuelve cada caracter que recibe salvo en el
		 *  prompt de la clave, donde no devuelve nada. Confirmado en una
		 *  traza en vivo contra rci3:
		 *
		 *      => term: <la clave><0D>
		 *      <= host: <0D><0A>          <-- la clave no vuelve
		 *
		 *  Las de movimiento siguen sin salir: son ordenes para la
		 *  pantalla, no texto.                                          */
		if (!ecoLocal && !movimiento)
		{
			const byte b = (byte)cmd;
			m_telnet->SendRaw(&b, 1);
			m_keyBuffer.SetLength(0);
		}
		else if (cmd == 13 || cmd == 10)
		{
			if (ecoLocal)
			{
				buf[0] = 10;
				m_display->WriteLocal(buf);
			}
			m_keyBuffer.Append((char)13);
			m_telnet->SendRaw((byte *)m_keyBuffer.GetChars(), m_keyBuffer.Length());
			m_keyBuffer.SetLength(0);
		}
		else if (cmd == '\b' && m_keyBuffer.Length() > 0)
		{
			m_keyBuffer.SetLength(m_keyBuffer.Length()-1);
		}
		else if (movimiento)
		{
			/*  FASE 03. Estos codigos son ordenes para la pantalla, no
			 *  texto: ya los ejecuto el WriteLocal de arriba (flechas,
			 *  Inicio, Fin, Insertar, Suprimir). Meterlos en la linea que
			 *  se le manda al host inyecta bytes de control en el medio
			 *  de lo tecleado.
			 *
			 *  No se notaba porque hasta la fase 03 las teclas de
			 *  navegacion nunca llegaban con su propio codigo: reenviaban
			 *  el de la tecla anterior. Al arreglar aquello, esto quedo
			 *  al descubierto.
			 *
			 *  PENDIENTE (fase 04): la edicion local de linea de verdad.
			 *  Hoy el buffer es una cola: mover el cursor con las flechas
			 *  cambia la pantalla pero no donde se inserta el proximo
			 *  caracter, asi que corregir el medio de una linea no
			 *  funciona en conversacional.                              */
		}
		else
		{
			m_keyBuffer.Append(cmd);
		}
	}
	m_display->SetRePaint(true);
	ValidateMem();
}

/*void Guardian::execLocalCommand(char cmd)
{
	char buf[2];
	buf[0] = cmd;
	buf[1] = '\0';

	display->writeLocal(buf);

	if (cmd == 10)
	{
		buf[0] = 13;
		display->writeLocal(buf);
	}
	if (display->getProtectMode())
	{
		if (cmd == '\t')
		{
			display->tab();
		}
	}
	else
	{
		if (cmd == 10)
		{
			keyBuffer.append((char) (char)13);
			telnet->send(keyBuffer, keyBuffer.length());
			keyBuffer.setLength(0);
		}
		else if (cmd == '\b')
		{
			keyBuffer.setLength(keyBuffer.length()-1);
		}
		else
		{
			keyBuffer.append((char)cmd);
		}
	}
	display->setRePaint();
}*/

#if defined(DEBUG) || defined(_DEBUG)
void Guardian::CheckMem() const
{
	m_strStack.CheckMem();
	m_accum.CheckMem();
	m_keyBuffer.CheckMem();
	m_blockBuf.CheckMem();
	DEBUG_NOTE_MEM_ALLOCATION(m_keys);
	m_keys->CheckMem();
	DEBUG_NOTE_MEM_ALLOCATION(m_telnet);
	m_telnet->CheckMem();
	DEBUG_NOTE_MEM_ALLOCATION(m_display);
	m_display->CheckMem();
}
void Guardian::ValidateMem() const
{
	m_strStack.ValidateMem();
	m_accum.ValidateMem();
	m_keyBuffer.CheckMem();
	m_blockBuf.ValidateMem();
	ASSERT_MEM(m_keys, sizeof(Keys));
	m_keys->ValidateMem();
	ASSERT_MEM(m_telnet, sizeof(Telnet));
	m_telnet->ValidateMem();
	ASSERT_MEM(m_display, sizeof(TextDisplay));
	m_display->ValidateMem();
}
#endif
