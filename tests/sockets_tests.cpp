/*
 *  sockets_tests -- que net/Sockets.h compile por los dos caminos.
 *
 *  Este archivo se compila DOS veces: una nativa (la rama POSIX, que ademas
 *  se ejecuta) y otra con -D_WIN32 y las cabeceras falsas de
 *  tests/falso-win32 (la rama Windows, que solo se compila y se enlaza).
 *  Ver Makefile.portable, objetivo "sockets".
 *
 *  Lo que atrapa: un nombre mal escrito, un argumento de mas o de menos, un
 *  tipo que no convierte -- SOCKET truncado a int, socklen_t donde Winsock
 *  pide int *. Es la clase de error que si no, aparece recien cuando alguien
 *  abre MSYS2, que es cuando uno no esta mirando.
 *
 *  Lo que NO atrapa: la semantica. Que el fracaso de un connect no bloqueante
 *  salga por exceptfds en Winsock y por writefds en POSIX, o que select() con
 *  las tres listas vacias duerma en uno y devuelva WSAEINVAL en el otro, no
 *  lo puede comprobar ningun compilador. Eso sale de la documentacion y esta
 *  explicado en los comentarios de Sockets.h.
 */
#include "../net/Sockets.h"

#include <cstdio>
#include <string>

static int g_ok = 0;
static int g_mal = 0;

static void Comprobar(bool condicion, const char *que)
{
	if (condicion) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLO: %s\n", que);
}

/*  Referencia a cada funcion del shim, para que el compilador tenga que
 *  instanciarlas todas aunque sean inline. Sin esto, una rama con un error de
 *  tipos podria no mirarse nunca.                                          */
static void TocarTodo()
{
	const red::Descriptor d = red::SinDescriptor();

	(void)red::Valido(d);
	(void)red::ComoEntero(d);
	(void)red::UltimoError();
	(void)red::Explicar(0);
	(void)red::ExplicarResolucion(0);
	(void)red::ConexionEnCurso(0);
	(void)red::CarpetaDelUsuario();

	/*  Estas cuatro tocan el descriptor de verdad, asi que solo se compilan:
	 *  llamarlas con uno invalido no tendria sentido. El if nunca es cierto
	 *  -- SinDescriptor() es invalido por definicion -- pero el compilador
	 *  igual tiene que generarlas.                                        */
	if (red::Valido(d))
	{
		red::Cerrar(d);
		red::PonerNoBloqueante(d, true);
		red::PonerSinRetardo(d);
		(void)red::EsperarLectura(d, 0);
		(void)red::EsperarEscritura(d, 0);
		int motivo = 0;
		(void)red::EsperarConexion(d, 0, &motivo);
	}
}

int main()
{
	std::printf("\nShim de sockets -- pruebas\n");
	std::printf("==========================\n\n");

	TocarTodo();

	/*  El descriptor invalido no es valido, y su forma entera es -1. Esto
	 *  importa de verdad: en Windows INVALID_SOCKET es (SOCKET)~0, que como
	 *  entero sin signo es enorme. Si ComoEntero lo dejara pasar tal cual,
	 *  Ssh6530Session le colgaria un QSocketNotifier a un numero absurdo en
	 *  vez de darse cuenta de que no hay conexion.                        */
	Comprobar(!red::Valido(red::SinDescriptor()),
	          "el descriptor vacio no es valido");
	Comprobar(red::ComoEntero(red::SinDescriptor()) == -1,
	          "el descriptor vacio da -1 como entero");
	std::printf("           el descriptor vacio da -1, no (SOCKET)~0\n");

	std::string error;
	Comprobar(red::Iniciar(&error), "la biblioteca de sockets arranca");
	Comprobar(red::Iniciar(&error), "arrancarla dos veces tambien da bien");
	std::printf("           arranca, y arrancar dos veces no molesta\n");

	Comprobar(!red::Explicar(0).empty(), "Explicar() siempre dice algo");
	std::printf("           Explicar(0): %s\n", red::Explicar(0).c_str());

	/*  Dormir tiene que dormir de verdad, no volver al instante. Es lo que
	 *  espacia los reintentos de Close(); si vuelve enseguida, los veinte
	 *  intentos pasan en un suspiro y channel_free nunca llega a liberar. */
	red::Dormir(20);
	std::printf("           Dormir(20) volvio\n");

	Comprobar(!red::CarpetaDelUsuario().empty() ||
	          std::getenv("HOME") == nullptr,
	          "la carpeta del usuario sale, o no habia de donde sacarla");
	std::printf("           carpeta del usuario: %s\n",
	            red::CarpetaDelUsuario().empty()
	                ? "(no se pudo averiguar)"
	                : red::CarpetaDelUsuario().c_str());

	std::printf("\n---------------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_ok);
	std::printf("  comprobaciones fallidas . %d\n", g_mal);
	std::printf("---------------------------\n\n");

	return g_mal == 0 ? 0 : 1;
}
