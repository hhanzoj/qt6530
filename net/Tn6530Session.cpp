#include "Tn6530Session.h"

Tn6530Session::Tn6530Session(QObject *parent)
:	Tn6530Session(tn6530::Policy(), parent)
{
}

Tn6530Session::Tn6530Session(const tn6530::Policy &policy, QObject *parent)
:	QObject(parent),
	m_socket(new QTcpSocket(this)),
	m_telnet(policy)
{
	wireTelnet();

	connect(m_socket, &QTcpSocket::connected,
	        this, &Tn6530Session::onConnected);
	connect(m_socket, &QTcpSocket::disconnected,
	        this, &Tn6530Session::onDisconnected);
	connect(m_socket, &QTcpSocket::readyRead,
	        this, &Tn6530Session::onReadyRead);
	connect(m_socket, &QAbstractSocket::errorOccurred,
	        this, &Tn6530Session::onSocketError);
}

Tn6530Session::~Tn6530Session() = default;

void Tn6530Session::wireTelnet()
{
	tn6530::Events ev;

	ev.onPayload = [this](const unsigned char *d, int n) {
		emit hostData(QByteArray(reinterpret_cast<const char *>(d), n));
	};

	ev.onSend = [this](const unsigned char *d, int n) {
		const QByteArray bytes(reinterpret_cast<const char *>(d), n);
		if (m_socket->state() == QAbstractSocket::ConnectedState)
			m_socket->write(bytes);
		else
			m_pendingOut.append(bytes);
		emit negotiationChanged();
	};

	ev.onEndOfRecord = [this]() { emit endOfRecord(); };

	ev.onLineRead = [this](const tn6530::LineRead &lectura) {
		emit lineReadRequested(lectura.maxBytes, lectura.echo);
	};

	ev.onTrace = [this](const std::string &line) {
		emit trace(QString::fromStdString(line));
	};

	m_telnet.SetEvents(ev);
}

/* ------------------------------------------------------------------ */

void Tn6530Session::connectToHost(const QString &host, quint16 port)
{
	m_pendingOut.clear();
	m_socket->connectToHost(host, port);
}

void Tn6530Session::disconnectFromHost()
{
	m_socket->disconnectFromHost();
}

bool Tn6530Session::isConnected() const
{
	return m_socket->state() == QAbstractSocket::ConnectedState;
}

QString Tn6530Session::negotiationSummary() const
{
	return QString::fromStdString(m_telnet.Summary());
}

bool Tn6530Session::binaryActive() const      { return m_telnet.BinaryActive(); }
bool Tn6530Session::endOfRecordActive() const { return m_telnet.EndOfRecordActive(); }
bool Tn6530Session::hostEchoes() const        { return m_telnet.HostEchoes(); }

void Tn6530Session::sendEndOfRecord()
{
	m_telnet.SendEndOfRecord();
}

void Tn6530Session::SendRaw(const byte *data, int len)
{
	/*  Guardian llama a esto desde dentro de ProcessRemoteString, o sea en
	 *  medio de onReadyRead. No hay reentrada problematica: QTcpSocket::write
	 *  encola, no vuelve a entrar al bucle de eventos. */
	emit sentToHost(QByteArray(reinterpret_cast<const char *>(data), len));
	m_telnet.SendPayload(reinterpret_cast<const unsigned char *>(data), len);
}

/* ------------------------------------------------------------------ */

void Tn6530Session::onConnected()
{
	m_telnet.Start();

	if (!m_pendingOut.isEmpty())
	{
		m_socket->write(m_pendingOut);
		m_pendingOut.clear();
	}
	emit connected();
}

void Tn6530Session::onDisconnected()
{
	emit disconnected();
}

void Tn6530Session::onReadyRead()
{
	const QByteArray bytes = m_socket->readAll();
	if (bytes.isEmpty()) return;

	/*  Se entrega tal como llego, sin reensamblar. La maquina de estados
	 *  retoma una secuencia partida entre dos lecturas, y ese reparto es
	 *  justamente el que hay que ejercitar: el nucleo tiene defectos que
	 *  solo aparecen cuando una secuencia no cae entera en un segmento. */
	m_telnet.Feed(reinterpret_cast<const unsigned char *>(bytes.constData()),
	              bytes.size());
}

void Tn6530Session::onSocketError(QAbstractSocket::SocketError)
{
	emit errorOccurred(m_socket->errorString());
}
