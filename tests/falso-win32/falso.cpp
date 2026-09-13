/*
 *  Los cuerpos de las cabeceras falsas de Windows.
 *
 *  Alcanza para que la rama _WIN32 de net/Sockets.h enlace y CORRA, que es un
 *  poco mas que compilar: se ejecuta de verdad el camino de Windows de
 *  Iniciar(), Explicar(), Dormir() y CarpetaDelUsuario(). Lo que toca el
 *  socket queda en compilar y enlazar, que es todo lo que se puede hacer sin
 *  un Windows.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int g_ultimoError = 0;

int WSAStartup(WORD, WSADATA *datos)
{
	if (datos != 0)
	{
		memset(datos, 0, sizeof(*datos));
		datos->wVersion = MAKEWORD(2, 2);
		strcpy(datos->szDescription, "winsock falso");
	}
	return 0;
}

int WSACleanup(void)      { return 0; }
int WSAGetLastError(void) { return g_ultimoError; }

SOCKET socket(int, int, int)                              { return INVALID_SOCKET; }
int    connect(SOCKET, const struct sockaddr *, int)      { return SOCKET_ERROR; }
int    closesocket(SOCKET)                                { return 0; }
int    ioctlsocket(SOCKET, long, u_long *)                { return 0; }
int    setsockopt(SOCKET, int, int, const char *, int)    { return 0; }

int getsockopt(SOCKET, int, int, char *valor, int *largo)
{
	if (valor != 0 && largo != 0 && *largo >= (int)sizeof(int))
		*(int *)valor = 0;
	return 0;
}

/*  No guardan nada: solo existen para que el compilador tenga que convertir
 *  lo que se les pasa a SOCKET y a fd_set *. Ver winsock2.h.            */
void FalsoFdZero(fd_set *) {}
void FalsoFdSet(SOCKET, fd_set *) {}

int select(int, fd_set *, fd_set *, fd_set *, const struct timeval *)
{
	return 0;   /* plazo vencido: es lo que pasaria sin nadie del otro lado */
}

DWORD FormatMessageA(DWORD, const void *, DWORD mensaje, DWORD,
                     LPSTR destino, DWORD, void *)
{
	/*  El de verdad reserva con LocalAlloc cuando se le pide
	 *  ALLOCATE_BUFFER, y quien llama libera con LocalFree. Se imita eso
	 *  para que la pareja reservar/liberar quede ejercitada.             */
	char texto[64];
	const int n = snprintf(texto, sizeof(texto),
	                       "error falso %lu\r\n", (unsigned long)mensaje);
	char *buf = (char *)malloc((size_t)n + 1);
	if (buf == 0) return 0;
	memcpy(buf, texto, (size_t)n + 1);
	*(char **)destino = buf;
	return (DWORD)n;
}

HLOCAL LocalFree(HLOCAL memoria)
{
	free(memoria);
	return 0;
}

/*  Crea la carpeta de verdad, para que la prueba de KnownHosts ejercite el
 *  camino completo tambien del lado Windows.                             */
int CreateDirectoryA(const char *ruta, void *)
{
	if (mkdir(ruta, 0700) == 0) return 1;
	g_ultimoError = (errno == EEXIST) ? ERROR_ALREADY_EXISTS : 5;
	return 0;
}

DWORD GetLastError(void) { return (DWORD)g_ultimoError; }

/*  La consola falsa acepta que le apaguen y le prendan el eco, y anota el
 *  modo para que se pueda comprobar que quedo como estaba.              */
static DWORD g_modoConsola = ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT;

HANDLE GetStdHandle(DWORD) { return (HANDLE)1; }

int GetConsoleMode(HANDLE h, DWORD *modo)
{
	if (h == INVALID_HANDLE_VALUE || modo == 0) return 0;
	*modo = g_modoConsola;
	return 1;
}

int SetConsoleMode(HANDLE h, DWORD modo)
{
	if (h == INVALID_HANDLE_VALUE) return 0;
	g_modoConsola = modo;
	return 1;
}

/*  Solo para la prueba: saber si el eco quedo prendido al final. */
int FalsoEcoPrendido(void)
{
	return (g_modoConsola & ENABLE_ECHO_INPUT) != 0;
}

void Sleep(DWORD ms)
{
	struct timespec t;
	t.tv_sec  = (time_t)(ms / 1000);
	t.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&t, 0);
}

int getaddrinfo(const char *, const char *, const struct addrinfo *,
                struct addrinfo **resultado)
{
	if (resultado != 0) *resultado = 0;
	return 1;
}

void freeaddrinfo(struct addrinfo *) {}
