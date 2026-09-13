#include "Ssh6530Session.h"

#include <QSocketNotifier>

Ssh6530Session::Ssh6530Session(const ssh6530::Politica &politica,
                               QObject *parent)
:	Ssh6530Session(politica, tn6530::Policy(), parent)
{
}

Ssh6530Session::Ssh6530Session(const ssh6530::Politica &politica,
                               const tn6530::Policy &politicaTelnet,
                               QObject *parent)
:	QObject(parent),
	m_transporte(new ssh6530::Ssh6530Transport(politica)),
	m_telnet(politicaTelnet),
	m_aviso(nullptr)
{
	cablearTelnet();

	/*  Lo que sale del canal SSH NO es payload: adentro viene telnet. Va
	 *  primero a la maquina de telnet, que separa la negociacion de los
	 *  datos y contesta lo que haya que contestar. */
	m_transporte->onHostData = [this](const char *d, int n) {
		int desde = 0;

		/*  Descartar el eco de lo que nosotros mismos acabamos de mandar.
		 *  Ver setEchoFilter() en la cabecera, que cuenta por que hace falta.
		 *
		 *  La regla es conservadora a proposito: se compara byte a byte con
		 *  lo pendiente y se descarta SOLO el prefijo que coincide. Al primer
		 *  byte que no coincide se abandona y se tira lo pendiente, asi un
		 *  desajuste cuesta un eco no filtrado y nunca un dato comido.     */
		if (m_filtrarEco && !m_ecoPendiente.empty())
		{
			while (desde < n && desde < (int)m_ecoPendiente.size() &&
			       d[desde] == m_ecoPendiente[(size_t)desde])
			{
				desde++;
			}
			if (desde == 0)
			{
				/* No era eco: lo pendiente ya no va a volver. */
				m_ecoPendiente.clear();
			}
			else if (desde == (int)m_ecoPendiente.size())
			{
				m_ecoPendiente.clear();
			}
			else
			{
				/*  El eco vino partido entre dos lecturas: se guarda lo que
				 *  falta para la proxima. */
				m_ecoPendiente.erase(0, (size_t)desde);
			}
		}

		if (desde >= n) return;
		m_telnet.Feed(reinterpret_cast<const unsigned char *>(d) + desde,
		              n - desde);
	};

	m_transporte->onTrace = [this](const std::string &t) {
		emit trace(QString::fromStdString(t));
	};

	m_transporte->onPedirClave = [this](const std::string &usuario, int intento,
	                                    std::string *clave) -> bool {
		if (!m_pedirClave) return false;
		QString q;
		if (!m_pedirClave(QString::fromStdString(usuario), intento, &q))
			return false;
		*clave = q.toStdString();
		/*  La copia de Qt tambien se pisa: no es blindaje, es higiene. */
		if (!q.isEmpty()) q.fill(QChar('\0'));
		return true;
	};

	m_transporte->onHostDesconocido = [this](const std::string &host,
	                                         const std::string &huella) -> bool {
		if (!m_hostDesconocido) return false;
		return m_hostDesconocido(QString::fromStdString(host),
		                         QString::fromStdString(huella));
	};
}

Ssh6530Session::~Ssh6530Session()
{
	delete m_aviso;
	m_aviso = nullptr;
	delete m_transporte;
	m_transporte = nullptr;
}

void Ssh6530Session::cablearTelnet()
{
	tn6530::Events ev;

	ev.onPayload = [this](const unsigned char *d, int n) {
		emit hostData(QByteArray(reinterpret_cast<const char *>(d), n));
	};

	/*  Las respuestas de la negociacion salen por el canal SSH, no por un
	 *  socket. Es el unico lugar donde este cableado difiere del de telnet. */
	ev.onSend = [this](const unsigned char *d, int n) {
		/*  Todo lo que sale pasa por aca, asi que aca se anota lo que
		 *  esperamos que el host nos devuelva. Se acota para que un envio
		 *  grande no deje una cola larga: el eco que nos molesta son siete
		 *  bytes de una secuencia AID.                                    */
		if (m_filtrarEco)
		{
			if (m_ecoPendiente.size() > 256) m_ecoPendiente.clear();
			m_ecoPendiente.append(reinterpret_cast<const char *>(d), (size_t)n);
		}
		if (m_transporte != nullptr && m_transporte->IsOpen())
			m_transporte->SendRaw(reinterpret_cast<const byte *>(d), n);
		emit negotiationChanged();
	};

	ev.onEndOfRecord = [this]() { emit endOfRecord(); };

	ev.onLineRead = [this](const tn6530::LineRead &lectura) {
		emit lineReadRequested(lectura.maxBytes, lectura.echo);
	};

	ev.onTrace = [this](const std::string &linea) {
		emit trace(QString::fromStdString(linea));
	};

	m_telnet.SetEvents(ev);
}

void Ssh6530Session::setPasswordCallback(
	std::function<bool(const QString &, int, QString *)> cb)
{
	m_pedirClave = cb;
}

void Ssh6530Session::setUnknownHostCallback(
	std::function<bool(const QString &, const QString &)> cb)
{
	m_hostDesconocido = cb;
}

bool Ssh6530Session::connectToHost(const QString &host, quint16 puerto)
{
	std::string error;
	if (!m_transporte->Connect(host.toStdString(), (int)puerto, &error))
	{
		emit errorOccurred(QString::fromStdString(error));
		return false;
	}

	/*  De aca en mas el bucle de eventos de Qt hace el trabajo: cuando el
	 *  socket tiene algo, se llama a Poll(0), que no bloquea.
	 *
	 *  Se traza el descriptor a proposito. Si este aviso no queda bien
	 *  enganchado, el sintoma es dificil de leer y facil de confundir con un
	 *  cuelgue: se lee UNA vez -- la llamada directa a onListo() de abajo --,
	 *  se contesta la negociacion, y despues nunca mas entra un byte, con la
	 *  aplicacion perfectamente viva y ociosa. Que el numero salga en la
	 *  traza convierte diez minutos de gdb en un vistazo.               */
	/*  qintptr y no int: en Windows un socket es un UINT_PTR de 64 bits, y
	 *  truncarlo a int da un descriptor que parece valido y no lo es.
	 *  QSocketNotifier pide justo un qintptr.                            */
	const qintptr fd = (qintptr)m_transporte->Descriptor();
	m_aviso = new QSocketNotifier(fd, QSocketNotifier::Read, this);
	connect(m_aviso, &QSocketNotifier::activated,
	        this, &Ssh6530Session::onListo);
	m_aviso->setEnabled(true);

	emit trace(QStringLiteral("aviso de lectura armado sobre el descriptor %1"
	                          " (activo: %2)")
	           .arg((qlonglong)fd).arg(m_aviso->isEnabled() ? "si" : "no"));

	m_conectado = true;
	m_telnet.Start();
	emit connected();

	/*  Puede haber bytes esperando desde antes de colgar el aviso: la
	 *  negociacion de STN llega enseguida y perderla seria empezar torcido. */
	onListo();
	return true;
}

void Ssh6530Session::onListo()
{
	if (m_transporte == nullptr) return;

	/*  Contar las vueltas sale casi gratis y distingue dos cosas que desde
	 *  la ventana se ven igual: "el aviso no dispara nunca" (se queda en 1,
	 *  la llamada directa de connectToHost) y "dispara pero el host no
	 *  manda nada" (sube). */
	m_vueltas++;

	if (!m_transporte->Poll(0))
	{
		cerrar(QString());
	}
}

int Ssh6530Session::pollCount() const
{
	return m_vueltas;
}

void Ssh6530Session::setEchoFilter(bool activo)
{
	m_filtrarEco = activo;
	if (!activo) m_ecoPendiente.clear();
}

void Ssh6530Session::cerrar(const QString &motivo)
{
	if (!m_conectado) return;
	m_conectado = false;

	if (m_aviso != nullptr)
	{
		m_aviso->setEnabled(false);
		m_aviso->deleteLater();
		m_aviso = nullptr;
	}
	m_transporte->Close();

	if (!motivo.isEmpty()) emit errorOccurred(motivo);
	emit disconnected();
}

void Ssh6530Session::disconnectFromHost()
{
	cerrar(QString());
}

bool Ssh6530Session::isConnected() const
{
	return m_conectado && m_transporte != nullptr && m_transporte->IsOpen();
}

QString Ssh6530Session::sessionSummary() const
{
	return QString::fromStdString(m_transporte->Resumen())
	     + QStringLiteral(" | ")
	     + QString::fromStdString(m_telnet.Summary());
}

QString Ssh6530Session::negotiationSummary() const
{
	return QString::fromStdString(m_telnet.Summary());
}

bool Ssh6530Session::binaryActive() const      { return m_telnet.BinaryActive(); }
bool Ssh6530Session::endOfRecordActive() const { return m_telnet.EndOfRecordActive(); }
bool Ssh6530Session::hostEchoes() const        { return m_telnet.HostEchoes(); }

void Ssh6530Session::sendEndOfRecord()
{
	m_telnet.SendEndOfRecord();
}

void Ssh6530Session::resize(int columnas, int filas)
{
	m_transporte->Redimensionar(columnas, filas);
}

void Ssh6530Session::SendRaw(const byte *data, int len)
{
	if (data == nullptr || len <= 0) return;
	emit sentToHost(QByteArray(reinterpret_cast<const char *>(data), len));

	/*  Por telnet, no por el canal pelado: hay que escapar los IAC y, si se
	 *  negocio END-OF-RECORD, cerrar el registro. Mandar el payload crudo
	 *  funcionaria hasta que un byte 0xFF apareciera en el medio. */
	m_telnet.SendPayload(reinterpret_cast<const unsigned char *>(data), len);
}
