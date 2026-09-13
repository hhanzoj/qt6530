/*
 *  sonda_ssh -- abre el canal 6530 sobre SSH y vuelca lo que pase, sin Qt.
 *
 *  POR QUE EXISTE
 *
 *  Cuando vt6530qt se queda mudo despues del banner de STN hay tres
 *  sospechosos, y desde la ventana no se distinguen: el transporte, el pegado
 *  con Qt, o el host, que puede estar esperando algo. Esta sonda saca a Qt de
 *  la ecuacion: mismo Ssh6530Transport, misma capa Tn6530Telnet, pero el
 *  bucle es un while con select y cada byte sale en hexadecimal con su marca
 *  de tiempo.
 *
 *      si la sonda muestra el prompt de TACL  -> el transporte anda,
 *                                                el problema es el Qt
 *      si la sonda tambien se queda muda      -> el host espera algo, y lo
 *                                                que espera esta en el hex
 *      si deja de decir "sigo vivo"           -> el transporte, y ahi mismo
 *                                                se ve donde
 *
 *  Las marcas de tiempo son la mitad del valor. "Ultima lectura hace 12s"
 *  distingue un programa colgado de un host que no tiene nada que decir, que
 *  es justo lo que a ojo se confunde.
 *
 *  LA CAPA TELNET VA ADENTRO, Y NO ES UN DETALLE
 *
 *  Adentro del canal ssh viaja telnet: STN abre con IAC WILL ECHO, WILL SGA,
 *  DO NAWS. La primera version de esta sonda no lo contestaba, y entonces no
 *  servia para lo que hacia falta -- un host que esta a mitad de una
 *  negociacion no procesa lo que uno teclee. Con -crudo se vuelve a ese
 *  comportamiento a proposito, que sirve para ver que manda el host cuando el
 *  terminal se queda callado.
 *
 *  USO
 *
 *      sonda_ssh usuario@host [-P 22] [-comando tacl] [-segundos 20]
 *                             [-enviar "sysinfo\r"] [-esperar 3]
 *                             [-naws] [-linemode] [-crudo] [-sin-pty] [-shell]
 *                             [-intentos 3]
 *
 *  -enviar manda ese texto despues de -esperar segundos, para ver si el host
 *  contesta a algo. \r \n \t \\ y \xNN se interpretan.
 */

#include "../apps/Consola.h"
#include "../apps/Opciones.h"
#include "../apps/PoliticaSsh.h"
#include "../net/Ssh6530Transport.h"
#include "../net/Tn6530Telnet.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace {

double Ahora()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

double g_inicio = 0.0;
double g_ultima = 0.0;

void Marca(const char *que)
{
	std::printf("[%7.3f] %s\n", Ahora() - g_inicio, que);
	std::fflush(stdout);
}

/*  Volcado en hexadecimal con la columna de texto al lado. Los bytes de
 *  telnet y los de control del 6530 se leen mejor asi que en cualquier
 *  intento de "interpretarlos" aca.                                     */
void Volcar(const char *rotulo, const char *datos, int n)
{
	const double t = Ahora();
	std::printf("[%7.3f] %s %d byte(s)", t - g_inicio, rotulo, n);
	if (g_ultima > 0.0)
		std::printf("   (%.3fs desde la anterior)", t - g_ultima);
	std::printf("\n");
	g_ultima = t;

	for (int i = 0; i < n; i += 16)
	{
		std::printf("           %04x  ", i);
		for (int j = 0; j < 16; j++)
		{
			if (i + j < n)
				std::printf("%02x ", (unsigned char)datos[i + j]);
			else
				std::printf("   ");
			if (j == 7) std::printf(" ");
		}
		std::printf(" |");
		for (int j = 0; j < 16 && i + j < n; j++)
		{
			const unsigned char c = (unsigned char)datos[i + j];
			std::printf("%c", (c >= 32 && c < 127) ? c : '.');
		}
		std::printf("|\n");
	}
	std::fflush(stdout);
}

/*  \r \n \t \\ y \xNN, para poder mandar un CR desde la linea de comandos. */
std::string Desescapar(const std::string &s)
{
	std::string r;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] != '\\' || i + 1 >= s.size()) { r += s[i]; continue; }
		switch (s[++i])
		{
			case 'r': r += '\r'; break;
			case 'n': r += '\n'; break;
			case 't': r += '\t'; break;
			case '0': r += '\0'; break;
			case '\\': r += '\\'; break;
			case 'x':
			{
				std::string hex;
				while (hex.size() < 2 && i + 1 < s.size() &&
				       std::isxdigit((unsigned char)s[i + 1]))
					hex += s[++i];
				if (!hex.empty())
					r += (char)std::strtol(hex.c_str(), nullptr, 16);
				break;
			}
			default: r += s[i]; break;
		}
	}
	return r;
}

bool PedirClave(const std::string &usuario, int intento, std::string *clave)
{
	if (intento > 1) std::fprintf(stderr, "clave incorrecta.\n");
	std::fprintf(stderr, "clave de %s: ", usuario.c_str());
	std::fflush(stderr);

	/*  El apagado del eco vive en apps/Consola.cpp, no aca: era la misma
	 *  rutina copiada en dos archivos, y la copia de la aplicacion Qt
	 *  resulto estar escrita solo para POSIX.                           */
	return consola::LeerLineaSinEco(clave);
}

/**
 *  La huella de un host nuevo, y si/no.
 *
 *  La sonda es donde mas conviene probar esto: no hay Qt en el medio, asi
 *  que lo que se ve es exactamente lo que hace el transporte.
 */
bool PreguntarPorHostNuevo(const std::string &host, const std::string &huella)
{
	if (!consola::Hay()) return false;

	std::fprintf(stderr,
		"\nEl host '%s' no esta en known_hosts.\n"
		"Su huella es:\n"
		"    %s\n"
		"Si acepta, queda anotada y no se vuelve a preguntar.\n"
		"\n"
		"Aceptar y anotar la clave? (si/no) ",
		host.c_str(), huella.c_str());
	std::fflush(stderr);

	std::string r;
	if (!consola::LeerLinea(&r))
	{
		std::fprintf(stderr, "\n");
		return false;
	}

	/*  Solo un si explicito: un ENTER de apuro no acepta nada. */
	const bool acepta = consola::EsSi(r);
	if (!acepta) std::fprintf(stderr, "no se acepto.\n");
	return acepta;
}

} // namespace

int main(int argc, char **argv)
{
	std::vector<std::string> args(argv + 1, argv + argc);

	/*  Estas tres son de la sonda y no de la aplicacion, asi que se sacan
	 *  antes de dejar que cli::Parsear vea el resto.                     */
	int segundos = 20;
	int esperar  = 3;
	bool crudo   = false;
	bool sinPty  = false;
	bool shell   = false;
	int  intentos = 0;   /* 0 = el que trae la politica */
	std::string enviar;

	std::vector<std::string> resto;
	for (size_t i = 0; i < args.size(); i++)
	{
		const std::string &a = args[i];
		const bool hayValor = (i + 1 < args.size());
		if ((a == "-segundos" || a == "--segundos") && hayValor)
			{ segundos = std::atoi(args[++i].c_str()); continue; }
		if ((a == "-esperar" || a == "--esperar") && hayValor)
			{ esperar = std::atoi(args[++i].c_str()); continue; }
		if ((a == "-enviar" || a == "--enviar") && hayValor)
			{ enviar = Desescapar(args[++i]); continue; }
		if (a == "-crudo" || a == "--crudo")
			{ crudo = true; continue; }
		if (a == "-sin-pty" || a == "--sin-pty")
			{ sinPty = true; continue; }
		if (a == "-shell" || a == "--shell")
			{ shell = true; continue; }
		if ((a == "-intentos" || a == "--intentos") && hayValor)
			{ intentos = std::atoi(args[++i].c_str()); continue; }
		resto.push_back(a);
	}

	if (!ssh6530::Disponible())
	{
		std::fprintf(stderr,
			"este binario se compilo sin soporte ssh.\n"
			"  dnf install libssh2-devel   (o pacman -S "
			"mingw-w64-ucrt-x86_64-libssh2)\n");
		return 2;
	}

	cli::Opciones op = cli::Parsear(resto);
	op.transporte = cli::Transporte::Ssh;
	if (op.puerto == 23) op.puerto = 22;
	if (op.comando.empty()) op.comando = "tacl";

	if (!op.error.empty())
	{
		std::fprintf(stderr, "%s\n\nuso: sonda_ssh usuario@host [-P 22] "
		                     "[-comando tacl] [-segundos 20] "
		                     "[-enviar \"sysinfo\\r\"] [-esperar 3]\n"
		                     "     [-naws] [-linemode] [-crudo] [-sin-pty] [-shell]\n",
		             op.error.c_str());
		return 2;
	}
	for (const std::string &aviso : op.avisos)
		std::fprintf(stderr, "[aviso] %s\n", aviso.c_str());

	/*  La misma traduccion que usa la aplicacion Qt, no una copia: si la
	 *  sonda armara la sesion distinto, dejaria de servir para lo unico que
	 *  existe, que es reproducir lo que hace la aplicacion sin Qt.       */
	ssh6530::Politica pol = cli::PoliticaSshDesde(op);

	/*  Las de la sonda, que no salen de las opciones comunes. */
	pol.pedirPty  = !sinPty;
	pol.usarShell = shell;
	if (intentos > 0) pol.intentosDeClave = intentos;

	tn6530::Policy polTelnet;
	polTelnet.terminalType   = op.tipoTerminal;
	polTelnet.acceptNaws     = op.naws;
	polTelnet.acceptLinemode = op.linemode;

	ssh6530::Ssh6530Transport t(pol);
	tn6530::Tn6530Telnet telnet(polTelnet);
	g_inicio = Ahora();

	t.onTrace      = [](const std::string &s) { Marca(s.c_str()); };
	t.onPedirClave = [](const std::string &u, int intento, std::string *c) {
		return PedirClave(u, intento, c);
	};
	t.onHostDesconocido = [](const std::string &h, const std::string &f) {
		return PreguntarPorHostNuevo(h, f);
	};

	/*  Los bytes crudos del canal se vuelcan SIEMPRE, aunque despues los
	 *  procese telnet: cuando algo no anda, lo que importa es lo que llego,
	 *  no lo que quedo despues de interpretarlo.                         */
	t.onRawFromHost = [](const char *d, int n) { Volcar("<= host", d, n); };

	if (crudo)
	{
		/*  Modo crudo: nadie contesta la negociacion. Sirve para ver que
		 *  manda el host cuando el terminal se queda callado.           */
		Marca("modo crudo: no se contesta la negociacion telnet");
	}
	else
	{
		/*  Lo normal, e identico a lo que hace la aplicacion Qt: la capa
		 *  telnet contesta la negociacion y separa el payload.          */
		tn6530::Events ev;
		ev.onPayload = [](const unsigned char *d, int n) {
			Volcar("   payload", (const char *)d, n);
		};
		ev.onSend = [&t](const unsigned char *d, int n) {
			Volcar("=> term (negociacion)", (const char *)d, n);
			t.SendRaw((const byte *)d, n);
		};
		ev.onTrace = [](const std::string &s) { Marca(s.c_str()); };
		ev.onEndOfRecord = []() { Marca("IAC EOR"); };
		ev.onLineRead = [](const tn6530::LineRead &l) {
			char linea[96];
			std::snprintf(linea, sizeof(linea),
			              "peticion de lectura: %d bytes, eco %s",
			              l.maxBytes, l.echo ? "si" : "no");
			Marca(linea);
		};
		telnet.SetEvents(ev);

		t.onHostData = [&telnet](const char *d, int n) {
			telnet.Feed((const unsigned char *)d, n);
		};
	}

	std::string error;
	if (!t.Connect(op.host, op.puerto, &error))
	{
		std::fprintf(stderr, "no conecto: %s\n", error.c_str());
		return 1;
	}
	Marca("conectado; escuchando");

	/*  El bucle. Poll(200) devuelve cada 200ms haya o no datos, asi que si
	 *  esto deja de imprimir "sigo vivo" el que se colgo es el transporte y
	 *  no el host -- que es exactamente lo que hay que distinguir.       */
	const double fin = Ahora() + segundos;
	bool enviado = enviar.empty();
	double ultimoLatido = 0.0;

	while (Ahora() < fin)
	{
		if (!t.Poll(200))
		{
			Marca("el canal se cerro");
			break;
		}

		if (!enviado && (Ahora() - g_inicio) >= esperar)
		{
			Volcar("=> term", enviar.data(), (int)enviar.size());
			/*  Por la capa telnet, como manda Guardian: escapa los IAC y
			 *  cierra el registro si se nego EOR. En crudo, derecho al
			 *  canal.                                                   */
			if (crudo)
				t.SendRaw((const byte *)enviar.data(), (int)enviar.size());
			else
				telnet.SendPayload((const unsigned char *)enviar.data(),
				                   (int)enviar.size());
			enviado = true;
		}

		const double ahora = Ahora();
		if (ahora - ultimoLatido >= 2.0)
		{
			ultimoLatido = ahora;
			char linea[128];
			if (g_ultima > 0.0)
				std::snprintf(linea, sizeof(linea),
				              "sigo vivo (ultima lectura hace %.1fs)",
				              ahora - g_ultima);
			else
				std::snprintf(linea, sizeof(linea),
				              "sigo vivo (todavia no llego nada)");
			Marca(linea);
		}
	}

	Marca("fin");
	std::printf("\n%s\n", t.Resumen().c_str());
	t.Close();
	return 0;
}
