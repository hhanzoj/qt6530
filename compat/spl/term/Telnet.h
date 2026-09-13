/*
 *  Capa de compatibilidad SPL -> STL: desacople de la capa de red.
 *
 *  Guardian recibia un spl::Telnet concreto y le pedia una sola cosa:
 *  SendRaw(bytes, longitud). Esa era toda la superficie. Aqui se sustituye
 *  por una interfaz abstracta, de modo que el nucleo deja de depender de
 *  como se llega al host.
 *
 *  Quien la implemente decide el transporte:
 *
 *    - las pruebas, con un sumidero en memoria que guarda lo enviado y
 *      permite afirmar sobre las respuestas del terminal;
 *    - la fase 01, con Tn6530Session sobre QTcpSocket.
 *
 *  El typedef mantiene el nombre "Telnet" para que el nucleo compile sin
 *  una sola edicion; el codigo nuevo usa IHostLink.
 */
#ifndef _spl_compat_telnet_h
#define _spl_compat_telnet_h

#include <spl/types.h>
#include <spl/debug.h>

/**
 *  Salida de bytes del terminal hacia el host.
 */
class IHostLink
{
public:
	virtual ~IHostLink() {}

	/**
	 *  Envia len bytes crudos al host. El nucleo ya aplico el protocolo
	 *  6530; quien implemente esto solo agrega el encuadre de telnet.
	 */
	virtual void SendRaw(const byte *data, int len) = 0;

	inline void CheckMem() const {}
	inline void ValidateMem() const {}
};

typedef IHostLink Telnet;

#endif
