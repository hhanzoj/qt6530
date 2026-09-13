/*
 *  Ssh6530Transport -- el canal 6530 sobre SSH, sin Qt.
 *
 *  Hermano de PosixTransport: mismas devoluciones de llamada, misma forma de
 *  usarse, y tambien implementa IHostLink para que Guardian le escriba sin
 *  saber que hay debajo.
 *
 *  POR QUE EXISTE
 *
 *  Un cliente SSH comun -- el ssh de Linux, PuTTY -- es un emulador ANSI.
 *  Aunque el host le mande 6530, no lo sabe dibujar, y por eso el modo bloque
 *  no funciona por ahi. Lo que falta no es el transporte sino el terminal, y
 *  el terminal ya lo tenemos.
 *
 *  COMO SE PIDE UN PTY 6530
 *
 *  Tres cosas, y las tres hacen falta:
 *
 *    1. pty-req con TERM = "TN6530-8" (en mayusculas, como lo manda
 *       OutsideView; el terminal-type de telnet va en minusculas y es otro
 *       campo)
 *    2. shell -- no exec
 *    3. y adentro del canal, ofrecer IAC WILL TERMINAL-TYPE
 *
 *  La tercera es la que falta siempre y la que cuesta encontrar, porque sin
 *  ella el host contesta algo -- banner de STN, a veces hasta un TACL
 *  generico -- y parece que el problema esta en las otras dos. Por el puerto
 *  23 TELSERV abre el la negociacion; por SSH espera a que el cliente ofrezca
 *  primero. De eso se ocupa Tn6530Telnet, que va tambien adentro del canal
 *  SSH: aca abajo SI hay telnet.
 *
 *  Este comentario decia antes que el TERM tenia que ser "xterm" y que shell
 *  dejaba en OSS. Las dos cosas eran falsas, y por la misma causa: sin el
 *  WILL TERMINAL-TYPE, STN no sabe con quien habla y cae a OSS. Queda dicho
 *  porque el sintoma vuelve a aparecer igual si alguien saca Start().
 *
 *  Y como SSH autentica, TACL arranca ya logueado: no hay prompt de clave de
 *  Guardian. Todo el asunto del eco y la clave que hace falta en telnet no
 *  aplica en este camino.
 *
 *  AVISO
 *
 *  Ni libssh2 ni Qt se pueden instalar en el entorno donde se escribio este
 *  port. El cuerpo de este archivo se compila contra una libssh2 falsa
 *  (tests/falso-win32/libssh2.h, objetivo "comprobar-ssh" de
 *  Makefile.portable) por los dos caminos, POSIX y Windows: eso verifica
 *  nombres, aridad y tipos, no semantica, y no reemplaza compilar contra la
 *  libssh2 de verdad. Lo que se pudo sacar de aca y probar con datos esta en
 *  apps/Opciones.cpp.
 */
#ifndef _ssh6530_transport_h
#define _ssh6530_transport_h

#include <spl/term/Telnet.h>

#include <functional>
#include <string>

namespace ssh6530 {

/** Que hacer cuando la clave del host no esta en known_hosts. */
enum class HostDesconocido
{
	Rechazar,      /**< por defecto: cortar y decir como aceptarla */
	Preguntar,     /**< llamar a onHostDesconocido y hacerle caso */
	Aceptar        /**< aceptar sin preguntar; solo para pruebas */
};

struct Politica
{
	std::string usuario;
	std::string terminalType = "TN6530-8";   /**< TERM del pty-req, en mayusculas */
	std::string comando      = "tacl";       /**< solo si usarShell es false */
	std::string claveArchivo;                /**< -i: clave privada */
	int  filas    = 24;
	int  columnas = 80;
	int  timeoutConexionMs = 15000;
	bool usarAgente = true;

	/** Cuantas veces se pregunta la clave antes de rendirse.
	 *
	 *  Equivocarse al tipear es lo normal, no un caso de borde. Este es
	 *  NUESTRO limite; el host tiene el suyo (MaxAuthTries) y cuando se le
	 *  acaba cierra el socket, lo que se detecta aparte. */
	int intentosDeClave = 3;

	bool pedirPty  = true;    /**< false: sin pty-req, como "ssh host cmd".
	                           *   Solo para diagnostico: sin PTY no hay 6530 */

	/** shell, que es lo que pide OutsideView y lo que entrega Guardian.
	 *
	 *  Con false se pide exec <comando>. Eso tambien anda, pero no es lo que
	 *  hace el cliente que funciona, y durante un dia entero se creyo que era
	 *  al reves -- que shell caia en OSS --. Caia, si, pero por no ofrecer
	 *  WILL TERMINAL-TYPE, no por pedir shell. */
	bool usarShell = true;

	/** Pedir el PTY en crudo: sin eco, sin ISIG, sin traducciones.
	 *
	 *  Esto NO es diagnostico, es lo que hace falta. Un PTY cocido hace eco
	 *  de las secuencias AID y trata el ETX (0x03) que llevan adentro como
	 *  Ctrl-C. Ver ModosCrudos() en el .cpp, que lo explica con la traza que
	 *  lo destapo. Se puede apagar por si algun host se porta raro. */
	bool ptyCrudo = true;

	HostDesconocido hostDesconocido = HostDesconocido::Rechazar;
	std::string knownHosts;                  /**< vacio = ~/.ssh/known_hosts */

	/** Anotar en known_hosts la clave de un host nuevo que se acepto.
	 *
	 *  Sin esto, aceptar no sirve de nada: la proxima conexion vuelve a
	 *  preguntar lo mismo, y una pregunta que se repite siempre es una
	 *  pregunta que la gente aprende a contestar sin leer.
	 *
	 *  Solo aplica a hosts NUEVOS. Una clave que cambio nunca se reescribe:
	 *  eso se arregla a mano, a proposito. */
	bool anotarHostNuevo = true;
};

/**
 *  Conexion SSH con un canal 6530 encima.
 *
 *  Connect() bloquea: hace el saludo, la autenticacion y pide el PTY. Son
 *  uno o dos segundos contra un host cercano. Despues de eso, Poll() no
 *  bloquea nunca y es lo que se engancha al bucle de eventos.
 */
class Ssh6530Transport : public IHostLink
{
public:
	explicit Ssh6530Transport(const Politica &politica = Politica());
	virtual ~Ssh6530Transport();

	/** Payload de la aplicacion recibido del host, listo para Guardian. */
	std::function<void(const char *, int)> onHostData;

	/** Traza legible: saludo, autenticacion, y lo que el host mande por
	 *  el canal de errores (STN manda ahi algunos diagnosticos). */
	std::function<void(const std::string &)> onTrace;

	/** Copia de los bytes crudos, para grabar la sesion igual que el proxy
	 *  de telnet y poder reproducirla con vt6530_replay. */
	std::function<void(const char *, int)> onRawFromHost;
	std::function<void(const char *, int)> onRawToHost;

	/**
	 *  Se llama cuando hace falta una clave. Devolver false cancela.
	 *
	 *  intento empieza en 1 y sube con cada rechazo, para que quien pregunte
	 *  pueda decir "clave incorrecta" en vez de volver a mostrar el mismo
	 *  cartel como si no hubiera pasado nada.
	 */
	std::function<bool(const std::string &usuario, int intento,
	                   std::string *clave)> onPedirClave;

	/** La clave del host no esta en known_hosts. Recibe la huella; devolver
	 *  true acepta la conexion. Solo se llama con HostDesconocido::Preguntar. */
	std::function<bool(const std::string &host, const std::string &huella)>
		onHostDesconocido;

	bool Connect(const std::string &host, int puerto, std::string *error);
	void Close();
	bool IsOpen() const;

	/** Espera datos hasta esperaMs y los procesa. Devuelve false si la
	 *  sesion se cerro. Un timeout no es un cierre: devuelve true. */
	bool Poll(int esperaMs);

	/** Descriptor del socket, para colgarle un QSocketNotifier. -1 si no
	 *  hay conexion.
	 *
	 *  Va en long long y no en int porque en Windows un socket es un SOCKET,
	 *  que es un UINT_PTR: 64 bits en x64. Truncarlo a int da un descriptor
	 *  que parece valido y no lo es. Y va en un entero, y no en el tipo
	 *  nativo, para no arrastrar winsock2.h a todo el que incluya esto --
	 *  QSocketNotifier pide un qintptr, asi que le calza igual. */
	long long Descriptor() const;

	/** Avisar al host que cambio el tamano de la ventana. */
	void Redimensionar(int columnas, int filas);

	/** Resumen de la sesion, al estilo del Summary() de la capa telnet. */
	std::string Resumen() const;

	/* --- IHostLink --- */
	virtual void SendRaw(const byte *data, int len);

private:
	/*  Se deja fuera de linea a proposito para no arrastrar <libssh2.h> a
	 *  todos los que incluyan esta cabecera. */
	struct Impl;
	Impl *m_impl;

	Ssh6530Transport(const Ssh6530Transport &);
	Ssh6530Transport &operator=(const Ssh6530Transport &);
};

/** true si el binario se compilo con soporte SSH. */
bool Disponible();

} // namespace ssh6530

#endif
