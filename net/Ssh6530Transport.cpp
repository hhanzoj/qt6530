#include "Ssh6530Transport.h"

#include <cstdio>
#include <cstring>
#include <string>

#if defined(VT6530_CON_SSH)

/*  La cabecera nativa, y la linea de compilacion ajustada para encontrarla.
 *  Nada de redeclarar constantes a mano: si el dia de manana libssh2 cambia
 *  un valor, que lo cambie el que corresponde.                            */
/*  Sockets.h va ANTES que libssh2.h en Windows: libssh2 incluye winsock2.h
 *  por su cuenta, y el orden de winsock2/windows.h tiene que quedar fijado
 *  por nosotros. En POSIX el orden da igual.                              */
#include "Sockets.h"
#include "KnownHosts.h"

#include <libssh2.h>

namespace ssh6530 {

bool Disponible() { return true; }

/* ------------------------------------------------------------------ */

struct Ssh6530Transport::Impl
{
	Politica politica;
	red::Descriptor fd = red::SinDescriptor();
	LIBSSH2_SESSION *sesion = nullptr;
	LIBSSH2_CHANNEL *canal  = nullptr;
	std::string metodoAuth;      /* como se autentico, para el resumen */
	std::string host;
	int         puerto = 0;
	bool        cerrado = false;

	explicit Impl(const Politica &p) : politica(p) {}
};

namespace {

int g_iniciada = 0;

/*  Devuelve false si winsock no arranco. En POSIX no puede fallar. */
bool IniciarBiblioteca(std::string *error)
{
	if (!red::Iniciar(error)) return false;
	if (g_iniciada == 0)
	{
		libssh2_init(0);
	}
	g_iniciada++;
	return true;
}

std::string ErrorDeSesion(LIBSSH2_SESSION *s, const std::string &quefallo)
{
	char *msg = nullptr;
	int largo = 0;
	if (s != nullptr)
	{
		libssh2_session_last_error(s, &msg, &largo, 0);
	}
	std::string r = quefallo;
	if (msg != nullptr && *msg != '\0')
	{
		r += ": ";
		r += msg;
	}
	return r;
}

std::string HuellaLegible(LIBSSH2_SESSION *s)
{
	const char *h = libssh2_hostkey_hash(s, LIBSSH2_HOSTKEY_HASH_SHA1);
	if (h == nullptr) return std::string("(sin huella)");

	std::string r;
	char tmp[4];
	for (int i = 0; i < 20; i++)
	{
		if (i) r += ":";
		std::snprintf(tmp, sizeof(tmp), "%02x", (unsigned char)h[i]);
		r += tmp;
	}
	return r;
}

/**
 *  La huella como la muestra ssh: SHA256 en base64, sin el relleno.
 *
 *  Es la que hay que poder comparar a ojo contra
 *
 *      ssh-keygen -lf /etc/ssh/ssh_host_rsa_key.pub
 *
 *  y contra lo que dice el ssh de OpenSSH la primera vez. Aceptar un host
 *  nuevo mirando una huella que no se parece a ninguna otra que uno tenga a
 *  mano no es verificar nada; es apretar que si.
 */
std::string HuellaSha256(LIBSSH2_SESSION *s)
{
	const char *h = libssh2_hostkey_hash(s, LIBSSH2_HOSTKEY_HASH_SHA256);
	if (h == nullptr) return std::string();

	std::string b64 = conocidos::Base64((const unsigned char *)h, 32);
	while (!b64.empty() && b64[b64.size() - 1] == '=') b64.erase(b64.size() - 1);
	return "SHA256:" + b64;
}

/**
 *  El nombre del algoritmo tal como va en known_hosts.
 *
 *  Traduce los LIBSSH2_HOSTKEY_TYPE_* que devuelve libssh2_session_hostkey a
 *  los nombres del protocolo. Vacio si no lo conocemos, y entonces no se
 *  escribe nada: inventar un nombre dejaria una linea que ssh no acepta.
 */
std::string NombreDeTipoDeClave(int tipo)
{
	switch (tipo)
	{
		case LIBSSH2_HOSTKEY_TYPE_RSA:        return "ssh-rsa";
		case LIBSSH2_HOSTKEY_TYPE_DSS:        return "ssh-dss";
#ifdef LIBSSH2_HOSTKEY_TYPE_ECDSA_256
		case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:  return "ecdsa-sha2-nistp256";
		case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:  return "ecdsa-sha2-nistp384";
		case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:  return "ecdsa-sha2-nistp521";
#endif
#ifdef LIBSSH2_HOSTKEY_TYPE_ED25519
		case LIBSSH2_HOSTKEY_TYPE_ED25519:    return "ssh-ed25519";
#endif
		default:                              return std::string();
	}
}

std::string RutaKnownHosts(const Politica &p)
{
	if (!p.knownHosts.empty()) return p.knownHosts;
	const std::string casa = red::CarpetaDelUsuario();
	if (casa.empty()) return std::string();

	/*  La barra normal sirve en las dos: las llamadas de archivo de Windows
	 *  aceptan '/' desde siempre, y asi la ruta que se le muestra al usuario
	 *  es la misma que escribiria ssh.                                    */
	return casa + "/.ssh/known_hosts";
}

/** Conecta el socket TCP con timeout. Devuelve un descriptor invalido y llena
 *  error si falla. */
red::Descriptor ConectarTcp(const std::string &host, int puerto, int timeoutMs,
                            std::string *error)
{
	char puertoTxt[16];
	std::snprintf(puertoTxt, sizeof(puertoTxt), "%d", puerto);

	struct addrinfo pistas;
	std::memset(&pistas, 0, sizeof(pistas));
	pistas.ai_family   = AF_UNSPEC;
	pistas.ai_socktype = SOCK_STREAM;

	struct addrinfo *lista = nullptr;
	const int rc = ::getaddrinfo(host.c_str(), puertoTxt, &pistas, &lista);
	if (rc != 0 || lista == nullptr)
	{
		*error = "no se pudo resolver '" + host + "': " +
		         red::ExplicarResolucion(rc);
		return red::SinDescriptor();
	}

	std::string ultimo = "sin candidatos";

	for (struct addrinfo *a = lista; a != nullptr; a = a->ai_next)
	{
		const red::Descriptor fd =
			::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
		if (!red::Valido(fd))
		{
			ultimo = red::Explicar(red::UltimoError());
			continue;
		}

		/* No bloqueante solo para poder aplicar el timeout al connect. */
		red::PonerNoBloqueante(fd, true);

		bool conectado = false;
		const int r = ::connect(fd, a->ai_addr, (int)a->ai_addrlen);

		if (r == 0)
		{
			conectado = true;
		}
		else if (red::ConexionEnCurso(red::UltimoError()))
		{
			int motivo = 0;
			switch (red::EsperarConexion(fd, timeoutMs, &motivo))
			{
				case 0:  conectado = true; break;
				case 1:  ultimo = "tiempo agotado"; break;
				default: ultimo = red::Explicar(motivo); break;
			}
		}
		else
		{
			ultimo = red::Explicar(red::UltimoError());
		}

		if (conectado)
		{
			red::PonerNoBloqueante(fd, false);   /* vuelve a bloqueante */
			red::PonerSinRetardo(fd);
			::freeaddrinfo(lista);
			return fd;
		}

		red::Cerrar(fd);
	}

	::freeaddrinfo(lista);
	*error = "no se pudo conectar a " + host + ":" + puertoTxt + ": " + ultimo;
	return red::SinDescriptor();
}

/*
 *  Los modos de terminal del pty-req, en crudo.
 *
 *  POR QUE HACE FALTA
 *
 *  Un PTY de Unix, por defecto, viene en modo "cocido": hace eco de todo lo
 *  que recibe y le da significado a ciertos bytes de control. Para una shell
 *  esta perfecto. Para un terminal de bloque es veneno, y se ve en la traza
 *  de un VIEWSYS real:
 *
 *      => term: <01>V!#"<03><00>       la secuencia AID que mandamos
 *      <= host: <01>V!#"<1B>:"...      el PTY nos la devuelve
 *      [INFO ] Unexpected char in 5000: 86      <- 0x56, nuestra propia 'V'
 *
 *  Dos problemas de una. El eco mete nuestros propios bytes en la maquina de
 *  estados del 6530, que los interpreta como si vinieran del host. Y fijarse
 *  donde se corta el eco: justo antes del <03>. ETX es 0x03, que en termios
 *  es Ctrl-C, o sea INTR. Un byte que forma parte de TODA secuencia AID.
 *
 *  Asi que se piden los modos explicitamente. Es lo mismo que hace cualquier
 *  programa que necesita el terminal en crudo, solo que del lado del host y
 *  a traves del pty-req.
 *
 *  El formato lo define la RFC 4254, seccion 8: pares de (codigo, valor de
 *  32 bits en big endian), terminados por un codigo 0. Si el host no los
 *  entiende, los ignora y quedamos como estabamos -- no hay forma de que
 *  esto empeore nada.
 */
std::string ModosCrudos()
{
	/*  Codigos de la RFC 4254 seccion 8. Se ponen los nombres al lado
	 *  porque el numero solo no dice nada dentro de seis meses.        */
	static const unsigned char kISIG   = 50;  /* Ctrl-C manda SIGINT     */
	static const unsigned char kICANON = 51;  /* lectura por lineas      */
	static const unsigned char kECHO   = 53;  /* el eco que nos molesta  */
	static const unsigned char kIXON   = 38;  /* Ctrl-S / Ctrl-Q         */
	static const unsigned char kICRNL  = 36;  /* CR se convierte en LF   */
	static const unsigned char kINLCR  = 34;  /* LF se convierte en CR   */
	static const unsigned char kOPOST  = 70;  /* traduccion a la salida  */

	static const unsigned char apagar[] = {
		kISIG, kICANON, kECHO, kIXON, kICRNL, kINLCR, kOPOST
	};

	std::string m;
	for (size_t i = 0; i < sizeof(apagar) / sizeof(apagar[0]); i++)
	{
		m += (char)apagar[i];
		m += (char)0; m += (char)0; m += (char)0; m += (char)0;  /* = 0 */
	}
	m += (char)0;   /* fin de la lista */
	return m;
}

/*
 *  Los modos EXACTOS que manda OutsideView, copiados de una captura.
 *
 *  No son "los de por defecto" ni una lista vacia: son 49 modos explicitos,
 *  246 bytes, que tools/tap6530ssh.py saco del pty-req de un OutsideView SSH
 *  Client 2016 contra este mismo host. El terminal va COCIDO -- ECHO, ICANON,
 *  ISIG y OPOST en uno --, que es justo lo contrario de lo que pedia el
 *  razonamiento desde el protocolo. El emulador que funciona hace esto, asi
 *  que esto es lo que se replica.
 *
 *  Mandar la lista vacia no equivale: con la lista vacia el host usa SUS
 *  valores por defecto, que no tienen por que ser estos.
 */
std::string ModosDeOutsideView()
{
	struct Par { unsigned char codigo; unsigned int valor; };
	static const Par modos[] = {
		{  1,     3 },  /* VINTR   */  {  2,    28 },  /* VQUIT   */
		{  3,     8 },  /* VERASE  */  {  4,    21 },  /* VKILL   */
		{  5,     4 },  /* VEOF    */  {  6,     0 },  /* VEOL    */
		{  7,     0 },  /* VEOL2   */  {  8,    17 },  /* VSTART  */
		{  9,    19 },  /* VSTOP   */  { 10,    26 },  /* VSUSP   */
		{ 12,    18 },  /* VREPRINT*/  { 13,    23 },  /* VWERASE */
		{ 14,    22 },  /* VLNEXT  */  { 18,    15 },  /* VDISCARD*/
		{ 30,     0 },  /* IGNPAR  */  { 31,     0 },  /* PARMRK  */
		{ 32,     0 },  /* INPCK   */  { 33,     0 },  /* ISTRIP  */
		{ 34,     0 },  /* INLCR   */  { 35,     0 },  /* IGNCR   */
		{ 36,     1 },  /* ICRNL   */  { 37,     0 },  /* IUCLC   */
		{ 38,     1 },  /* IXON    */  { 39,     0 },  /* IXANY   */
		{ 40,     0 },  /* IXOFF   */  { 41,     0 },  /* IMAXBEL */
		{ 50,     1 },  /* ISIG    */  { 51,     1 },  /* ICANON  */
		{ 53,     1 },  /* ECHO    */  { 54,     0 },  /* ECHOE   */
		{ 55,     0 },  /* ECHOK   */  { 56,     0 },  /* ECHONL  */
		{ 57,     0 },  /* NOFLSH  */  { 58,     0 },  /* TOSTOP  */
		{ 59,     1 },  /* IEXTEN  */  { 60,     0 },  /* ECHOCTL */
		{ 61,     0 },  /* ECHOKE  */  { 70,     1 },  /* OPOST   */
		{ 71,     0 },  /* OLCUC   */  { 72,     1 },  /* ONLCR   */
		{ 73,     0 },  /* OCRNL   */  { 74,     0 },  /* ONOCR   */
		{ 75,     0 },  /* ONLRET  */  { 90,     1 },  /* CS7     */
		{ 91,     1 },  /* CS8     */  { 92,     0 },  /* PARENB  */
		{ 93,     0 },  /* PARODD  */
		{ 128, 38400 }, /* TTY_OP_ISPEED */
		{ 129, 38400 }, /* TTY_OP_OSPEED */
	};

	std::string m;
	for (size_t i = 0; i < sizeof(modos) / sizeof(modos[0]); i++)
	{
		m += (char)modos[i].codigo;
		m += (char)((modos[i].valor >> 24) & 0xFF);
		m += (char)((modos[i].valor >> 16) & 0xFF);
		m += (char)((modos[i].valor >>  8) & 0xFF);
		m += (char)( modos[i].valor        & 0xFF);
	}
	m += (char)0;
	return m;
}

} // namespace

/* ------------------------------------------------------------------ */

Ssh6530Transport::Ssh6530Transport(const Politica &politica)
:	m_impl(new Impl(politica))
{
	/*  Si winsock no arranca no se puede avisar desde un constructor, asi
	 *  que aca se ignora y Connect() lo vuelve a intentar, que es donde si
	 *  hay a quien decirselo. Es idempotente.                            */
	IniciarBiblioteca(nullptr);
}

Ssh6530Transport::~Ssh6530Transport()
{
	Close();
	delete m_impl;
}

bool Ssh6530Transport::IsOpen() const
{
	return m_impl->canal != nullptr && !m_impl->cerrado;
}

long long Ssh6530Transport::Descriptor() const
{
	return red::ComoEntero(m_impl->fd);
}

void Ssh6530Transport::Close()
{
	/*  El socket quedo no bloqueante (ver el final de Connect), asi que estas
	 *  tres pueden contestar EAGAIN y no hacer nada. Se reintenta un rato
	 *  corto y despues se sigue igual: el socket se cierra abajo pase lo que
	 *  pase, y colgar la salida del programa por un saludo de despedida
	 *  seria peor que perderselo. Sin el reintento, channel_free devuelve
	 *  EAGAIN y no libera.
	 *
	 *  La espera va por red::Dormir y no por select() con las tres listas
	 *  vacias: eso ultimo duerme en POSIX pero en Winsock devuelve WSAEINVAL
	 *  al instante, y entonces los veinte reintentos pasan en un suspiro sin
	 *  darle tiempo a nada.                                              */
	auto insistir = [](const std::function<int()> &f) {
		for (int i = 0; i < 20; i++)
		{
			if (f() != LIBSSH2_ERROR_EAGAIN) return;
			red::Dormir(5);
		}
	};

	if (m_impl->canal != nullptr)
	{
		LIBSSH2_CHANNEL *c = m_impl->canal;
		insistir([c]() { return libssh2_channel_free(c); });
		m_impl->canal = nullptr;
	}
	if (m_impl->sesion != nullptr)
	{
		LIBSSH2_SESSION *s = m_impl->sesion;
		insistir([s]() {
			return libssh2_session_disconnect(s, "hasta luego");
		});
		insistir([s]() { return libssh2_session_free(s); });
		m_impl->sesion = nullptr;
	}
	if (red::Valido(m_impl->fd))
	{
		red::Cerrar(m_impl->fd);
		m_impl->fd = red::SinDescriptor();
	}
	m_impl->cerrado = true;
}

/* ------------------------------------------------------------------ */
/*  Conexion                                                           */
/* ------------------------------------------------------------------ */

bool Ssh6530Transport::Connect(const std::string &host, int puerto,
                               std::string *error)
{
	std::string basura;
	if (error == nullptr) error = &basura;

	Impl &d = *m_impl;
	d.host = host;
	d.puerto = puerto;
	d.cerrado = false;

	auto trazar = [&](const std::string &s) { if (onTrace) onTrace(s); };

	if (!IniciarBiblioteca(error)) return false;

	d.fd = ConectarTcp(host, puerto, d.politica.timeoutConexionMs, error);
	if (!red::Valido(d.fd)) return false;

	d.sesion = libssh2_session_init();
	if (d.sesion == nullptr)
	{
		*error = "no se pudo crear la sesion ssh";
		Close();
		return false;
	}

	/*  El saludo y la autenticacion van bloqueantes: es mas simple y son
	 *  uno o dos segundos. Recien despues se pasa a no bloqueante, que es
	 *  como tiene que estar para el bucle de eventos.                    */
	libssh2_session_set_blocking(d.sesion, 1);

	if (libssh2_session_handshake(d.sesion, d.fd) != 0)
	{
		*error = ErrorDeSesion(d.sesion, "fallo el saludo ssh");
		Close();
		return false;
	}

	const std::string huella = HuellaLegible(d.sesion);
	const std::string huella256 = HuellaSha256(d.sesion);

	/*  La SHA256 es la que se puede comparar contra ssh-keygen y contra lo
	 *  que dijo el ssh de OpenSSH; la SHA1 queda porque hay documentacion
	 *  vieja que todavia la usa.                                         */
	if (!huella256.empty()) trazar("huella del host: " + huella256);
	trazar("huella del host (sha1): " + huella);

	/* --- known_hosts ------------------------------------------------- */
	{
		const std::string ruta = RutaKnownHosts(d.politica);
		LIBSSH2_KNOWNHOSTS *kh = libssh2_knownhost_init(d.sesion);
		int estado = LIBSSH2_KNOWNHOST_CHECK_NOTFOUND;

		/*  La clave cruda se guarda para poder anotarla despues si el host
		 *  resulta nuevo y se acepta.                                     */
		size_t largoClave = 0;
		int    tipoClave  = 0;
		const char *clave = libssh2_session_hostkey(d.sesion, &largoClave,
		                                            &tipoClave);

		if (kh != nullptr)
		{
			if (!ruta.empty())
			{
				libssh2_knownhost_readfile(kh, ruta.c_str(),
				                           LIBSSH2_KNOWNHOST_FILE_OPENSSH);
			}
			if (clave != nullptr)
			{
				estado = libssh2_knownhost_checkp(
					kh, host.c_str(), puerto, clave, largoClave,
					LIBSSH2_KNOWNHOST_TYPE_PLAIN |
					LIBSSH2_KNOWNHOST_KEYENC_RAW,
					nullptr);
			}
			libssh2_knownhost_free(kh);
		}

		/*  DOS HOSTS DETRAS DE LA MISMA DIRECCION
		 *
		 *  Con NAT por puertos es normal que 192.168.1.193:22 sea una maquina
		 *  y 192.168.1.193:2200 sea otra. OpenSSH los trata como entradas
		 *  distintas -- "192.168.1.193" y "[192.168.1.193]:2200" -- y nosotros
		 *  escribimos con esa misma convencion. Pero la comprobacion la hace
		 *  libssh2, y ahi el puerto se pierde: encuentra la entrada del 22 y
		 *  dice que la clave del 2200 "cambio".
		 *
		 *  Eso es un falso positivo, y de los peores: acusa de intromision en
		 *  una configuracion corriente, y un aviso grave que salta cuando no
		 *  pasa nada es como se le ensena a la gente a ignorarlo.
		 *
		 *  Asi que el MISMATCH se degrada a "host nuevo" SOLO si podemos
		 *  demostrar que no hay ninguna entrada para este host Y este puerto.
		 *  Y "demostrar" es literal: si el archivo tiene nombres hasheados o
		 *  con comodines, no se ve todo por nombre, no se puede concluir una
		 *  ausencia, y entonces no se degrada nada.
		 *
		 *  Esto NO afloja la verificacion. Deja las cosas como las deja
		 *  OpenSSH, que para un host:puerto que no figura pregunta en vez de
		 *  acusar. Si la entrada de este mismo host y puerto existe y no
		 *  coincide, sigue cortando abajo y no hay bandera que lo saltee. */
		const conocidos::Lectura leido = conocidos::Revisar(ruta, host, puerto);

		if (estado == LIBSSH2_KNOWNHOST_CHECK_MISMATCH &&
		    leido.Completa() && !leido.exacta)
		{
			estado = LIBSSH2_KNOWNHOST_CHECK_NOTFOUND;
			trazar("hay otra entrada para '" + host + "' en known_hosts, pero "
			       "ninguna para " + conocidos::NombreDeHost(host, puerto) +
			       ": se trata como host nuevo (otra maquina en otro puerto)");
		}

		if (estado == LIBSSH2_KNOWNHOST_CHECK_MISMATCH)
		{
			/*  Este NO se negocia y no hay opcion para saltearlo -- ni
			 *  -aceptar-host-nuevo, que es solo para hosts NUEVOS --: una
			 *  clave que cambio es exactamente lo que se ve cuando alguien se
			 *  mete en el medio. Si el host de verdad cambio de clave, se
			 *  borra la linea vieja de known_hosts a mano y se sabe por que
			 *  se hizo.
			 *
			 *  PERO este mensaje puede significar dos cosas muy distintas, y
			 *  antes no las separaba:
			 *
			 *    1. la clave de ESTE host, de ESTE tipo, cambio -- lo grave;
			 *    2. el archivo tiene el host con una clave de OTRO tipo, o
			 *       para OTRO puerto, y ninguna coincide con la que el host
			 *       presento ahora -- que casi siempre es inocente.
			 *
			 *  Decir "CAMBIO" a secas en el caso 2 es gritar por algo que no
			 *  pasa, y eso, repetido, es lo que ensena a ignorar el aviso. Se
			 *  muestra entonces lo que el archivo tiene de verdad, para que
			 *  el que lee decida con datos y no con adjetivos.            */
			const std::string tipoAhora = NombreDeTipoDeClave(tipoClave);

			std::string detalle;
			for (std::size_t i = 0; i < leido.delHost.size(); i++)
			{
				detalle += "    " + leido.delHost[i].nombre + "  " +
				           leido.delHost[i].tipo + "\n";
			}

			if (leido.delHost.empty())
				detalle = "    (ninguna con este nombre)\n";

			if (leido.hasheadas > 0 || leido.comodines > 0)
			{
				detalle += "    (ademas hay " +
				           std::to_string(leido.hasheadas) +
				           " con el nombre hasheado y " +
				           std::to_string(leido.comodines) +
				           " con comodines, que no se pueden comparar por "
				           "nombre)\n";
			}

			*error =
				"la clave del host no coincide con known_hosts.\n"
				"host      : " + conocidos::NombreDeHost(host, puerto) + "\n"
				"archivo   : " + ruta + "\n"
				"presenta  : " + (tipoAhora.empty() ? "(tipo desconocido)"
				                                    : tipoAhora) + " " +
				(huella256.empty() ? huella : huella256) + "\n"
				"en el archivo hay:\n" + detalle +
				"\n"
				"Si alguna de esas lineas es del MISMO nombre y del MISMO "
				"tipo que\n"
				"la de arriba, la clave cambio de verdad y conviene averiguar "
				"por que\n"
				"antes de tocar nada. Si son de otro tipo o de otro puerto, "
				"basta con\n"
				"agregar la nueva: borre esa linea de " + ruta + " y vuelva a "
				"conectar.";
			Close();
			return false;
		}

		if (estado != LIBSSH2_KNOWNHOST_CHECK_MATCH)
		{
			const std::string paraMostrar =
				huella256.empty() ? huella : huella256;

			bool aceptar = false;
			switch (d.politica.hostDesconocido)
			{
				case HostDesconocido::Aceptar:
					aceptar = true;
					trazar("host nuevo, aceptado sin preguntar");
					break;
				case HostDesconocido::Preguntar:
					aceptar = onHostDesconocido
					        ? onHostDesconocido(host, paraMostrar) : false;
					break;
				case HostDesconocido::Rechazar:
				default:
					aceptar = false;
					break;
			}

			if (!aceptar)
			{
				*error = "el host '" + host + "' no esta en known_hosts.\n"
				         "huella: " + paraMostrar + "\n"
				         "para aceptarlo y anotarlo: -aceptar-host-nuevo\n"
				         "(o conectese una vez con ssh, que escribe la misma "
				         "entrada).";
				Close();
				return false;
			}

			/*  Aceptado: se anota, o la proxima vez volvemos a preguntar lo
			 *  mismo y la pregunta pierde todo el sentido.
			 *
			 *  Que esto falle NO corta la conexion. La clave ya se miro y se
			 *  acepto; no poder escribir el archivo es un problema del disco,
			 *  no de la sesion, y cortar aca seria castigar al usuario por
			 *  algo que no tiene que ver con lo que pidio. Se avisa y se
			 *  sigue.                                                      */
			if (d.politica.anotarHostNuevo)
			{
				const std::string tipo = NombreDeTipoDeClave(tipoClave);
				std::string porQueNo;

				if (clave == nullptr || tipo.empty())
				{
					porQueNo = "no se reconoce el tipo de clave del host";
				}
				else
				{
					const std::string linea = conocidos::Linea(
						host, puerto, tipo,
						(const unsigned char *)clave, largoClave);
					conocidos::Agregar(ruta, linea, &porQueNo);
				}

				if (porQueNo.empty())
					trazar("clave anotada en " + ruta + " (" + tipo + ")");
				else
					trazar("aviso: no se pudo anotar la clave en known_hosts: " +
					       porQueNo + " -- se sigue igual, pero la proxima vez "
					       "va a volver a preguntar");
			}
		}
		else
		{
			trazar("la clave del host coincide con known_hosts");
		}
	}

	/* --- autenticacion ------------------------------------------------ */
	{
		const std::string usuario = d.politica.usuario;
		if (usuario.empty())
		{
			*error = "falta el usuario: use usuario@host o -l usuario";
			Close();
			return false;
		}

		bool autenticado = false;

		/*  1. clave privada explicita (-i). */
		if (!d.politica.claveArchivo.empty())
		{
			const std::string publica = d.politica.claveArchivo + ".pub";
			if (libssh2_userauth_publickey_fromfile(
			        d.sesion, usuario.c_str(), publica.c_str(),
			        d.politica.claveArchivo.c_str(), "") == 0)
			{
				autenticado = true;
				d.metodoAuth = "clave privada " + d.politica.claveArchivo;
			}
			else
			{
				trazar(ErrorDeSesion(d.sesion, "la clave privada no sirvio"));
			}
		}

		/*  2. el agente, si hay uno. */
		if (!autenticado && d.politica.usarAgente)
		{
			LIBSSH2_AGENT *agente = libssh2_agent_init(d.sesion);
			if (agente != nullptr)
			{
				if (libssh2_agent_connect(agente) == 0 &&
				    libssh2_agent_list_identities(agente) == 0)
				{
					struct libssh2_agent_publickey *id = nullptr;
					struct libssh2_agent_publickey *previa = nullptr;
					while (!autenticado)
					{
						if (libssh2_agent_get_identity(agente, &id, previa) != 0)
							break;
						if (libssh2_agent_userauth(agente, usuario.c_str(), id) == 0)
						{
							autenticado = true;
							d.metodoAuth = std::string("agente (") +
							               (id->comment ? id->comment : "?") + ")";
						}
						previa = id;
					}
					libssh2_agent_disconnect(agente);
				}
				libssh2_agent_free(agente);
			}
		}

		/*  Que metodos ofrece el host. No cambia lo que se intenta -- eso ya
		 *  esta decidido arriba --, pero cuando nada funciona la diferencia
		 *  entre "la clave esta mal" y "este host no acepta claves" es todo
		 *  lo que hay para saber por donde seguir.                        */
		std::string metodos;
		if (!autenticado)
		{
			const char *lista =
				libssh2_userauth_list(d.sesion, usuario.c_str(),
				                      (unsigned int)usuario.size());
			if (lista != nullptr)
			{
				metodos = lista;
				trazar("metodos de autenticacion que ofrece el host: " + metodos);
			}
		}
		const bool aceptaClave =
			metodos.empty() || metodos.find("password") != std::string::npos;

		/*  3. la clave, preguntando a quien nos llamo.
		 *
		 *  Y se reintenta, como hacen ssh y PuTTY. Equivocarse al tipear una
		 *  clave es lo normal, no un caso de borde; un programa que ante el
		 *  primer error se queda sin salida obliga a volver a arrancarlo,
		 *  rehacer la conexion y perder lo que ya habia contestado.
		 *
		 *  El numero de intentos es NUESTRO limite, no el del host. El host
		 *  tiene el suyo (MaxAuthTries, seis por defecto en OpenSSH) y cuando
		 *  se le acaba cierra el socket sin decir nada -- eso es el "Remote
		 *  side unexpectedly closed network connection" que muestra PuTTY --.
		 *  Por eso abajo se distingue el rechazo de la clave del cierre de la
		 *  conexion: seguir preguntando contra un socket muerto seria pedirle
		 *  al usuario que tipee para nadie.                                */
		bool cerroElHost = false;

		if (!autenticado && onPedirClave && aceptaClave)
		{
			const int maximo = d.politica.intentosDeClave > 0
			                       ? d.politica.intentosDeClave : 1;

			for (int intento = 1; intento <= maximo && !autenticado; intento++)
			{
				std::string clave;
				if (!onPedirClave(usuario, intento, &clave))
				{
					trazar("cancelado por el usuario");
					break;
				}

				const int rc = libssh2_userauth_password(
					d.sesion, usuario.c_str(), clave.c_str());

				/*  Que no quede dando vueltas en memoria mas de lo
				 *  necesario. No es blindaje, es higiene.               */
				if (!clave.empty())
					std::memset(&clave[0], 0, clave.size());

				if (rc == 0)
				{
					autenticado = true;
					d.metodoAuth = "contrasena";
					break;
				}

				/*  Un rechazo se reintenta; cualquier otra cosa no.
				 *
				 *  LIBSSH2_ERROR_AUTHENTICATION_FAILED es "esa clave no
				 *  sirve". Los errores de socket son otra cosa: el host se
				 *  fue, y ahi no hay nada que reintentar.                */
				if (rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED)
				{
					trazar("clave incorrecta (intento " +
					       std::to_string(intento) + " de " +
					       std::to_string(maximo) + ")");
					continue;
				}

				if (rc == LIBSSH2_ERROR_SOCKET_SEND ||
				    rc == LIBSSH2_ERROR_SOCKET_RECV ||
				    rc == LIBSSH2_ERROR_SOCKET_DISCONNECT ||
				    rc == LIBSSH2_ERROR_SOCKET_TIMEOUT)
				{
					cerroElHost = true;
				}

				trazar(ErrorDeSesion(d.sesion, "la clave no sirvio"));
				break;
			}
		}

		if (!autenticado)
		{
			if (cerroElHost)
			{
				*error = "el host cerro la conexion durante la "
				         "autenticacion.\n"
				         "Suele ser el limite de intentos del propio host "
				         "(MaxAuthTries).\n"
				         "Espere unos segundos y vuelva a conectar.";
				Close();
				return false;
			}

			if (!aceptaClave)
			{
				*error = "el host no acepta contrasena para '" + usuario +
				         "'.\n"
				         "Metodos que ofrece: " + metodos + "\n"
				         "Para clave publica: -i <archivo>, o un agente ssh.";
				Close();
				return false;
			}

			*error = "no se pudo autenticar como '" + usuario + "'";
			if (!metodos.empty())
				*error += " (el host ofrece: " + metodos + ")";
			Close();
			return false;
		}
		trazar("autenticado como " + usuario + " por " + d.metodoAuth);
	}

	/* --- canal, pty 6530 y exec --------------------------------------- */
	{
		d.canal = libssh2_channel_open_session(d.sesion);
		if (d.canal == nullptr)
		{
			*error = ErrorDeSesion(d.sesion, "no se pudo abrir el canal");
			Close();
			return false;
		}

		/*  ACA esta todo el asunto.
		 *
		 *  El TERM del pty-req es lo que hace que STN asigne un PTY 6530 en
		 *  vez de dejarnos en OSS. Confirmado contra rci3: con "tn6530-8"
		 *  contesta "STN46 Secure SSH session: tn6530-8" y da un $ZPTY;
		 *  sin eso ni aparece el banner de STN.                          */
		const std::string &term = d.politica.terminalType;
		if (d.politica.pedirPty)
		{
			const std::string modos =
				d.politica.ptyCrudo ? ModosCrudos() : ModosDeOutsideView();

			if (libssh2_channel_request_pty_ex(
			        d.canal, term.c_str(), (unsigned int)term.size(),
			        modos.empty() ? nullptr : modos.c_str(),
			        (unsigned int)modos.size(),
			        d.politica.columnas, d.politica.filas, 0, 0) != 0)
			{
				*error = ErrorDeSesion(d.sesion,
				                       "el host rechazo el pty '" + term + "'");
				Close();
				return false;
			}
			trazar("pty pedido: " + term + " " +
			       std::to_string(d.politica.columnas) + "x" +
			       std::to_string(d.politica.filas) +
			       (d.politica.ptyCrudo ? " (crudo: sin eco, sin ISIG)"
			                            : " (cocido: los 49 modos de OutsideView)"));
		}
		else
		{
			/*  Sin pty. No sirve para el modo bloque -- sin PTY 6530 el host
			 *  no manda 6530 --, pero es la unica configuracion que se vio
			 *  arrancar TACL de verdad ("ssh host tacl" sin -t), asi que
			 *  poder pedirla es lo que permite comparar.                 */
			trazar("sin pty (a proposito)");
		}

		/*  Por defecto exec y no shell: shell deja en OSS, que no es lo que
		 *  este emulador dibuja, y para OSS ya esta PuTTY.
		 *
		 *  usarShell existe para poder probarlo, no porque sirva. Con el
		 *  exec sobre un PTY 6530 el host se queda mudo, y hasta saber por
		 *  que, cerrarse a una sola forma de pedir la sesion es cerrarse el
		 *  unico camino de averiguarlo.                                  */
		if (d.politica.usarShell)
		{
			if (libssh2_channel_shell(d.canal) != 0)
			{
				*error = ErrorDeSesion(d.sesion, "el host rechazo shell");
				Close();
				return false;
			}
			trazar("shell (en vez de exec)");
			libssh2_session_set_blocking(d.sesion, 0);
			red::PonerNoBloqueante(d.fd, true);
			return true;
		}

		const std::string &cmd = d.politica.comando;
		if (cmd.empty())
		{
			*error = "falta el comando remoto (por ejemplo 'tacl')";
			Close();
			return false;
		}
		if (libssh2_channel_exec(d.canal, cmd.c_str()) != 0)
		{
			*error = ErrorDeSesion(d.sesion,
			                       "el host rechazo exec \"" + cmd + "\"");
			Close();
			return false;
		}
		trazar("exec \"" + cmd + "\"");
	}

	/*  De aca en mas, no bloqueante: Poll() no puede colgar el bucle de
	 *  eventos de la interfaz.
	 *
	 *  Y LAS DOS COSAS, no una sola. libssh2_session_set_blocking(0) le dice
	 *  a libssh2 que devuelva EAGAIN en vez de esperar, pero libssh2 lee con
	 *  recv() sobre este descriptor: si el descriptor sigue en modo
	 *  bloqueante, el recv() se cuelga adentro de libssh2 y el EAGAIN nunca
	 *  llega. El socket quedo bloqueante despues del connect con timeout --
	 *  ver ConectarTcp, que lo pone no bloqueante solo para eso y despues lo
	 *  deja como estaba --, asi que hay que volver a sacarselo aca.
	 *
	 *  El sintoma cuando falta: la sesion abre, el banner se dibuja, la
	 *  negociacion telnet se contesta entera... y despues la ventana se
	 *  congela. Todo eso pasa adentro del primer Poll(); el segundo
	 *  libssh2_channel_read se queda esperando en recv() y no vuelve nunca,
	 *  con el bucle de eventos de Qt adentro. Desde afuera parece que el
	 *  host dejo de contestar, que es justo lo que no pasa.              */
	libssh2_session_set_blocking(d.sesion, 0);

	if (!red::PonerNoBloqueante(d.fd, true))
	{
		*error = "no se pudo poner el socket en modo no bloqueante: "
		         + red::Explicar(red::UltimoError());
		Close();
		return false;
	}

	return true;
}

/* ------------------------------------------------------------------ */
/*  Datos                                                              */
/* ------------------------------------------------------------------ */

bool Ssh6530Transport::Poll(int esperaMs)
{
	Impl &d = *m_impl;
	if (d.canal == nullptr || !red::Valido(d.fd)) return false;

	if (esperaMs > 0) red::EsperarLectura(d.fd, esperaMs);

	char buf[4096];

	/*  El tipo lo pone libssh2 (ssize_t, que define el mismo para los
	 *  compiladores que no lo traen). Escribirlo a mano aca obligaria a
	 *  saber cual es en cada plataforma.                                 */
	for (;;)
	{
		const auto n = libssh2_channel_read(d.canal, buf, sizeof(buf));
		if (n > 0)
		{
			if (onRawFromHost) onRawFromHost(buf, (int)n);
			if (onHostData)    onHostData(buf, (int)n);
			continue;
		}
		if (n == LIBSSH2_ERROR_EAGAIN) break;
		if (n == 0) break;
		/* cualquier otro negativo es un error de verdad */
		if (onTrace) onTrace(ErrorDeSesion(d.sesion, "error al leer el canal"));
		d.cerrado = true;
		return false;
	}

	/*  El canal de errores: STN manda ahi algunos diagnosticos, y perderlos
	 *  seria justo perder lo que uno quiere leer cuando algo no anda.    */
	for (;;)
	{
		const auto n = libssh2_channel_read_stderr(d.canal, buf, sizeof(buf));
		if (n > 0)
		{
			if (onTrace) onTrace("[host stderr] " + std::string(buf, (size_t)n));
			continue;
		}
		break;
	}

	if (libssh2_channel_eof(d.canal))
	{
		if (onTrace) onTrace("el host cerro el canal");
		d.cerrado = true;
		return false;
	}
	return true;
}

void Ssh6530Transport::SendRaw(const byte *data, int len)
{
	Impl &d = *m_impl;
	if (d.canal == nullptr || data == nullptr || len <= 0) return;

	if (onRawToHost) onRawToHost((const char *)data, len);

	int escritos = 0;
	while (escritos < len)
	{
		const auto n = libssh2_channel_write(
			d.canal, (const char *)data + escritos, (size_t)(len - escritos));
		if (n > 0) { escritos += (int)n; continue; }
		if (n == LIBSSH2_ERROR_EAGAIN)
		{
			/*  El canal esta lleno. Se espera a que se pueda escribir en vez
			 *  de girar en vacio: son pocos bytes -- una tecla, una
			 *  secuencia AID -- y esto no pasa casi nunca.               */
			red::EsperarEscritura(d.fd, 50);
			continue;
		}
		if (onTrace) onTrace(ErrorDeSesion(d.sesion, "error al escribir"));
		d.cerrado = true;
		return;
	}
}

void Ssh6530Transport::Redimensionar(int columnas, int filas)
{
	Impl &d = *m_impl;
	if (d.canal == nullptr) return;
	d.politica.columnas = columnas;
	d.politica.filas    = filas;
	libssh2_channel_request_pty_size(d.canal, columnas, filas);
}

std::string Ssh6530Transport::Resumen() const
{
	const Impl &d = *m_impl;
	std::string s = "sesion ssh:\n";
	s += "  host .............. " + d.host + ":" + std::to_string(d.puerto) + "\n";
	s += "  usuario ........... " + d.politica.usuario + "\n";
	s += "  autenticacion ..... " + (d.metodoAuth.empty() ? "(ninguna)" : d.metodoAuth) + "\n";
	s += "  pty ............... " + d.politica.terminalType + " " +
	     std::to_string(d.politica.columnas) + "x" +
	     std::to_string(d.politica.filas) + "\n";
	s += "  comando ........... exec \"" + d.politica.comando + "\"\n";
	return s;
}

} // namespace ssh6530

#else   /* sin VT6530_CON_SSH: se compila un cuerpo que dice que no hay */

namespace ssh6530 {

bool Disponible() { return false; }

struct Ssh6530Transport::Impl { Politica politica; };

Ssh6530Transport::Ssh6530Transport(const Politica &p) : m_impl(new Impl{p}) {}
Ssh6530Transport::~Ssh6530Transport() { delete m_impl; }

bool Ssh6530Transport::Connect(const std::string &, int, std::string *error)
{
	if (error != nullptr)
	{
		*error = "este binario se compilo sin soporte ssh.\n"
		         "instale libssh2 (dnf install libssh2-devel, o en MSYS2\n"
		         "pacman -S mingw-w64-ucrt-x86_64-libssh2) y recompile.";
	}
	return false;
}

void Ssh6530Transport::Close() {}
bool Ssh6530Transport::IsOpen() const { return false; }
bool Ssh6530Transport::Poll(int) { return false; }
long long Ssh6530Transport::Descriptor() const { return -1; }
void Ssh6530Transport::SendRaw(const byte *, int) {}
void Ssh6530Transport::Redimensionar(int, int) {}
std::string Ssh6530Transport::Resumen() const
{
	return std::string("sesion ssh: no compilado\n");
}

} // namespace ssh6530

#endif
