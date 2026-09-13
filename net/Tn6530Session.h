/*
 *  Tn6530Session -- transporte Qt 6 para el terminal 6530.
 *
 *  QTcpSocket asincrono mas Tn6530Telnet. Reemplaza el modelo de la
 *  biblioteca original, que corria telnet en su propio hilo y bloqueaba en
 *  Join(): eso es incompatible con el bucle de eventos de Qt. Aca no hay
 *  hilos, todo ocurre en readyRead.
 *
 *  Tambien reemplaza la interfaz Vt6530EventListener por senales, que es lo
 *  que un widget espera consumir.
 *
 *  AVISO: este archivo no se pudo compilar en el entorno donde se escribio
 *  -- el mirror de paquetes bloquea Qt 6 --, asi que esta verificado por
 *  lectura, no por compilador. La logica de telnet que hay debajo si esta
 *  probada: vive en Tn6530Telnet, es C++17 puro, y tests/telnet_tests.cpp la
 *  ejercita contra la negociacion real de un TELSERV. Lo que falta verificar
 *  aca es el pegado con Qt, que es mecanico.
 */
#ifndef _tn6530_session_h
#define _tn6530_session_h

#include "Tn6530Telnet.h"
#include "Vt6530Terminal.h"

#include <spl/term/Telnet.h>

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTcpSocket>

/**
 *  Conexion a un host NonStop con la capa telnet TN6530-8.
 *
 *  Implementa IHostLink, asi que Guardian le escribe directamente sin saber
 *  que del otro lado hay un QTcpSocket.
 */
class Tn6530Session : public QObject, public IHostLink
{
	Q_OBJECT

public:
	explicit Tn6530Session(QObject *parent = nullptr);
	explicit Tn6530Session(const tn6530::Policy &policy,
	                       QObject *parent = nullptr);
	~Tn6530Session() override;

	void connectToHost(const QString &host, quint16 port);
	void disconnectFromHost();
	bool isConnected() const;

	/** Estado de la negociacion, para una barra de estado o un diagnostico. */
	QString negotiationSummary() const;
	bool binaryActive() const;
	bool endOfRecordActive() const;

	/** El host se encarga del eco. Ver Tn6530Telnet::HostEchoes(). */
	bool hostEchoes() const;


	/** Cierra un registro explicitamente. Sin efecto si no se negocio EOR. */
	void sendEndOfRecord();

	/* --- IHostLink: por aca escribe Guardian --- */
	void SendRaw(const byte *data, int len) override;

signals:
	/** La conexion quedo establecida. La negociacion puede seguir despues. */
	void connected();
	void disconnected();

	/** Error de socket, con el texto ya legible. */
	void errorOccurred(const QString &message);

	/** Payload de aplicacion, ya sin telnet. Va a Vt6530Terminal. */
	void hostData(const QByteArray &payload);

	/** Llego un marcador IAC EOR. El payload del registro ya se emitio. */
	void endOfRecord();

	/** Traza de la negociacion, para el log o una ventana de diagnostico. */
	void trace(const QString &line);

	/** La negociacion se movio: util para refrescar la barra de estado. */
	void negotiationChanged();

	/** El host pidio una lectura. Si eco viene en false es una clave: el
	 *  terminal no tiene que dibujar lo que se teclee. */
	void lineReadRequested(int maxBytes, bool eco);

	/** Lo que el terminal le manda al host, antes de que telnet lo escape.
	 *  Solo para diagnostico. */
	void sentToHost(const QByteArray &datos);

private slots:
	void onReadyRead();
	void onConnected();
	void onDisconnected();
	void onSocketError(QAbstractSocket::SocketError error);

private:
	void wireTelnet();

	QTcpSocket           *m_socket;
	tn6530::Tn6530Telnet  m_telnet;
	QByteArray            m_pendingOut;   /* si se escribe antes de conectar */
};

#endif
