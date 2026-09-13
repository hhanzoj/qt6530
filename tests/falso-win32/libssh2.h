/*
 *  libssh2.h FALSO -- para que el cuerpo real de net/Ssh6530Transport.cpp
 *  pase por el compilador aunque no haya libssh2 instalada.
 *
 *  POR QUE
 *
 *  Todo el camino VT6530_CON_SSH estuvo desde el primer dia "verificado por
 *  lectura": el mirror de paquetes del entorno donde se escribio bloquea
 *  libssh2, asi que ese archivo nunca lo miro un compilador. Con esto si,
 *  y de paso queda cubierta la rama Windows del mismo archivo.
 *
 *  QUE PRUEBA Y QUE NO
 *
 *  Prueba que el archivo compile: nombres, cantidad de argumentos, tipos que
 *  convierten, macros que expanden a algo valido. Encontro cosas de verdad la
 *  primera vez que se corrio.
 *
 *  NO prueba nada de lo que hace libssh2, obviamente, y hay una trampa mas
 *  seria: si alguna firma de aca no coincide con la de la libssh2 de verdad,
 *  esta prueba dice que si y el compilador de MSYS2 dice que no. Estan
 *  copiadas de libssh2.h 1.11, incluidas las que alli son macros sobre las
 *  variantes _ex -- eso importa, porque una macro expande argumentos y es
 *  justo donde se cuela un parentesis de menos.
 *
 *  O sea: esto no reemplaza compilar contra la libssh2 posta. Adelanta el
 *  momento en que se descubre el error, que es distinto.
 */
#ifndef _falso_libssh2_h
#define _falso_libssh2_h

#include <stddef.h>
#include <string.h>

#if defined(_WIN32)
#  include <winsock2.h>
typedef SOCKET        libssh2_socket_t;
typedef long long     ssize_t_falso;
#  define libssh2_ssize_t ssize_t_falso
#else
#  include <sys/types.h>
typedef int           libssh2_socket_t;
#  define libssh2_ssize_t ssize_t
#endif

typedef struct _LIBSSH2_SESSION    LIBSSH2_SESSION;
typedef struct _LIBSSH2_CHANNEL    LIBSSH2_CHANNEL;
typedef struct _LIBSSH2_KNOWNHOSTS LIBSSH2_KNOWNHOSTS;
typedef struct _LIBSSH2_AGENT      LIBSSH2_AGENT;

struct libssh2_agent_publickey
{
	unsigned int   magic;
	void          *node;
	unsigned char *blob;
	size_t         blob_len;
	char          *comment;
};

struct libssh2_knownhost
{
	unsigned int magic;
	void        *node;
	char        *name;
	char        *key;
	int          typemask;
};

#define LIBSSH2_ERROR_EAGAIN            (-37)

/*  Los que distinguen "esa clave no sirve" de "el host se fue". */
#define LIBSSH2_ERROR_SOCKET_SEND       (-7)
#define LIBSSH2_ERROR_SOCKET_DISCONNECT (-13)
#define LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED (-19)
#define LIBSSH2_ERROR_AUTHENTICATION_FAILED LIBSSH2_ERROR_PUBLICKEY_UNVERIFIED
#define LIBSSH2_ERROR_SOCKET_TIMEOUT    (-30)
#define LIBSSH2_ERROR_SOCKET_RECV       (-43)

#define LIBSSH2_HOSTKEY_HASH_MD5        1
#define LIBSSH2_HOSTKEY_HASH_SHA1       2
#define LIBSSH2_HOSTKEY_HASH_SHA256     3

#define LIBSSH2_HOSTKEY_TYPE_UNKNOWN    0
#define LIBSSH2_HOSTKEY_TYPE_RSA        1
#define LIBSSH2_HOSTKEY_TYPE_DSS        2
#define LIBSSH2_HOSTKEY_TYPE_ECDSA_256  3
#define LIBSSH2_HOSTKEY_TYPE_ECDSA_384  4
#define LIBSSH2_HOSTKEY_TYPE_ECDSA_521  5
#define LIBSSH2_HOSTKEY_TYPE_ED25519    6

#define LIBSSH2_KNOWNHOST_FILE_OPENSSH  1
#define LIBSSH2_KNOWNHOST_TYPE_PLAIN    1
#define LIBSSH2_KNOWNHOST_KEYENC_RAW    (1 << 16)
#define LIBSSH2_KNOWNHOST_CHECK_MATCH    0
#define LIBSSH2_KNOWNHOST_CHECK_MISMATCH 1
#define LIBSSH2_KNOWNHOST_CHECK_NOTFOUND 2
#define LIBSSH2_KNOWNHOST_CHECK_FAILURE  3

#define LIBSSH2_CHANNEL_WINDOW_DEFAULT  (2 * 1024 * 1024)
#define LIBSSH2_CHANNEL_PACKET_DEFAULT  32768
#define SSH_DISCONNECT_BY_APPLICATION   11
#define SSH_EXTENDED_DATA_STDERR        1

int  libssh2_init(int banderas);
void libssh2_exit(void);

LIBSSH2_SESSION *libssh2_session_init_ex(void *(*mialloc)(size_t, void **),
                                         void (*mifree)(void *, void **),
                                         void *(*mirealloc)(void *, size_t, void **),
                                         void *abstract);
#define libssh2_session_init() libssh2_session_init_ex(0, 0, 0, 0)

void libssh2_session_set_blocking(LIBSSH2_SESSION *sesion, int bloqueante);
int  libssh2_session_handshake(LIBSSH2_SESSION *sesion, libssh2_socket_t s);
int  libssh2_session_last_error(LIBSSH2_SESSION *sesion, char **mensaje,
                                int *largo, int quierebuf);
int  libssh2_session_free(LIBSSH2_SESSION *sesion);
int  libssh2_session_disconnect_ex(LIBSSH2_SESSION *sesion, int motivo,
                                   const char *descripcion, const char *idioma);
#define libssh2_session_disconnect(s, d) \
	libssh2_session_disconnect_ex((s), SSH_DISCONNECT_BY_APPLICATION, (d), "")

const char *libssh2_session_hostkey(LIBSSH2_SESSION *sesion, size_t *largo,
                                    int *tipo);
const char *libssh2_hostkey_hash(LIBSSH2_SESSION *sesion, int tipo);

LIBSSH2_KNOWNHOSTS *libssh2_knownhost_init(LIBSSH2_SESSION *sesion);
int  libssh2_knownhost_readfile(LIBSSH2_KNOWNHOSTS *kh, const char *archivo,
                                int tipo);
int  libssh2_knownhost_checkp(LIBSSH2_KNOWNHOSTS *kh, const char *host,
                              int puerto, const char *clave, size_t largo,
                              int mascara, struct libssh2_knownhost **coincide);
void libssh2_knownhost_free(LIBSSH2_KNOWNHOSTS *kh);

int libssh2_userauth_password_ex(LIBSSH2_SESSION *sesion, const char *usuario,
                                 unsigned int largoUsuario, const char *clave,
                                 unsigned int largoClave,
                                 void (*cambiar)(LIBSSH2_SESSION *, char **, int *));
#define libssh2_userauth_password(s, u, c) \
	libssh2_userauth_password_ex((s), (u), (unsigned int)strlen(u), \
	                             (c), (unsigned int)strlen(c), 0)

int libssh2_userauth_publickey_fromfile_ex(LIBSSH2_SESSION *sesion,
                                           const char *usuario,
                                           unsigned int largoUsuario,
                                           const char *publica,
                                           const char *privada,
                                           const char *frase);
#define libssh2_userauth_publickey_fromfile(s, u, pu, pr, f) \
	libssh2_userauth_publickey_fromfile_ex((s), (u), (unsigned int)strlen(u), \
	                                       (pu), (pr), (f))

char *libssh2_userauth_list(LIBSSH2_SESSION *sesion, const char *usuario,
                            unsigned int largo);
int   libssh2_userauth_authenticated(LIBSSH2_SESSION *sesion);

LIBSSH2_AGENT *libssh2_agent_init(LIBSSH2_SESSION *sesion);
int  libssh2_agent_connect(LIBSSH2_AGENT *agente);
int  libssh2_agent_list_identities(LIBSSH2_AGENT *agente);
int  libssh2_agent_get_identity(LIBSSH2_AGENT *agente,
                                struct libssh2_agent_publickey **cual,
                                struct libssh2_agent_publickey *previa);
int  libssh2_agent_userauth(LIBSSH2_AGENT *agente, const char *usuario,
                            struct libssh2_agent_publickey *identidad);
int  libssh2_agent_disconnect(LIBSSH2_AGENT *agente);
void libssh2_agent_free(LIBSSH2_AGENT *agente);

LIBSSH2_CHANNEL *libssh2_channel_open_ex(LIBSSH2_SESSION *sesion,
                                         const char *tipo,
                                         unsigned int largoTipo,
                                         unsigned int ventana,
                                         unsigned int paquete,
                                         const char *mensaje,
                                         unsigned int largoMensaje);
#define libssh2_channel_open_session(s) \
	libssh2_channel_open_ex((s), "session", sizeof("session") - 1, \
	                        LIBSSH2_CHANNEL_WINDOW_DEFAULT, \
	                        LIBSSH2_CHANNEL_PACKET_DEFAULT, 0, 0)

int libssh2_channel_request_pty_ex(LIBSSH2_CHANNEL *canal, const char *term,
                                   unsigned int largoTerm, const char *modos,
                                   unsigned int largoModos, int ancho,
                                   int alto, int anchoPx, int altoPx);

int libssh2_channel_request_pty_size_ex(LIBSSH2_CHANNEL *canal, int ancho,
                                        int alto, int anchoPx, int altoPx);
#define libssh2_channel_request_pty_size(c, a, l) \
	libssh2_channel_request_pty_size_ex((c), (a), (l), 0, 0)

int libssh2_channel_process_startup(LIBSSH2_CHANNEL *canal, const char *peticion,
                                    unsigned int largoPeticion,
                                    const char *mensaje,
                                    unsigned int largoMensaje);
#define libssh2_channel_exec(c, cmd) \
	libssh2_channel_process_startup((c), "exec", sizeof("exec") - 1, \
	                                (cmd), (unsigned int)strlen(cmd))
#define libssh2_channel_shell(c) \
	libssh2_channel_process_startup((c), "shell", sizeof("shell") - 1, 0, 0)

libssh2_ssize_t libssh2_channel_read_ex(LIBSSH2_CHANNEL *canal, int flujo,
                                        char *buf, size_t largo);
#define libssh2_channel_read(c, b, l)        libssh2_channel_read_ex((c), 0, (b), (l))
#define libssh2_channel_read_stderr(c, b, l) \
	libssh2_channel_read_ex((c), SSH_EXTENDED_DATA_STDERR, (b), (l))

libssh2_ssize_t libssh2_channel_write_ex(LIBSSH2_CHANNEL *canal, int flujo,
                                         const char *buf, size_t largo);
#define libssh2_channel_write(c, b, l) libssh2_channel_write_ex((c), 0, (b), (l))

int libssh2_channel_eof(LIBSSH2_CHANNEL *canal);
int libssh2_channel_free(LIBSSH2_CHANNEL *canal);

#endif
