/*  ws2tcpip.h FALSO. Ver el comentario de winsock2.h. */
#ifndef _falso_ws2tcpip_h
#define _falso_ws2tcpip_h

#include <winsock2.h>

struct addrinfo
{
	int              ai_flags;
	int              ai_family;
	int              ai_socktype;
	int              ai_protocol;
	size_t           ai_addrlen;
	char            *ai_canonname;
	struct sockaddr *ai_addr;
	struct addrinfo *ai_next;
};

int  getaddrinfo(const char *nodo, const char *servicio,
                 const struct addrinfo *pistas, struct addrinfo **resultado);
void freeaddrinfo(struct addrinfo *lista);

#endif
