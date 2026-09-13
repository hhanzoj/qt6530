/*
 *  winsock2.h FALSO -- no es Windows, es lo justo para que el compilador
 *  revise la rama _WIN32 de net/Sockets.h.
 *
 *  QUE PRUEBA Y QUE NO
 *
 *  Prueba nombres, aridad, tipos de argumento y de retorno, y constantes: los
 *  errores que de otro modo solo aparecerian el dia que alguien compile en
 *  MSYS2, que es justo el dia en que uno no esta mirando. NO prueba la
 *  semantica de Winsock -- que el fracaso del connect salga por exceptfds, que
 *  select() con las listas vacias devuelva WSAEINVAL -- porque eso no lo
 *  puede probar ningun compilador; sale de la documentacion.
 *
 *  Las firmas estan copiadas de la documentacion de Microsoft. Si alguna
 *  difiere de la real, esta prueba miente, asi que vale la pena que la mire
 *  quien tenga un Windows a mano.
 */
#ifndef _falso_winsock2_h
#define _falso_winsock2_h

#include <stddef.h>

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned long  u_long;
typedef char          *LPSTR;
typedef void          *HLOCAL;
typedef unsigned long long UINT_PTR;

typedef UINT_PTR SOCKET;

#define INVALID_SOCKET  ((SOCKET)(~0))
#define SOCKET_ERROR    (-1)

#define MAKEWORD(a, b) ((WORD)(((unsigned char)(a)) | (((WORD)((unsigned char)(b))) << 8)))

typedef struct WSAData
{
	WORD wVersion;
	WORD wHighVersion;
	char szDescription[257];
	char szSystemStatus[129];
} WSADATA;

int  WSAStartup(WORD version, WSADATA *datos);
int  WSACleanup(void);
int  WSAGetLastError(void);

#define WSAEWOULDBLOCK  10035
#define WSAEINPROGRESS  10036
#define WSAEINVAL       10022

/* --- sockets --- */

#define AF_UNSPEC     0
#define SOCK_STREAM   1
#define SOL_SOCKET    0xffff
#define SO_ERROR      0x1007
#define IPPROTO_TCP   6
#define TCP_NODELAY   0x0001
#define FIONBIO       0x8004667e

struct sockaddr;

SOCKET socket(int familia, int tipo, int protocolo);
int    connect(SOCKET s, const struct sockaddr *nombre, int largo);
int    closesocket(SOCKET s);
int    ioctlsocket(SOCKET s, long orden, u_long *arg);
int    setsockopt(SOCKET s, int nivel, int opcion, const char *valor, int largo);
int    getsockopt(SOCKET s, int nivel, int opcion, char *valor, int *largo);

/* --- select --- */

/*  fd_set y timeval se toman del sistema anfitrion en vez de declararse aca.
 *
 *  No es pereza: cualquier cabecera de la biblioteca estandar -- <cstdlib> le
 *  alcanza -- termina arrastrando <sys/select.h>, y dos definiciones del
 *  mismo struct no compilan. La contra es que el layout es el de POSIX y no
 *  el de Winsock; no importa, porque lo que esta prueba mira es la INTERFAZ:
 *  que se le pase un SOCKET y no un int, que select reciba las tres listas.
 *
 *  Las macros si se reemplazan. Las de POSIX indexan un mapa de bits por
 *  numero de descriptor, y un SOCKET de Windows es un puntero disfrazado: un
 *  FD_SET de verdad con ese valor se iria del arreglo. Las de aca solo
 *  comprueban el tipo y no guardan nada.                                  */
#include <sys/select.h>

#undef FD_ZERO
#undef FD_SET

void FalsoFdZero(fd_set *c);
void FalsoFdSet(SOCKET s, fd_set *c);

#define FD_ZERO(c)     FalsoFdZero(c)
#define FD_SET(s, c)   FalsoFdSet((s), (c))

int select(int cuantos, fd_set *lectura, fd_set *escritura, fd_set *fallo,
           const struct timeval *plazo);

#endif
