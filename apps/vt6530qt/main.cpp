/*
 *  vt6530qt -- emulador de terminal 6530 con interfaz Qt 6.
 *
 *  Junta las tres capas: Tn6530Session pone los bytes en el socket,
 *  Vt6530Terminal los interpreta, y Vt6530Widget los dibuja.
 *
 *      vt6530qt --host rci3 --puerto 23
 *
 *  Alcance de la fase 02: lo minimo utilizable. Host y puerto por linea de
 *  comandos, sin dialogo de conexion ni menus. Lo que hay es la pantalla y
 *  el teclado, que es lo que hace falta para usarlo contra un host real.
 */

#include "../../gui/Vt6530Widget.h"
#include "../../net/Ssh6530Session.h"
#include "../../net/Tn6530Session.h"
#include "../../net/Vt6530Terminal.h"

#include <spl/Log.h>

#include <QApplication>
#include <QDebug>
#include "formularioconexion.h"
#include "formulariosshcred.h"

#include "../Consola.h"
#include "../Opciones.h"
#include "../PoliticaSsh.h"

#include <cstdio>
#include <cstring>
#include <type_traits>
#include <string>
#include <vector>

/*  Aca habia un bloque que incluia termios.h bajo #if Q_OS_UNIX, para apagar
 *  el eco al pedir la clave. Se fue entero a apps/Consola.cpp: era la unica
 *  parte de este archivo que sabia de sistemas operativos, y estaba escrita
 *  para uno solo. Ver el comentario de Consola.h.                        */

#include <QKeySequence>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QStatusBar>
#include <QString>
#include <QTimer>

/**
 *  true si la sesion se armo con los formularios y no con la linea de
 *  comandos.
 *
 *  Decide DONDE se hacen las dos preguntas de la primera conexion -- la clave
 *  y la clave del host --, y la regla es la que uno espera: quien entro por
 *  formularios sigue con formularios, y quien entro por la linea de comandos
 *  sigue por consola. Mezclarlos es lo que confunde: abrir tres dialogos y
 *  despues quedarse esperando un "si/no" en una terminal que el usuario ni
 *  esta mirando.
 *
 *  Si no hay consola, se usa el dialogo igual: mejor preguntar en una ventana
 *  que rendirse.
 */
static bool g_modoGrafico = false;

/**
 *  Pide la clave por consola, con el eco apagado.
 *
 *  A proposito NO hay opcion -pw como la de PuTTY: una clave en la linea de
 *  comandos queda en el ps y en el historial del shell. Si hace falta algo
 *  no interactivo, el camino es una clave publica con -i o el agente, que
 *  para eso estan.
 */
static bool PedirClavePorConsola(const QString &usuario, int intento,
                                 QString *clave)
{
	if (!consola::Hay())
	{
		std::fprintf(stderr,
			"no hay consola para pedir la clave: use -i con una clave privada\n"
			"o un agente ssh.\n");
		return false;
	}

	/*  Que el segundo intento se note. Repetir el mismo prompt como si no
	 *  hubiera pasado nada hace dudar de si se llego a mandar algo.      */
	if (intento > 1) std::fprintf(stderr, "clave incorrecta.\n");

	std::fprintf(stderr, "clave de %s: ", usuario.toStdString().c_str());
	std::fflush(stderr);

	std::string linea;
	if (!consola::LeerLineaSinEco(&linea)) return false;

	*clave = QString::fromStdString(linea);
	if (!linea.empty()) std::memset(&linea[0], 0, linea.size());
	return true;
}

/**
 *  Muestra la huella de un host nuevo y espera si/no.
 *
 *  Es la misma pregunta que hacen ssh y PuTTY, y por el mismo motivo: la
 *  primera conexion es el unico momento en que se puede verificar con quien
 *  se esta hablando. De ahi en mas se confia en lo anotado.
 *
 *  Solo se pregunta por hosts NUEVOS. Una clave que CAMBIO no llega hasta
 *  aca: el transporte corta antes y no ofrece ninguna forma de seguir.
 *
 *  Sin consola no se pregunta y se contesta que no, en vez de quedarse
 *  esperando para siempre una respuesta que nadie puede dar. El mensaje del
 *  transporte dice cual es la bandera para el caso no interactivo.
 */
static bool PreguntarPorHostNuevoPorConsola(const QString &host,
                                            const QString &huella)
{
	if (!consola::Hay()) return false;

	std::fprintf(stderr,
		"\nEl host '%s' no esta en known_hosts.\n"
		"Su huella es:\n"
		"    %s\n"
		"Comparela con la que le dio el administrador, o con la que ya tenga\n"
		"de una conexion con ssh. Si acepta, queda anotada y no se vuelve a\n"
		"preguntar.\n"
		"\n"
		"Aceptar y anotar la clave? (si/no) ",
		host.toStdString().c_str(), huella.toStdString().c_str());
	std::fflush(stderr);

	std::string r;
	if (!consola::LeerLinea(&r))
	{
		std::fprintf(stderr, "\n");
		return false;
	}

	/*  Solo un si explicito: un ENTER de apuro no puede alcanzar para aceptar
	 *  la clave de un host que nadie miro. Ver consola::EsSi, que es donde
	 *  esta esa decision y donde se prueba.                              */
	const bool acepta = consola::EsSi(r);

	/*  Sin operador ternario adentro del fprintf: una cadena de formato que
	 *  no es un literal saca -Wformat-security, y en el MinGW de MSYS2 eso
	 *  es ruido que despues hay que ir a mirar.                          */
	if (acepta) std::fprintf(stderr, "\n");
	else        std::fprintf(stderr, "\nno se acepto.\n");

	return acepta;
}

/**
 *  La misma pregunta, en una ventana.
 *
 *  El texto es el mismo que el de la consola, y eso es a proposito: lo que
 *  hay que decidir no cambia porque cambie el lugar donde se pregunta.
 *
 *  El boton por defecto es No. Aceptar la clave de un host que nadie miro no
 *  puede ser lo que pasa cuando alguien aprieta ENTER para sacarse de encima
 *  un cartel.
 */
static bool PreguntarPorHostNuevoEnVentana(QWidget *padre, const QString &host,
                                           const QString &huella)
{
	QMessageBox cuadro(padre);
	cuadro.setIcon(QMessageBox::Warning);
	cuadro.setWindowTitle(QStringLiteral("vt6530"));
	cuadro.setText(
		QObject::tr("El host '%1' no esta en known_hosts.").arg(host));
	cuadro.setInformativeText(QObject::tr(
		"Su huella es:\n\n    %1\n\n"
		"Comparela con la que le dio el administrador, o con la que ya tenga "
		"de una conexion con ssh.\n\n"
		"Si acepta, queda anotada y no se vuelve a preguntar.").arg(huella));
	cuadro.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
	cuadro.setDefaultButton(QMessageBox::No);
	cuadro.button(QMessageBox::Yes)->setText(
		QObject::tr("Aceptar y anotar"));
	cuadro.button(QMessageBox::No)->setText(QObject::tr("Cancelar"));

	return cuadro.exec() == QMessageBox::Yes;
}

/*  Las dos preguntas de la primera conexion, cada una al lugar que
 *  corresponde. Ver g_modoGrafico: quien entro por formularios sigue con
 *  formularios.                                                          */

static bool PedirClave(QWidget *padre, const QString &usuario, int intento,
                       QString *clave)
{
	if (g_modoGrafico || !consola::Hay())
		return FormularioSSHcred::Pedir(padre, usuario, intento, clave);
	return PedirClavePorConsola(usuario, intento, clave);
}

static bool PreguntarPorHostNuevo(QWidget *padre, const QString &host,
                                  const QString &huella)
{
	if (g_modoGrafico || !consola::Hay())
		return PreguntarPorHostNuevoEnVentana(padre, host, huella);
	return PreguntarPorHostNuevoPorConsola(host, huella);
}

/**
 *  Muestra un error de la sesion en los tres lugares que hacen falta.
 *
 *  La barra de estado sola no alcanza y se vio con el primero que importaba:
 *  "la clave del host CAMBIO respecto de known_hosts" son cuatro lineas --
 *  la huella, el archivo, y que hacer-- y en la barra entra media, cortada
 *  con puntos suspensivos. Un mensaje de seguridad que no se puede leer es
 *  lo mismo que no haberlo escrito.
 *
 *  Asi que: la barra queda para el resumen, la consola recibe el texto
 *  completo -- que ademas se puede copiar y pegar --, y si la ventana ya
 *  esta arriba, un cuadro de dialogo, porque al que lo lanzo desde un icono
 *  la consola no le sirve de nada.
 */
static void MostrarError(QMainWindow *ventana, const QString &mensaje)
{
	/*  Completo y de una sola pieza por la consola. */
	std::fprintf(stderr, "\nerror: %s\n", mensaje.toStdString().c_str());
	std::fflush(stderr);

	/*  En la barra, solo la primera linea: es lo unico que va a entrar. */
	const QString primera = mensaje.section('\n', 0, 0);
	if (ventana != nullptr && ventana->statusBar() != nullptr)
		ventana->statusBar()->showMessage(QStringLiteral("error: %1").arg(primera));

	if (ventana != nullptr)
	{
		QMessageBox cuadro(ventana);
		cuadro.setIcon(QMessageBox::Critical);
		cuadro.setWindowTitle(QStringLiteral("vt6530"));
		cuadro.setText(primera);

		/*  El resto abajo, con tipografia de ancho fijo: adentro van huellas
		 *  y rutas, que con una proporcional se leen mal.                 */
		const QString resto = mensaje.section('\n', 1);
		if (!resto.trimmed().isEmpty())
		{
			cuadro.setInformativeText(resto);
			cuadro.setStyleSheet(QStringLiteral("QLabel { font-family: monospace; }"));
		}
		cuadro.exec();
	}
}

/**
 *  Vuelca bytes de forma legible: lo imprimible tal cual, el resto en hexa
 *  entre corchetes angulares. Es lo que hace falta para responder la unica
 *  pregunta que importa cuando algo sale duplicado en pantalla: quien lo
 *  mando.
 */
static QString Legible(const QByteArray &b)
{
	QString out;
	for (int i = 0; i < b.size(); i++)
	{
		const unsigned char c = (unsigned char)b.at(i);
		if (c >= 0x20 && c < 0x7F) out += QChar(c);
		else out += QStringLiteral("<%1>").arg(c, 2, 16, QLatin1Char('0')).toUpper();
	}
	return out;
}

/**
 *  Ventana: el widget adentro, y una barra de estado que dice como va la
 *  conexion. La barra no es adorno -- cuando algo no anda, la diferencia
 *  entre "no conecto" y "conecte pero el host no negocia" es lo primero que
 *  uno quiere saber.
 */
class VentanaPrincipal : public QMainWindow
{
public:
	/** De donde sale el eco de lo que se teclea. */
	enum class Eco { Auto, Local, Host };

	VentanaPrincipal(const cli::Opciones &op)
	:	m_host(QString::fromStdString(op.host)),
		m_puerto((quint16)op.puerto),
		m_quedarse(op.quedarse),
		m_eco(  (op.eco == cli::Eco::Local) ? Eco::Local
		      : (op.eco == cli::Eco::Host)  ? Eco::Host : Eco::Auto),
		m_esSsh(op.transporte == cli::Transporte::Ssh)
	{
		const QString host = m_host;
		const quint16 puerto = m_puerto;
		const bool traza = op.traza;
		const Eco eco = m_eco;

		tn6530::Policy politica;
		politica.terminalType = op.tipoTerminal;
		politica.acceptLinemode = op.linemode;
		politica.acceptNaws     = op.naws;

		/*  Si el eco lo queremos local, hay que DECIRSELO al host, no
		 *  filtrar despues lo que devuelve. Contestarle DO ECHO -- que es
		 *  lo que haciamos -- es pedirle que se encargue el; despues
		 *  descartar sus bytes seria pelearse con algo que pedimos.
		 *
		 *  Con DONT ECHO el host tiene que dejar de hacerlo, y ahi el eco
		 *  local no duplica nada.                                        */
		if (eco == Eco::Local)
		{
			politica.acceptRemoteEcho = false;
		}

		IHostLink *enlace = nullptr;
		if (m_esSsh)
		{
			/*  La traduccion de opciones a politica vive en apps/PoliticaSsh,
			 *  sin Qt, para que la compile alguien y no solo la lea. La
			 *  sonda usa la misma, que es como se garantiza que reproduzca
			 *  lo que hace esta aplicacion.                              */
			ssh6530::Politica ps = cli::PoliticaSshDesde(op);
			ps.columnas = 80;
			ps.filas    = 24;
			/*  La misma politica de telnet que en el puerto 23: adentro del
			 *  canal ssh viaja la misma negociacion. */
			m_ssh = new Ssh6530Session(ps, politica, this);
			enlace = m_ssh;
		}
		else
		{
			m_sesion = new Tn6530Session(politica, this);
			enlace = m_sesion;
		}

		m_terminal = new Vt6530Terminal(enlace);
		m_widget   = new Vt6530Widget(m_terminal, this);

		setCentralWidget(m_widget);
		setWindowTitle(QStringLiteral("vt6530 — %1:%2").arg(host).arg(puerto));
		statusBar()->showMessage(
			/*  Se nombran las dos formas porque el escritorio se queda con
			 *  varias de las Alt+Fn -- Alt+F6 nunca llega en GNOME -- y sin
			 *  el aviso uno cree que la tecla no anda. */
			QStringLiteral("conectando a %1:%2…   (F11–F16: Ctrl+F1 a Ctrl+F6, "
			               "o Alt+F1 a Alt+F6 si el escritorio las deja pasar; "
			               "salir: Ctrl+Shift+Q)")
			.arg(host).arg(puerto));

		/*  Una salida que el terminal no se pueda comer. Alt+F4 ya llega al
		 *  gestor de ventanas -- el widget la deja pasar a proposito -- pero
		 *  Ctrl+Shift+Q no colisiona con nada del 6530 y funciona igual en
		 *  Windows y en Linux. Ctrl+Q solo no serviria: es XON.           */
		QShortcut *salir = new QShortcut(
			QKeySequence(QStringLiteral("Ctrl+Shift+Q")), this);
		salir->setContext(Qt::ApplicationShortcut);
		QObject::connect(salir, &QShortcut::activated,
		                 this, &VentanaPrincipal::close);

		if (m_esSsh)
		{
			cablearSesion(m_ssh, traza);
			/*  La clave se pide por consola. Una ventana de dialogo es
			 *  trabajo de la fase siguiente; lo que no queria era una
			 *  opcion -pw, que deja la clave en el ps y en el historial. */
			m_ssh->setPasswordCallback(
				[this](const QString &usuario, int intento,
				       QString *clave) -> bool {
					return PedirClave(this, usuario, intento, clave);
				});

			/*  Y la otra pregunta de la primera conexion: la clave del host.
			 *  Solo se llama si la politica es Preguntar; con
			 *  -aceptar-host-nuevo o -rechazar-host-nuevo el transporte
			 *  decide solo y esto no se usa.                            */
			m_ssh->setUnknownHostCallback(
				[this](const QString &host, const QString &huella) -> bool {
					return PreguntarPorHostNuevo(this, host, huella);
				});

			m_ssh->setEchoFilter(op.filtrarEco);

			/*  Por ssh esto BLOQUEA hasta el timeout y devuelve si pudo. Por
			 *  telnet es al reves: vuelve enseguida y el fracaso llega mas
			 *  tarde por errorOccurred. Los dos casos se atienden, pero por
			 *  caminos distintos -- ver m_llegoAConectar.                */
			m_arranco = m_ssh->connectToHost(host, puerto);

			/*  Un latido, solo con --traza. Sirve para leer de un vistazo la
			 *  diferencia entre "el aviso de lectura no dispara" (las vueltas
			 *  se quedan en 1) y "dispara pero el host no manda nada" (suben
			 *  y no aparece ningun "<= host"). Desde la ventana esos dos casos
			 *  se ven identicos, y son problemas de lugares distintos.     */
			if (traza)
			{
				QTimer *latido = new QTimer(this);
				QObject::connect(latido, &QTimer::timeout, [this]() {
					qInfo().noquote()
						<< "[ssh] vueltas de lectura:" << m_ssh->pollCount();
				});
				latido->start(3000);
			}
		}
		else
		{
			cablearSesion(m_sesion, traza);

			/*  QTcpSocket conecta en diferido y connectToHost() no devuelve
			 *  nada, asi que aca no hay forma de saber si salio bien. Se
			 *  sigue como si si; el fracaso llega despues por errorOccurred,
			 *  que es quien pide volver al formulario.                    */
			m_sesion->connectToHost(host, puerto);
			m_arranco = true;
		}
	}

	/** false si la conexion fallo de entrada y no hay sesion que mostrar. */
	bool arranco() const { return m_arranco; }

	~VentanaPrincipal() override
	{
		/*  El orden importa. Qt borra los hijos en ~QObject, o sea DESPUES
		 *  del cuerpo de este destructor: si aca soltaramos el terminal
		 *  primero, ~Vt6530Widget correria despues sobre un puntero colgado
		 *  al llamar a SetObserver(nullptr).
		 *
		 *  Asi que primero el widget, que deja de observar; despues el
		 *  terminal; y la sesion queda para Qt, que es hija de la ventana. */
		delete m_widget;
		m_widget = nullptr;
		delete m_terminal;
		m_terminal = nullptr;
	}

private:
	/*  El cableado de la sesion, igual para telnet y para ssh.
	 *
	 *  Las dos clases emiten las mismas senales con los mismos nombres, asi
	 *  que una plantilla evita duplicar treinta lineas que despues se
	 *  desincronizan. Lo unico distinto entre las dos es de donde sale el
	 *  eco, y eso se resuelve en hostEchoes().                            */
	template <class S>
	void cablearSesion(S *sesion, bool traza)
	{
		/*  La traza va PRIMERO, antes del manejador que alimenta al
		 *  nucleo. Qt entrega las conexiones en el orden en que se hicieron,
		 *  y el nucleo contesta al host desde adentro de su propio
		 *  manejador: con la traza conectada despues, la respuesta se
		 *  imprimia ANTES que el comando que la provoco, y la traza mentia
		 *  sobre el orden de los bytes.                                   */
		if (traza)
		{
			/*  Las dos direcciones, legibles, sin proxy de por medio. Con
			 *  esto se ve de un vistazo si un texto que aparece dos veces en
			 *  pantalla lo mando el host o lo dibujamos nosotros.          */
			QObject::connect(sesion, &S::hostData,
			                 [](const QByteArray &d) {
				qInfo().noquote() << "<= host:" << Legible(d);
			});
			QObject::connect(sesion, &S::sentToHost,
			                 [](const QByteArray &d) {
				qInfo().noquote() << "=> term:" << Legible(d);
			});
		}


		/*  Los bytes ya sin telnet van derecho al nucleo. */
		QObject::connect(sesion, &S::hostData,
		                 [this](const QByteArray &datos) {
			m_terminal->FeedFromHost(datos.constData(), datos.size());

			/*  El modo cambia en banda y puede cambiar sin que llegue un
			 *  ENQ despues -- al salir de VIEWSYS pasa exactamente eso. Si
			 *  solo se refrescara con hostWaiting, la barra se queda
			 *  diciendo "bloque protegido" mientras la linea de estado del
			 *  propio terminal ya dice CONV.                             */
			actualizarModo();
		});

		QObject::connect(sesion, &S::connected, [this]() {
			m_llegoAConectar = true;
			statusBar()->showMessage(
				QStringLiteral("conectado a %1:%2").arg(m_host).arg(m_puerto));
		});

		QObject::connect(sesion, &S::disconnected, [this]() {
			/*  El host cerro: es lo que pasa al dar "exit". Sin esto la
			 *  ventana quedaba viva con una pantalla muerta, y el widget
			 *  seguia comiendose las teclas. */
			m_widget->setEnabled(false);
			if (m_quedarse)
			{
				statusBar()->showMessage(QStringLiteral(
					"desconectado — Ctrl+Shift+Q o cerrar la ventana"));
				return;
			}
			statusBar()->showMessage(QStringLiteral("desconectado — cerrando…"));
			QTimer::singleShot(1200, this, &VentanaPrincipal::close);
		});

		QObject::connect(sesion, &S::errorOccurred,
		                 [this](const QString &mensaje) {
			MostrarError(this, mensaje);

			/*  NUNCA SE LLEGO A CONECTAR: no hay sesion a la que volver.
			 *
			 *  Antes la ventana se quedaba abierta con una pantalla negra y
			 *  el error en la barra: sin host, sin teclado que sirva, y sin
			 *  forma de corregir el dato mal escrito salvo cerrar todo y
			 *  arrancar de nuevo. Un error del que no se sale no es un
			 *  error, es un pozo.
			 *
			 *  Si se entro por el formulario se vuelve al formulario, con lo
			 *  que ya estaba escrito: el caso tipico es una direccion mal
			 *  tipeada, y ahi lo unico que hace falta es corregir un digito.
			 *  Si se entro por la linea de comandos no hay a donde volver, y
			 *  se sale con codigo de error, que es lo que espera un script.
			 *
			 *  Un error DESPUES de conectar es otra cosa y no pasa por aca:
			 *  ahi hay una sesion, y la maneja el camino de disconnected. */
			if (m_llegoAConectar) return;

			/*  Solo se baja la bandera y se cierra la ventana. Quien decide
			 *  que hacer es main(), mirando arranco().
			 *
			 *  ACA HABIA UN DEFECTO, y vale la pena que quede escrito porque
			 *  no se ve leyendo: esto llamaba a QCoreApplication::exit() para
			 *  pedir la vuelta al formulario. Por ssh la conexion falla
			 *  DENTRO del constructor, o sea antes de que arranque exec(), y
			 *  exit() ahi no es inocuo: marca el hilo con "hay que salir", y
			 *  el proximo bucle de eventos vuelve en el acto. El proximo
			 *  bucle era el exec() del formulario. Resultado: salia el cartel
			 *  del timeout y despues no aparecia nada.
			 *
			 *  Cerrar la ventana alcanza: con quitOnLastWindowClosed, si el
			 *  bucle estaba corriendo termina solo, y si no estaba corriendo
			 *  no hay nada que terminar.                                  */
			m_arranco = false;
			close();
		});

		const char *etiqueta = m_esSsh ? "[ssh]" : "[telnet]";
		QObject::connect(sesion, &S::trace,
		                 [etiqueta](const QString &linea) {
			qInfo().noquote() << etiqueta << linea;
		});

		/*  El modo cambia en banda, no por telnet, asi que se refresca
		 *  cuando el host termina de dibujar. */
		QObject::connect(m_widget, &Vt6530Widget::hostWaiting,
		                 [this]() { actualizarModo(); });

		/*  El eco.
		 *
		 *  Confirmado con una traza en vivo: rci3 devuelve cada linea que
		 *  recibe -- "logon nkaizen.jaracena" volvio tal cual -- y la unica
		 *  que no devolvio fue la clave, donde mando solo CR LF.
		 *
		 *      => term: logon nkaizen.jaracena<0D>
		 *      <= host: logon nkaizen.jaracena<0D><0A>
		 *      <= host: Password:
		 *      => term: <la clave><0D>
		 *      <= host: <0D><0A>                  <-- la clave no vuelve
		 *
		 *  Y nosotros le contestamos DO ECHO, o sea que le pedimos que se
		 *  encargue el. Hacer ademas eco local duplicaba todo: por eso
		 *  "sysinfo" aparecia dos veces.
		 *
		 *  Asi que en automatico: si el host hace el eco, el terminal no.
		 *  La clave queda oculta sola, porque el host no la devuelve.    */
		/*  Esto valia solo para telnet hasta que la primera conexion ssh real
		 *  mostro un IAC WILL ECHO adentro del canal: STN atiende ssh con el
		 *  mismo servicio que telnet, asi que la negociacion es la misma y
		 *  las dos sesiones emiten las mismas senales. Aca habia un
		 *  "if constexpr" separandolas; ya no hace falta.                  */
		QObject::connect(sesion, &S::negotiationChanged,
		                 [this]() { aplicarEco(); });

		/*  Y ademas, la peticion de lectura de TELSERV trae un bit que dice
		 *  si esa lectura lleva eco. Es redundante con lo de arriba en este
		 *  host, pero es explicito y no cuesta nada respetarlo: si alguna
		 *  vez el host cambia de idea, gana el bit.                       */
		QObject::connect(sesion, &S::lineReadRequested,
		                 [this](int maxBytes, bool eco) {
			m_lecturas++;
			if (!eco)
			{
				m_terminal->SetLocalEcho(false);
				statusBar()->showMessage(
					QStringLiteral("lectura sin eco (%1 bytes)").arg(maxBytes),
					6000);
			}
			else
			{
				aplicarEco();
			}
		});

		QObject::connect(m_widget, &Vt6530Widget::unsupportedKey,
		                 [this](const QString &motivo) {
			statusBar()->showMessage(motivo, 6000);
		});

		/*  La traza del teclado, solo con --traza. Sirve para lo unico que
		 *  desde afuera no se puede saber: si una combinacion llego o se la
		 *  quedo el escritorio. Una tecla que no aparece aca nunca llego. */
		if (traza)
		{
			QObject::connect(m_widget, &Vt6530Widget::keyTrace,
			                 [](const QString &linea) {
				qInfo().noquote() << "[tecla]" << linea;
			});
		}

		/*  m_eco, no "eco": la variable local vive en el constructor y esto
		 *  es otra funcion. El compilador de Juan lo encontro y el mio no,
		 *  porque aca no hay Qt para instanciar la plantilla. */
		if (m_eco == Eco::Local)
		{
			/*  Y el aviso que corresponde. Con eco local, lo que impide que
			 *  una clave se dibuje es la peticion de lectura de TELSERV con
			 *  el bit de eco en cero. En la sesion que trazamos ese host no
			 *  mando ninguna peticion, asi que hasta ver una, este modo
			 *  muestra la clave.                                          */
			qWarning().noquote()
				<< "[eco] local: si el host no manda peticiones de lectura,"
				<< "la clave se va a ver al teclearla";
		}
	}

	/** Decide quien hace el eco y se lo dice al terminal. */
	void aplicarEco()
	{
		bool local;
		switch (m_eco)
		{
			case Eco::Local: local = true;  break;
			case Eco::Host:  local = false; break;
			/*  Lo decide la negociacion, y por ssh tambien: adentro del
			 *  canal viaja el mismo telnet. Esto antes asumia que por ssh
			 *  el eco lo hacia el host y lo dejaba anotado como sin
			 *  confirmar; ya no hace falta suponer nada.               */
			default:
				local = m_esSsh ? !m_ssh->hostEchoes()
				                : !m_sesion->hostEchoes();
				break;
		}
		if (local == m_terminal->LocalEcho()) return;
		m_terminal->SetLocalEcho(local);
		qInfo().noquote() << "[eco]"
			<< (local ? "lo dibuja el terminal"
			          : "lo dibuja el host (el terminal no)");
	}

	void actualizarModo()
	{
		const char *modo = m_terminal->IsProtectMode() ? "bloque protegido"
		                 : m_terminal->IsBlockMode()   ? "bloque"
		                                               : "conversacional";
		/*  Se llama por cada paquete del host: sin esto la barra parpadea
		 *  y se come los mensajes temporales (la lectura sin eco, la tecla
		 *  no soportada). */
		if (modo == m_modo) return;
		m_modo = modo;
		QString msg = QStringLiteral("%1:%2 — modo %3")
		              .arg(m_host).arg(m_puerto).arg(QLatin1String(modo));
		if (m_eco == Eco::Local && m_lecturas == 0)
			msg += QStringLiteral("   ⚠ eco local sin peticiones de lectura");
		statusBar()->showMessage(msg);
	}

	QString          m_host;
	quint16          m_puerto;
	bool             m_quedarse;
	Eco              m_eco;
	int              m_lecturas = 0;   /* peticiones de lectura vistas */
	const char      *m_modo = nullptr; /* ultimo modo mostrado */
	Tn6530Session   *m_sesion = nullptr;
	Ssh6530Session  *m_ssh    = nullptr;
	bool             m_esSsh  = false;
	bool             m_arranco = false;        /* la conexion no fallo de entrada */
	bool             m_llegoAConectar = false; /* hubo senal connected */
	Vt6530Terminal  *m_terminal;
	Vt6530Widget    *m_widget;
};

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QApplication::setApplicationName(QStringLiteral("vt6530qt"));

	/*  DOS ENTRADAS, UNA SOLA SALIDA
	 *
	 *  Sin argumentos sale el formulario, al estilo de PuTTY; con argumentos
	 *  manda la linea de comandos. Los dos caminos terminan en el mismo
	 *  cli::Opciones y de ahi en adelante el codigo es uno solo: si el
	 *  formulario armara la sesion por su cuenta, habria dos formas de
	 *  configurarla y se irian separando. En este proyecto ya paso con la
	 *  politica de ssh, copiada en main.cpp y en sonda_ssh.cpp.
	 *
	 *  El parseo vive en apps/Opciones.cpp y el formulario en
	 *  formularioconexion.cpp; los dos llaman a cli::AplicarDefectos(), que
	 *  es donde estan los cuatro valores que hacen que el host arranque TACL
	 *  en una ventana 6530.                                                */
	/*  El formulario se crea UNA vez y se reusa: asi conserva lo que se
	 *  escribio. Cuando una conexion falla se vuelve a el, y el caso tipico
	 *  -- una direccion mal tipeada -- se arregla corrigiendo un digito en
	 *  vez de volver a cargar todo.                                       */
	FormularioConexion *formulario = nullptr;
	if (argc <= 1)
	{
		g_modoGrafico = true;
		formulario = new FormularioConexion();
	}

	/*  Y el bucle existe por lo mismo: si no se pudo conectar, el programa
	 *  vuelve a preguntar en vez de quedarse con una ventana negra y un
	 *  error en la barra de estado. Por linea de comandos no hay a donde
	 *  volver, asi que ahi se sale con codigo de error.                   */
	int salida = 0;

	for (;;)
	{
		cli::Opciones op;

		if (g_modoGrafico)
		{
			if (formulario->exec() != QDialog::Accepted) break;
			op = formulario->opciones();
		}
		else
		{
			std::vector<std::string> args;
			for (int i = 1; i < argc; i++) args.push_back(std::string(argv[i]));

			op = cli::Parsear(args);

			if (op.ayuda)
			{
				std::fputs(cli::Uso("vt6530qt").c_str(), stdout);
				break;
			}
			if (!op.error.empty())
			{
				std::fprintf(stderr, "%s\n\n", op.error.c_str());
				std::fputs(cli::Uso("vt6530qt").c_str(), stderr);
				salida = 2;
				break;
			}
		}

		/*  Los avisos se dicen y se sigue: ninguno impide conectar, y frenar
		 *  al operador por algo que no es fatal es peor que el aviso.      */
		for (const std::string &aviso : op.avisos)
		{
			std::fprintf(stderr, "aviso: %s\n", aviso.c_str());
		}

		if (op.transporte == cli::Transporte::Ssh && !ssh6530::Disponible())
		{
			/*  Este si detiene, y a proposito: caer a telnet sin cifrar
			 *  cuando alguien pidio ssh seria una sorpresa fea. Pero en modo
			 *  grafico se vuelve al formulario, donde se puede elegir telnet
			 *  a sabiendas.                                                */
			const QString falta = QObject::tr(
				"Este binario se compilo sin soporte ssh.\n\n"
				"Instale libssh2 (dnf install libssh2-devel, o en MSYS2\n"
				"pacman -S mingw-w64-ucrt-x86_64-libssh2) y recompile.");

			std::fprintf(stderr, "%s\n", falta.toStdString().c_str());

			if (g_modoGrafico)
			{
				QMessageBox::critical(nullptr, QStringLiteral("vt6530"), falta);
				continue;
			}
			salida = 2;
			break;
		}

		/*  El log del nucleo va a la consola; todavia no hay ventana de
		 *  diagnostico donde ponerlo. */
		Log::SetQuiet(false);

		VentanaPrincipal ventana(op);

		/*  Los dos transportes fallan en momentos distintos y arranco() los
		 *  cubre a los dos:
		 *
		 *    ssh    -- Connect() bloquea hasta el timeout y contesta, asi que
		 *              aca ya se sabe y no se llega a mostrar la ventana;
		 *    telnet -- QTcpSocket conecta en diferido, la ventana se muestra,
		 *              y el fracaso llega durante exec(); el manejador de
		 *              errorOccurred baja la bandera y cierra.
		 *
		 *  Por eso se mira DESPUES de exec() y no antes.               */
		if (ventana.arranco())
		{
			ventana.show();
			app.exec();
		}

		if (!ventana.arranco())
		{
			if (g_modoGrafico) continue;   /* a corregir el dato */
			salida = 2;
			break;
		}

		salida = 0;
		break;
	}

	delete formulario;
	return salida;
}
