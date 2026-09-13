/*
 *  Sockets.h -- la unica parte del transporte que sabe si esto es Windows.
 *
 *  POR QUE EXISTE
 *
 *  Ssh6530Transport estaba escrito contra POSIX de punta a punta: fcntl,
 *  close, errno, strerror, select. Sobre Winsock nada de eso existe con ese
 *  nombre, y dos cosas ademas no se traducen sino que cambian de forma:
 *
 *    - El fracaso de un connect no bloqueante. En POSIX el descriptor sale
 *      escribible y uno pregunta por SO_ERROR. En Winsock sale por exceptfds
 *      y si uno no lo mira, el select vence por plazo y el error real se
 *      pierde.
 *    - select() sin descriptores. En POSIX es la forma corta de dormir; en
 *      Winsock devuelve WSAEINVAL al instante. Close() lo usaba para espaciar
 *      los reintentos de channel_free: sobre Windows habria girado en vacio
 *      veinte veces sin esperar nada.
 *
 *  Asi que esto no es una capa de compatibilidad por prolijidad: son dos
 *  defectos reales que solo aparecen del otro lado.
 *
 *  Va todo inline y no tiene .cpp a proposito, para no tocar dos sistemas de
 *  compilacion por cuatro funciones. Lo unico que hay que ajustar en la linea
 *  de compilacion es enlazar ws2_32 en Windows.
 *
 *  AVISO
 *
 *  La rama Windows no se pudo compilar con un compilador de Windows en el
 *  entorno donde se escribio -- el mirror bloquea mingw-w64, igual que libssh2
 *  y Qt --. Esta compilada contra cabeceras falsas (tests/falso-win32), que
 *  verifican nombres, aridad y tipos, no la semantica. Lo de arriba sale de la
 *  documentacion de Winsock, no de una corrida.
 */
#ifndef _vt6530_sockets_h
#define _vt6530_sockets_h

#include <cstdlib>
#include <string>

#if defined(_WIN32)

/*  El orden importa: winsock2.h antes que windows.h, o windows.h arrastra el
 *  winsock.h de la version 1 y los dos chocan.                             */
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>

#else

#  include <arpa/inet.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <string.h>
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>

#endif

namespace red {

/*  En Windows un socket es un SOCKET, que es un UINT_PTR: 64 bits en x64 y
 *  no entra en un int. Por eso no alcanza con typedef int y hay que llevar
 *  el tipo hasta arriba.                                                   */
#if defined(_WIN32)
typedef SOCKET Descriptor;
#else
typedef int Descriptor;
#endif

inline Descriptor SinDescriptor()
{
#if defined(_WIN32)
	return INVALID_SOCKET;
#else
	return -1;
#endif
}

inline bool Valido(Descriptor d)
{
#if defined(_WIN32)
	return d != INVALID_SOCKET;
#else
	return d >= 0;
#endif
}

/** El descriptor como entero con signo, para QSocketNotifier (qintptr) y para
 *  cualquier cabecera publica que no quiera incluir winsock. */
inline long long ComoEntero(Descriptor d)
{
	return Valido(d) ? (long long)d : -1LL;
}

/**
 *  Arranca la biblioteca de sockets. En POSIX no hay nada que arrancar.
 *
 *  Idempotente y una sola vez por proceso: el static de una funcion inline es
 *  uno solo para todo el programa, aunque la incluyan diez traducciones.
 */
inline bool Iniciar(std::string *error)
{
#if defined(_WIN32)
	static bool hecho = false;
	static bool salioBien = false;
	static std::string motivo;

	if (!hecho)
	{
		hecho = true;
		WSADATA datos;
		const int rc = ::WSAStartup(MAKEWORD(2, 2), &datos);
		salioBien = (rc == 0);
		if (!salioBien)
			motivo = "no se pudo arrancar winsock: WSAStartup dio " +
			         std::to_string(rc);
	}
	if (!salioBien && error != nullptr) *error = motivo;
	return salioBien;
#else
	(void)error;
	return true;
#endif
}

inline void Cerrar(Descriptor d)
{
	if (!Valido(d)) return;
#if defined(_WIN32)
	::closesocket(d);
#else
	::close(d);
#endif
}

inline int UltimoError()
{
#if defined(_WIN32)
	return ::WSAGetLastError();
#else
	return errno;
#endif
}

inline std::string Explicar(int codigo)
{
#if defined(_WIN32)
	char *texto = nullptr;
	const DWORD n = ::FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, (DWORD)codigo,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&texto, 0, nullptr);

	if (n == 0 || texto == nullptr) return "error " + std::to_string(codigo);

	std::string r(texto, (size_t)n);
	::LocalFree(texto);
	while (!r.empty() &&
	       (r[r.size() - 1] == '\n' || r[r.size() - 1] == '\r' ||
	        r[r.size() - 1] == ' '))
	{
		r.erase(r.size() - 1);
	}
	return r;
#else
	return std::string(::strerror(codigo));
#endif
}

/** getaddrinfo no usa la misma numeracion que el resto en POSIX; en Winsock
 *  si, asi que del otro lado es el mismo Explicar(). */
inline std::string ExplicarResolucion(int codigo)
{
#if defined(_WIN32)
	return Explicar(codigo);
#else
	return std::string(::gai_strerror(codigo));
#endif
}

/** true si el connect que acaba de fallar en realidad esta en curso. */
inline bool ConexionEnCurso(int codigo)
{
#if defined(_WIN32)
	return codigo == WSAEWOULDBLOCK || codigo == WSAEINPROGRESS;
#else
	return codigo == EINPROGRESS;
#endif
}

inline bool PonerNoBloqueante(Descriptor d, bool si)
{
#if defined(_WIN32)
	/*  Winsock no deja preguntar el modo actual, solo ponerlo. No importa:
	 *  aca siempre se pone explicitamente uno de los dos.                 */
	u_long modo = si ? 1 : 0;
	return ::ioctlsocket(d, FIONBIO, &modo) == 0;
#else
	const int b = ::fcntl(d, F_GETFL, 0);
	if (b < 0) return false;
	const int nuevo = si ? (b | O_NONBLOCK) : (b & ~O_NONBLOCK);
	return ::fcntl(d, F_SETFL, nuevo) == 0;
#endif
}

inline void PonerSinRetardo(Descriptor d)
{
	const int uno = 1;
#if defined(_WIN32)
	::setsockopt(d, IPPROTO_TCP, TCP_NODELAY, (const char *)&uno, sizeof(uno));
#else
	::setsockopt(d, IPPROTO_TCP, TCP_NODELAY, &uno, sizeof(uno));
#endif
}

/**
 *  Duerme sin mirar ningun descriptor.
 *
 *  En POSIX esto es select() con las tres listas vacias, que es lo que hacia
 *  el codigo de antes. En Winsock ese mismo select devuelve WSAEINVAL sin
 *  esperar: hay que usar Sleep(), o los reintentos de Close() giran en vacio.
 */
inline void Dormir(int ms)
{
	if (ms <= 0) return;
#if defined(_WIN32)
	::Sleep((DWORD)ms);
#else
	struct timeval tv;
	tv.tv_sec  = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	::select(0, nullptr, nullptr, nullptr, &tv);
#endif
}

namespace detalle {

/** El primer argumento de select(). Winsock lo ignora, y encima un SOCKET no
 *  entra en el int que pide la firma. */
inline int Cuantos(Descriptor d)
{
#if defined(_WIN32)
	(void)d;
	return 0;
#else
	return (int)d + 1;
#endif
}

inline struct timeval Plazo(int ms)
{
	struct timeval tv;
	tv.tv_sec  = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	return tv;
}

} // namespace detalle

/** Espera hasta ms a que haya algo para leer. Devuelve lo mismo que select. */
inline int EsperarLectura(Descriptor d, int ms)
{
	fd_set listo;
	FD_ZERO(&listo);
	FD_SET(d, &listo);
	struct timeval tv = detalle::Plazo(ms);
	return ::select(detalle::Cuantos(d), &listo, nullptr, nullptr, &tv);
}

/** Espera hasta ms a que se pueda escribir. Devuelve lo mismo que select. */
inline int EsperarEscritura(Descriptor d, int ms)
{
	fd_set listo;
	FD_ZERO(&listo);
	FD_SET(d, &listo);
	struct timeval tv = detalle::Plazo(ms);
	return ::select(detalle::Cuantos(d), nullptr, &listo, nullptr, &tv);
}

/**
 *  Espera a que termine un connect no bloqueante.
 *
 *  Devuelve 0 si conecto, 1 si vencio el plazo, -1 si fallo (y deja el motivo
 *  en *codigo).
 *
 *  Los dos sistemas avisan el fracaso de distinta manera -- POSIX deja el
 *  descriptor escribible, Winsock lo pone en exceptfds -- asi que se miran las
 *  dos listas y despues se pregunta por SO_ERROR, que sirve para los dos.
 */
inline int EsperarConexion(Descriptor d, int ms, int *codigo)
{
	int basura = 0;
	if (codigo == nullptr) codigo = &basura;

	fd_set escribible;
	FD_ZERO(&escribible);
	FD_SET(d, &escribible);

	fd_set fallado;
	FD_ZERO(&fallado);
	FD_SET(d, &fallado);

	struct timeval tv = detalle::Plazo(ms);
	const int r = ::select(detalle::Cuantos(d), nullptr, &escribible,
	                       &fallado, &tv);

	if (r == 0) return 1;
	if (r < 0) { *codigo = UltimoError(); return -1; }

	int err = 0;
#if defined(_WIN32)
	int largo = (int)sizeof(err);
	if (::getsockopt(d, SOL_SOCKET, SO_ERROR, (char *)&err, &largo) != 0)
		err = UltimoError();
#else
	socklen_t largo = sizeof(err);
	if (::getsockopt(d, SOL_SOCKET, SO_ERROR, &err, &largo) != 0)
		err = errno;
#endif

	if (err != 0) { *codigo = err; return -1; }
	return 0;
}

/**
 *  Crea una carpeta si no existe. true si al terminar la carpeta esta.
 *
 *  Esto no tiene nada que ver con sockets, y vive aca igual para no repartir
 *  los #ifdef _WIN32 por todo el proyecto: mientras sean dos o tres cosas,
 *  que esten juntas vale mas que el nombre del archivo.
 *
 *  Crea un solo nivel, que es lo que hace falta (~/.ssh). El modo 0700 es a
 *  proposito: es una carpeta con claves adentro, y ssh se niega a usarla si
 *  esta abierta. En Windows el permiso lo hereda del padre.
 */
inline bool CrearCarpeta(const std::string &ruta)
{
	if (ruta.empty()) return false;
#if defined(_WIN32)
	if (::CreateDirectoryA(ruta.c_str(), nullptr)) return true;
	return ::GetLastError() == ERROR_ALREADY_EXISTS;
#else
	if (::mkdir(ruta.c_str(), 0700) == 0) return true;
	return errno == EEXIST;
#endif
}

/** La carpeta del usuario: HOME en POSIX, USERPROFILE en Windows. Vacia si no
 *  se pudo averiguar. */
inline std::string CarpetaDelUsuario()
{
#if defined(_WIN32)
	const char *casa = ::getenv("USERPROFILE");
	if (casa != nullptr && *casa != '\0') return std::string(casa);

	/*  En MSYS2 y en Git Bash puede venir HOME y no USERPROFILE. */
	casa = ::getenv("HOME");
	if (casa != nullptr && *casa != '\0') return std::string(casa);

	const char *unidad = ::getenv("HOMEDRIVE");
	const char *resto  = ::getenv("HOMEPATH");
	if (unidad != nullptr && resto != nullptr)
		return std::string(unidad) + resto;

	return std::string();
#else
	const char *casa = ::getenv("HOME");
	if (casa == nullptr || *casa == '\0') return std::string();
	return std::string(casa);
#endif
}

} // namespace red

#endif
