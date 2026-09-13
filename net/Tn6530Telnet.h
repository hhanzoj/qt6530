/*
 *  Tn6530Telnet -- capa telnet para sesiones TN6530-8.
 *
 *  C++17 puro: ni Qt ni sockets. Entran bytes del cable y salen dos cosas,
 *  el payload de aplicacion que consume Guardian y los bytes de respuesta
 *  que hay que devolver al host. Quien tenga el socket decide como moverlos.
 *
 *  Esa separacion no es purismo. La negociacion telnet es la parte del port
 *  que hay que escribir sin referencia -- el codigo de SPL que la resolvia no
 *  vino en el paquete -- asi que conviene poder ejercitarla contra la
 *  negociacion real capturada del host, sin red y en una prueba unitaria.
 *  tests/telnet_tests.cpp hace exactamente eso.
 *
 *  La politica de negociacion sale de capturas reales de un TELSERV en el
 *  puerto 23 (ver capturas/). Lo que ese host pide:
 *
 *      host: WILL ECHO, DO TERMINAL-TYPE, WILL SGA, DO NAWS, DO LINEMODE
 *      term: DO SGA, WILL TERMINAL-TYPE, DO ECHO, WONT NAWS
 *            SB TERMINAL-TYPE IS tn6530-8
 *
 *  BINARY y END-OF-RECORD no aparecen en esa sesion, pero se aceptan igual:
 *  hacen falta en los hosts que enmarcan el modo bloque con registros.
 */
#ifndef _tn6530_telnet_h
#define _tn6530_telnet_h

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tn6530 {

/* ---- vocabulario de telnet ---------------------------------------- */

enum : unsigned char
{
	IAC   = 255,
	DONT  = 254,
	DO    = 253,
	WONT  = 252,
	WILL  = 251,
	SB    = 250,
	GA    = 249,
	SE    = 240,
	EOR   = 239,
	NOP   = 241,
};

enum : unsigned char
{
	OPT_BINARY        = 0,
	OPT_ECHO          = 1,
	OPT_SGA           = 3,
	OPT_END_OF_RECORD = 19,
	OPT_TERMINAL_TYPE = 24,
	OPT_NAWS          = 31,
	OPT_LINEMODE      = 34,
};

const char *OptionName(unsigned char option);
const char *CommandName(unsigned char command);

/* ---- politica ------------------------------------------------------ */

struct Policy
{
	/** Lo que respondemos al SB TERMINAL-TYPE SEND del host. */
	std::string terminalType = "tn6530-8";

	/** El emulador de referencia rechaza NAWS y el host acepta el rechazo;
	 *  la pantalla del 6530 es de 80x24 fija, asi que no aporta nada.
	 *
	 *  Se activa con -naws. Si se activa, al WILL NAWS le sigue el tamano en
	 *  una subnegociacion, que es lo que pide la RFC 1073: decir que si y
	 *  despues no mandarlo es peor que decir que no.                    */
	bool acceptNaws = false;

	/** El tamano que se declara si NAWS esta activo. La pantalla del 6530 es
	 *  de 80x24; esto existe para no clavarlo adentro del codigo. */
	int columns = 80;
	int rows    = 24;

	/** LINEMODE: ofrecerlo en Start() y aceptarlo si el host lo pide.
	 *
	 *  Lo ofrece el emulador de referencia, y de las subnegociaciones que le
	 *  siguen salen las peticiones de lectura de HP con el bit de eco -- las
	 *  que ocultan la clave. Sigue apagado por defecto porque en la captura
	 *  de ssh el host y el emulador se lo ofrecen y se lo retiran cuatro
	 *  veces, y esa danza todavia no se entiende.                          */
	bool acceptLinemode = false;

	/** Hacen falta para el modo bloque en los hosts que enmarcan por
	 *  registro. El TELSERV capturado no los negocia, pero aceptarlos no
	 *  cuesta nada si el host no los pide. */
	bool acceptBinary = true;
	bool acceptEndOfRecord = true;

	/** El host es quien hace eco en modo conversacional. */
	bool acceptRemoteEcho = true;

	bool acceptSga = true;

	/** Con END-OF-RECORD activo, cerrar cada envio con IAC EOR.
	 *
	 *  Guardian llama a SendRaw() una vez por respuesta completa, asi que
	 *  un envio equivale a un registro. PENDIENTE de confirmar contra una
	 *  captura de modo bloque real: ninguna de las capturas disponibles
	 *  negocia END-OF-RECORD. */
	bool autoEndOfRecord = true;
};

/* ---- peticion de lectura de HP -------------------------------------- */

/**
 *  Lo que TELSERV manda para pedirle una linea al terminal.
 *
 *  Viaja en una subnegociacion de LINEMODE con subcomando 4, que NO es de
 *  la RFC 1184 -- es una extension propia de HP. No tenemos el documento,
 *  asi que esto sale de observar una sesion completa contra rci3: ocho
 *  peticiones, todas con el mismo cuerpo de ocho bytes.
 *
 *      IAC SA 22 04  08 18 19 0D  <cuenta:2>  <banderas>  00  IAC SE
 *                    ^^^^^^^^^^^  ^^^^^^^^^^  ^^^^^^^^^^
 *                    igual en las  maximo de   bit 0x40
 *                    ocho          bytes       = con eco
 *
 *  El bit 0x40 es el que importa. En la sesion capturada valia 1 en siete
 *  de las ocho peticiones; la unica que lo tenia en 0 cayo exactamente
 *  despues de "TACL 1> Password: ". Esa es la forma en que este host pide
 *  una lectura sin eco, y por lo tanto la forma en que una clave no se
 *  dibuja en pantalla.
 *
 *  Los bytes 08 18 19 0D no variaron nunca; el 0D es con toda probabilidad
 *  el terminador de linea. Del resto no se sabe, y no se inventa.
 */
struct LineRead
{
	int  maxBytes   = 0;      /**< bytes 4 y 5, big endian. */
	bool echo       = true;   /**< bit 0x40 del byte de banderas. */
	unsigned char terminator = 0x0D;  /**< byte 3. */
	unsigned char flags      = 0;     /**< el byte entero, para diagnostico. */
};

/* ---- eventos que la capa superior necesita ver --------------------- */

struct Events
{
	/** Payload de aplicacion, ya sin telnet. Va a Guardian. */
	std::function<void(const unsigned char *, int)> onPayload;

	/** Bytes que hay que mandar al host. Los produce la negociacion. */
	std::function<void(const unsigned char *, int)> onSend;

	/** Marcador IAC EOR: fin de registro. */
	std::function<void()> onEndOfRecord;

	/** Traza legible, para el log y las pruebas. */
	std::function<void(const std::string &)> onTrace;

	/** El host pidio una lectura. Lo que hay que mirar es el eco: si viene
	 *  en false, el terminal no debe dibujar lo que se teclee -- es una
	 *  clave. Ver LineRead. */
	std::function<void(const LineRead &)> onLineRead;
};

/* ---- la maquina ---------------------------------------------------- */

class Tn6530Telnet
{
public:
	explicit Tn6530Telnet(const Policy &policy = Policy());

	void SetEvents(const Events &events) { m_ev = events; }

	/** Alimenta bytes recien llegados del socket. Puede cortarse en
	 *  cualquier lado: una secuencia IAC partida entre dos lecturas se
	 *  retoma en la siguiente. */
	void Feed(const unsigned char *data, int len);

	/** Envia datos de aplicacion al host, escapando los IAC. Si
	 *  END-OF-RECORD esta activo y la politica lo pide, cierra con IAC EOR. */
	void SendPayload(const unsigned char *data, int len);

	/** Cierra un registro explicitamente. Sin efecto si no se negocio EOR. */
	void SendEndOfRecord();

	/** Cambia el tamano declarado y lo reenvia. Sin efecto si NAWS no se
	 *  negocio, asi que se puede llamar siempre. */
	void SetWindowSize(int columnas, int filas);

	/** Arranque: lo que ofrecemos apenas se abre la conexion.
	 *
	 *  Ofrece WILL TERMINAL-TYPE, y WILL LINEMODE si la politica lo pide.
	 *
	 *  Aca decia "deliberadamente vacio: el host abre y nosotros
	 *  respondemos". Valia para telnet, donde ese TELSERV pregunta el tipo
	 *  por su cuenta. El servicio SSH del MISMO host no pregunta hasta que el
	 *  cliente ofrece, asi que con Start() vacio nunca se enteraba de que
	 *  somos un 6530. Capturado con tools/tap6530ssh.py contra OutsideView. */
	void Start();

	/* --- estado negociado, para diagnostico y para el resumen --- */
	bool WeWill(unsigned char option) const;
	bool HeWill(unsigned char option) const;
	bool BinaryActive() const     { return WeWill(OPT_BINARY) || HeWill(OPT_BINARY); }
	bool EndOfRecordActive() const{ return WeWill(OPT_END_OF_RECORD) ||
	                                       HeWill(OPT_END_OF_RECORD); }

	/** El host se encarga del eco.
	 *
	 *  Confirmado con una traza en vivo contra rci3: el host devuelve cada
	 *  linea recibida -- "logon nkaizen.jaracena" volvio tal cual -- y la
	 *  unica que no devolvio fue la clave, donde mando solo CR LF. O sea
	 *  que el eco del host es el mecanismo, y ocultar la clave es no
	 *  devolverla.
	 *
	 *  Nosotros contestamos DO ECHO, asi que le dijimos al host que se
	 *  encargue el. Hacer ademas eco local es duplicar: por eso "sysinfo"
	 *  salia dos veces en pantalla.                                       */
	bool HostEchoes() const
	{
		/*  Con LINEMODE EDIT no, aunque el host haya dicho WILL ECHO.
		 *
		 *  La RFC 1184 es clara: con EDIT puesto, el terminal junta la linea
		 *  y la dibuja, y manda la linea entera al terminador. El WILL ECHO
		 *  del host queda sin efecto para lo que se teclea.
		 *
		 *  Se vio en vivo sobre ssh: el host negocio LINEMODE EDIT, nosotros
		 *  leimos su WILL ECHO, apagamos el eco local y mandamos caracter por
		 *  caracter. Resultado: no se veia lo tecleado y el host no contestaba
		 *  nada, porque estaba esperando una linea entera.
		 *
		 *  Y reinterpreta el bit 0x40 de las peticiones de lectura: no dice
		 *  "el host va a hacer eco" sino "eco de ESTA lectura", dirigido al
		 *  terminal. Por eso la de la clave viene con el bit en cero.      */
		if (LinemodeEdit()) return false;
		return HeWill(OPT_ECHO);
	}

	/** LINEMODE activo con el bit EDIT: la linea la arma el terminal. */
	bool LinemodeEdit() const
	{
		return WeWill(OPT_LINEMODE) && m_linemodeModeConocido &&
		       (m_linemodeMode & 0x01) != 0;
	}
	std::string Summary() const;

private:
	enum State
	{
		ST_DATA = 0,
		ST_IAC,
		ST_OPTION,     /* despues de WILL/WONT/DO/DONT */
		ST_SB,
		ST_SB_IAC
	};

	/* Estado por opcion, para no entrar en bucles de negociacion: solo se
	 * responde cuando el estado cambia, nunca a una respuesta. */
	struct OptionState { bool us = false; bool him = false;
	                     bool usKnown = false; bool himKnown = false; };

	Policy      m_policy;
	Events      m_ev;
	State       m_state = ST_DATA;
	unsigned char m_negCommand = 0;
	std::vector<unsigned char> m_sb;
	std::vector<unsigned char> m_payload;
	OptionState m_opt[256];
	bool          m_linemodeModeEnviado = false;
	bool          m_linemodeModeConocido = false;
	unsigned char m_linemodeMode = 0;

	bool WantToEnableLocal(unsigned char option) const;   /* nosotros WILL */
	bool WantToEnableRemote(unsigned char option) const;  /* el host WILL  */

	void OnWill(unsigned char option);
	void OnWont(unsigned char option);
	void OnDo(unsigned char option);
	void OnDont(unsigned char option);
	void OnSubnegotiation();

	void SendNaws();
	void SendLinemodeMode();
	void SendCommand(unsigned char command, unsigned char option);
	void SendRawBytes(const unsigned char *data, int len);
	void FlushPayload();
	void Trace(const std::string &texto);
};

} // namespace tn6530

#endif
