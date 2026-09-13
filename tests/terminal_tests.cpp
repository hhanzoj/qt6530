/*
 *  Pruebas del pegamento -- Vt6530Terminal.
 *
 *  Cubren el punto donde es facil equivocarse: las dos entradas del teclado.
 *  Las constantes SPC_* del nucleo van de 0 a 29 y pisan los codigos de
 *  control ASCII, asi que un solo entero no alcanza para saber si el usuario
 *  apreto F1 o tecleo NUL. Enrutar mal no da error de compilacion: da una
 *  tecla que no hace nada.
 */

#include "../net/Vt6530Terminal.h"

#include <spl/Log.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

/* ------------------------------------------------------------------ */

static int g_ok = 0, g_mal = 0;
static std::string g_prueba;

static void Check(bool cond, const std::string &que)
{
	if (cond) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLA  %s :: %s\n", g_prueba.c_str(), que.c_str());
}

static std::string Legible(const std::string &crudo)
{
	std::string out;
	char tmp[8];
	for (size_t i = 0; i < crudo.size(); i++)
	{
		unsigned char c = (unsigned char)crudo[i];
		if (c >= 0x20 && c < 0x7F) out.push_back((char)c);
		else { std::snprintf(tmp, sizeof(tmp), "<%02X>", c); out += tmp; }
	}
	return out;
}

class Sumidero : public IHostLink
{
public:
	std::string enviado;
	virtual void SendRaw(const byte *d, int n)
	{
		if (d && n > 0) enviado.append((const char *)d, (size_t)n);
	}
	void Reset() { enviado.clear(); }
};

class Contador : public ITerminalObserver
{
public:
	int pantalla = 0, espera = 0, reset = 0;
	virtual void OnScreenChanged() { pantalla++; }
	virtual void OnHostWaiting()   { espera++; }
	virtual void OnLineReset()     { reset++; }
};

static const char SOH = 1, ETX = 3;

static void ModoConversacional(Vt6530Terminal &t)
{
	const char m[] = { SOH, 'C', ETX };
	t.FeedFromHost(m, 3);
}

static void ModoBloque(Vt6530Terminal &t)
{
	const char m[] = { SOH, 'B', ETX };
	t.FeedFromHost(m, 3);
}

/*  Direccionamiento de cursor del host: DC3, fila sesgada +0x20, columna
 *  +0x21. Las pruebas de aca abajo no fijan a que coordenada absoluta cae
 *  -- eso es asunto del sesgo, que tiene su propia prueba -- sino cuanto se
 *  mueve despues.                                                         */
static void PonerCursor(Vt6530Terminal &t, int fila, int col)
{
	const char m[] = { 0x13, (char)(fila + 0x20), (char)(col + 0x21) };
	t.FeedFromHost(m, 3);
}

static void ModoBloqueProtegido(Vt6530Terminal &t)
{
	const char m[] = { SOH, 'B', ETX, 27, 'W' };
	t.FeedFromHost(m, 5);
}

/* ------------------------------------------------------------------ */

static void Test_TextoDelHostLlegaYAvisa()
{
	g_prueba = "texto_del_host";
	Sumidero s; Vt6530Terminal t(&s);
	Contador c; t.SetObserver(&c);

	t.FeedFromHost("SYSTEM \\NONSTOP", 15);
	Check(t.ScreenText().compare(0, 15, "SYSTEM \\NONSTOP") == 0,
	      "el texto llega a la pantalla");
	Check(c.pantalla > 0, "avisa que la pantalla cambio");
}

static void Test_EotEsElTurnoDePalabra()
{
	/*  El TELSERV capturado usa EOT como turno de palabra en conversacional:
	 *  32 EOT en la sesion y ni un solo ENQ. El observador tiene que verlos
	 *  por OnLineReset, no por OnHostWaiting.                              */
	g_prueba = "eot_turno_de_palabra";
	Sumidero s; Vt6530Terminal t(&s);
	Contador c; t.SetObserver(&c);

	const char rci3[] = { 0x04, SOH, 'C', ETX, 0x00,
	                      0x04, SOH, 'C', ETX, 0x00 };
	t.FeedFromHost(rci3, sizeof(rci3));

	Check(c.reset == 2, "cada EOT despacha un reset de linea");
	Check(c.espera == 0, "no hay ENQ en esta secuencia");
	Check(!t.IsBlockMode(), "SOH C ETX deja el terminal en conversacional");
}

static void Test_ConversacionalAcumulaHastaElEnter()
{
	/*  En conversacional el nucleo NO manda tecla por tecla: acumula en
	 *  m_keyBuffer y suelta la linea entera al CR. Tiene sentido con este
	 *  host, que hace el eco el mismo (negocia WILL ECHO) y usa EOT como
	 *  turno de palabra.                                                   */
	g_prueba = "conversacional_acumula";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	s.Reset();

	t.TypeChar('A');
	t.TypeChar('B');
	Check(s.enviado.empty(),
	      "los caracteres se acumulan, no salen de a uno");

	t.TypeChar(13);
	Check(s.enviado.find("AB") != std::string::npos,
	      "la linea entera sale al presionar Enter");
}

static void Test_EnterEnConversacionalMandaLaLinea()
{
	/*  En conversacional el nucleo acumula lo tecleado y lo suelta al CR.
	 *  Esto tambien cubre el enrutado: si CR se mandara por PressSpecial en
	 *  vez de TypeChar, no pasaria nada -- 13 no es una constante SPC_.   */
	g_prueba = "enter_manda_la_linea";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	s.Reset();

	t.TypeChar('S'); t.TypeChar('T'); t.TypeChar('A');
	t.TypeChar(13);

	Check(s.enviado.find("STA") != std::string::npos,
	      "la linea acumulada sale al presionar Enter");
	std::printf("           tecleado STA + CR -> %s\n",
	            Legible(s.enviado).c_str());
}

static void Test_EnterPorElCaminoEquivocadoNoHaceNada()
{
	/*  Fija el defecto que motivo separar las dos entradas: KeyReleased
	 *  solo atiende las constantes SPC_*, y 13 no es ninguna. Es tambien la
	 *  razon por la que Vt6530::FakeEnter(), FakeBackspace() y FakeTab() de
	 *  la biblioteca original no hacen nada: llaman a KeyReleased con 10,
	 *  8 y 9, que caen en los huecos de la tabla SPC_.                     */
	g_prueba = "DEFECTO_enter_por_camino_equivocado";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	s.Reset();

	t.PressSpecial(13);        /* como si CR fuera una tecla especial */
	Check(s.enviado.empty(),
	      "DEFECTO: un CR mandado como tecla especial se pierde en silencio");

	for (int hueco : {8, 9, 10, 13})
	{
		s.Reset();
		t.PressSpecial(hueco);
		Check(s.enviado.empty(),
		      "DEFECTO: el codigo " + std::to_string(hueco) +
		      " es un hueco de la tabla SPC_ y no hace nada");
	}
}

static void Test_LasDieciseisTeclasDeFuncion()
{
	/*  Las cuatro ultimas se agregaron en la fase 03. La comprobacion de F16
	 *  no es teorica: son exactamente los siete bytes que mando un emulador
	 *  comercial al salir de VIEWSYS sobre rci3.                            */
	g_prueba = "las_dieciseis_teclas_de_funcion";
	Sumidero s; Vt6530Terminal t(&s);
	ModoBloqueProtegido(t);
	t.Keyboard()->UnlockKeyboard();

	for (int f = 1; f <= 16; f++)
	{
		s.Reset();
		Check(t.FunctionKey(f), "F" + std::to_string(f) + " se acepta");
		Check(s.enviado.size() == 7,
		      "F" + std::to_string(f) + " produce siete bytes");
		if (s.enviado.size() == 7)
		{
			Check((unsigned char)s.enviado[0] == 1, "arranca con SOH");
			Check((unsigned char)s.enviado[1] == (unsigned char)(0x40 + f - 1),
			      "el codigo sigue el patron 0x40 + (n-1)");
			Check((unsigned char)s.enviado[5] == 3, "termina con ETX");
		}
	}

	/* Byte a byte contra la captura real de VIEWSYS. */
	s.Reset();
	t.FunctionKey(16);
	const std::string capturado =
		std::string("\x01\x4F\x21\x20\x20\x03", 6) + std::string(1, '\0');
	Check(s.enviado == capturado,
	      "F16 coincide byte a byte con lo que mando el emulador comercial");
	std::printf("           F16 -> %s   (capturado de rci3: <01>O!  <03><00>)\n",
	            Legible(s.enviado).c_str());

	Check(!t.FunctionKey(17), "F17 se rechaza");
	Check(!t.FunctionKey(0),  "un numero fuera de rango se rechaza");
}

static void Test_DEFECTO_CrYLfEstanCruzados()
{
	/*  SharedProtocol::WriteChar tiene el switch cruzado:
	 *
	 *      case '\\r': Linefeed(page);     break;
	 *      case '\\n': CarageReturn(page); break;
	 *
	 *  Las implementaciones si son correctas -- CarageReturn pone la columna
	 *  en cero y Linefeed baja una fila --, asi que lo cruzado es el switch.
	 *
	 *  Hoy no se nota, y por eso importa dejarlo fijado: el unico camino que
	 *  llega ahi es Guardian::ExecLocalCommand, que ante un CR manda CR y
	 *  despues LF. Los dos errores se cancelan y el efecto neto es correcto.
	 *  Queda como trampa para la fase 04: en cuanto algo mande un CR suelto
	 *  por WriteChar, va a bajar una linea en vez de volver al margen.     */
	g_prueba = "DEFECTO_cr_y_lf_cruzados";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	t.Display()->SetEchoOn();

	t.TypeChar('A'); t.TypeChar('B'); t.TypeChar('C');
	const int filaAntes = t.Display()->GetCursorRow();
	Check(t.Display()->GetCursorCol() == 4, "tras tres letras la columna es 4");

	t.TypeChar(13);
	Check(t.Display()->GetCursorCol() == 1,
	      "el efecto neto del Enter es correcto: vuelve al margen");
	Check(t.Display()->GetCursorRow() == filaAntes + 1,
	      "y baja una fila");
	std::printf("           Enter deja el cursor en fila %d, columna %d "
	            "(los dos errores se cancelan)\n",
	            t.Display()->GetCursorRow(), t.Display()->GetCursorCol());
}

static void Test_ElPrimerCambioDeModoConservaLaPantalla()
{
	/*  CORREGIDO en la fase 03. Antes: TextDisplay::Init() dejaba las paginas
	 *  en m_pages[0] mientras SetModeConv, SetProtectMode y ExitProtectMode
	 *  usaban m_pages[1], asi que el primer SOH <modo> ETX saltaba a una
	 *  pagina en blanco. Se veia como que entrar a TACL borraba el menu de
	 *  TELSERV que ya estaba dibujado.                                      */
	g_prueba = "primer_cambio_de_modo_conserva";
	Sumidero s; Vt6530Terminal t(&s);

	t.FeedFromHost("WELCOME TO RCI3", 15);
	Check(t.ScreenText().compare(0, 15, "WELCOME TO RCI3") == 0,
	      "el banner se dibuja");

	ModoConversacional(t);
	Check(t.ScreenText().compare(0, 15, "WELCOME TO RCI3") == 0,
	      "el cambio de modo ya no se lleva puesto lo dibujado");

	t.FeedFromHost("\r\nTACL 1>", 9);
	ModoConversacional(t);
	Check(t.ScreenText().find("TACL 1>") != std::string::npos,
	      "y lo que viene despues tampoco se pierde");
}

static void Test_EscPQyBNoSeComenLosBytesSiguientes()
{
	/*  CORREGIDO en la fase 03. ESC p, ESC q y ESC b dejan el parser en el
	 *  estado 10000, que espera CR y despues LF. Antes no comprobaba:
	 *  consumiera lo que consumiera avanzaba igual. TELSERV no manda ese
	 *  CR LF -- manda el comando siguiente -- asi que se perdian dos bytes
	 *  despues de cada uno. En la captura de VIEWSYS eso se llevo puesto un
	 *  ESC q entero.
	 *
	 *  Ahora el byte inesperado se devuelve al flujo en vez de tragarse.    */
	g_prueba = "esc_p_q_b_no_comen_bytes";

	/* ESC q solo: reinicializa y borra. */
	{
		Sumidero s; Vt6530Terminal t(&s);
		t.FeedFromHost("TEXTO PREVIO", 12);
		const char d[] = { 27, 'q' };
		t.FeedFromHost(d, 2);
		Check(t.ScreenText().compare(0, 12, "TEXTO PREVIO") != 0,
		      "ESC q por si solo borra la pantalla");
	}

	/* ESC p seguido de ESC q, tal como lo manda rci3. */
	{
		Sumidero s; Vt6530Terminal t(&s);
		t.FeedFromHost("TEXTO PREVIO", 12);
		const char d[] = { 27, 'p', '2', 27, 'q' };
		t.FeedFromHost(d, 5);
		Check(t.ScreenText().compare(0, 12, "TEXTO PREVIO") != 0,
		      "tras ESC p, el ESC q sigue llegando y reinicializa");
	}

	/* ESC b seguido de otro comando. */
	{
		Sumidero s; Vt6530Terminal t(&s);
		const char d[] = { 27, 'b', 27, 'W' };
		t.FeedFromHost(d, 4);
		Check(t.IsProtectMode(), "tras ESC b, el ESC W sigue llegando");
	}

	/* Y con el CR LF que el estado espera, tambien. */
	{
		Sumidero s; Vt6530Terminal t(&s);
		const char d[] = { 27, 'b', 13, 10, 27, 'W' };
		t.FeedFromHost(d, 6);
		Check(t.IsProtectMode(), "con el CR LF intercalado sigue funcionando");
	}
}

static void Test_ReparteTcpSinRomper()
{
	/*  El mismo contenido entregado de a un byte tiene que dar la misma
	 *  pantalla. Es la propiedad que el nucleo NO cumple para ESC r, y por
	 *  eso conviene comprobarla en el pegamento. */
	g_prueba = "reparto_tcp";
	const std::string datos = "PRIMERA LINEA\r\nSEGUNDA LINEA\r\nTERCERA";

	Sumidero s1; Vt6530Terminal entero(&s1);
	entero.FeedFromHost(datos.data(), (int)datos.size());

	Sumidero s2; Vt6530Terminal partido(&s2);
	for (size_t i = 0; i < datos.size(); i++)
		partido.FeedFromHost(datos.data() + i, 1);

	Check(entero.ScreenText() == partido.ScreenText(),
	      "byte a byte produce la misma pantalla que de una sola vez");
}

static void Test_LasFlechasMuevenElCursor()
{
	/*  CORREGIDO en la fase 03. Keys::KeyReleased asignaba m_pressedKey
	 *  solo en los casos de teclas de funcion; las de navegacion llamaban a
	 *  KeyAction sin asignarlo, y KeyAction descartaba su parametro. Una
	 *  flecha reenviaba lo que hubiera mapeado la tecla anterior.
	 *
	 *  La cadena completa: PressSpecial -> Keys::KeyReleased ->
	 *  m_localCmd[i] (que vale i) -> KeyCommand -> ExecLocalCommand ->
	 *  WriteLocal -> SharedProtocol::WriteChar, que hace switch sobre las
	 *  mismas constantes SPC_. Faltaba solo el primer eslabon.            */
	g_prueba = "las_flechas_mueven_el_cursor";
	Sumidero s; Vt6530Terminal t(&s);
	ModoBloque(t);
	t.Keyboard()->UnlockKeyboard();

	PonerCursor(t, 5, 10);
	const int fila = t.Display()->GetCursorRow();
	const int col  = t.Display()->GetCursorCol();
	Check(fila > 1 && col > 1,
	      "el host deja el cursor lejos del margen, que es de donde parte");

	s.Reset();
	t.PressSpecial(SPC_DOWN);
	Check(t.Display()->GetCursorRow() == fila + 1, "abajo baja una fila");

	t.PressSpecial(SPC_RIGHT);
	Check(t.Display()->GetCursorCol() == col + 1, "derecha avanza una columna");

	t.PressSpecial(SPC_UP);
	Check(t.Display()->GetCursorRow() == fila, "arriba vuelve a la fila");

	t.PressSpecial(SPC_LEFT);
	Check(t.Display()->GetCursorCol() == col, "izquierda vuelve a la columna");

	std::printf("           tras las cuatro flechas: fila %d, columna %d\n",
	            t.Display()->GetCursorRow(), t.Display()->GetCursorCol());

	Check(s.enviado.empty(),
	      "en modo bloque la navegacion es local: no sale nada al host");
}

static void Test_LaFlechaYaNoRepiteLaTeclaAnterior()
{
	/*  La otra mitad del defecto, y la que se veia: escribir una letra y
	 *  despues pulsar una flecha duplicaba la letra en la pantalla, porque
	 *  m_pressedKey seguia apuntando a ella.                              */
	g_prueba = "la_flecha_no_repite_la_anterior";
	Sumidero s; Vt6530Terminal t(&s);
	ModoBloque(t);
	t.Keyboard()->UnlockKeyboard();

	PonerCursor(t, 3, 1);
	t.TypeChar('A');
	t.TypeChar('B');
	Check(t.ScreenText().find("AB") != std::string::npos,
	      "las dos letras se dibujan");
	const int col = t.Display()->GetCursorCol();

	t.PressSpecial(SPC_LEFT);
	Check(t.ScreenText().find("ABB") == std::string::npos,
	      "la flecha no vuelve a escribir la letra anterior");
	Check(t.Display()->GetCursorCol() == col - 1,
	      "y el cursor retrocede una columna");
}

static void Test_LaNavegacionNoEnsuciaLaLineaConversacional()
{
	/*  Consecuencia directa de arreglar lo anterior. En conversacional el
	 *  nucleo acumula lo tecleado en m_keyBuffer y lo suelta al CR;
	 *  ExecLocalCommand metia en ese buffer cualquier codigo que no fuera
	 *  CR ni backspace. Con las flechas ya mandando su propio codigo, eso
	 *  habria inyectado bytes de control en el medio de la linea.
	 *
	 *  Ahora los codigos de movimiento se ejecutan en la pantalla y no
	 *  entran a la linea.                                                 */
	g_prueba = "navegacion_no_ensucia_la_linea";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	s.Reset();

	t.TypeChar('L'); t.TypeChar('O'); t.TypeChar('G');
	t.PressSpecial(SPC_LEFT);
	t.PressSpecial(SPC_RIGHT);
	t.PressSpecial(SPC_UP);
	t.TypeChar('O'); t.TypeChar('N');
	t.TypeChar(13);

	std::printf("           LOG + flechas + ON + CR -> %s\n",
	            Legible(s.enviado).c_str());

	for (size_t i = 0; i < s.enviado.size(); i++)
	{
		const unsigned char c = (unsigned char)s.enviado[i];
		Check(c >= 0x20 || c == 13,
		      "la linea no lleva codigos de control de navegacion");
	}
	Check(s.enviado.find("LOGON") != std::string::npos,
	      "y lo tecleado llega entero");
}

static void Test_Esc6PintaInvisible()
{
	/*  CORREGIDO en la fase 03, y es el mecanismo real por el que una clave
	 *  no se ve.
	 *
	 *  Primero hubo un intento equivocado: atar el eco local a la
	 *  negociacion telnet (WILL ECHO). No sirve. TELSERV declara WILL ECHO
	 *  y despues NO hace el eco caracter por caracter: en TN6530
	 *  conversacional el terminal edita la linea localmente y la manda
	 *  entera al CR. Con el eco apagado no se veia nada de lo tecleado.
	 *
	 *  Lo que de verdad oculta la clave es el atributo de video invisible:
	 *  el host manda ESC 6 con el bit 3 y lo que se escriba a partir de ahi
	 *  --incluido el eco local-- no se dibuja. El eco sigue siendo local.
	 *
	 *  Estaba roto por dos lados, y los dos defectos se tapaban entre si:
	 *
	 *    1. Guardian pasaba el byte del cable crudo a SetWriteAttribute,
	 *       que empieza con ASSERT((attr & MASK_CHAR) == 0). El byte del
	 *       cable siempre cae en el byte bajo, asi que la asercion se
	 *       disparaba siempre y el atributo nunca se aplicaba. Peor: el
	 *       valor crudo se colaba en el caracter y lo corrompia.
	 *       El decodificador ya existia (DecodeVideoAttrs) pero solo lo
	 *       usaba el camino de inicio de campo, que es el de modo bloque.
	 *
	 *    2. SharedProtocol::WriteChar, en su rama por defecto --la que usa
	 *       TODO el modo conversacional-- se salteaba GetWriteAttr(). La
	 *       rama de al lado si lo aplica, y ProtectPage tambien.         */
	g_prueba = "esc6_pinta_invisible";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);

	/*  ESC 6 con el bit 3: invisible. El bit 5 es el de encuadre que manda
	 *  el host para que el byte caiga en el rango imprimible.            */
	const char esc[] = { 27, '6', (char)((1 << 3) | (1 << 5)) };
	t.FeedFromHost(esc, 3);

	const char *clave = "CLAVE";
	for (const char *c = clave; *c; c++) t.TypeChar(*c);

	Page *pagina = t.Display()->GetDisplayPage();
	const int fila = t.Display()->GetCursorRow() - 1;

	int invisibles = 0, correctos = 0;
	for (int col = 0; col < 5; col++)
	{
		PageCell *celda = pagina->GetCell(col, fila);
		if (celda->IsInvis())            invisibles++;
		if (celda->Get() == clave[col])  correctos++;
	}
	std::printf("           tras ESC 6 invisible: %d de 5 celdas invisibles, "
	            "%d con el caracter intacto\n", invisibles, correctos);

	Check(invisibles == 5, "lo tecleado queda marcado invisible");
	Check(correctos == 5,
	      "y el caracter no se corrompe (antes el atributo crudo se le "
	      "colaba encima)");

	/*  El widget ya respeta IsInvis() al pintar, asi que con esto la clave
	 *  no llega a la pantalla. Y sale igual al host, que es lo que tiene
	 *  que pasar.                                                        */
	s.Reset();
	t.TypeChar(13);
	Check(s.enviado.find(clave) != std::string::npos,
	      "la clave si llega al host, aunque no se dibuje");
}

static void Test_EnBloqueElEcoEsSiempreLocal()
{
	/*  La excepcion que importa: en modo bloque las teclas no salen al host
	 *  hasta que se manda el bloque, asi que no hay nada que el host pueda
	 *  devolver. El eco tiene que ser local aunque telnet ECHO este activo,
	 *  o la pantalla quedaria muda mientras se llena un formulario.       */
	g_prueba = "en_bloque_el_eco_es_local";
	Sumidero s; Vt6530Terminal t(&s);
	ModoBloque(t);
	t.Keyboard()->UnlockKeyboard();
	t.SetLocalEcho(false);          /* si algo alguna vez lo apaga */

	PonerCursor(t, 4, 4);
	for (const char *c = "DATO"; *c; c++) t.TypeChar(*c);

	Check(t.ScreenText().find("DATO") != std::string::npos,
	      "en bloque lo tecleado se dibuja igual");
}

static void Test_ConEcoDelHostNoSeDuplicaNiSeVeLaClave()
{
	/*  Esta prueba es una traza real de rci3, reproducida.
	 *
	 *      => term: logon nkaizen.jaracena<0D>
	 *      <= host: logon nkaizen.jaracena<0D><0A>   <- el host lo devuelve
	 *      <= host: Password:
	 *      => term: <la clave><0D>
	 *      <= host: <0D><0A>                          <- la clave NO vuelve
	 *
	 *  El host hace el eco de todo menos de la clave. Y nosotros le
	 *  contestamos DO ECHO, o sea que le pedimos que se encargue el. Con el
	 *  eco local tambien prendido, cada comando salia DOS veces en pantalla
	 *  -- "sysinfo" arriba y "sysinfo" abajo -- y la clave salia una.
	 *
	 *  Con el eco del host: cada cosa una vez, y la clave ninguna.       */
	g_prueba = "eco_del_host_sin_duplicar";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	t.SetLocalEcho(false);          /* el host se encarga */

	/* El usuario teclea el logon. */
	s.Reset();
	const char *linea = "logon nkaizen.jaracena";
	for (const char *c = linea; *c; c++) t.TypeChar(*c);

	Check(t.ScreenText().find(linea) == std::string::npos,
	      "mientras se teclea, el terminal no dibuja nada");

	t.TypeChar(13);
	Check(s.enviado.find(linea) != std::string::npos,
	      "pero la linea si sale al host");

	/* El host la devuelve, y ahi aparece -- una sola vez. */
	const std::string eco = std::string(linea) + "\r\n";
	t.FeedFromHost(eco.data(), (int)eco.size());

	const std::string pantalla = t.ScreenText();
	const size_t primera = pantalla.find(linea);
	Check(primera != std::string::npos, "el eco del host si se dibuja");
	Check(pantalla.find(linea, primera + 1) == std::string::npos,
	      "y una sola vez: no hay duplicado");
	std::printf("           tras el eco del host, aparece %s\n",
	            (primera != std::string::npos &&
	             pantalla.find(linea, primera + 1) == std::string::npos)
	            ? "una sola vez" : "MAL");

	/*  Ahora la clave. El host manda el prompt, el usuario teclea, y el
	 *  host devuelve solo CR LF.                                         */
	t.FeedFromHost("Password:", 9);
	s.Reset();
	const char *clave = "unaclavesecreta";
	for (const char *c = clave; *c; c++) t.TypeChar(*c);
	t.TypeChar(13);

	Check(s.enviado.find(clave) != std::string::npos,
	      "la clave sale al host");

	t.FeedFromHost("\r\n", 2);
	Check(t.ScreenText().find(clave) == std::string::npos,
	      "y no aparece en pantalla en ningun momento: el host no la devuelve");
}

static void Test_ConEcoLocalSeDibujaAlTeclear()
{
	/*  La contracara, para un host que NO haga el eco: el terminal dibuja
	 *  lo tecleado al momento. Es el comportamiento de 2007 y sigue estando
	 *  disponible con --eco local.                                        */
	g_prueba = "eco_local_dibuja_al_teclear";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	t.SetLocalEcho(true);

	for (const char *c = "sysinfo"; *c; c++) t.TypeChar(*c);
	Check(t.ScreenText().find("sysinfo") != std::string::npos,
	      "se dibuja mientras se teclea, sin esperar al host");
}

static void Test_ConEcoDelHostSeMandaCaracterPorCaracter()
{
	/*  Lo que hace que se vea lo tecleado MIENTRAS se teclea, y no recien
	 *  al apretar Enter.
	 *
	 *  Si el eco lo hace el host, quien dibuja es el host devolviendo lo
	 *  que recibe. Acumular la linea hasta el CR significa que el host no
	 *  recibe nada hasta entonces, y por lo tanto no devuelve nada: se
	 *  teclea a ciegas. Mandando cada tecla en el momento, el host la
	 *  devuelve en el momento.
	 *
	 *  No es un invento: rci3 negocia WILL ECHO y WILL SGA, que juntos son
	 *  la definicion de telnet caracter por caracter.                    */
	g_prueba = "eco_del_host_caracter_por_caracter";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	t.SetLocalEcho(false);
	s.Reset();

	t.TypeChar('t');
	Check(s.enviado == "t", "la primera tecla sale sola, sin esperar al CR");
	t.TypeChar('a');
	t.TypeChar('c');
	t.TypeChar('l');
	Check(s.enviado == "tacl", "y las siguientes tambien, en orden");

	t.TypeChar(13);
	Check(s.enviado == std::string("tacl\r"), "el CR sale igual que el resto");
	std::printf("           con eco del host, 'tacl'+CR sale como %s\n",
	            Legible(s.enviado).c_str());
}

static void Test_ConEcoLocalSeSigueAcumulandoLaLinea()
{
	/*  La contracara. Con eco local el terminal edita la linea y la manda
	 *  entera al CR, que es el modelo del 6530 y el comportamiento de 2007.
	 *  Cambiar eso seria romper el modo conversacional para los hosts que
	 *  no hacen el eco.                                                   */
	g_prueba = "eco_local_acumula_la_linea";
	Sumidero s; Vt6530Terminal t(&s);
	ModoConversacional(t);
	t.SetLocalEcho(true);
	s.Reset();

	t.TypeChar('t'); t.TypeChar('a'); t.TypeChar('c'); t.TypeChar('l');
	Check(s.enviado.empty(), "no sale nada hasta el CR");

	t.TypeChar(13);
	Check(s.enviado.find("tacl") != std::string::npos,
	      "y al CR sale la linea entera");
}

static void Test_SalirDeBloqueLimpiaLaPantalla()
{
	/*  CORREGIDO en la fase 03, y sale de una captura de pantalla real:
	 *  al salir de VIEWSYS, la pantalla de VIEWSYS quedaba abajo y los
	 *  prompts nuevos de TACL se dibujaban encima, mezclados.
	 *
	 *  Era la unica transicion de modo que no limpiaba. ESC W limpia,
	 *  ESC X limpia, y SOH B ETX limpia porque pasa por ExitProtectMode.
	 *  Solo SOH C ETX se lo salteaba.                                    */
	g_prueba = "salir_de_bloque_limpia";
	Sumidero s; Vt6530Terminal t(&s);

	/* Se entra a modo bloque protegido y el host dibuja algo. */
	ModoBloqueProtegido(t);
	t.FeedFromHost("VIEWSYS L06.08", 14);
	Check(t.ScreenText().find("VIEWSYS") != std::string::npos,
	      "la pantalla de bloque se dibuja");

	/* Y se sale a conversacional, como hace VIEWSYS al terminar. */
	ModoConversacional(t);
	Check(t.ScreenText().find("VIEWSYS") == std::string::npos,
	      "al salir de bloque la pantalla queda limpia");
	Check(!t.IsBlockMode() && !t.IsProtectMode(),
	      "y el terminal queda en conversacional");
}

static void Test_ElTurnoDePalabraNoBorraLaPantalla()
{
	/*  La contracara, y la razon por la que no se puede limpiar siempre.
	 *
	 *  Este host manda SOH C ETX todo el tiempo como turno de palabra en
	 *  conversacional -- diez veces en una sesion corta, y varias seguidas.
	 *  Tal cual sale de una traza en vivo:
	 *
	 *      <= host: <04><01>C<03><00><04><01>C<03><00>TACL 1>
	 *
	 *  Si cada uno limpiara, la pantalla se borraria cada dos lineas.    */
	g_prueba = "turno_de_palabra_no_borra";
	Sumidero s; Vt6530Terminal t(&s);

	t.FeedFromHost("WELCOME TO RCI3", 15);

	/* Cuatro cambios seguidos estando ya en conversacional. */
	for (int i = 0; i < 4; i++) ModoConversacional(t);

	Check(t.ScreenText().find("WELCOME TO RCI3") != std::string::npos,
	      "el turno de palabra no se lleva puesto lo dibujado");

	/*  Y lo mismo con el EOT delante, que es como llega de verdad. */
	const char turno[] = { 0x04, SOH, 'C', ETX, 0x00,
	                       0x04, SOH, 'C', ETX, 0x00 };
	t.FeedFromHost(turno, sizeof(turno));
	Check(t.ScreenText().find("WELCOME TO RCI3") != std::string::npos,
	      "tampoco con el EOT delante, tal como lo manda rci3");
}

/*  Arma una pantalla como la de TEDIT: modo bloque protegido y una tira de
 *  campos DESPROTEGIDOS, uno por fila, que empiezan en la columna 0. La
 *  celda del GS es el inicio de campo y queda protegida; los datos van de
 *  la columna 1 en adelante.                                             */
static void PantallaDeCampos(Vt6530Terminal &t, int filas)
{
	ModoBloqueProtegido(t);
	for (int f = 0; f < filas; f++)
	{
		const char campo[] = {
			0x11, (char)(f + 0x20), (char)(0 + 0x20),   /* DC1 fila,col */
			0x1D, (char)(0 + 0x20), (char)(32 + 0x20)   /* GS  video,dato -- 32 = desprotegido */
		};
		t.FeedFromHost(campo, sizeof(campo));
	}
}

static void Test_LasFlechasVerticalesConservanLaColumna()
{
	/*  CORREGIDO en la fase 03, y sale de usar TEDIT de verdad.
	 *
	 *  ProtectPage::ArrowDown y ArrowUp eran, literalmente, Tab(page, 1) y
	 *  Tab(page, -1): no bajaban ni subian, saltaban al campo desprotegido
	 *  siguiente o anterior y perdian la columna. En una pantalla donde
	 *  cada linea es un campo, "abajo" parecia andar de casualidad -- caia
	 *  en la primera posicion del campo de abajo -- y "arriba" no andaba,
	 *  porque el Tab hacia atras estaba roto por partida doble.           */
	g_prueba = "flechas_verticales_conservan_columna";
	Sumidero s; Vt6530Terminal t(&s);
	PantallaDeCampos(t, 10);
	t.Keyboard()->UnlockKeyboard();

	/* Se posiciona en una columna que no sea la primera del campo. */
	for (int i = 0; i < 5; i++) t.PressSpecial(SPC_RIGHT);
	const int col = t.Display()->GetCursorCol();
	const int fila = t.Display()->GetCursorRow();
	Check(col > 2, "el cursor esta bien adentro del campo");

	t.PressSpecial(SPC_DOWN);
	Check(t.Display()->GetCursorRow() == fila + 1, "abajo baja una fila");
	Check(t.Display()->GetCursorCol() == col, "y conserva la columna");

	t.PressSpecial(SPC_UP);
	Check(t.Display()->GetCursorRow() == fila, "arriba vuelve a la fila");
	Check(t.Display()->GetCursorCol() == col, "y tambien conserva la columna");
	std::printf("           bajar y subir desde (%d,%d) deja el cursor en (%d,%d)\n",
	            fila, col, t.Display()->GetCursorRow(), t.Display()->GetCursorCol());

	/*  Y arriba desde la primera fila envuelve a la ultima, no se queda
	 *  clavado ni se va a la columna 80 como hacia antes.                */
	while (t.Display()->GetCursorRow() > 1) t.PressSpecial(SPC_UP);
	t.PressSpecial(SPC_UP);
	Check(t.Display()->GetCursorRow() > 1,
	      "arriba desde la primera fila envuelve hacia abajo");
	Check(t.Display()->GetCursorCol() == col,
	      "y sigue conservando la columna");
}

static void Test_SalirDeBloqueLimpiaLaLineaDeEstado()
{
	/*  CORREGIDO en la fase 03, y sale de una captura de pantalla suya: al
	 *  salir de TEDIT la pagina quedaba limpia pero abajo seguia colgado
	 *  "1) $DATA01.JARACENA.PRUEBA 1/24 (BOF) (EOF)" mientras arriba ya
	 *  corria TACL. El mensaje es de la aplicacion que se fue.            */
	g_prueba = "salir_de_bloque_limpia_la_linea_de_estado";
	Sumidero s; Vt6530Terminal t(&s);
	ModoBloqueProtegido(t);

	/* La aplicacion escribe su mensaje, como hace TEDIT. */
	std::string m;
	m.push_back(27); m.push_back('o');
	m += "1) $DATA01.JARACENA.PRUEBA 1/24 (BOF) (EOF)";
	m.push_back(13);
	t.FeedFromHost(m.data(), (int)m.size());

	std::string linea(t.Display()->GetStatusLine()->GetChars(), 80);
	Check(linea.find("$DATA01.JARACENA.PRUEBA") != std::string::npos,
	      "el mensaje de la aplicacion se escribe");

	ModoConversacional(t);
	linea.assign(t.Display()->GetStatusLine()->GetChars(), 80);
	Check(linea.find("$DATA01.JARACENA.PRUEBA") == std::string::npos,
	      "y al salir de bloque se limpia");
	Check(linea.find("CONV") != std::string::npos,
	      "pero el modo queda, que es de la sesion y no de la aplicacion");
	std::printf("           linea de estado tras salir: [%s]\n", linea.c_str());
}

static void Test_LaRespuestaAEscInterrogacionTerminaBien()
{
	/*  CORREGIDO en la fase 03, y es el que colgaba a TEDIT.
	 *
	 *  TEDIT arranca preguntando ESC ^ (estado) y ESC ? (configuracion), y
	 *  se queda esperando la respuesta de la segunda. La de conversacional
	 *  terminaba en "\13", que en C es OCTAL y da 0x0B -- no el CR que
	 *  usan las otras tres consultas (ESC ^, ESC _ y ESC a, todas con el 13
	 *  decimal). El sintoma era que la pantalla de TEDIT no aparecia hasta
	 *  apretar cualquier tecla, que era lo que destrababa al host.
	 *
	 *  Sale de una traza en vivo, que termina justo ahi:
	 *
	 *      <= host: <1B>^
	 *      => term: <01>?CFC<0D>
	 *      <= host: <1B>?
	 *      => term: <01>!A 2B72C ... 1h10<0B>     <-- deberia ser <0D>
	 *      (y el host no manda nada mas)
	 *
	 *  El segundo defecto es del modo bloque: esa respuesta termina en
	 *  ETX NUL, pero se mandaba con strlen(), que corta justo en el NUL.  */
	g_prueba = "respuesta_esc_interrogacion";

	{
		Sumidero s; Vt6530Terminal t(&s);
		ModoConversacional(t);
		s.Reset();
		const char q[] = { 27, '?' };
		t.FeedFromHost(q, 2);

		Check(!s.enviado.empty(), "contesta algo en conversacional");
		Check(s.enviado.find("A 2B72C") != std::string::npos,
		      "y es la cadena de configuracion");
		Check(!s.enviado.empty() &&
		      (unsigned char)s.enviado[s.enviado.size() - 1] == 0x0D,
		      "termina en CR, como las otras consultas");
		std::printf("           ESC ? en conversacional -> %d bytes, "
		            "termina en %02X\n", (int)s.enviado.size(),
		            (unsigned char)s.enviado[s.enviado.size() - 1]);
	}

	{
		Sumidero s; Vt6530Terminal t(&s);
		ModoBloqueProtegido(t);
		s.Reset();
		const char q[] = { 27, '?' };
		t.FeedFromHost(q, 2);

		Check(s.enviado.size() >= 2, "contesta algo en bloque");
		if (s.enviado.size() >= 2)
		{
			const size_t n = s.enviado.size();
			Check((unsigned char)s.enviado[n - 2] == 0x03 &&
			      (unsigned char)s.enviado[n - 1] == 0x00,
			      "y termina en ETX NUL: el NUL ya no se pierde en strlen");
		}
	}

	/*  Y las otras tres consultas, que ya estaban bien: se fijan para que
	 *  nadie las "arregle" al reves.                                     */
	const char consultas[] = { '^', '_', 'a' };
	for (size_t i = 0; i < sizeof(consultas); i++)
	{
		Sumidero s; Vt6530Terminal t(&s);
		ModoConversacional(t);
		s.Reset();
		const char q[] = { 27, consultas[i] };
		t.FeedFromHost(q, 2);
		Check(!s.enviado.empty() &&
		      (unsigned char)s.enviado[s.enviado.size() - 1] == 0x0D,
		      std::string("ESC ") + consultas[i] + " tambien termina en CR");
	}
}

/** Lee las primeras n columnas de una fila (base 0) como texto. */
static std::string FilaDeLaPantalla(Vt6530Terminal &t, int fila, int n)
{
	Page *p = t.Display()->GetDisplayPage();
	std::string s;
	for (int c = 0; c < n; c++)
	{
		const char ch = p->GetCell(c, fila)->Get();
		s.push_back((ch >= 0x20 && ch < 0x7F) ? ch : '.');
	}
	return s;
}

static void Test_InsertarYBorrarCaracter()
{
	/*  FASE 04. SharedProtocol::InsertChar y DeleteChar tenian el cuerpo
	 *  vacio desde 2007, asi que las teclas Insertar y Suprimir no hacian
	 *  nada. Se nota apenas se corrige un texto con TEDIT.                */
	g_prueba = "insertar_y_borrar_caracter";
	Sumidero s; Vt6530Terminal t(&s);
	PantallaDeCampos(t, 6);
	t.Keyboard()->UnlockKeyboard();

	/*  La primera posicion escribible es la 1: la 0 es la celda del inicio
	 *  de campo. Es donde TEDIT deja el cursor.                          */
	t.Display()->SetCursorRowCol(0, 1);
	for (const char *c = "ABCDEFGH"; *c; c++) t.TypeChar(*c);
	Check(FilaDeLaPantalla(t, 0, 9) == " ABCDEFGH",
	      "el texto se escribe en el campo");

	for (int i = 0; i < 4; i++) t.PressSpecial(SPC_LEFT);
	t.PressSpecial(SPC_INS);
	std::printf("           tras INSERTAR: [%s]\n",
	            FilaDeLaPantalla(t, 0, 12).c_str());
	Check(FilaDeLaPantalla(t, 0, 10) == " ABCD EFGH",
	      "Insertar abre un hueco donde esta el cursor");

	t.PressSpecial(SPC_DEL);
	std::printf("           tras SUPRIMIR: [%s]\n",
	            FilaDeLaPantalla(t, 0, 12).c_str());
	Check(FilaDeLaPantalla(t, 0, 9) == " ABCDEFGH",
	      "Suprimir cierra el hueco y deja el texto como estaba");
}

static void Test_InsertarCaracterNoPisaElCampoDeAlLado()
{
	/*  El limite es el CAMPO, no la fila: correr mas alla pisaria el campo
	 *  vecino, que en un formulario es de otra cosa. Por eso ProtectPage
	 *  redefine las dos.                                                  */
	g_prueba = "insertar_no_pisa_el_campo_vecino";
	Sumidero s; Vt6530Terminal t(&s);
	ModoBloqueProtegido(t);

	/*  Dos campos en la misma fila: uno en la columna 0 y otro en la 10. */
	const char pantalla[] = {
		0x11, 0x20, 0x20,  0x1D, 0x20, (char)(32 + 0x20),
		'A','B','C',
		0x11, 0x20, (char)(10 + 0x20), 0x1D, 0x20, (char)(32 + 0x20),
		'X','Y','Z'
	};
	t.FeedFromHost(pantalla, sizeof(pantalla));
	t.Keyboard()->UnlockKeyboard();

	const std::string antes = FilaDeLaPantalla(t, 0, 16);
	Check(antes.find("XYZ") != std::string::npos,
	      "el segundo campo esta escrito");

	/* Se inserta en el primer campo, pegado al borde. */
	t.Display()->SetCursorRowCol(0, 1);
	for (int i = 0; i < 6; i++) t.PressSpecial(SPC_INS);

	const std::string despues = FilaDeLaPantalla(t, 0, 16);
	std::printf("           antes  [%s]\n           despues[%s]\n",
	            antes.c_str(), despues.c_str());
	Check(despues.find("XYZ") != std::string::npos,
	      "el campo de al lado no se movio");
}

/** Escribe UNO/DOS/TRES en las tres primeras filas, desde la columna 1. */
static void TresFilasDeTexto(Vt6530Terminal &t)
{
	Page *p = t.Display()->GetDisplayPage();
	const char *txt[] = { "UNO", "DOS", "TRES" };
	for (int r = 0; r < 3; r++)
		for (int c = 0; txt[r][c] != '\0'; c++)
			p->GetCell(c + 1, r)->Set(txt[r][c]);
}

static void Test_InsertarYBorrarLineaEnConversacional()
{
	/*  FASE 04. ESC L y ESC M tenian el cuerpo vacio, asi que no hacian
	 *  nada ni desde el host ni desde el teclado. En el widget van por
	 *  Ctrl+Insertar y Ctrl+Suprimir.
	 *
	 *  Mueven el CONTENIDO de las filas, no su estructura de campos: los
	 *  inicios de campo estan ademas en listas de punteros a celda, y
	 *  correrlos las dejaria apuntando a cualquier lado.
	 *
	 *  ESTA prueba corria antes sobre una pantalla en modo PROTEGIDO, que
	 *  era el lugar equivocado: ver la de abajo.                          */
	g_prueba = "insertar_y_borrar_linea_en_conversacional";
	Sumidero s; Vt6530Terminal t(&s);
	t.Keyboard()->UnlockKeyboard();

	TresFilasDeTexto(t);

	t.Display()->SetCursorRowCol(1, 1);
	t.InsertLine();
	std::printf("           tras INSERTAR LINEA: [%s][%s][%s][%s]\n",
	            FilaDeLaPantalla(t,0,5).c_str(), FilaDeLaPantalla(t,1,5).c_str(),
	            FilaDeLaPantalla(t,2,5).c_str(), FilaDeLaPantalla(t,3,5).c_str());
	Check(FilaDeLaPantalla(t, 0, 4) == " UNO",  "la de arriba no se toca");
	Check(FilaDeLaPantalla(t, 1, 4) == "    ",  "la fila del cursor queda en blanco");
	Check(FilaDeLaPantalla(t, 2, 4) == " DOS",  "y lo que habia baja una fila");
	Check(FilaDeLaPantalla(t, 3, 5) == " TRES", "y la siguiente tambien");

	t.Display()->SetCursorRowCol(1, 1);
	t.DeleteLine();
	std::printf("           tras BORRAR LINEA:   [%s][%s][%s]\n",
	            FilaDeLaPantalla(t,0,5).c_str(), FilaDeLaPantalla(t,1,5).c_str(),
	            FilaDeLaPantalla(t,2,5).c_str());
	Check(FilaDeLaPantalla(t, 1, 4) == " DOS",  "borrar la linea sube lo de abajo");
	Check(FilaDeLaPantalla(t, 2, 5) == " TRES", "todo lo de abajo, no solo una");
}

static void Test_EnProtegidoElTecladoNoCorreLaPantalla()
{
	/*
	 *  Ctrl+Insertar y Ctrl+Suprimir NO corren la pantalla en modo
	 *  protegido.
	 *
	 *  Esta prueba contradice a proposito lo que afirmaba la anterior. La
	 *  version vieja de Test_InsertarYBorrarLinea armaba una pantalla de
	 *  campos -- o sea, modo protegido -- y comprobaba que las filas se
	 *  corrieran. Fijaba el defecto en vez de la regla.
	 *
	 *  El defecto se vio usando VIEWSYS: Ctrl+Suprimir borraba NUESTRA
	 *  pantalla, y de ahi en mas lo dibujado no tenia que ver con lo que el
	 *  host creia que habia. En modo protegido la pantalla es un formulario
	 *  que armo la aplicacion; correr las filas rompe esa correspondencia, y
	 *  el manual del 6530 dice ademas que un campo protegido no se modifica.
	 *
	 *  Ojo con lo que esta prueba NO dice: no dice que haya que ignorar la
	 *  tecla. VIEWSYS documenta esas dos como "Reset MAXIMUMs" y "Exit", que
	 *  son funciones de la aplicacion, asi que podria corresponder mandarle
	 *  algo al host. Eso se resuelve con una captura de un cliente que
	 *  funcione. Lo unico que esta prueba fija es lo que ya se sabe: que la
	 *  pantalla local no se toca.
	 */
	g_prueba = "en_protegido_el_teclado_no_corre_la_pantalla";
	Sumidero s; Vt6530Terminal t(&s);
	PantallaDeCampos(t, 6);
	t.Keyboard()->UnlockKeyboard();

	TresFilasDeTexto(t);
	Check(t.IsProtectMode(), "la pantalla de campos deja el modo protegido");

	const std::string antes0 = FilaDeLaPantalla(t, 0, 5);
	const std::string antes1 = FilaDeLaPantalla(t, 1, 5);
	const std::string antes2 = FilaDeLaPantalla(t, 2, 5);

	t.Display()->SetCursorRowCol(1, 1);
	t.InsertLine();
	Check(FilaDeLaPantalla(t, 0, 5) == antes0 &&
	      FilaDeLaPantalla(t, 1, 5) == antes1 &&
	      FilaDeLaPantalla(t, 2, 5) == antes2,
	      "Ctrl+Insertar no mueve nada en protegido");

	t.Display()->SetCursorRowCol(1, 1);
	t.DeleteLine();
	Check(FilaDeLaPantalla(t, 0, 5) == antes0 &&
	      FilaDeLaPantalla(t, 1, 5) == antes1 &&
	      FilaDeLaPantalla(t, 2, 5) == antes2,
	      "Ctrl+Suprimir tampoco");

	std::printf("           en protegido la pantalla queda igual: [%s][%s][%s]\n",
	            FilaDeLaPantalla(t,0,5).c_str(), FilaDeLaPantalla(t,1,5).c_str(),
	            FilaDeLaPantalla(t,2,5).c_str());

	/*  Y el camino del HOST sigue intacto: si la aplicacion manda ESC L, se
	 *  hace. El que decide ahi es el host, no el teclado.                 */
	t.Display()->SetCursorRowCol(1, 1);
	t.Display()->LineDown();
	Check(FilaDeLaPantalla(t, 1, 5) != antes1,
	      "pero un ESC L del host si mueve la pantalla");
	std::printf("           y el ESC L del host sigue funcionando igual\n");
}

static void Test_EnterEnProtegidoSoloMandaElAid()
{
	/*  FASE 04, y sale de usar TEDIT: el cursor bajaba DOS lineas por cada
	 *  Enter.
	 *
	 *  En modo protegido Enter es una tecla AID: transmite y le cede el
	 *  turno al host, que decide donde queda el cursor. A la rama del AID
	 *  le faltaba el "return", asi que ademas caia al comando local y hacia
	 *  un Tab al campo siguiente -- una bajada nuestra mas la del host al
	 *  contestar. Todos los otros caminos AID (las teclas de funcion) ya
	 *  devolvian ahi mismo.                                               */
	g_prueba = "enter_en_protegido_solo_manda_el_aid";
	Sumidero s; Vt6530Terminal t(&s);
	PantallaDeCampos(t, 6);
	t.Keyboard()->UnlockKeyboard();

	t.Display()->SetCursorRowCol(1, 3);
	const int fila = t.Display()->GetCursorRow();
	const int col  = t.Display()->GetCursorCol();

	s.Reset();
	t.TypeChar(13);

	Check(s.enviado.size() == 7, "manda la secuencia AID de siete bytes");
	if (s.enviado.size() == 7)
	{
		Check((unsigned char)s.enviado[0] == 1,   "arranca con SOH");
		Check(s.enviado[1] == 'V',                "el codigo de Enter es 'V'");
		Check((unsigned char)s.enviado[5] == 3,   "termina con ETX");
	}
	Check(t.Display()->GetCursorRow() == fila &&
	      t.Display()->GetCursorCol() == col,
	      "y NO mueve el cursor: eso lo decide el host");
	std::printf("           Enter en protegido: %d bytes al host, "
	            "cursor sigue en (%d,%d)\n", (int)s.enviado.size(),
	            t.Display()->GetCursorRow(), t.Display()->GetCursorCol());

	/*  Fuera de protegido no hay AID y el Enter sigue siendo local, como
	 *  siempre: es el camino del modo conversacional.                     */
	Sumidero s2; Vt6530Terminal t2(&s2);
	ModoConversacional(t2);
	t2.SetLocalEcho(true);
	const int fila2 = t2.Display()->GetCursorRow();
	t2.TypeChar(13);
	Check(t2.Display()->GetCursorRow() == fila2 + 1,
	      "en conversacional el Enter sigue bajando una linea");
}

/* ------------------------------------------------------------------ */

typedef void (*Fn)();

int main()
{
	Log::SetQuiet(true);

	std::printf("\nPegamento del terminal -- pruebas de la fase 01\n");
	std::printf("==============================================\n\n");

	Fn pruebas[] = {
		Test_TextoDelHostLlegaYAvisa,
		Test_EotEsElTurnoDePalabra,
		Test_ConversacionalAcumulaHastaElEnter,
		Test_EnterEnConversacionalMandaLaLinea,
		Test_LasDieciseisTeclasDeFuncion,
		Test_ReparteTcpSinRomper,
		Test_ElPrimerCambioDeModoConservaLaPantalla,
		Test_SalirDeBloqueLimpiaLaPantalla,
		Test_SalirDeBloqueLimpiaLaLineaDeEstado,
		Test_LasFlechasVerticalesConservanLaColumna,
		Test_LaRespuestaAEscInterrogacionTerminaBien,
		Test_InsertarYBorrarCaracter,
		Test_InsertarCaracterNoPisaElCampoDeAlLado,
		Test_InsertarYBorrarLineaEnConversacional,
		Test_EnProtegidoElTecladoNoCorreLaPantalla,
		Test_EnterEnProtegidoSoloMandaElAid,
		Test_ElTurnoDePalabraNoBorraLaPantalla,
		Test_EscPQyBNoSeComenLosBytesSiguientes,
		Test_EnterPorElCaminoEquivocadoNoHaceNada,
		Test_LasFlechasMuevenElCursor,
		Test_LaFlechaYaNoRepiteLaTeclaAnterior,
		Test_LaNavegacionNoEnsuciaLaLineaConversacional,
		Test_Esc6PintaInvisible,
		Test_ConEcoDelHostNoSeDuplicaNiSeVeLaClave,
		Test_ConEcoLocalSeDibujaAlTeclear,
		Test_ConEcoDelHostSeMandaCaracterPorCaracter,
		Test_ConEcoLocalSeSigueAcumulandoLaLinea,
		Test_EnBloqueElEcoEsSiempreLocal,
		Test_DEFECTO_CrYLfEstanCruzados,
	};

	for (size_t i = 0; i < sizeof(pruebas) / sizeof(pruebas[0]); i++)
		pruebas[i]();

	std::printf("\n----------------------------------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_ok);
	std::printf("  comprobaciones fallidas . %d\n", g_mal);
	std::printf("----------------------------------------------\n\n");
	return (g_mal == 0) ? 0 : 1;
}
