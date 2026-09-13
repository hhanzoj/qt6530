/*
 *  Banco de pruebas del nucleo de libvt6530 -- fase 0.
 *
 *  Objetivo: poder alimentar el parser con bytes fijos y observar la
 *  pantalla resultante, sin red y sin host NonStop. Es la pieza que hace
 *  posible la fase 03 (corregir el parser), porque convierte "probar contra
 *  una pantalla real" en "reproducir una captura guardada".
 *
 *  Varias pruebas comprueban defectos CONOCIDOS y por eso afirman el
 *  comportamiento equivocado, dejandolo documentado y fijado. Estan
 *  marcadas con DEFECTO y son las que hay que dar vuelta en la fase 03.
 */

#include <spl/debug.h>
#include <spl/Log.h>
#include <spl/StringBuffer.h>
#include <spl/collection/Vector.h>
#include <spl/term/Telnet.h>

#include <vt6530/TextDisplay.h>
#include <vt6530/Keys.h>
#include <vt6530/Guardian.h>
#include <vt6530/TermEventListener.h>
#include <vt6530/MappedKeyListener.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/* ====================================================================== */
/*  Andamiaje                                                             */
/* ====================================================================== */

static int g_pass = 0;
static int g_fail = 0;
static std::string g_currentTest;

static void Check(bool ok, const std::string &what)
{
	if (ok) { g_pass++; return; }
	g_fail++;
	std::printf("    FALLA  %s :: %s\n", g_currentTest.c_str(), what.c_str());
}

static void CheckEq(const std::string &got, const std::string &want,
                    const std::string &what)
{
	if (got == want) { g_pass++; return; }
	g_fail++;
	std::printf("    FALLA  %s :: %s\n", g_currentTest.c_str(), what.c_str());
	std::printf("           esperado: [%s]\n", want.c_str());
	std::printf("           obtenido: [%s]\n", got.c_str());
}

/** Convierte bytes a una forma legible: los imprimibles tal cual, el
 *  resto en hexadecimal entre corchetes angulares. */
static std::string Readable(const std::string &raw)
{
	std::string out;
	char tmp[8];
	for (size_t i = 0; i < raw.size(); i++)
	{
		unsigned char c = (unsigned char)raw[i];
		if (c >= 0x20 && c < 0x7F) { out.push_back((char)c); }
		else { std::snprintf(tmp, sizeof(tmp), "<%02X>", c); out += tmp; }
	}
	return out;
}

/** Sumidero de bytes hacia el "host": guarda todo lo que el terminal envia. */
class CapturingLink : public IHostLink
{
public:
	std::string sent;
	virtual void SendRaw(const byte *data, int len)
	{
		if (data != nullptr && len > 0) sent.append((const char *)data, (size_t)len);
	}
	void Reset() { sent.clear(); }
};

/** Cuenta los eventos que el nucleo despacha al contenedor. En la fase 02
 *  estos son los que se convierten en senales de Qt. */
class CountingListener : public Vt6530EventListener
{
public:
	int connect = 0, disconnect = 0, resetLine = 0, enquire = 0;
	int displayChanged = 0, error = 0, debug = 0, recv34 = 0, textWatch = 0;

	virtual void Vt6530_OnConnect()          { connect++; }
	virtual void Vt6530_OnDisconnect()       { disconnect++; }
	virtual void Vt6530_OnResetLine()        { resetLine++; }
	virtual void Vt6530_OnEnquire()          { enquire++; }
	virtual void Vt6530_OnDisplayChanged()   { displayChanged++; }
	virtual void Vt6530_OnError(const char *)   { error++; }
	virtual void Vt6530_OnDebug(const char *)   { debug++; }
	virtual void Vt6530_OnRecv34(const char *, const char *, const int) { recv34++; }
	virtual void Vt6530_OnTextWatch(const char *, const int) { textWatch++; }
};

/** Recolecta las lineas de log. Los comandos ESC que el nucleo solo registra
 *  en lugar de ejecutar se detectan justamente aqui. */
class LogCapture : public ILogSink
{
public:
	std::vector<std::string> lines;
	virtual void OnLogLine(SplLogLevel, const std::string &line)
	{
		lines.push_back(line);
	}
	bool Contains(const char *needle) const
	{
		for (size_t i = 0; i < lines.size(); i++)
			if (lines[i].find(needle) != std::string::npos) return true;
		return false;
	}
	void Reset() { lines.clear(); }
};

/** Terminal completo sin red: display + teclado + parser + sumidero. */
struct Harness
{
	CapturingLink link;
	CountingListener listener;
	Vector<Vt6530EventListener *> listeners;
	TextDisplay *display;
	Keys *keys;
	Guardian *guardian;

	Harness()
	{
		listeners.Add(&listener);
		display  = new TextDisplay(2, 80, 24);
		keys     = new Keys();
		guardian = new Guardian(&listeners, display, keys, &link);
	}

	~Harness()
	{
		delete guardian;
		delete keys;
		delete display;
	}

	/** Entrega bytes al parser como si llegaran del host. */
	void Feed(const std::string &bytes)
	{
		guardian->ProcessRemoteString(bytes.data(), (int)bytes.size());
	}

	/** Una fila de la pagina visible, sin espacios finales. */
	std::string Row(int row) const
	{
		std::string out;
		Page *page = display->GetDisplayPage();
		for (int c = 0; c < display->GetNumColumns(); c++)
			out.push_back(page->GetCell(c, row)->Get());
		while (!out.empty() && out[out.size() - 1] == ' ') out.erase(out.size() - 1);
		return out;
	}
};

static const char ESC = 27;   /* 0x1B: el valor correcto, no el "\27" del original */
static const char SOH = 1;
static const char ETX = 3;

/* ====================================================================== */
/*  Pruebas: lo que funciona                                              */
/* ====================================================================== */

static void Test_TextoLlegaALaPantalla()
{
	g_currentTest = "texto_a_pantalla";
	Harness h;
	h.Feed("SYSTEM \\NONSTOP");
	CheckEq(h.Row(0), "SYSTEM \\NONSTOP", "el texto plano se escribe en la fila 0");
}

static void Test_DireccionamientoDeCursor()
{
	g_currentTest = "direccionamiento_cursor";
	Harness h;
	/* 0x13 = fijar direccion de cursor; fila y columna vienen sesgadas +0x20 */
	std::string s;
	s.push_back(0x13);
	s.push_back((char)(0x20 + 5));   /* fila 5  */
	s.push_back((char)(0x20 + 10));  /* col 10  */
	s += "PATHMON";
	h.Feed(s);
	CheckEq(h.Row(5), "          PATHMON", "el texto arranca en la columna 10 de la fila 5");
}

static void Test_SaltoDeLinea()
{
	g_currentTest = "salto_de_linea";
	Harness h;
	h.Feed("PRIMERA\r\nSEGUNDA");
	CheckEq(h.Row(0), "PRIMERA", "primera linea");
	CheckEq(h.Row(1), "SEGUNDA", "segunda linea tras CR LF");
}

static void Test_ModoProtegido()
{
	g_currentTest = "modo_protegido";
	Harness h;
	Check(!h.display->GetProtectMode(), "arranca fuera de modo protegido");

	std::string s; s.push_back(ESC); s.push_back('W');   /* ESC W */
	h.Feed(s);
	Check(h.display->GetProtectMode(), "ESC W entra en modo protegido");
	Check(h.display->IsBlockMode(), "modo protegido implica modo bloque");

	std::string x; x.push_back(ESC); x.push_back('X');   /* ESC X */
	h.Feed(x);
	Check(!h.display->GetProtectMode(), "ESC X sale de modo protegido");
}

static void Test_EnquireYResetLine()
{
	g_currentTest = "enquire_y_reset";
	Harness h;
	std::string s;
	s.push_back(5);   /* ENQ */
	s.push_back(4);   /* reset de linea */
	h.Feed(s);
	Check(h.listener.enquire == 1,   "ENQ despacha Vt6530_OnEnquire una vez");
	Check(h.listener.resetLine == 1, "0x04 despacha Vt6530_OnResetLine una vez");
}

static void Test_ResponderConfiguracionDeTerminal()
{
	g_currentTest = "responder_config";
	Harness h;
	std::string s; s.push_back(ESC); s.push_back('?');
	h.Feed(s);
	Check(!h.link.sent.empty(), "ESC ? produce una respuesta hacia el host");
	Check(h.link.sent.find("A 2B72C") != std::string::npos,
	      "la respuesta lleva la cadena de configuracion del 6530");
}

static void Test_LeerDireccionDeCursor()
{
	g_currentTest = "leer_direccion_cursor";
	Harness h;
	std::string pos;
	pos.push_back(0x13);
	pos.push_back((char)(0x20 + 3));
	pos.push_back((char)(0x20 + 7));
	h.Feed(pos);
	h.link.Reset();

	std::string s; s.push_back(ESC); s.push_back('a');   /* ESC a */
	h.Feed(s);
	Check(h.link.sent.size() == 6, "ESC a responde seis bytes");
	if (h.link.sent.size() == 6)
	{
		Check((unsigned char)h.link.sent[0] == 1,   "arranca con SOH");
		Check((unsigned char)h.link.sent[1] == '_', "codigo de respuesta '_'");
		Check((unsigned char)h.link.sent[2] == '!', "tercer byte fijo '!'");
		/* GetCursorRow/Col devuelven la posicion mas uno */
		Check((unsigned char)h.link.sent[3] == 4, "fila informada (base 1)");
		Check((unsigned char)h.link.sent[4] == 8, "columna informada (base 1)");
		Check((unsigned char)h.link.sent[5] == 13, "termina con CR");
	}
	std::printf("           ESC a -> %s\n", Readable(h.link.sent).c_str());
}

static void Test_DEFECTO_EscALeeCursorSinSesgo()
{
	/*  Hallazgo nuevo de la fase 0.
	 *
	 *  Todo el protocolo 6530 transporta coordenadas sesgadas +0x20 para
	 *  que caigan en el rango imprimible: asi las manda el host en 0x13 y
	 *  0x11, y asi las devuelve Keys al armar la secuencia AID
	 *  (KeyGetCursorX() + 0x20). La respuesta a ESC a, en cambio, escribe
	 *  GetCursorRow() y GetCursorCol() crudos.
	 *
	 *  Con el cursor en (3,7) el terminal responde los bytes 0x04 y 0x08,
	 *  que son EOT y BACKSPACE en medio de una respuesta. Con el cursor en
	 *  el origen serian 0x01 y 0x01, o sea SOH. La misma omision esta en
	 *  el estado 50 (simular tecla de funcion).                          */
	g_currentTest = "DEFECTO_esc_a_sin_sesgo";
	Harness h;
	std::string pos;
	pos.push_back(0x13);
	pos.push_back((char)(0x20 + 3));
	pos.push_back((char)(0x20 + 7));
	h.Feed(pos);
	h.link.Reset();

	std::string s; s.push_back(ESC); s.push_back('a');
	h.Feed(s);

	if (h.link.sent.size() == 6)
	{
		Check((unsigned char)h.link.sent[3] == 4,
		      "DEFECTO: la fila viaja como 0x04 (EOT) en vez de 0x24");
		Check((unsigned char)h.link.sent[4] == 8,
		      "DEFECTO: la columna viaja como 0x08 (BS) en vez de 0x28");
	}
	else
	{
		Check(false, "se esperaban seis bytes de respuesta");
	}
}

static void Test_CamposProtegidosYLecturaDeBloque()
{
	g_currentTest = "campos_y_lectura_bloque";
	Harness h;

	std::string s;
	s.push_back(ESC); s.push_back('W');            /* modo protegido      */
	s.push_back(0x13); s.push_back((char)0x20); s.push_back((char)0x20);
	s += "USUARIO:";
	/* 0x1D = inicio de campo: atributos de video y de dato, sesgados +0x20.
	 * El bit 5 del atributo de dato marca el campo como no protegido. */
	s.push_back(0x1D);
	s.push_back((char)(0x20 + 1));                  /* video: normal      */
	s.push_back((char)(0x20 + (1 << 6) + (1 << 5)));/* dato: no protegido */
	h.Feed(s);

	Check(h.Row(0).find("USUARIO:") == 0, "la etiqueta protegida se pinta");

	StringBuffer out;
	h.display->ReadBufferUnprotectIgnoreMdt(&out, 0, 0, 23, 79);
	Check(out.Length() > 0, "la lectura de bloque devuelve datos");
}

/* ====================================================================== */
/*  Pruebas: defectos confirmados de la auditoria                          */
/* ====================================================================== */

static void Test_DEFECTO_TablaDeTiposCortaElFlujo()
{
	/*  Auditoria, defecto: ESC r (definir tabla de tipos de dato) cuenta
	 *  los 96 bytes con una variable LOCAL de ProcessRemoteString, que se
	 *  reinicia en cada lectura del socket. Repartida en dos lecturas, la
	 *  cuenta nunca llega a 96 y el parser se come todo lo que sigue.
	 *
	 *  Es el defecto mas facil de disparar en produccion: 96 bytes casi
	 *  nunca caen enteros en un solo segmento TCP.                        */
	g_currentTest = "DEFECTO_tabla_tipos_corta_flujo";
	Harness h;

	std::string abre; abre.push_back(ESC); abre.push_back('r');
	abre += std::string(50, 'x');          /* primera lectura: 50 de 96 */
	h.Feed(abre);

	h.Feed(std::string(50, 'x'));          /* segunda lectura: otros 50 */
	h.Feed("DEBERIA VERSE");

	CheckEq(h.Row(0), "",
	        "DEFECTO: el texto posterior se pierde porque el contador se reinicio");
}

static void Test_DEFECTO_TablaDeTiposEnUnaSolaLectura()
{
	/*  La misma secuencia entregada de una sola vez si termina, lo que
	 *  confirma que la causa es el reinicio del contador y no otra cosa. */
	g_currentTest = "DEFECTO_tabla_tipos_lectura_unica";
	Harness h;

	std::string s; s.push_back(ESC); s.push_back('r');
	s += std::string(96, 'x');
	s += "AHORA SI";
	h.Feed(s);

	CheckEq(h.Row(0), "AHORA SI",
	        "con los 96 bytes en una sola lectura el parser se recupera");
}

static void Test_LaLineaDeEstadoNoCrece()
{
	/*  CORREGIDO en la fase 03.
	 *
	 *  WriteStatus y WriteMessage limpiaban su tramo con SetCharAt y
	 *  despues llamaban a StringBuffer::Insert, que INSERTA: corre el resto
	 *  a la derecha en vez de pisarlo. Cada escritura alargaba la linea, y
	 *  hasta el constructor la dejaba en 84 columnas antes de que nadie
	 *  escribiera nada.
	 *
	 *  Con TEDIT se nota enseguida, porque escribe el nombre del archivo y
	 *  la posicion del cursor en cada refresco: en una sesion real de mil
	 *  bytes la linea llego a 222 columnas.
	 *
	 *  Insert se dejo como estaba a proposito -- es la semantica de SPL y
	 *  ProtectPage::ReadBuffer se apoya en ella. Lo que se corrigio es el
	 *  uso: ahora se pisa el tramo y se rellena con espacios.            */
	g_currentTest = "linea_de_estado_no_crece";
	Harness h;

	const int inicial = h.display->GetStatusLine()->Length();
	Check(inicial == 80, "la linea arranca en 80 columnas");

	/* Diez cambios de modo, que escriben el tramo de la derecha. */
	for (int i = 0; i < 10; i++)
	{
		std::string s;
		s.push_back(SOH); s.push_back('B'); s.push_back(ETX);
		h.Feed(s);
		s.clear();
		s.push_back(SOH); s.push_back('C'); s.push_back(ETX);
		h.Feed(s);
	}
	Check(h.display->GetStatusLine()->Length() == 80,
	      "tras diez cambios de modo sigue en 80");

	/*  Y el tramo de la izquierda, que es el que usa TEDIT: ESC o con el
	 *  texto hasta el CR, repetido.                                      */
	for (int i = 0; i < 10; i++)
	{
		std::string s;
		s.push_back(ESC); s.push_back('o');
		s += "1) $DATA01.JARACENA.PRUEBA 1/24 (BOF) (EOF) 1:79          L01.";
		s.push_back(13);
		h.Feed(s);
	}
	const int fin = h.display->GetStatusLine()->Length();
	Check(fin == 80, "y tras diez mensajes de TEDIT tambien");
	std::printf("           largo de la linea de estado: inicial %d, final %d\n",
	            inicial, fin);

	/*  El mensaje tiene que estar, y el modo tambien: son tramos distintos
	 *  de la misma linea y no se pisan.                                  */
	const std::string linea(h.display->GetStatusLine()->GetChars(), 80);
	Check(linea.find("$DATA01.JARACENA.PRUEBA") != std::string::npos,
	      "el mensaje de la aplicacion queda escrito");
	Check(linea.find("CONV") != std::string::npos,
	      "y el modo, en su propio tramo");
}

static void Test_LaLineaDeMensajeNoDibujaLosEscapes()
{
	/*  TEDIT mete un ESC 6 ADENTRO del mensaje, para darle atributo de
	 *  video. Como el estado 52 acumulaba todo tal cual, la secuencia
	 *  terminaba dibujada como texto:
	 *
	 *      [ <1B>6%1) $DATA01.JARACENA.PRUEBA 1/24 ... ]
	 *
	 *  La linea de estado de este emulador es una cadena sin atributos,
	 *  asi que el atributo no se puede aplicar; lo que se puede es no
	 *  ensuciarla. Se consume y se descarta. Aplicarlo de verdad queda
	 *  para la fase 04.                                                  */
	g_currentTest = "linea_de_mensaje_sin_escapes";
	Harness h;

	std::string s;
	s.push_back(ESC); s.push_back('o');
	s.push_back(ESC); s.push_back('6'); s.push_back((char)0x25);
	s += "1) $DATA01.JARACENA.PRUEBA";
	s.push_back(13);
	h.Feed(s);

	const std::string linea(h.display->GetStatusLine()->GetChars(), 80);
	Check(linea.find("1) $DATA01.JARACENA.PRUEBA") != std::string::npos,
	      "el texto del mensaje llega limpio");
	Check(linea.find((char)0x1B) == std::string::npos,
	      "y no queda ningun ESC dibujado en la linea");
	std::printf("           linea de estado: [%s]\n", linea.c_str());
}

static void Test_DEFECTO_ComandosQueSoloSeRegistran()
{
	/*  Auditoria, seccion 03: nueve comandos ESC no hacen nada mas que
	 *  dejar una linea en el log. Se comprueba con el sumidero de log. */
	g_currentTest = "DEFECTO_comandos_solo_log";
	LogCapture cap;
	Log::SetSink(&cap);
	Log::SetQuiet(true);

	{
		Harness h;
		std::string s;
		s.push_back(ESC); s.push_back('S');   /* roll up    */
		s.push_back(ESC); s.push_back('U');   /* page down  */
		s.push_back(ESC); s.push_back('V');   /* page up    */
		s.push_back(ESC); s.push_back('N');   /* sin edicion local */
		h.Feed(s);
	}

	Check(cap.Contains("Roll up"),   "DEFECTO: ESC S solo registra 'Roll up'");
	Check(cap.Contains("Page down"), "DEFECTO: ESC U solo registra 'Page down'");
	Check(cap.Contains("Page up"),   "DEFECTO: ESC V solo registra 'Page up'");
	Check(cap.Contains("Disable local line editing"),
	      "DEFECTO: ESC N solo registra el mensaje");

	Log::SetSink(nullptr);
	Log::SetQuiet(false);
}

static void Test_DEFECTO_CursorAbajoEIzquierdaNoExisten()
{
	/*  Auditoria, seccion 03: el switch reconoce ESC A (arriba) y ESC C
	 *  (derecha), pero ESC B (abajo) y ESC D (izquierda) no figuran y caen
	 *  en el default como comandos desconocidos.                          */
	g_currentTest = "DEFECTO_faltan_esc_b_esc_d";
	LogCapture cap;
	Log::SetSink(&cap);
	Log::SetQuiet(true);

	{
		Harness h;
		std::string s;
		s.push_back(0x13); s.push_back((char)(0x20 + 5)); s.push_back((char)(0x20 + 5));
		s.push_back(ESC); s.push_back('B');
		s.push_back(ESC); s.push_back('D');
		h.Feed(s);
	}

	Check(cap.Contains("Unknown ESC 66"),
	      "DEFECTO: ESC B ('B' = 66) cae en el default");
	Check(cap.Contains("Unknown ESC 68"),
	      "DEFECTO: ESC D ('D' = 68) cae en el default");

	Log::SetSink(nullptr);
	Log::SetQuiet(false);
}

/* ====================================================================== */
/*  Pruebas del teclado                                                    */
/* ====================================================================== */

/** Captura lo que Keys resuelve para cada pulsacion. */
class KeyCapture : public MappedKeyListener
{
public:
	std::string sent;
	int page = 1, cursorX = 1, cursorY = 1;

	virtual void KeyMappedKey(const char *s, int len)
	{
		if (s != nullptr && len > 0) sent.append(s, (size_t)len);
	}
	virtual void KeyCommand(const char c) { sent.push_back(c); }
	virtual int KeyGetPage()    { return page; }
	virtual int KeyGetCursorX() { return cursorX; }
	virtual int KeyGetCursorY() { return cursorY; }
	virtual void KeyGetStartFieldASCII(StringBuffer *sb)
	{
		sb->Append((char)0x20);
		sb->Append((char)0x21);
	}
	void Reset() { sent.clear(); }
};

static void Test_TeclaDeFuncionEnModoBloque()
{
	/*  El camino que SI funciona: en modo bloque las teclas de funcion no
	 *  pasan por las tablas con los escapes octales rotos, sino por la
	 *  secuencia AID (SOH, codigo, pagina, campo, ETX, NUL).              */
	g_currentTest = "tecla_funcion_modo_bloque";
	Keys keys;
	KeyCapture cap;
	keys.SetListener(&cap);
	keys.SetKeySet(KEYS_BLOCK);
	keys.SetProtectMode();
	keys.UnlockKeyboard();

	keys.KeyReleased(SPC_F1, false, false, false);

	Check(cap.sent.size() == 7, "F1 produce una secuencia AID de 7 bytes");
	if (cap.sent.size() == 7)
	{
		Check((unsigned char)cap.sent[0] == (unsigned char)SOH, "arranca con SOH");
		Check(cap.sent[1] == '@', "F1 se codifica como '@'");
		Check((unsigned char)cap.sent[5] == (unsigned char)ETX, "termina con ETX");
	}
	std::printf("           F1 en bloque -> %s\n", Readable(cap.sent).c_str());
}

static void Test_DEFECTO_EscapesOctalesEnElKeysetAnsi()
{
	/*  Auditoria, defecto 01: TDM_ESC vale "\27", que en C es octal y da
	 *  0x17 (ETB), no 0x1B (ESC).
	 *
	 *  Esta prueba acota el dano, que resulto menor de lo estimado. Se
	 *  recorren los tres juegos de teclas y se cuenta cuantos indices
	 *  emiten un 0x17 de cabecera. Solo el juego ANSI esta afectado: en
	 *  conversacional y bloque, la tabla m_localCmd se consulta ANTES que
	 *  la de cadenas y devuelve los codigos correctos, de modo que las
	 *  entradas con el escape roto quedan tapadas.                        */
	g_currentTest = "DEFECTO_escapes_octales_ansi";

	KeyCapture cap;
	int rotos = 0;

	for (int i = 0; i < 32; i++)
	{
		Keys keys;
		cap.Reset();
		keys.SetListener(&cap);
		keys.SetKeySet(KEYS_ANSI);
		keys.UnlockKeyboard();
		keys.KeyTyped(i, false, false, false);

		if (!cap.sent.empty() && (unsigned char)cap.sent[0] == 0x17) rotos++;
		if (i == 0)
			std::printf("           ANSI indice 0 -> %s   (deberia ser <1B>@)\n",
			            Readable(cap.sent).c_str());
	}

	Check(rotos == 22,
	      "DEFECTO: 22 entradas del juego ANSI emiten 0x17 en vez de 0x1B");
	std::printf("           entradas ANSI con el escape roto: %d de 32\n", rotos);
}

static void Test_ModoBloqueNoSufreElEscapeOctal()
{
	/*  La contracara del defecto anterior: en modo bloque la tecla ESC si
	 *  llega bien al host, porque m_localCmd la resuelve antes de tocar la
	 *  tabla de cadenas. Es la razon por la que el autor pudo declarar el
	 *  build de Win32 sin defectos conocidos.                             */
	g_currentTest = "bloque_sin_escape_octal";

	const int juegos[] = { KEYS_CONV, KEYS_BLOCK };
	const char *nombres[] = { "conversacional", "bloque" };

	for (int j = 0; j < 2; j++)
	{
		Keys keys;
		KeyCapture cap;
		keys.SetListener(&cap);
		keys.SetKeySet(juegos[j]);
		keys.UnlockKeyboard();
		keys.KeyTyped(27, false, false, false);

		Check(cap.sent.size() == 1 && (unsigned char)cap.sent[0] == 0x1B,
		      std::string("la tecla ESC envia 0x1B en el juego ") + nombres[j]);
	}
}

static void Test_DEFECTO_TeclasDeFuncionMudasEnAnsi()
{
	/*  Hallazgo nuevo de la fase 0.
	 *
	 *  En el juego ANSI m_sendCursorWithFn es false, asi que KeyAction
	 *  salta el bloque de la secuencia AID; y al final la escritura por
	 *  tabla esta guardada tras un "if (!fn)". Con fn true y sin AID no
	 *  queda ninguna rama: la tecla no envia nada.
	 *
	 *  Efecto colateral: las entradas ESC@..ESCG de ansiChar -- las que
	 *  arrastran el escape octal roto -- son inalcanzables para las teclas
	 *  de funcion. El defecto de las tablas y este se tapan entre si.    */
	g_currentTest = "DEFECTO_teclas_funcion_mudas_en_ansi";
	Keys keys;
	KeyCapture cap;
	keys.SetListener(&cap);
	keys.SetKeySet(KEYS_ANSI);
	keys.UnlockKeyboard();

	for (int f = SPC_F1; f <= SPC_F8; f++)
	{
		cap.Reset();
		keys.KeyReleased(f, false, false, false);
		Check(cap.sent.empty(),
		      "DEFECTO: la tecla de funcion no envia nada en el juego ANSI");
	}
}

static void Test_NavegacionUsaSuPropioCodigo()
{
	/*  FASE 03, defecto corregido.
	 *
	 *  Antes: KeyReleased() asignaba m_pressedKey solo en los casos de
	 *  teclas de funcion. Las de navegacion llamaban a KeyAction sin
	 *  asignarlo, y KeyAction descartaba su parametro keycode, asi que una
	 *  flecha reenviaba lo que hubiera mapeado la tecla anterior.
	 *
	 *  Ahora KeyAction toma su parametro. Esta prueba comprueba las dos
	 *  mitades: que la tecla anterior ya no contamina, y que cada tecla de
	 *  navegacion emite su propio codigo SPC_ como byte crudo -- que es lo
	 *  que espera SharedProtocol::WriteChar, cuyo switch va justo sobre
	 *  esas constantes.                                                   */
	g_currentTest = "navegacion_usa_su_codigo";
	Keys keys;
	KeyCapture cap;
	keys.SetListener(&cap);
	keys.SetKeySet(KEYS_BLOCK);
	keys.UnlockKeyboard();

	/* El usuario teclea una 'A': deja m_pressedKey en 'A'. */
	keys.KeyTyped('A', false, false, false);
	CheckEq(cap.sent, "A", "la letra tecleada se envia tal cual");

	/* Ahora pulsa la flecha arriba: ya no reenvia la 'A'. */
	cap.Reset();
	keys.KeyReleased(SPC_UP, false, false, false);
	std::printf("           tras 'A', flecha arriba -> %s\n",
	            Readable(cap.sent).c_str());
	Check(cap.sent.size() == 1 && (unsigned char)cap.sent[0] == SPC_UP,
	      "la flecha arriba emite SPC_UP, no la tecla anterior");

	/*  Y cada una la suya. SPC_BREAK queda afuera a proposito: no tiene
	 *  caso en el switch de KeyReleased y todavia no se sabe que deberia
	 *  mandar (ver el README).                                            */
	const int nav[] = { SPC_UP, SPC_DOWN, SPC_LEFT, SPC_RIGHT,
	                    SPC_HOME, SPC_END, SPC_INS, SPC_DEL,
	                    SPC_PGUP, SPC_PGDN, SPC_PRINTSCR, SPC_SCROLLOCK };
	for (size_t i = 0; i < sizeof(nav) / sizeof(nav[0]); i++)
	{
		cap.Reset();
		keys.KeyReleased(nav[i], false, false, false);
		Check(cap.sent.size() == 1 &&
		      (unsigned char)cap.sent[0] == (unsigned char)nav[i],
		      "cada tecla de navegacion emite su propio codigo");
	}
}

static void Test_IndiceDeTeclaAcotado()
{
	/*  FASE 03. Al arreglar lo de arriba salio otro: las tablas de cadenas
	 *  llegan al indice 127 y tres de las de comandos locales solo tenian
	 *  120 entradas. Un caracter acentuado -- 0xF1, la enie -- indexaba
	 *  m_plain[241] y le hacia strlen a lo que hubiera ahi. El widget se
	 *  lo pasaba tal cual, asi que era alcanzable escribiendo.
	 *
	 *  Ahora el indice se acota. Arriba de 127 se manda el byte crudo,
	 *  porque el terminal que negociamos es tn6530-8, de ocho bits: es lo
	 *  razonable pero NO esta confirmado contra el manual.                */
	g_currentTest = "indice_de_tecla_acotado";

	const int juegos[] = { KEYS_ANSI, KEYS_CONV, KEYS_BLOCK };
	const char *nombres[] = { "ANSI", "conversacional", "bloque" };

	for (int j = 0; j < 3; j++)
	{
		Keys keys;
		KeyCapture cap;
		keys.SetListener(&cap);
		keys.SetKeySet(juegos[j]);
		keys.UnlockKeyboard();

		/* Los ocho indices que antes leian fuera del arreglo. */
		for (int i = 120; i <= 127; i++)
		{
			cap.Reset();
			keys.KeyTyped(i, false, false, false);
		}

		/* La enie de latin-1. */
		cap.Reset();
		keys.KeyTyped(0xF1, false, false, false);
		Check(cap.sent.size() == 1 && (unsigned char)cap.sent[0] == 0xF1,
		      std::string("0xF1 viaja como byte de 8 bits en el juego ") +
		      nombres[j]);

		/* Y nada de lo que quede fuera del rango de un byte. */
		cap.Reset();
		keys.KeyTyped(0x2192, false, false, false);
		Check(cap.sent.empty(),
		      std::string("un codigo mayor que 255 no envia nada en el juego ") +
		      nombres[j]);

		cap.Reset();
		keys.KeyTyped(-1, false, false, false);
		Check(cap.sent.empty(),
		      std::string("un codigo negativo no envia nada en el juego ") +
		      nombres[j]);
	}
}

/* ====================================================================== */

typedef void (*TestFn)();

int main()
{
	/* El nucleo registra mucho en el log; las pruebas que lo necesitan
	 * instalan su propio sumidero. */
	Log::SetQuiet(true);

	std::printf("\nNucleo de libvt6530 -- pruebas de la fase 0\n");
	std::printf("==========================================\n\n");

	struct { const char *grupo; TestFn fn; } tests[] = {
		{ "protocolo",  Test_TextoLlegaALaPantalla },
		{ "protocolo",  Test_DireccionamientoDeCursor },
		{ "protocolo",  Test_SaltoDeLinea },
		{ "protocolo",  Test_ModoProtegido },
		{ "protocolo",  Test_EnquireYResetLine },
		{ "protocolo",  Test_ResponderConfiguracionDeTerminal },
		{ "protocolo",  Test_LeerDireccionDeCursor },
		{ "protocolo",  Test_CamposProtegidosYLecturaDeBloque },
		{ "teclado",    Test_TeclaDeFuncionEnModoBloque },
		{ "teclado",    Test_ModoBloqueNoSufreElEscapeOctal },
		{ "teclado",    Test_NavegacionUsaSuPropioCodigo },
		{ "teclado",    Test_IndiceDeTeclaAcotado },
		{ "pantalla",   Test_LaLineaDeEstadoNoCrece },
		{ "pantalla",   Test_LaLineaDeMensajeNoDibujaLosEscapes },
		{ "defectos",   Test_DEFECTO_EscALeeCursorSinSesgo },
		{ "defectos",   Test_DEFECTO_TablaDeTiposCortaElFlujo },
		{ "defectos",   Test_DEFECTO_TablaDeTiposEnUnaSolaLectura },
		{ "defectos",   Test_DEFECTO_ComandosQueSoloSeRegistran },
		{ "defectos",   Test_DEFECTO_CursorAbajoEIzquierdaNoExisten },
		{ "defectos",   Test_DEFECTO_EscapesOctalesEnElKeysetAnsi },
		{ "defectos",   Test_DEFECTO_TeclasDeFuncionMudasEnAnsi },
	};

	const char *grupoActual = "";
	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
	{
		if (std::strcmp(grupoActual, tests[i].grupo) != 0)
		{
			grupoActual = tests[i].grupo;
			std::printf("-- %s\n", grupoActual);
		}
		tests[i].fn();
	}

	std::printf("\n------------------------------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_pass);
	std::printf("  comprobaciones fallidas . %d\n", g_fail);
	std::printf("  aserciones heredadas .... %d\n", spl_compat_assert_failures);
	std::printf("------------------------------------------\n\n");

	return (g_fail == 0) ? 0 : 1;
}
