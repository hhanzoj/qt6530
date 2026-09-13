/*
 *  Opciones -- la linea de comandos, al estilo PuTTY.
 *
 *  Vive aparte de las aplicaciones y sin Qt a proposito. El parseo es donde
 *  se cuelan los errores tontos -- el puerto por defecto equivocado, el
 *  usuario@host que se come el host -- y aca abajo se puede probar con
 *  compilador en vez de a ojo. main.cpp queda como adaptador.
 *
 *  La forma es la de PuTTY, que es la que el usuario ya tiene en los dedos:
 *
 *      vt6530qt [opciones] [usuario@]host
 *
 *      -ssh                transporte SSH        (puerto 22, comando "tacl")
 *      -telnet             transporte telnet     (puerto 23)
 *      -P puerto
 *      -l usuario
 *      -comando "tacl"     el "Remote command" de PuTTY
 *      -i archivo          clave privada
 *
 *  Sobre el comando: en SSH, un cliente pide "shell" o "exec <comando>".
 *  Pedir shell te deja en OSS; pedir exec "tacl" hace que STN arme un PTY
 *  6530 y ahi corre Guardian. Es exactamente lo que hace el campo "Remote
 *  command" de PuTTY. Este emulador es de 6530, asi que siempre pide exec,
 *  y por eso no hay opcion para pedir shell: para OSS ya esta PuTTY.
 */
#ifndef _vt6530_opciones_h
#define _vt6530_opciones_h

#include <string>
#include <vector>

namespace cli {

enum class Transporte
{
	Telnet,
	Ssh
};

/** De donde sale el eco de lo que se teclea. */
enum class Eco
{
	Auto,
	Local,
	Host
};

/** Que hacer con un host que todavia no esta en known_hosts. */
enum class HostNuevo
{
	Preguntar,   /**< por defecto: mostrar la huella y esperar si/no */
	Aceptar,     /**< -aceptar-host-nuevo: aceptar y anotar, sin preguntar */
	Rechazar     /**< -rechazar-host-nuevo: cortar y decir como aceptarlo */
};

struct Opciones
{
	Transporte  transporte = Transporte::Telnet;
	std::string host;
	std::string usuario;
	int         puerto = 0;          /**< 0 = el que corresponda al transporte */
	std::string comando;             /**< vacio en telnet; "tacl" en ssh */
	std::string tipoTerminal = "tn6530-8";

	/** El TERM del pty-req de ssh, cuando tiene que ser distinto del
	 *  terminal-type que contestamos por telnet.
	 *
	 *  Vacio = el mismo que tipoTerminal, que es lo normal. Existe porque
	 *  son dos cosas distintas que veniamos mandando juntas, y resulta que
	 *  el host las trata distinto: con TERM=xterm arranca la aplicacion
	 *  (llega "STN44 Application has connected") y con tn6530-8 no arranca
	 *  nada. Poder pedir la ventana como xterm y seguir declarandonos
	 *  tn6530-8 por telnet es justamente el experimento que hace falta. */
	std::string tipoPty;
	std::string identidad;           /**< -i, clave privada */
	Eco         eco = Eco::Auto;
	bool        traza = false;
	bool        quedarse = false;
	bool        linemode = false;

	/** Aceptar NAWS y mandarle el tamano de la ventana al host.
	 *
	 *  Por defecto se rechaza: el emulador de referencia contesta WONT NAWS
	 *  y el TELSERV capturado lo acepta sin chistar. La pantalla del 6530 es
	 *  de 80x24 fija, asi que no aporta. Pero por ssh el host tambien manda
	 *  DO NAWS y hace falta poder probar si le importa la respuesta. */
	bool        naws = false;

	/** Pedir el PTY de ssh en crudo: sin eco, sin ISIG. Verdadero por
	 *  defecto, y hace falta: un PTY cocido hace eco de las secuencias AID y
	 *  trata el ETX que llevan adentro como Ctrl-C. -pty-cocido lo apaga. */
	bool        ptyCrudo = true;

	/** Descartar el eco que el host hace de lo que le mandamos (ssh).
	 *  Verdadero por defecto: STN devuelve las secuencias AID enteras y
	 *  sus bytes terminan dibujados en la pantalla. -sin-filtro-eco lo
	 *  apaga, que es lo que hace falta para volver a verlo. */
	bool        filtrarEco = true;

	/** Pedir shell en vez de exec (ssh).
	 *
	 *  Existe porque asi lo hace OutsideView. Se lo capturo con el proxy:
	 *  pide shell, no exec "tacl". Ver -comando, que solo aplica al exec. */
	bool        shell = false;

	/** Que hacer con un host que no esta en known_hosts.
	 *
	 *  Por defecto Preguntar, como ssh y PuTTY: se muestra la huella y se
	 *  espera si/no. Quien conteste que si, ademas de conectar, deja la clave
	 *  anotada -- aceptar sin anotar seria volver a preguntar lo mismo
	 *  mañana, y una pregunta que se repite siempre termina contestandose sin
	 *  leerla.
	 *
	 *  Esto NO alcanza a una clave que CAMBIO, que sigue cortando y no tiene
	 *  bandera para saltearla. Son dos situaciones distintas: un host nuevo
	 *  es lo normal la primera vez; una clave que cambia es lo que se ve
	 *  cuando alguien se metio en el medio. */
	HostNuevo   hostNuevo = HostNuevo::Preguntar;

	/** Ruta alternativa de known_hosts. Vacio = ~/.ssh/known_hosts. */
	std::string knownHosts;

	/** Segundos de espera para que el TCP conecte. 0 = el que trae el
	 *  transporte (15).
	 *
	 *  Existe porque quince segundos mirando una ventana que no aparece se
	 *  parecen mucho a un programa que no arranco. Con una direccion mal
	 *  tipeada en una red local, dos segundos alcanzan y sobran. */
	int timeoutSegundos = 0;

	bool        ayuda = false;

	/** Vacio si el parseo salio bien; si no, que hay que corregir. */
	std::string error;

	/** Avisos no fatales: se muestran y el programa sigue. */
	std::vector<std::string> avisos;
};

/** Largo maximo de un nombre de usuario que aceptamos escribir.
 *
 *  Treinta, y no quince como decia la primera version del formulario: un
 *  usuario Guardian es "grupo.usuario" con hasta ocho y ocho, o sea diecisiete
 *  contando el punto, y hay instalaciones con nombres mas largos. Un limite
 *  corto de mas no protege de nada y deja gente afuera -- el primer usuario
 *  que se probo, 'nkaizen.jaracena', tiene dieciseis y no entraba. */
const int kUsuarioLargoMaximo = 30;

/**
 *  true si el nombre de usuario tiene una forma que vale la pena mandar.
 *
 *  Deliberadamente laxa: letras, digitos, punto, guion y guion bajo, hasta
 *  kUsuarioLargoMaximo. La autoridad sobre quien existe es el host, no
 *  nosotros; esto solo ataja lo que seguro esta mal -- vacio, con espacios,
 *  con caracteres de control, absurdamente largo -- para no gastar un viaje
 *  de red y una negociacion en algo que no puede funcionar.
 *
 *  Sin expresiones regulares a proposito: asi la misma funcion vale para el
 *  formulario Qt y para la linea de comandos, sin depender de que
 *  QRegularExpression y std::regex entiendan lo mismo.
 */
bool UsuarioValido(const std::string &usuario);

/** Puerto por defecto de cada transporte. */
int PuertoPorDefecto(Transporte t);

/** Comando por defecto de cada transporte. */
const char *ComandoPorDefecto(Transporte t);

/**
 *  Las opciones que se dieron explicitamente y no tienen centinela.
 *
 *  El puerto en 0, el comando vacio o el tipoPty vacio ya dicen "no me lo
 *  dieron". Para -linemode y -shell no hay forma: false puede ser el valor
 *  por defecto o lo que alguien pidio, y confundirlos significa pisar lo que
 *  el usuario escribio. Asi que se dice aparte.
 */
struct Dados
{
	bool linemode = false;   /**< se dio -linemode o -sin-linemode */
	bool shell    = false;   /**< se dio -shell o -exec */
	bool comando  = false;   /**< se dio -comando */
};

/**
 *  Completa lo que falta segun el transporte, y valida lo que no se puede
 *  completar.
 *
 *  Esto estaba adentro de Parsear y salio para que lo use tambien el
 *  formulario de conexion. No es prolijidad: los defectos de ssh -- puerto
 *  22, pty TN6530-8, shell, LINEMODE ofrecido -- son los cuatro que hacen que
 *  el host arranque TACL en una ventana 6530, y costaron una semana de
 *  diagnostico. Un formulario que llene el struct a mano y se los saltee
 *  vuelve a la sesion muda, sin que nada parezca roto.
 *
 *  Llena o->error si algo no se puede resolver, y o->avisos con lo que se
 *  resolvio suponiendo (el usuario del sistema, por ejemplo). Los avisos no
 *  impiden conectar.
 */
void AplicarDefectos(Opciones *o, const Dados &dados = Dados());

/**
 *  Parsea los argumentos SIN el nombre del programa.
 *
 *  Acepta las dos formas de guion: -ssh y --ssh son lo mismo, porque PuTTY
 *  usa uno y el resto del proyecto venia usando dos.
 */
Opciones Parsear(const std::vector<std::string> &args);

/** El texto de ayuda. */
std::string Uso(const char *programa);

} // namespace cli

#endif
