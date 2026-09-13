/*
 *  PosixTransport -- socket TCP sobre la API de BSD, sin Qt.
 *
 *  Existe por dos razones. La primera es poder probar la capa telnet contra
 *  el host real desde una consola, sin arrastrar Qt ni una GUI. La segunda
 *  es que deja demostrado que Tn6530Telnet no depende de Qt: si un dia hay
 *  que llevar el cliente a OSS o a un servicio sin interfaz, el transporte
 *  se cambia y el resto queda igual.
 *
 *  El transporte de produccion es net/Tn6530Session, sobre QTcpSocket.
 */
#ifndef _posix_transport_h
#define _posix_transport_h

#include "Tn6530Telnet.h"

#include <spl/term/Telnet.h>

#include <functional>
#include <string>

/**
 *  Conexion TCP con la capa telnet encima. Implementa IHostLink, asi que
 *  Guardian puede escribirle directamente.
 */
class PosixTransport : public IHostLink
{
public:
	explicit PosixTransport(const tn6530::Policy &policy = tn6530::Policy());
	virtual ~PosixTransport();

	/** Payload de aplicacion recibido del host, listo para Guardian. */
	std::function<void(const char *, int)> onHostData;

	/** Marcador de fin de registro. */
	std::function<void()> onEndOfRecord;

	/** Traza legible de la negociacion. */
	std::function<void(const std::string &)> onTrace;

	/** El host pidio una lectura. Con eco en false, es una clave. */
	std::function<void(const tn6530::LineRead &)> onLineRead;

	/** Copia de los bytes crudos, para grabar la sesion en el mismo formato
	 *  que tools/tap6530.py y poder compararla con la del emulador. */
	std::function<void(const char *, int)> onRawFromHost;
	std::function<void(const char *, int)> onRawToHost;

	bool Connect(const std::string &host, int puerto, std::string *error);
	void Close();
	bool IsOpen() const { return m_fd >= 0; }

	/** Espera datos hasta esperaMs y los procesa. Devuelve false si la
	 *  conexion se cerro. Un timeout no es un cierre: devuelve true. */
	bool Poll(int esperaMs);

	/* --- IHostLink --- */
	virtual void SendRaw(const byte *data, int len);

	tn6530::Tn6530Telnet &Telnet() { return m_telnet; }

private:
	int m_fd;
	tn6530::Tn6530Telnet m_telnet;

	void WriteAll(const unsigned char *data, int len);
};

#endif
