/*
 *  Pruebas de la capa telnet -- fase 01.
 *
 *  La prueba central reproduce byte a byte la negociacion de un TELSERV real
 *  (host rci3, puerto 23) y verifica que nuestro cliente responda igual que
 *  el emulador comercial que se uso de referencia. Es lo que reemplaza al
 *  codigo de SPL que no vino en el paquete: en vez de adivinar el protocolo,
 *  se lo compara contra una sesion que funciona.
 */

#include "../net/Tn6530Telnet.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace tn6530;

/* ------------------------------------------------------------------ */

static int g_ok = 0, g_mal = 0;
static std::string g_prueba;

static void Check(bool cond, const std::string &que)
{
	if (cond) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLA  %s :: %s\n", g_prueba.c_str(), que.c_str());
}

static std::string Hex(const std::string &s)
{
	std::string out;
	char tmp[8];
	for (size_t i = 0; i < s.size(); i++)
	{
		if (i) out += " ";
		std::snprintf(tmp, sizeof(tmp), "%02X", (unsigned char)s[i]);
		out += tmp;
	}
	return out;
}

static void CheckEqHex(const std::string &obtenido, const std::string &esperado,
                       const std::string &que)
{
	if (obtenido == esperado) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLA  %s :: %s\n", g_prueba.c_str(), que.c_str());
	std::printf("           esperado: %s\n", Hex(esperado).c_str());
	std::printf("           obtenido: %s\n", Hex(obtenido).c_str());
}

/** Banco: engancha los eventos y acumula lo que sale. */
struct Banco
{
	Tn6530Telnet telnet;
	std::string enviado;      /* lo que mandariamos al host */
	std::string payload;      /* lo que le llega a Guardian */
	int registros = 0;        /* marcadores IAC EOR recibidos */
	std::vector<std::string> trazas;
	std::vector<LineRead> lecturas;   /* peticiones de lectura del host */

	explicit Banco(const Policy &p = Policy()) : telnet(p)
	{
		Events ev;
		ev.onPayload = [this](const unsigned char *d, int n) {
			payload.append((const char *)d, (size_t)n); };
		ev.onSend = [this](const unsigned char *d, int n) {
			enviado.append((const char *)d, (size_t)n); };
		ev.onEndOfRecord = [this]() { registros++; };
		ev.onTrace = [this](const std::string &t) { trazas.push_back(t); };
		ev.onLineRead = [this](const LineRead &l) { lecturas.push_back(l); };
		telnet.SetEvents(ev);
	}

	void Feed(const std::string &bytes)
	{
		telnet.Feed((const unsigned char *)bytes.data(), (int)bytes.size());
	}

	/** Entrega los mismos bytes en trozos de n, para probar que una
	 *  secuencia partida entre dos lecturas se retoma bien. */
	void FeedEnTrozos(const std::string &bytes, size_t n)
	{
		for (size_t i = 0; i < bytes.size(); i += n)
			Feed(bytes.substr(i, n));
	}
};

static std::string B(std::initializer_list<int> bytes)
{
	std::string s;
	for (int b : bytes) s.push_back((char)(unsigned char)b);
	return s;
}

/* ------------------------------------------------------------------ */
/*  La prueba que importa: la negociacion real de rci3                 */
/* ------------------------------------------------------------------ */

static void Test_NegociacionRealDeRci3()
{
	/*  Capturado con tools/tap6530.py entre un emulador 6530 comercial y un
	 *  TELSERV en rci3:23. El host abre la negociacion; el emulador
	 *  responde. Se le da al nuestro la misma entrada y se compara la
	 *  salida contra la del emulador de referencia.                      */
	g_prueba = "negociacion_real_rci3";
	Banco b;
	b.telnet.Start();

	const std::string delHost =
		B({IAC, WILL, OPT_ECHO}) +
		B({IAC, DO,   OPT_TERMINAL_TYPE}) +
		B({IAC, WILL, OPT_SGA}) +
		B({IAC, DO,   OPT_NAWS}) +
		B({IAC, SB,   OPT_TERMINAL_TYPE, 1, IAC, SE}) +      /* SEND */
		B({IAC, DO,   OPT_LINEMODE}) +
		B({IAC, DONT, OPT_NAWS}) +
		B({IAC, SB,   OPT_LINEMODE, 0x01, 0x05, IAC, SE}) +
		B({IAC, SB,   OPT_LINEMODE, 0x04, 0x08, 0x18, 0x19, 0x0D,
		              0x00, 0x09, 0xE0, 0x00, IAC, SE});

	b.Feed(delHost);

	/*  El WILL TERMINAL-TYPE va PRIMERO, antes de que el host diga nada.
	 *
	 *  Hasta que una captura de OutsideView sobre ssh mostro lo contrario,
	 *  Start() no mandaba nada y ese WILL salia como respuesta al DO del
	 *  host, o sea tercero. Este TELSERV pregunta el tipo por su cuenta, asi
	 *  que por telnet daba igual; el servicio ssh del mismo host NO pregunta
	 *  hasta que el cliente ofrece, y ahi la diferencia deja de ser de orden
	 *  y pasa a ser de funciona o no funciona.
	 *
	 *  Y como el estado ya quedo anotado al ofrecerlo, el DO TERMINAL-TYPE
	 *  que llega despues no dispara un segundo WILL: eso es lo que fija la
	 *  ausencia de un WILL repetido en esta lista.                         */
	const std::string esperado =
		B({IAC, WILL, OPT_TERMINAL_TYPE}) + /* ofrecido de entrada         */
		B({IAC, DO,   OPT_ECHO}) +          /* aceptamos el eco del host   */
		B({IAC, DO,   OPT_SGA}) +
		B({IAC, WONT, OPT_NAWS}) +          /* como el emulador comercial  */
		B({IAC, SB,   OPT_TERMINAL_TYPE, 0}) + "tn6530-8" + B({IAC, SE}) +
		B({IAC, WONT, OPT_LINEMODE});       /* declinamos: evita todo SLC  */

	CheckEqHex(b.enviado, esperado,
	           "la respuesta coincide con la del emulador de referencia");

	Check(b.payload.empty(), "la negociacion no deja basura en el payload");
	Check(b.telnet.HeWill(OPT_ECHO), "el host hace eco");
	Check(b.telnet.WeWill(OPT_TERMINAL_TYPE), "declaramos el tipo de terminal");
	Check(!b.telnet.WeWill(OPT_NAWS), "NAWS queda rechazado");
	Check(!b.telnet.WeWill(OPT_LINEMODE), "LINEMODE queda rechazado");
	Check(!b.telnet.EndOfRecordActive(), "este host no negocia END-OF-RECORD");
	Check(!b.telnet.BinaryActive(), "este host no negocia BINARY");
}

static void Test_NegociacionRealPartidaPorTcp()
{
	/*  Los mismos bytes de rci3 entregados de a uno. Si la maquina de
	 *  estados retoma bien una secuencia partida, la respuesta debe ser
	 *  identica byte a byte. */
	g_prueba = "negociacion_real_partida";

	const std::string delHost =
		B({IAC, WILL, OPT_ECHO}) +
		B({IAC, DO,   OPT_TERMINAL_TYPE}) +
		B({IAC, WILL, OPT_SGA}) +
		B({IAC, DO,   OPT_NAWS}) +
		B({IAC, SB,   OPT_TERMINAL_TYPE, 1, IAC, SE}) +
		B({IAC, DO,   OPT_LINEMODE}) +
		B({IAC, DONT, OPT_NAWS});

	Banco entera;
	entera.telnet.Start();
	entera.Feed(delHost);

	for (size_t trozo : {(size_t)1, (size_t)2, (size_t)3, (size_t)5})
	{
		Banco partida;
		partida.telnet.Start();
		partida.FeedEnTrozos(delHost, trozo);
		CheckEqHex(partida.enviado, entera.enviado,
		           "misma respuesta con trozos de " + std::to_string(trozo));
	}
}

static void Test_ElHostSshNoPreguntaHastaQueOfrecemos()
{
	/*  La negociacion del servicio SSH de rci3, capturada con
	 *  tools/tap6530ssh.py entre OutsideView y el host.
	 *
	 *  Lo que la hace distinta de la de telnet: el host abre con ECHO, SGA y
	 *  NAWS y NO menciona TERMINAL-TYPE. Recien despues de que el cliente
	 *  ofrece WILL TERMINAL-TYPE aparecen el DO y el SEND. Si el cliente no
	 *  ofrece nada, el host nunca se entera de que somos un 6530 -- y eso es
	 *  exactamente lo que nos pasaba.
	 *
	 *  Esta prueba existe para que Start() no vuelva a quedar vacio.        */
	g_prueba = "el_host_ssh_no_pregunta_hasta_que_ofrecemos";
	Banco b;
	b.telnet.Start();

	CheckEqHex(b.enviado, B({IAC, WILL, OPT_TERMINAL_TYPE}),
	           "lo primero que sale es el ofrecimiento del tipo de terminal");

	/*  La apertura del host por ssh: ni una palabra de TERMINAL-TYPE. */
	b.Feed(B({IAC, WILL, OPT_ECHO}) +
	       B({IAC, WILL, OPT_SGA}) +
	       B({IAC, DO,   OPT_NAWS}));

	/*  Y ahora si pregunta, porque le ofrecimos. */
	b.enviado.clear();
	b.Feed(B({IAC, DO, OPT_TERMINAL_TYPE}) +
	       B({IAC, SB, OPT_TERMINAL_TYPE, 1, IAC, SE}));

	CheckEqHex(b.enviado,
	           B({IAC, SB, OPT_TERMINAL_TYPE, 0}) + "tn6530-8" + B({IAC, SE}),
	           "el DO no repite el WILL; solo se contesta el tipo");

	Check(b.telnet.WeWill(OPT_TERMINAL_TYPE), "el tipo queda declarado");
	Check(b.payload.empty(), "nada de esto ensucia el payload");
}

static void Test_LinemodeSoloSiLoPideLaPolitica()
{
	/*  OutsideView tambien ofrece LINEMODE de entrada, y de ahi salen las
	 *  peticiones de lectura de HP. Queda detras de -linemode porque en esa
	 *  captura el host y el emulador se lo ofrecen y se lo retiran cuatro
	 *  veces, y esa danza todavia no la entendemos.                        */
	g_prueba = "linemode_solo_si_lo_pide_la_politica";

	Banco sin_el;
	sin_el.telnet.Start();
	Check(sin_el.enviado.find(B({IAC, WILL, OPT_LINEMODE})) == std::string::npos,
	      "por defecto no se ofrece LINEMODE");

	Policy p;
	p.acceptLinemode = true;
	Banco con_el(p);
	con_el.telnet.Start();
	Check(con_el.enviado.find(B({IAC, WILL, OPT_LINEMODE})) != std::string::npos,
	      "con la politica puesta, si se ofrece");

	/*  Y al DO del host hay que contestarle el MODE, aunque el estado ya
	 *  estuviera fijado por haberlo ofrecido. Sin esto la negociacion se
	 *  cortaba justo ahi: el host mandaba un byte suelto y se callaba.   */
	con_el.enviado.clear();
	con_el.Feed(B({IAC, DO, OPT_LINEMODE}));
	CheckEqHex(con_el.enviado,
	           B({IAC, SB, OPT_LINEMODE, 0x01, 0x01, IAC, SE}),
	           "al DO LINEMODE le sigue el MODE 01 (EDIT), como OutsideView");

	/*  Y una sola vez: repetir el DO no repite el MODE. */
	con_el.enviado.clear();
	con_el.Feed(B({IAC, DO, OPT_LINEMODE}));
	Check(con_el.enviado.empty(), "un DO LINEMODE repetido no repite el MODE");
}

static void Test_ConLinemodeEditElEcoEsNuestro()
{
	/*  El defecto que dejo la sesion muda por ssh.
	 *
	 *  El host manda WILL ECHO y ademas negocia LINEMODE con EDIT. Leyendo
	 *  solo el WILL ECHO, el terminal apaga su eco local y manda caracter por
	 *  caracter -- y el host no contesta nada, porque con EDIT esta esperando
	 *  una linea entera que el terminal tiene que armar y dibujar.
	 *
	 *  La RFC 1184 manda sobre el WILL ECHO: con EDIT, el eco es del
	 *  terminal.                                                           */
	g_prueba = "con_linemode_edit_el_eco_es_nuestro";

	Policy p;
	p.acceptLinemode = true;
	Banco b(p);
	b.telnet.Start();

	b.Feed(B({IAC, WILL, OPT_ECHO}));
	Check(b.telnet.HeWill(OPT_ECHO), "el host dice que hace eco");
	Check(b.telnet.HostEchoes(), "y sin LINEMODE, le creemos");

	b.Feed(B({IAC, DO, OPT_LINEMODE}));
	Check(!b.telnet.LinemodeEdit(),
	      "todavia no: falta que el host confirme el MODE");

	/*  MODE 05 = EDIT | MODE_ACK, tal cual lo manda rci3. */
	b.Feed(B({IAC, SB, OPT_LINEMODE, 0x01, 0x05, IAC, SE}));
	Check(b.telnet.LinemodeEdit(), "con el MODE confirmado, EDIT esta activo");
	Check(!b.telnet.HostEchoes(),
	      "y entonces el eco pasa a ser del terminal, pese al WILL ECHO");

	/*  Sin el bit EDIT vuelve a mandar el WILL ECHO. */
	b.Feed(B({IAC, SB, OPT_LINEMODE, 0x01, 0x04, IAC, SE}));
	Check(!b.telnet.LinemodeEdit(), "sin EDIT, no hay edicion local");
	Check(b.telnet.HostEchoes(), "y el eco vuelve a ser del host");
}

static void Test_NoHayBucleDeNegociacion()
{
	/*  Un cliente que responde a cada mensaje sin mirar el estado entra en
	 *  bucle: WILL / DO / WILL / DO. Repetir la misma oferta no debe
	 *  producir una segunda respuesta. */
	g_prueba = "sin_bucle_de_negociacion";
	Banco b;

	b.Feed(B({IAC, WILL, OPT_SGA}));
	const size_t tras_primera = b.enviado.size();
	Check(tras_primera == 3, "la primera oferta produce una respuesta");

	b.Feed(B({IAC, WILL, OPT_SGA}));
	Check(b.enviado.size() == tras_primera,
	      "repetir la oferta no produce una segunda respuesta");

	b.Feed(B({IAC, DONT, OPT_NAWS}));
	const size_t tras_dont = b.enviado.size();
	b.Feed(B({IAC, DONT, OPT_NAWS}));
	Check(b.enviado.size() == tras_dont,
	      "un DONT repetido sobre algo ya rechazado no responde");
}

/* ------------------------------------------------------------------ */
/*  Payload                                                            */
/* ------------------------------------------------------------------ */

static void Test_PayloadLimpio()
{
	g_prueba = "payload_limpio";
	Banco b;
	b.Feed("PANTALLA" + B({IAC, WILL, OPT_SGA}) + "SIGUE");
	CheckEqHex(b.payload, std::string("PANTALLASIGUE"),
	           "la negociacion se saca del payload sin perder datos");
}

static void Test_IacDobleEsUnByteDeDatos()
{
	g_prueba = "iac_doble";
	Banco b;
	b.Feed("A" + B({IAC, IAC}) + "B");
	CheckEqHex(b.payload, std::string("A") + B({0xFF}) + "B",
	           "IAC IAC entrega un solo 0xFF al payload");
}

static void Test_FinDeRegistro()
{
	/*  IAC EOR marca el fin de un registro. El payload acumulado tiene que
	 *  entregarse ANTES de avisar, o la capa de arriba procesa el aviso
	 *  sobre una pantalla incompleta. */
	g_prueba = "fin_de_registro";
	Policy p;
	Banco b(p);

	b.Feed(B({IAC, WILL, OPT_END_OF_RECORD}));
	Check(b.telnet.EndOfRecordActive(), "END-OF-RECORD queda activo");

	std::string entregadoAlAvisar;
	Events ev;
	ev.onPayload = [&b](const unsigned char *d, int n) {
		b.payload.append((const char *)d, (size_t)n); };
	ev.onSend = [&b](const unsigned char *d, int n) {
		b.enviado.append((const char *)d, (size_t)n); };
	ev.onEndOfRecord = [&]() { b.registros++; entregadoAlAvisar = b.payload; };
	b.telnet.SetEvents(ev);

	b.Feed("PANTALLA COMPLETA" + B({IAC, EOR}));
	Check(b.registros == 1, "se detecta un marcador de fin de registro");
	CheckEqHex(entregadoAlAvisar, std::string("PANTALLA COMPLETA"),
	           "el payload ya estaba entregado cuando llego el aviso");
}

static void Test_SubnegociacionPartida()
{
	g_prueba = "subnegociacion_partida";
	const std::string entrada =
		B({IAC, SB, OPT_TERMINAL_TYPE, 1, IAC, SE});

	Banco entera;
	entera.Feed(entrada);

	Banco partida;
	partida.FeedEnTrozos(entrada, 1);

	CheckEqHex(partida.enviado, entera.enviado,
	           "el SB partido byte a byte produce la misma respuesta");
	Check(entera.enviado.find("tn6530-8") != std::string::npos,
	      "la respuesta lleva el tipo de terminal");
}

/* ------------------------------------------------------------------ */
/*  Salida hacia el host                                               */
/* ------------------------------------------------------------------ */

static void Test_EscapadoDeSalida()
{
	g_prueba = "escapado_de_salida";
	Banco b;
	const std::string datos = "AB" + B({0xFF}) + "C";
	b.telnet.SendPayload((const unsigned char *)datos.data(), (int)datos.size());
	CheckEqHex(b.enviado, "AB" + B({IAC, IAC}) + "C",
	           "el 0xFF de los datos se dobla al salir");
}

static void Test_CierreDeRegistroAutomatico()
{
	g_prueba = "cierre_registro_automatico";

	Banco sin_eor;
	sin_eor.telnet.SendPayload((const unsigned char *)"HOLA", 4);
	CheckEqHex(sin_eor.enviado, std::string("HOLA"),
	           "sin END-OF-RECORD negociado no se agrega marcador");

	Banco con_eor;
	con_eor.Feed(B({IAC, DO, OPT_END_OF_RECORD}));
	con_eor.enviado.clear();
	con_eor.telnet.SendPayload((const unsigned char *)"HOLA", 4);
	CheckEqHex(con_eor.enviado, "HOLA" + B({IAC, EOR}),
	           "con END-OF-RECORD activo se cierra el registro");
}

static void Test_PoliticaConfigurable()
{
	g_prueba = "politica_configurable";

	Policy p;
	p.terminalType = "tn6530-9";
	p.acceptNaws = true;
	Banco b(p);

	/*  Con NAWS aceptado, al WILL le sigue el tamano en la misma andanada.
	 *  La RFC 1073 lo pide asi, y no es un detalle de forma: un host que
	 *  espera ese SB y no lo recibe se queda esperando, que es peor que
	 *  haberle dicho WONT. Esta prueba antes esperaba solo el WILL.    */
	b.Feed(B({IAC, DO, OPT_NAWS}));
	CheckEqHex(b.enviado,
	           B({IAC, WILL, OPT_NAWS,
	              IAC, SB, OPT_NAWS, 0, 80, 0, 24, IAC, SE}),
	           "con acceptNaws se acepta NAWS y se manda el tamano");

	/*  Y un cambio de tamano se reenvia solo. */
	b.enviado.clear();
	b.telnet.SetWindowSize(132, 50);
	CheckEqHex(b.enviado,
	           B({IAC, SB, OPT_NAWS, 0, 132, 0, 50, IAC, SE}),
	           "y un cambio de tamano se reenvia");

	/*  Sin NAWS negociado, cambiar el tamano no manda nada. */
	Banco sinNaws;
	sinNaws.telnet.SetWindowSize(132, 50);
	Check(sinNaws.enviado.empty(),
	      "sin NAWS negociado, cambiar el tamano no manda nada");

	b.enviado.clear();
	b.Feed(B({IAC, SB, OPT_TERMINAL_TYPE, 1, IAC, SE}));
	Check(b.enviado.find("tn6530-9") != std::string::npos,
	      "el tipo de terminal sale de la politica");
}

/* ------------------------------------------------------------------ */

typedef void (*Fn)();

static void Test_PeticionDeLecturaSinEcoEsLaClave()
{
	/*  La prueba que cierra el asunto de la clave, y sale de una sesion
	 *  real contra rci3.
	 *
	 *  TELSERV pide cada lectura con una subnegociacion de LINEMODE con
	 *  subcomando 4, que no es de la RFC 1184 -- es de HP. Llega aunque
	 *  hayamos contestado WONT LINEMODE, asi que no hizo falta cambiar la
	 *  negociacion para verla.
	 *
	 *  Los ocho cuerpos de abajo son los ocho de esa sesion, tal cual. En
	 *  siete el byte de banderas vale E0; en uno vale A0. La diferencia es
	 *  el bit 0x40, y ese unico A0 cayo exactamente despues de
	 *  "TACL 1> Password: ".
	 *
	 *  Los tres primeros bytes (08 18 19) y el ultimo (00) no variaron
	 *  nunca y no se sabe que son. El 0D es el terminador de linea.      */
	g_prueba = "peticion_de_lectura_sin_eco";
	Banco b;

	struct Muestra { int cuenta; int banderas; bool eco; const char *donde; };
	const Muestra muestras[] = {
		{   9, 0xE0, true,  "menu de TELSERV"            },
		{   9, 0xE0, true,  "menu de TELSERV"            },
		{ 300, 0xE0, true,  "despues de 'TACL 1> '"      },
		{ 300, 0xA0, false, "despues de 'Password: '"    },
		{ 239, 0xE0, true,  "TACL"                       },
		{   8, 0xE0, true,  "TACL"                       },
		{ 239, 0xE0, true,  "TACL"                       },
		{ 239, 0xE0, true,  "TACL"                       },
	};
	const int n = (int)(sizeof(muestras) / sizeof(muestras[0]));

	for (int i = 0; i < n; i++)
	{
		b.Feed(B({ 0xFF, 0xFA, 0x22, 0x04,
		           0x08, 0x18, 0x19, 0x0D,
		           (muestras[i].cuenta >> 8) & 0xFF,
		           muestras[i].cuenta & 0xFF,
		           muestras[i].banderas, 0x00,
		           0xFF, 0xF0 }));
	}

	Check((int)b.lecturas.size() == n,
	      "se reconocen las ocho peticiones de lectura");
	if ((int)b.lecturas.size() != n) return;

	int sinEco = 0;
	for (int i = 0; i < n; i++)
	{
		Check(b.lecturas[i].maxBytes == muestras[i].cuenta,
		      std::string("la cuenta de bytes sale bien en ") +
		      muestras[i].donde);
		Check(b.lecturas[i].echo == muestras[i].eco,
		      std::string("el eco sale bien en ") + muestras[i].donde);
		Check(b.lecturas[i].terminator == 0x0D,
		      "el terminador es CR");
		if (!b.lecturas[i].echo) sinEco++;
	}

	Check(sinEco == 1, "una sola lectura de las ocho va sin eco");
	std::printf("           %d peticiones, %d sin eco (la de la clave)\n",
	            (int)b.lecturas.size(), sinEco);

	/*  Y el payload queda limpio: nada de esto llega a Guardian. */
	Check(b.payload.empty(),
	      "la peticion de lectura no ensucia el payload");
}

static void Test_PeticionDeLecturaPartidaPorTcp()
{
	/*  Lo mismo entregado de a un byte. La subnegociacion tiene que
	 *  retomarse igual: es una secuencia de catorce bytes y el TCP la
	 *  puede cortar en cualquier lado.                                   */
	g_prueba = "peticion_de_lectura_partida";
	Banco b;
	const std::string bloque = B({ 0xFF, 0xFA, 0x22, 0x04,
	                               0x08, 0x18, 0x19, 0x0D,
	                               0x01, 0x2C, 0xA0, 0x00,
	                               0xFF, 0xF0 });
	b.FeedEnTrozos(bloque, 1);

	Check(b.lecturas.size() == 1, "se reconoce igual byte a byte");
	if (b.lecturas.empty()) return;
	Check(b.lecturas[0].maxBytes == 300, "la cuenta sobrevive al corte");
	Check(!b.lecturas[0].echo,     "y la bandera de eco tambien");
}

static void Test_OtrasSubnegociacionesDeLinemodeNoSeConfunden()
{
	/*  MODE (subcomando 1) y SLC (3) son de la RFC 1184 y no son
	 *  peticiones de lectura. No tienen que disparar el evento.          */
	g_prueba = "otras_subnegociaciones_de_linemode";
	Banco b;

	b.Feed(B({ 0xFF, 0xFA, 0x22, 0x01, 0x05, 0xFF, 0xF0 }));       /* MODE */
	b.Feed(B({ 0xFF, 0xFA, 0x22, 0x03, 0x01, 0x02, 0x03, 0xFF, 0xF0 })); /* SLC */

	Check(b.lecturas.empty(),
	      "MODE y SLC no se toman por peticiones de lectura");

	/* Y un subcomando 4 demasiado corto tampoco. */
	b.Feed(B({ 0xFF, 0xFA, 0x22, 0x04, 0x08, 0xFF, 0xF0 }));
	Check(b.lecturas.empty(), "un subcomando 4 truncado se ignora");
}

int main()
{
	std::printf("\nCapa telnet TN6530-8 -- pruebas de la fase 01\n");
	std::printf("============================================\n\n");

	struct { const char *grupo; Fn fn; } pruebas[] = {
		{ "negociacion", Test_NegociacionRealDeRci3 },
		{ "negociacion", Test_NegociacionRealPartidaPorTcp },
		{ "negociacion", Test_ElHostSshNoPreguntaHastaQueOfrecemos },
		{ "negociacion", Test_LinemodeSoloSiLoPideLaPolitica },
		{ "negociacion", Test_ConLinemodeEditElEcoEsNuestro },
		{ "negociacion", Test_NoHayBucleDeNegociacion },
		{ "negociacion", Test_PoliticaConfigurable },
		{ "entrada",     Test_PayloadLimpio },
		{ "entrada",     Test_IacDobleEsUnByteDeDatos },
		{ "entrada",     Test_FinDeRegistro },
		{ "entrada",     Test_SubnegociacionPartida },
		{ "lectura",     Test_PeticionDeLecturaSinEcoEsLaClave },
		{ "lectura",     Test_PeticionDeLecturaPartidaPorTcp },
		{ "lectura",     Test_OtrasSubnegociacionesDeLinemodeNoSeConfunden },
		{ "salida",      Test_EscapadoDeSalida },
		{ "salida",      Test_CierreDeRegistroAutomatico },
	};

	const char *grupo = "";
	for (size_t i = 0; i < sizeof(pruebas) / sizeof(pruebas[0]); i++)
	{
		if (std::strcmp(grupo, pruebas[i].grupo) != 0)
		{
			grupo = pruebas[i].grupo;
			std::printf("-- %s\n", grupo);
		}
		pruebas[i].fn();
	}

	/* El resumen del estado negociado, tal como quedaria contra rci3. */
	{
		Banco b;
		b.Feed(B({IAC, WILL, OPT_ECHO}) + B({IAC, DO, OPT_TERMINAL_TYPE}) +
		       B({IAC, WILL, OPT_SGA}) + B({IAC, DO, OPT_NAWS}) +
		       B({IAC, DO, OPT_LINEMODE}));
		std::printf("\ncontra rci3, %s", b.telnet.Summary().c_str());
	}

	std::printf("\n--------------------------------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_ok);
	std::printf("  comprobaciones fallidas . %d\n", g_mal);
	std::printf("--------------------------------------------\n\n");
	return (g_mal == 0) ? 0 : 1;
}
