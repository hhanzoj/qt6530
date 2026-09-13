#include <stdio.h>
#include <spl/debug.h>
#include <vt6530/TextDisplay.h>
#include <vt6530/LocalPage.h>
#include <spl/Exception.h>


TextDisplay::TextDisplay(int pageCount, int cols, int rows)
{
	if ((m_statusLine = new StringBuffer(80)) == NULL)
	{
		throw OutOfMemoryException();
	}
	m_echoOn = true;
	m_blockMode = false;
	m_protectMode = false;
	
	m_requiresRepaint = true;
	
	m_ppprotectMode = new ProtectPage();
	m_ppunProtectMode = new UnprotectPage();
	m_ppconvMode = new UnprotectPage();

	m_numPages = pageCount;
	m_numRows = rows;
	m_numColumns = cols;
	m_ppRemote = m_ppconvMode;
	
	m_pages = NULL;
	Init();

	/*  FASE 03: aca tambien. El Insert de 82 espacios seguido de
	 *  SetLength(80) y otro Insert dejaba la linea en 84 columnas ya en el
	 *  constructor, antes de que nadie escribiera nada. SetLength rellena
	 *  con espacios, asi que alcanza con eso.                            */
	m_statusLine->SetLength(0);
	m_statusLine->SetLength(80);
	WriteStatus("CONV");
	ASSERT(m_statusLine->Length() == 80);
	//WriteDisplay("*******************************************************************************\r\n");
	//WriteDisplay("*\r\n*                   Washington State Department of Revenue\r\n*                             VT6530 Emulator\r\n\r\n");
	//SetCursorRowCol(23, 0);
	//WriteDisplay("*******************************************************************************");
	
	//for (int x = 1; x < m_numRows; x++)
	//{
	//	SetCursorRowCol(x, 0);
	//	WriteDisplay("*");
	//	SetCursorRowCol(x, m_numColumns-1);
	//	WriteDisplay("*");			
	//}
}

TextDisplay::~TextDisplay()
{
	ASSERT_MEM(m_statusLine, sizeof(StringBuffer));
	ASSERT_MEM(m_ppprotectMode, sizeof(ProtectPage));
	ASSERT_MEM(m_ppunProtectMode, sizeof(UnprotectPage));
	ASSERT_MEM(m_ppconvMode, sizeof(UnprotectPage));

	delete m_statusLine;
	delete m_ppprotectMode;
	delete m_ppunProtectMode;
	delete m_ppconvMode;

	ASSERT_MEM(m_pages, (m_numPages+1) * sizeof(Page *));

	for (int x = 0; x < m_numPages+1; x++)
	{
		ASSERT_MEM(m_pages[x], sizeof(Page));
		delete m_pages[x];
	}
	delete[] m_pages;
	m_pages = NULL;
}

void TextDisplay::WriteLocal(char *text)
{
	ASSERT_MEM(m_displayPage, sizeof(Page));
	ASSERT_PTR(m_ppRemote);
	m_displayPage->WriteCursorLocal(m_ppRemote, text);
	m_ppRemote->ValidateCursorPos(m_displayPage);
	m_requiresRepaint = true;
}

void TextDisplay::EchoDisplay(const char *text)
{
	ASSERT_MEM(m_displayPage, sizeof(Page));
	if (m_echoOn)
	{
		m_displayPage->WriteCursor(m_ppRemote, text);
		m_requiresRepaint = true;
		return;
	}
	char c = text[0];
	if (c == 13)
	{
		char buf[3];
		buf[0] = c;
		buf[1] = 10;
		buf[2] = 0;
		m_displayPage->WriteCursor(m_ppRemote, buf);
		m_requiresRepaint = true;
	}
}

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
void TextDisplay::SetProtectMode()
{
#ifdef _DEBUG
	ASSERT_MEM(m_pages, (m_numPages+2) * sizeof(Page *));
	for (int x = 0; x < m_numPages+1; x++)
	{
		ASSERT_MEM(m_pages[x], sizeof(Page));
	}
#endif
	m_protectMode = true;
	m_displayPage = m_pages[1];
	m_writePage = m_pages[1];
	m_ppRemote = m_ppprotectMode;
	ClearAll();
	SetVideoPriorCondition(VID_NORMAL);
	SetInsertMode(INSERT_INSERT);
	InitDataTypeTable();
	SetKeysLocked();
	WriteStatus("BLOCK PROT");
	m_requiresRepaint = true;
}


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
void TextDisplay::ExitProtectMode()
{
#ifdef _DEBUG
	ASSERT_MEM(m_pages, (m_numPages+2) * sizeof(Page *));
	for (int x = 0; x < m_numPages+1; x++)
	{
		ASSERT_MEM(m_pages[x], sizeof(Page));
	}
#endif
	m_displayPage = m_pages[1];
	m_writePage = m_pages[1];
	m_ppRemote = m_ppunProtectMode;
	ClearAll();
	SetInsertMode(INSERT_INSERT);
	ClearAllTabs();
	SetKeysLocked();
	WriteStatus("BLOCK");
	m_protectMode = false;
	m_blockMode = true;
}


/** ESC 1
 * 
 *  Set a tab at the current cursor location
 */
void TextDisplay::SetTab()
{
}

/** ESC 2
 * 
 *  Clear the tab at the current cursor location
 */
void TextDisplay::ClearTab()
{
}

/** ESC 3
 */
void TextDisplay::ClearAllTabs()
{
}

/** ESC ! or ESC ' '
 */
void TextDisplay::SetDisplayPage(int page)
{
	//if (page >= numPages)
	//{
	//	return;
	//}
	ASSERT_MEM(m_pages[page], sizeof(Page));
	m_displayPage = m_pages[page];
	m_displayPage->ForceDirty();
	m_requiresRepaint = true;
}

void TextDisplay::SetModeConv()
{
#ifdef _DEBUG
	ASSERT_MEM(m_pages, (m_numPages+2) * sizeof(Page *));
	for (int x = 0; x < m_numPages+1; x++)
	{
		ASSERT_MEM(m_pages[x], sizeof(Page));
	}
#endif
	/*  FASE 03 -- limpiar al SALIR del modo bloque.
	 *
	 *  Era la unica transicion de modo que no limpiaba. SetProtectMode
	 *  (ESC W) llama a ClearAll, ExitProtectMode (ESC X) tambien, y
	 *  SetModeBlock (SOH B ETX) limpia porque pasa por ExitProtectMode.
	 *  Solo esta se lo salteaba, y el sintoma es el que se ve al salir de
	 *  VIEWSYS: la pantalla de VIEWSYS queda abajo y los prompts nuevos de
	 *  TACL se dibujan encima.
	 *
	 *  Pero OJO con limpiar siempre: este host manda SOH C ETX todo el
	 *  tiempo como turno de palabra en conversacional -- diez veces en una
	 *  sesion corta, varias seguidas. Si limpiara en cada uno, la pantalla
	 *  se borraria cada dos lineas, que es justo el defecto que se corrigio
	 *  antes con el indice de pagina de Init() y que la prueba
	 *  Test_ElPrimerCambioDeModoConservaLaPantalla deja fijado.
	 *
	 *  Asi que se limpia solo cuando de verdad se venia de bloque.       */
	const bool veniaDeBloque = m_blockMode || m_protectMode;

	m_blockMode = false;
	m_displayPage = m_pages[1];
	m_writePage = m_pages[1];
	m_ppRemote = m_ppunProtectMode;
	if (veniaDeBloque)
	{
		ClearAll();
		/*  FASE 03: y la linea de estado tambien. Limpiabamos la pagina pero
		 *  no el tramo del mensaje, asi que al salir de TEDIT quedaba abajo
		 *  su "1) $DATA01.JARACENA.PRUEBA 1/24 (BOF) (EOF)" mientras arriba
		 *  ya corria TACL. El mensaje es de la aplicacion que se fue.     */
		WriteMessage("");
	}
	SetInsertMode(INSERT_INSERT);
	ClearAllTabs();
	SetKeysLocked();
	WriteStatus("CONV");
	m_protectMode = false;
	m_ppRemote = m_ppconvMode;
	m_requiresRepaint = true;
}

/** ESC q
 */
void TextDisplay::Init()
{
	if (m_pages != NULL)
	{
		//ASSERT_MEM(pages, (numPages+2) * sizeof(Page *));
		int x = 0;
		while (m_pages[x] != NULL)
		{
			ASSERT_MEM(m_pages[x], sizeof(Page));
			delete m_pages[x++];
		}
		delete[] m_pages;
	}
	m_pages = new Page *[m_numPages+2];

	for (int x = 0; x < m_numPages+1; x++)
	{
		if ((m_pages[x] = new Page(m_numRows, m_numColumns)) == NULL)
		{
			throw OutOfMemoryException();
		}
	}
	m_pages[m_numPages+1] = NULL;

	/*  FASE 03 -- antes era m_pages[0].
	 *
	 *  Todo el resto del archivo -- SetModeConv, SetProtectMode,
	 *  ExitProtectMode -- usa m_pages[1], porque en el 6530 las paginas se
	 *  numeran desde 1. Arrancar en la 0 hacia que lo dibujado antes del
	 *  primer SOH <modo> ETX quedara en una pagina que ya no se volvia a
	 *  mostrar: se veia como que entrar a TACL borraba el menu de TELSERV. */
	m_displayPage = m_pages[1];
	m_writePage = m_pages[1];
	m_requiresRepaint = true;
}

/*  FASE 03 -- la linea de estado crecia.
 *
 *  WriteStatus y WriteMessage limpiaban su tramo con SetCharAt y despues
 *  llamaban a Insert, que INSERTA: corre el resto a la derecha en vez de
 *  pisarlo. Cada escritura alargaba la linea. Con TEDIT se nota enseguida
 *  porque escribe el nombre del archivo y la posicion en cada refresco: en
 *  una sesion de mil bytes la linea llego a 222 columnas.
 *
 *  Insert se deja como esta a proposito -- es la semantica de SPL y
 *  ProtectPage::ReadBuffer se apoya en ella. Lo que se corrige es el uso.
 *
 *  Esta funcion pisa el tramo [desde, hasta) con el texto, rellena con
 *  espacios lo que sobra, y no toca ni un caracter fuera de el.          */
static void PisarTramo(StringBuffer *linea, int desde, int hasta,
                       const char *msg)
{
	ASSERT_PTR(linea);
	int x = desde;
	if (msg != NULL)
	{
		for (const char *c = msg; *c != '\0' && x < hasta; c++, x++)
		{
			linea->SetCharAt(x, *c);
		}
	}
	for (; x < hasta; x++)
	{
		linea->SetCharAt(x, ' ');
	}
}

void TextDisplay::WriteStatus(const char *msg)
{
	/*  El tramo de la derecha: el modo (CONV, BLOCK, BLOCK PROT). */
	m_statusLine->SetLength(80);
	PisarTramo(m_statusLine, 67, 80, msg);
	ASSERT(m_statusLine->Length() == 80);
	m_requiresRepaint = true;
}

/** ESC o
 *
 *  El tramo de la izquierda: lo que la aplicacion quiera decir. TEDIT pone
 *  ahi el nombre del archivo, la linea actual y los marcadores BOF/EOF.
 */
void TextDisplay::WriteMessage(const char *msg)
{
	m_statusLine->SetLength(80);
	PisarTramo(m_statusLine, 1, 66, msg);
	ASSERT(m_statusLine->Length() == 80);
	m_requiresRepaint = true;
}

void TextDisplay::InitDataTypeTable()
{
}

int TextDisplay::GetCurrentPage()
{
	for (int x = 0; x < m_numPages+1; x++)
	{
		if (m_pages[x] == m_writePage)
		{
			return x;
		}
	}
	ASSERT(false);
	return 1;
}

void TextDisplay::ClearAll()
{
	for (int x = 0; x < m_numPages+1; x++)
	{
		if (m_pages[x] == NULL)
		{
			break;
		}
		ASSERT_PTR(m_pages[x]);
		m_pages[x]->ClearPage();
		m_pages[x]->m_bufferPos.Clear();
		m_pages[x]->m_cursorPos.Clear();
		m_pages[x]->SetWriteAttribute(VID_NORMAL);
		m_pages[x]->SetVideoPriorCondition(VID_NORMAL);
	}
	m_requiresRepaint = true;
}
	
void TextDisplay::DumpScreen(StringBuffer *pw)
{	
	for (int r = 0; r < m_displayPage->GetNumRows(); r++)
	{
		for (int c = 0; c < m_displayPage->GetNumColumns(); c++)
		{
			pw->Append(m_displayPage->GetCell(c, r)->Get());
		}
		pw->Append((char)13);
		pw->Append((char)10);
	}
	pw->Append((char)13);
	pw->Append((char)10);
}

void TextDisplay::DumpAttibutes(StringBuffer *pw)
{
	for (int r = 0; r < m_displayPage->GetNumRows(); r++)
	{
		for (int c = 0; c < m_displayPage->GetNumColumns(); c++)
		{
			int cell = m_displayPage->GetCell(c, r)->GetAttributes();
			pw->Append(m_displayPage->GetCell(c, r)->Get());
			if ( (cell & VID_NORMAL) != 0)
			{
				pw->Append("N");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & VID_BLINKING) != 0)
			{
				pw->Append("B");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & VID_REVERSE) != 0)
			{
				pw->Append("R");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & VID_INVIS) != 0)
			{
				pw->Append("I");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & VID_UNDERLINE) != 0)
			{
				pw->Append("U");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & DAT_MDT) != 0)
			{
				pw->Append("M");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & DAT_TYPE) != 0)
			{
				pw->Append((c>>SHIFT_DAT_TYPE) & 7);
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & DAT_AUTOTAB) != 0)
			{
				pw->Append("A");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & DAT_UNPROTECT) != 0)
			{
				pw->Append("0");
			}
			else
			{
				pw->Append("P");
			}
			if ( (cell & KEY_UPSHIFT) != 0)
			{
				pw->Append("S");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & KEY_KB_ONLY) != 0)
			{
				pw->Append("K");
			}
			else
			{
				pw->Append("0");
			}
			if ( (cell & KEY_AID_ONLY) != 0)
			{
				pw->Append("");
			}
			else
			{
				pw->Append("");
			}
			if ( (cell & KEY_EITHER) != 0)
			{
				pw->Append("");
			}
			else
			{
				pw->Append("");
			}
			if ( (cell & CHAR_START_FIELD) != 0)
			{
				pw->Append("F");
			}
			else
			{
				pw->Append("0");
			}
			pw->Append(",");
		}
		pw->Append((char)13);
		pw->Append((char)10);
	}
	pw->Append((char)13);
	pw->Append((char)10);
}

/**
 *  Get the 'index'nth field on the screen.
 *  The first field is index ZERO.  If the
 *  index is larger than the number of field,
 *  an empty string is returned.
 */
void TextDisplay::GetField(int index, StringBuffer *accum)
{
	int count = 0;
	bool cap = false;
	
	for (int r = 0; r < m_numRows; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			if (m_displayPage->GetCell(c, r)->IsStartField())
			{
				if (cap)
				{
					return;
				}
				if (count++ == index)
				{
					cap = true;
				}
			}
			if (cap)
			{
				accum->Append (m_displayPage->GetCell(c, r)->Get());
			}
		}
	}
}

/**
 *  Get the video, data, and key attributes for a
 *  field.
 */
int TextDisplay::GetFieldAttributes(int index)
{
	int count = 0;
	
	for (int r = 0; r < m_numRows; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			if (m_displayPage->GetCell(c, r)->IsStartField())
			{
				if (count++ == index)
				{
					int r2 = r;
					int c2 = c+1;
					if (c2 >= m_numColumns)
					{
						r2++;
						c2 = 0;
					}
					return m_displayPage->GetCell(c2, r2)->GetAttributes();
				}
			}
		}
	}
	return 0;
}

/**
 *  Get the text in the field at the cursor
 *  position.
 */
void TextDisplay::GetCurrentField(StringBuffer *accum)
{
	int count = 0;
	bool cap = false;
	int r = m_displayPage->m_cursorPos.m_row;
	int c = m_displayPage->m_cursorPos.m_column;
	
	while (c > 0)
	{
		if (!m_displayPage->GetCell(c, r)->IsStartField())
		{
			break;
		}
		c--;
	}
	c++;
	while (c < m_numColumns)
	{
		if (m_displayPage->GetCell(c, r)->IsStartField())
		{
			break;
		}
		accum->Append (m_displayPage->GetCell(c, r)->Get());
		c++;
	}
}

/**
 *  Get the 'index'nth unprotected field on 
 *  the screen.  The first field is index 
 *  ZERO.  If the index is larger than the 
 *  number of field, an empty string is 
 *  returned.
 */
void TextDisplay::GetUnprotectField(int index, StringBuffer *accum)
{
	int count = 0;
	bool cap = false;
	
	for (int r = 0; r < m_numRows; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			if (m_displayPage->GetCell(c, r)->IsStartField())
			{
				if (cap)
				{
					return;
				}
				int r2 = r;
				int c2 = c+1;
				if (c2 >= m_numColumns)
				{
					c2 = 0;
					r2++;
				}
				if (m_displayPage->GetCell(c2, r2)->IsUnprotect())
				{
					if (count++ == index)
					{
						cap = true;
					}
				}
			}
			if (cap)
			{
				accum->Append (m_displayPage->GetCell(c, r)->Get());
			}
		}
	}
}

/**
 *  Write text into the 'index'nth 
 *  unprotected field on the screen.  The 
 *  first field is index ZERO.  If the 
 *  index is larger than the number of field, 
 *  the request is ignored.
 */
void TextDisplay::SetField(int index, char *text)
{
	int count = 0;
	
	for (int r = 0; r < m_numRows; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			if (m_displayPage->GetCell(c, r)->IsStartField())
			{
				int r2 = r;
				int c2 = c+1;
				if (c2 >= m_numColumns)
				{
					c2 = 0;
					r2++;
				}
				if (m_displayPage->GetCell(c2, r2)->IsUnprotect())
				{
					if (count++ == index)
					{
						SetCursorRowCol(r2, c2);
						WriteDisplay(text);
						m_requiresRepaint = true;
						return;
					}
				}
			}
		}
	}
}

/**
 *  Returns true if the 'index'nth unprotected
 *  field has its MDT set. The first field is 
 *  index ZERO.  If the index is larger than 
 *  the  number of fields, false is returned.
 */
bool TextDisplay::IsFieldChanged(int index)
{
	int count = 0;
	
	for (int r = 0; r < m_numRows; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			if (m_displayPage->GetCell(c, r)->IsStartField())
			{
				int r2 = r;
				int c2 = c+1;
				if (c2 >= m_numColumns)
				{
					c2 = 0;
					r2++;
				}
				if (m_displayPage->GetCell(c2, r2)->IsUnprotect())
				{
					if (count++ == index)
					{
						return m_displayPage->GetCell(c2, r2)->IsMDT();
					}
				}
			}
		}
	}
	return false;
}

/**
 *  Get a full line of display text.  
 */
void TextDisplay::GetLine(int lineNumber, StringBuffer *sb)
{
	for (int c = 0; c < m_numColumns; c++)
	{
		sb->Append(m_displayPage->GetCell(c, lineNumber)->Get());
	}
}

/**
 *  Set the cursor at the start if the 
 *  'index'nth unprotected field on the screen.  
 *  The first field is index ZERO.  If the 
 *  index is larger than the number of field, 
 *  the request is ignored.
 */
void TextDisplay::CursorToField(int index)
{
	int count = 0;
	
	for (int r = 0; r < m_numRows; r++)
	{
		for (int c = 0; c < m_numColumns; c++)
		{
			if (m_displayPage->GetCell(c, r)->IsStartField())
			{
				int r2 = r;
				int c2 = c+1;
				if (c2 >= m_numColumns)
				{
					c2 = 0;
					r2++;
				}
				if ( r2 >= m_numRows )
				{
					r2 = 0;
				}
				if (m_displayPage->GetCell(c2, r2)->IsUnprotect())
				{
					if (count++ == index)
					{
						SetCursorRowCol(r2, c2);
						m_requiresRepaint = true;
						return;
					}
				}
			}
		}
	}
}

void TextDisplay::ToHTML(Color *fg, Color *bg, StringBuffer *buf)
{
	char fgRGB[20];
	char bgRGB[20];

	fg->AsHexString(fgRGB);
	bg->AsHexString(bgRGB);

	int fieldCount = 0;
	bool inUnprot = false;
	
	StringBuffer accum;
	
	// write the style and script
	buf->Append("<html>");
	buf->Append("<script language='javascript'>function keys(){if (event.keyCode < 112 || event.keyCode > 123) {event.returnValue=true;return;} event.cancelBubble=true; event.returnValue=false;var k = document.forms('screen')('hdnKey'); switch(event.keyCode){case 112: k.value = 'F1'; break; case 10: k.value = 'ENTER'; break;} document.forms('screen').submit();} function canxIt(){event.cancelBubble = true;event.returnValue = false;}</script>");
	buf->Append("<script language='javascript'>function loaded(){document.onkeydown=keys; document.onhelp=canxIt; var f = document.forms('screen')('F0'); if (f != null)f.focus();}</script>");
	buf->Append("<script language='javascript'>function tabcheck(field){var f = document.forms('screen')('F'+field); if (f.value.length == f.maxLength()){field++; if (document.forms('screen')('F'+field) != null){document.forms('screen')('F'+field).focus();}else{document.forms('screen')('F0').focus();}}}</script>\r\n");
	buf->Append("<body onload='loaded()' style='color: green; background: #3F3F3F'>\r\n");
	buf->Append("<style type='text/css' >");
	buf->Append(".normal { color: #");
	buf->Append(fgRGB);
	buf->Append("; background: #");
	buf->Append(bgRGB);
	buf->Append("; text-decoration: none}");
	buf->Append(".reverse { color: #");
	buf->Append(bgRGB);
	buf->Append("; background: #");
	buf->Append(fgRGB);
	buf->Append("; text-decoration: none}");
	buf->Append(".underline { color: #");
	buf->Append(fgRGB);
	buf->Append("; background: #");
	buf->Append(bgRGB);
	buf->Append("; text-decoration: underline} ");
	buf->Append(".reverseunderline { color: #");
	buf->Append(bgRGB);
	buf->Append("; background: #");
	buf->Append(fgRGB);
	buf->Append("; text-decoration: underline}");
	buf->Append(".blink { color: #");
	buf->Append(fgRGB);
	buf->Append("; background: #");
	buf->Append(bgRGB);
	buf->Append("; text-decoration: blink}");
	buf->Append(".blinkreverse { color: #");
	buf->Append(bgRGB);
	buf->Append("; background: #");
	buf->Append(fgRGB);
	buf->Append("; text-decoration: blink}");
	buf->Append("</style>\r\n");
	
	// write the table header
	buf->Append("<form id='screen' method='post'><input type='hidden' id='hdnKey' value='' /><table cols='80' width='100%' >");
	
	for (int r = 0; r < m_numRows; r++)
	{
		buf->Append("<tr>");
		
		for (int c = 0; c < m_numColumns; c++)
		{
			PageCell *cell = m_displayPage->GetCell(c, r);
			int ch = cell->GetAttributes();

			if (! inUnprot)
			{
				buf->Append("<td class=");
				if ( (ch & MASK_COLOR) == 0)
				{
				}
				else
				{
				}
				if ( (ch & (VID_UNDERLINE|VID_REVERSE)) == (VID_UNDERLINE|VID_REVERSE))
				{
					buf->Append("reverseunderline");
				}
				else if ( (ch & (VID_BLINKING|VID_REVERSE)) == (VID_BLINKING|VID_REVERSE))
				{
					buf->Append("blinkreverse");
				}
				else if ( (ch & VID_BLINKING) != 0)
				{
					buf->Append("blink");
				}
				else if ( (ch & VID_REVERSE) != 0)
				{
					buf->Append("reverse");
				}
				else if ( (ch & VID_UNDERLINE) != 0)
				{
					buf->Append("underline");
				}
				else
				{
					buf->Append("normal");
				}
				if ( (ch & CHAR_START_FIELD) == 0)
				{
					buf->Append(">");
					buf->Append(cell->Get());
					buf->Append("</td>");
				}
			}
			if ( (ch & CHAR_START_FIELD) != 0)
			{
				if (inUnprot)
				{
					// end the input tag
					accum.Append("' maxlength='");
					accum.Append(accum.Length());
					accum.Append("' />");
					inUnprot = false;
					buf->Append(" colspan=");
					buf->Append(accum.Length());
					buf->Append(">");
					buf->Append(accum.GetChars());
					buf->Append("</td>");
					accum.SetLength(0);
				}
				// is the new field unprotected?
				int c2 = c + 1;
				if (c2 >= m_numColumns)
				{
					c2 = 0;
					cell = m_displayPage->GetCell(c2, ++r);
				}
				if (cell->IsUnprotect())
				{
					inUnprot = true;
					accum.Append ("<input type='text' id='F");
					accum.Append (fieldCount);
					accum.Append ("' onkeypress='tabcheck(");
					accum.Append ( fieldCount );
					accum.Append (")' value='");
					accum.Append (cell->Get());
					fieldCount++;
				}
			}
			else if (inUnprot)
			{
				accum.Append((char)(ch & MASK_CHAR));
			}
		}
		buf->Append("</tr>\r\n");
	}
	buf->Append("</table></form><p/><center><a href='mailto:johnga@dor.wa.gov'>Got Bugs?</a></center></body></html>");
}

void TextDisplay::GetSubString(int row, int col, int len, StringBuffer *sb)
{
	ASSERT(row < m_numRows && col+len < m_numColumns);

	for (int c = col; c < col+len; c++)
	{
		sb->Append( m_displayPage->GetCell(c, row)->Get() );
	}
}

void TextDisplay::GetText(StringBuffer *sb)
{
	for ( int row = 0; row < GetNumRows(); row++ )
	{
		for ( int col = 0; col < GetNumColumns(); col++ )
		{
			sb->Append( m_displayPage->GetCell(col, row)->Get() );
		}
	}
}

int TextDisplay::DecodeKeyAttrs(int attr)
{
	int ret = 0;
	if (attr == 0)
	{
		return 0;
	}		
	ASSERT ( (attr & (1<<6)) != 0);
	if ((attr & (1<<0)) != 0)
	{
		ret |= KEY_UPSHIFT;
	}
	if ((attr & (1<<1)) != 0)
	{
		ret |= KEY_KB_ONLY;
	}
	if ((attr & (1<<2)) != 0)
	{
		ret |= KEY_AID_ONLY;
	}
	if ((attr & (1<<3)) != 0)
	{
		ret |= KEY_EITHER;
	}
	if (ret == 0 && (ret & ~(1<<6)) != 0) 
	{
		//System.out.println("Unknown video attr " + attr);
	}
	return ret;
}

int TextDisplay::DecodeDataAttrs(int attr)
{
	int ret = 0;
	if (attr == 0)
	{
		return 0;
	}
	//ASSERT.fatal ( (attr & (1<<6)) != 0, "TextDisplay", 367, "Invalid attribute format");
	if ((attr & (1<<0)) != 0)
	{
		ret |= DAT_MDT;
	}
	if ((attr & (1<<4)) != 0)
	{
		ret |= DAT_AUTOTAB;
	}
	if ((attr & (1<<5)) != 0)
	{
		ret |= DAT_UNPROTECT;
	}
	int type = attr & ((1<<1)|(1<<2)|(1<<3));
	ret |= type<<SHIFT_DAT_TYPE;
	ASSERT( (attr & ~((1<<6)|(1<<5)|(1<<4)|(1<<0)|(1<<1)|(1<<2)|(1<<3))) == 0);
	return ret;
}

int TextDisplay::DecodeVideoAttrs(int attr)
{
	int ret = 0;
	if (attr == 0 || attr == 32)
	{
		return 0;
	}
	//ASSERT.fatal ( (attr & (1<<5)) != 0, "TextDisplay", 367, "Invalid attribute format");
	
	if ((attr & (1<<0)) != 0)
	{
		ret |= VID_NORMAL;
	}
	if ((attr & (1<<1)) != 0)
	{
		ret |= VID_BLINKING;
	}
	if ((attr & (1<<2)) != 0)
	{
		ret |= VID_REVERSE;
	}
	if ((attr & (1<<3)) != 0)
	{
		ret |= VID_INVIS;
	}
	if ((attr & (1<<4)) != 0)
	{
		ret |= VID_UNDERLINE;
	}
	if ( (attr & ~((1<<5)|(1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4))) != 0) 
	{
		//System.out.println("Unknown video attr " + attr);
	}
	return ret;
}	

#if defined(DEBUG) || defined(_DEBUG)
void TextDisplay::CheckMem() const
{
	DEBUG_NOTE_MEM_ALLOCATION(m_statusLine);
	m_statusLine->CheckMem();
	DEBUG_NOTE_MEM_ALLOCATION(m_ppprotectMode);
	DEBUG_NOTE_MEM_ALLOCATION(m_ppunProtectMode);
	DEBUG_NOTE_MEM_ALLOCATION(m_ppconvMode);
	DEBUG_NOTE_MEM_ALLOCATION(m_pages);

	for (int x = 0; x < m_numPages+1; x++)
	{
		DEBUG_NOTE_MEM_ALLOCATION(m_pages[x]);
		m_pages[x]->CheckMem();
	}
}

void TextDisplay::ValidateMem() const
{
	ASSERT_MEM(m_statusLine, sizeof(StringBuffer));
	m_statusLine->ValidateMem();
	ASSERT_MEM(m_ppprotectMode, sizeof(ProtectPage));
	ASSERT_MEM(m_ppunProtectMode, sizeof(UnprotectPage));
	ASSERT_MEM(m_ppconvMode, sizeof(UnprotectPage));
	ASSERT_MEM(m_pages, (m_numPages+1) * sizeof(Page *));

	for (int x = 0; x < m_numPages+1; x++)
	{
		ASSERT_MEM(m_pages[x], sizeof(Page));
		m_pages[x]->ValidateMem();
	}
}
#endif

