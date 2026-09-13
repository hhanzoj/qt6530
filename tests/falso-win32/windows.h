/*  windows.h FALSO. Ver el comentario de winsock2.h. */
#ifndef _falso_windows_h
#define _falso_windows_h

#include <winsock2.h>

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000

#define LANG_NEUTRAL     0x00
#define SUBLANG_DEFAULT  0x01
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))

DWORD  FormatMessageA(DWORD banderas, const void *origen, DWORD mensaje,
                      DWORD idioma, LPSTR destino, DWORD tamano,
                      void *argumentos);
HLOCAL LocalFree(HLOCAL memoria);
void   Sleep(DWORD ms);

#define ERROR_ALREADY_EXISTS 183

int   CreateDirectoryA(const char *ruta, void *seguridad);
DWORD GetLastError(void);

/* --- la consola: apagar el eco para leer una clave --- */

typedef void *HANDLE;

#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#define STD_INPUT_HANDLE     ((DWORD)-10)

#define ENABLE_ECHO_INPUT    0x0004
#define ENABLE_LINE_INPUT    0x0002

HANDLE GetStdHandle(DWORD cual);
int    GetConsoleMode(HANDLE h, DWORD *modo);
int    SetConsoleMode(HANDLE h, DWORD modo);

#endif
