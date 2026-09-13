/*
 *  Ssh6530Session -- la cascara Qt de Ssh6530Transport.
 *
 *  Deliberadamente delgada. Toda la logica de SSH -- saludo, known_hosts,
 *  autenticacion, pty-req, exec, leer y escribir el canal -- vive en
 *  Ssh6530Transport, que no depende de Qt y por lo tanto se puede compilar y
 *  mirar sin arrastrar media biblioteca. Aca solo hay un QSocketNotifier
 *  sobre el descriptor y las mismas senales que Tn6530Session, para que la
 *  ventana no tenga que saber por donde vinieron los bytes.
 *
 *  ADENTRO DEL CANAL SSH HAY TELNET
 *
 *  Esto no lo esperaba y es la correccion mas importante del transporte.
 *  La primera conexion real contra rci3 tiro esto:
 *
 *      [INFO ] Unexpected char in 5000: 255     <- IAC
 *      [INFO ] Expected 3 in 5000: 251          <- WILL
 *      [ERROR] Unknown command char 3           <- SGA
 *      [ERROR] Unknown command char 31          <- NAWS
 *
 *  O sea IAC WILL ECHO, IAC WILL SGA, IAC DO NAWS: exactamente la misma
 *  apertura que manda TELSERV en el puerto 23. STN atiende las sesiones SSH
 *  con el mismo servicio que las de telnet, asi que SSH reemplaza al socket
 *  pero no al protocolo. TN6530-8 sigue siendo TN6530-8.
 *
 *  Por eso Tn6530Telnet va en el medio, igual que en Tn6530Session, y por eso
 *  esta clase emite las mismas senales de negociacion. Eso resuelve de paso
 *  la pregunta que habia quedado abierta sobre el eco: no hay que adivinarlo,
 *  se negocia, y HostEchoes() lo contesta igual que en telnet.
 *
 *  A diferencia de Tn6530Session, connectToHost() BLOQUEA: el saludo y la
 *  autenticacion son sincronicos. Contra un host cercano es un segundo o
 *  dos. Sacarlo a un hilo es trabajo para cuando moleste, no antes.
 */
#ifndef _ssh6530_session_h
#define _ssh6530_session_h

#include "Ssh6530Transport.h"
#include "Tn6530Telnet.h"
#include "Vt6530Terminal.h"

#include <QByteArray>
#include <QObject>
#include <QString>

class QSocketNotifier;

class Ssh6530Session : public QObject, public IHostLink
{
	Q_OBJECT

public:
	explicit Ssh6530Session(const ssh6530::Politica &politica,
	                        QObject *parent = nullptr);
	Ssh6530Session(const ssh6530::Politica &politica,
	               const tn6530::Policy &politicaTelnet,
	               QObject *parent = nullptr);
	~Ssh6530Session() override;

	/** Bloquea hasta que la sesion queda armada o falla. */
	bool connectToHost(const QString &host, quint16 puerto);
	void disconnectFromHost();
	bool isConnected() const;

	/** Resumen de la sesion: el canal SSH y la negociacion telnet de adentro. */
	QString sessionSummary() const;

	/** Estado de la negociacion telnet, la de adentro del canal. */
	QString negotiationSummary() const;
	bool binaryActive() const;
	bool endOfRecordActive() const;

	/** El host se encarga del eco. Ver Tn6530Telnet::HostEchoes(). */
	bool hostEchoes() const;

	/** Cuantas veces se llamo a Poll(). Diagnostico: si se queda en 1, el
	 *  QSocketNotifier no esta disparando y la unica lectura que hubo fue
	 *  la directa de connectToHost(). */
	int pollCount() const;

	/** Descartar el eco de lo que nosotros mismos mandamos.
	 *
	 *  STN devuelve por el canal los bytes que le escribimos, y no es el
	 *  eco del termios: se le pidio ECHO=0 en el pty-req y lo devuelve
	 *  igual. En modo conversacional daria lo mismo -- alguien tiene que
	 *  dibujar lo tecleado --, pero en modo bloque es veneno, porque lo que
	 *  vuelve es una secuencia AID entera:
	 *
	 *      => term: <01>V"#!<03><00>
	 *      <= host: <01>V"#!<1B>:!...
	 *
	 *  Ese <01> abre el estado 5000 del nucleo, se comen dos bytes, y el
	 *  '#' y el '!' terminan DIBUJADOS en la pantalla. Es el caracter suelto
	 *  que aparecia al apretar una tecla de funcion.
	 *
	 *  Como ademas por este camino no hay negociacion telnet -- con TERM
	 *  xterm el host no manda un solo IAC --, el eco lo hacemos nosotros, y
	 *  entonces descartar el del host es lo correcto en los dos modos. */
	void setEchoFilter(bool activo);

	/** Cierra un registro explicitamente. Sin efecto si no se negocio EOR. */
	void sendEndOfRecord();

	/** El tamano de la ventana cambio. */
	void resize(int columnas, int filas);

	/** Se llama cuando hace falta la clave. Se pone antes de conectar.
	 *
	 *  intento empieza en 1 y sube con cada rechazo: sirve para decir "clave
	 *  incorrecta" en vez de repetir el mismo cartel. */
	void setPasswordCallback(
		std::function<bool(const QString &usuario, int intento,
		                   QString *clave)> cb);

	/** La clave del host no esta en known_hosts. Se pone antes de conectar. */
	void setUnknownHostCallback(
		std::function<bool(const QString &host, const QString &huella)> cb);

	/* --- IHostLink: por aca escribe Guardian --- */
	void SendRaw(const byte *data, int len) override;

signals:
	void connected();
	void disconnected();
	void errorOccurred(const QString &message);

	/** Payload de aplicacion, ya sin telnet. Va a Vt6530Terminal. */
	void hostData(const QByteArray &payload);

	/** Llego un marcador IAC EOR. El payload del registro ya se emitio. */
	void endOfRecord();

	/** Lo que el terminal le manda al host, antes de que telnet lo escape.
	 *  Solo para diagnostico. */
	void sentToHost(const QByteArray &datos);

	void trace(const QString &line);

	/** La negociacion se movio: util para refrescar la barra de estado. */
	void negotiationChanged();

	/** El host pidio una lectura. Si eco viene en false es una clave. */
	void lineReadRequested(int maxBytes, bool eco);

private slots:
	void onListo();

private:
	void cablearTelnet();
	void cerrar(const QString &motivo);

	ssh6530::Ssh6530Transport *m_transporte;
	tn6530::Tn6530Telnet       m_telnet;
	QSocketNotifier           *m_aviso;
	std::function<bool(const QString &, int, QString *)>  m_pedirClave;
	std::function<bool(const QString &, const QString &)> m_hostDesconocido;
	bool m_conectado = false;
	int  m_vueltas   = 0;
	bool m_filtrarEco = true;
	std::string m_ecoPendiente;   /* lo que mandamos y esperamos de vuelta */
};

#endif
