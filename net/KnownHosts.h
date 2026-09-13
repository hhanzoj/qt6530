/*
 *  KnownHosts -- anotar la clave de un host en ~/.ssh/known_hosts.
 *
 *  POR QUE NO LO HACE LIBSSH2
 *
 *  libssh2 tiene libssh2_knownhost_writefile, y es una linea. El problema es
 *  que REESCRIBE el archivo entero a partir de lo que su parser entendio: las
 *  lineas que no reconoce -- @cert-authority, @revoked, tipos de clave que su
 *  version no soporta -- no vuelven a salir. Y este archivo no es nuestro: es
 *  el mismo que usa el ssh del sistema. Perderle una entrada a alguien por
 *  registrar la nuestra seria un precio absurdo.
 *
 *  Asi que la clave se AGREGA, en modo append, y no se toca una sola linea de
 *  las que ya estaban. El costo es armar el base64 a mano, que son veinte
 *  lineas.
 *
 *  Para comprobar se sigue usando libssh2 (libssh2_knownhost_checkp), que
 *  para leer esta perfecto: si no entiende una linea, la ignora y a lo sumo
 *  nos dice "no lo conozco". Leer de menos es seguro; escribir de menos no.
 *
 *  Todo esto no depende de libssh2 ni de Qt a proposito: es la parte que
 *  puede salir mal en silencio -- un base64 mal armado deja una linea que ssh
 *  no va a reconocer nunca -- asi que tiene sus propias pruebas.
 */
#ifndef _vt6530_known_hosts_h
#define _vt6530_known_hosts_h

#include <cstddef>
#include <string>
#include <vector>

namespace conocidos {

/** Base64 tal como lo usa known_hosts (el alfabeto normal, con relleno). */
std::string Base64(const unsigned char *datos, std::size_t largo);

/**
 *  El nombre del host como se escribe en known_hosts.
 *
 *  En el puerto 22 va pelado; en cualquier otro va "[host]:puerto", que es la
 *  convencion de OpenSSH y la que entiende libssh2_knownhost_checkp cuando se
 *  le pasa el puerto aparte.
 */
std::string NombreDeHost(const std::string &host, int puerto);

/**
 *  La linea entera, sin el salto final.
 *
 *      rci3 ssh-rsa AAAAB3NzaC1yc2E...
 *
 *  tipo es el nombre del algoritmo tal como viaja en el protocolo
 *  ("ssh-rsa", "ssh-ed25519", "ecdsa-sha2-nistp256"...). Devuelve una cadena
 *  vacia si el tipo esta vacio o la clave no tiene bytes: mejor no escribir
 *  nada que escribir una linea rota.
 */
std::string Linea(const std::string &host, int puerto, const std::string &tipo,
                  const unsigned char *clave, std::size_t largo);

/**
 *  Agrega la linea al final del archivo, creando la carpeta si hace falta.
 *
 *  Si el archivo no terminaba en salto de linea, le pone uno antes: sin eso
 *  la entrada nueva se pegaria a la ultima y se perderian las dos.
 *
 *  Devuelve false y llena error si no se pudo. Nunca reescribe ni reordena lo
 *  que ya estaba.
 */
bool Agregar(const std::string &ruta, const std::string &linea,
             std::string *error);

/** Una entrada del archivo, como esta escrita. */
struct Entrada
{
	std::string nombre;   /**< "rci3" o "[rci3]:2200" */
	std::string tipo;     /**< "ssh-rsa", "ssh-ed25519"... */
};

/** Lo que el archivo dice sobre un host, leido por nombre. */
struct Lectura
{
	/** Se pudo abrir el archivo. false no es un error: puede no existir. */
	bool leido = false;

	/** Entradas cuyo nombre es el host, pelado o con cualquier puerto. */
	std::vector<Entrada> delHost;

	/** Hay una cuyo nombre es EXACTAMENTE el de este host y este puerto. */
	bool exacta = false;

	/*  Lo que no se puede comparar por nombre. Mientras alguno sea mayor que
	 *  cero, la lectura esta incompleta y no se puede concluir "aca no hay
	 *  nada de este host": podria estar y no verse.                       */
	int hasheadas = 0;   /**< |1|... : el nombre va cifrado */
	int comodines = 0;   /**< nombres con * o ?, que matchean varios hosts */

	/** true si todo el archivo se pudo leer por nombre, sin zonas ciegas. */
	bool Completa() const { return leido && hasheadas == 0 && comodines == 0; }
};

/**
 *  Lee lo que el archivo tiene para un host, por nombre.
 *
 *  NO reemplaza a la verificacion de libssh2, que sigue siendo la que decide.
 *  Sirve para dos cosas que libssh2 no contesta:
 *
 *    1. mostrar en el error que hay de verdad en el archivo, en vez de decir
 *       "la clave CAMBIO" y dejar al que lee sin datos;
 *    2. distinguir dos hosts distintos detras de la misma direccion en
 *       puertos distintos, que es un caso comun con NAT y en el que la
 *       entrada del otro puerto no dice absolutamente nada sobre este.
 *
 *  Para el punto 2 lo que importa es Completa(): si el archivo tiene nombres
 *  hasheados o con comodines, lo que se ve por nombre es incompleto y no se
 *  puede concluir nada de una ausencia.
 */
Lectura Revisar(const std::string &ruta, const std::string &host, int puerto);

/** Como Revisar, pero solo las entradas. Se conserva porque es lo que usan
 *  las pruebas mas viejas y porque a veces el puerto no viene al caso. */
std::vector<Entrada> EntradasDe(const std::string &ruta, const std::string &host,
                                int *hasheadas);

} // namespace conocidos

#endif
