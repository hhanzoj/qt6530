#include "Opciones.h"

#include <cstdlib>
#include <cstring>

namespace cli {

namespace {

/*  Saca los guiones de adelante. -ssh y --ssh son la misma opcion: PuTTY usa
 *  uno solo y el resto del proyecto venia con dos, y no vale la pena hacer
 *  elegir a nadie.                                                          */
std::string SinGuiones(const std::string &s)
{
	size_t i = 0;
	while (i < s.size() && s[i] == '-' && i < 2) i++;
	return s.substr(i);
}

bool EsOpcion(const std::string &s)
{
	return s.size() > 1 && s[0] == '-';
}

/*  El usuario del sistema, para cuando no se dio ninguno.
 *
 *  En telnet no hace falta: TELSERV presenta su menu y el logon de Guardian
 *  se teclea adentro de la sesion. En ssh no existe ese momento -- SSH
 *  autentica ANTES de que TACL arranque --, asi que el usuario tiene que
 *  estar cuando se abre el canal. Si no se dio, se toma el del sistema, que
 *  es lo que hacen ssh y PuTTY, y se avisa cual se tomo.
 *
 *  USERNAME es el de Windows; USER y LOGNAME los de Unix. Se miran los tres
 *  para no tener que partir este archivo por sistema operativo.           */
std::string UsuarioDelSistema()
{
	static const char *const vars[] = { "USER", "LOGNAME", "USERNAME" };
	for (const char *v : vars)
	{
		const char *valor = std::getenv(v);
		if (valor != nullptr && *valor != '\0') return valor;
	}
	return std::string();
}

/*  Parte "usuario@host" en sus dos mitades. Sin arroba, todo es host.
 *  Se parte por la ULTIMA arroba: un usuario Guardian puede tener puntos,
 *  y aunque hoy no lleve arrobas, partir por la ultima es lo que hace ssh. */
void PartirUsuarioHost(const std::string &s, std::string *usuario,
                       std::string *host)
{
	const size_t at = s.rfind('@');
	if (at == std::string::npos)
	{
		*host = s;
		return;
	}
	*usuario = s.substr(0, at);
	*host    = s.substr(at + 1);
}

bool ANumero(const std::string &s, int *out)
{
	if (s.empty()) return false;
	for (size_t i = 0; i < s.size(); i++)
	{
		if (s[i] < '0' || s[i] > '9') return false;
	}
	const long v = std::strtol(s.c_str(), nullptr, 10);
	if (v <= 0 || v > 65535) return false;
	*out = (int)v;
	return true;
}

} // namespace

int PuertoPorDefecto(Transporte t)
{
	return (t == Transporte::Ssh) ? 22 : 23;
}

const char *ComandoPorDefecto(Transporte t)
{
	/*  En SSH hay que pedir exec "tacl" para caer en Guardian; pidiendo
	 *  shell se cae en OSS, que no es lo que este emulador dibuja. En
	 *  telnet no hay comando: TELSERV presenta su propio menu.           */
	return (t == Transporte::Ssh) ? "tacl" : "";
}

bool UsuarioValido(const std::string &usuario)
{
	if (usuario.empty()) return false;
	if (usuario.size() > (std::size_t)kUsuarioLargoMaximo) return false;

	for (std::size_t i = 0; i < usuario.size(); i++)
	{
		const unsigned char c = (unsigned char)usuario[i];
		const bool letra  = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
		const bool digito = (c >= '0' && c <= '9');
		const bool signo  = (c == '.' || c == '-' || c == '_');
		if (!letra && !digito && !signo) return false;
	}
	return true;
}

void AplicarDefectos(Opciones *o, const Dados &dados)
{
	if (o == nullptr) return;

	/*  Se mira ANTES de rellenar el comando por defecto, o el centinela se
	 *  pierde: despues de la linea de abajo, o->comando nunca esta vacio. */
	const bool comandoDado = dados.comando || !o->comando.empty();

	if (o->puerto == 0) o->puerto  = PuertoPorDefecto(o->transporte);
	if (!comandoDado)   o->comando = ComandoPorDefecto(o->transporte);

	/*  Los tres valores por defecto de ssh, que salieron de una sesion que
	 *  funciona de punta a punta contra rci3 el 11/09:
	 *
	 *      TERM del pty-req = TN6530-8   (en mayusculas, como OutsideView)
	 *      shell, no exec
	 *      LINEMODE ofrecido
	 *
	 *  Los tres juntos, mas el WILL TERMINAL-TYPE que ahora manda
	 *  Tn6530Telnet::Start(), son lo que hace que el host conteste
	 *  "STN44 Application ... has connected to this window" y arranque TACL
	 *  en una ventana 6530 de verdad, con modo bloque, VIEWSYS y las teclas
	 *  de funcion. Sin alguno de ellos la sesion queda muda o cae en OSS.
	 *
	 *  Cada uno se puede apagar por separado -- -tipo-pty, -exec,
	 *  -sin-linemode -- porque otro host puede estar armado distinto.     */
	if (o->transporte == Transporte::Ssh)
	{
		if (!dados.linemode) o->linemode = true;
		if (!dados.shell)    o->shell    = !comandoDado;
	}

	/*  El TERM del pty-req en ssh: TN6530-8.
	 *
	 *  En MAYUSCULAS, que es lo que manda OutsideView. Se lo capturo con
	 *  tools/tap6530ssh.py y se replico tal cual, porque con esa forma el
	 *  host arma la ventana 6530 y engancha la aplicacion.
	 *
	 *  Hubo una etapa en que esto decia "xterm", porque con tn6530-8 la
	 *  sesion quedaba muda. Era cierto, pero la causa no era el TERM: era que
	 *  nunca ofreciamos WILL TERMINAL-TYPE y el host no llegaba a preguntar
	 *  el tipo. Con eso arreglado, TN6530-8 es lo correcto y xterm deja en
	 *  OSS, que es donde no queremos estar.
	 *
	 *  Sigue siendo distinto del terminal-type de telnet (-tipo), que va en
	 *  minusculas dentro de la subnegociacion. Son dos canales.           */
	if (o->transporte == Transporte::Ssh && o->tipoPty.empty())
	{
		o->tipoPty = "TN6530-8";
	}

	/*  Diferencia real con telnet, y vale la pena que el aviso la explique:
	 *  por telnet uno se loguea adentro de la sesion, por ssh el usuario
	 *  viaja en la autenticacion y tiene que estar de antemano.          */
	if (o->transporte == Transporte::Ssh && o->usuario.empty())
	{
		o->usuario = UsuarioDelSistema();
		if (o->usuario.empty())
		{
			o->error = "ssh necesita un usuario y no se pudo deducir del "
			           "sistema: usar 'usuario@host' o -l <usuario>";
			return;
		}
		o->avisos.push_back("ssh autentica antes de arrancar TACL, asi que "
		                    "el usuario no se puede teclear despues como en "
		                    "telnet; se usa '" + o->usuario + "' (el del "
		                    "sistema). Para otro: 'usuario@host' o -l");
	}

}

Opciones Parsear(const std::vector<std::string> &args)
{
	Opciones o;
	bool puertoDado    = false;
	bool comandoDado   = false;
	bool transporteDado = false;
	bool linemodeDado  = false;
	bool shellDado     = false;
	std::string usuarioDeOpcion;

	for (size_t i = 0; i < args.size(); i++)
	{
		const std::string &a = args[i];

		if (!EsOpcion(a))
		{
			if (!o.host.empty())
			{
				o.error = "sobra un argumento: '" + a + "'";
				return o;
			}
			PartirUsuarioHost(a, &o.usuario, &o.host);
			if (o.host.empty())
			{
				o.error = "falta el host en '" + a + "'";
				return o;
			}
			continue;
		}

		const std::string n = SinGuiones(a);

		/* --- las que no llevan valor --- */
		if (n == "ssh")      { o.transporte = Transporte::Ssh;    transporteDado = true; continue; }
		if (n == "telnet")   { o.transporte = Transporte::Telnet; transporteDado = true; continue; }
		if (n == "traza")    { o.traza = true;    continue; }
		if (n == "quedarse") { o.quedarse = true; continue; }
		if (n == "linemode")     { o.linemode = true;  linemodeDado = true; continue; }
		if (n == "sin-linemode") { o.linemode = false; linemodeDado = true; continue; }
		if (n == "naws")     { o.naws     = true; continue; }
		if (n == "pty-cocido") { o.ptyCrudo = false; continue; }
		if (n == "sin-filtro-eco") { o.filtrarEco = false; continue; }
		if (n == "shell")     { o.shell = true;  shellDado = true; continue; }
		if (n == "exec")      { o.shell = false; shellDado = true; continue; }

		/*  Esta bandera ya estaba nombrada en el mensaje de error del
		 *  transporte -- "o use -aceptar-host-nuevo si sabe lo que hace" --
		 *  y nunca habia existido. Prometer una salida que no esta es peor
		 *  que no ofrecer ninguna: el que la busca cree que se equivoco el. */
		if (n == "aceptar-host-nuevo")  { o.hostNuevo = HostNuevo::Aceptar;  continue; }
		if (n == "rechazar-host-nuevo") { o.hostNuevo = HostNuevo::Rechazar; continue; }

		if (n == "h" || n == "help" || n == "ayuda") { o.ayuda = true; continue; }

		/* --- las que llevan valor --- */
		const bool ultima = (i + 1 >= args.size());
		auto valor = [&](const char *nombre) -> std::string {
			if (ultima)
			{
				o.error = std::string("a -") + nombre + " le falta el valor";
				return std::string();
			}
			return args[++i];
		};

		if (n == "P" || n == "puerto")
		{
			const std::string v = valor("P");
			if (!o.error.empty()) return o;
			if (!ANumero(v, &o.puerto))
			{
				o.error = "puerto invalido: '" + v + "'";
				return o;
			}
			puertoDado = true;
			continue;
		}
		if (n == "l" || n == "usuario")
		{
			usuarioDeOpcion = valor("l");
			if (!o.error.empty()) return o;
			continue;
		}
		if (n == "comando")
		{
			o.comando = valor("comando");
			if (!o.error.empty()) return o;
			comandoDado = true;
			continue;
		}
		if (n == "i" || n == "identidad")
		{
			o.identidad = valor("i");
			if (!o.error.empty()) return o;
			continue;
		}
		if (n == "timeout")
		{
			const std::string v = valor("timeout");
			if (!o.error.empty()) return o;
			if (!ANumero(v, &o.timeoutSegundos) || o.timeoutSegundos <= 0)
			{
				o.error = "timeout invalido: '" + v + "' (segundos, mayor que cero)";
				return o;
			}
			continue;
		}
		if (n == "known-hosts")
		{
			o.knownHosts = valor("known-hosts");
			if (!o.error.empty()) return o;
			continue;
		}
		if (n == "tipo")
		{
			o.tipoTerminal = valor("tipo");
			if (!o.error.empty()) return o;
			continue;
		}
		if (n == "tipo-pty")
		{
			o.tipoPty = valor("tipo-pty");
			if (!o.error.empty()) return o;
			continue;
		}
		if (n == "eco")
		{
			const std::string v = valor("eco");
			if (!o.error.empty()) return o;
			if      (v == "auto")  o.eco = Eco::Auto;
			else if (v == "local") o.eco = Eco::Local;
			else if (v == "host")  o.eco = Eco::Host;
			else
			{
				o.error = "-eco tiene que ser auto, local o host; vino '" + v + "'";
				return o;
			}
			continue;
		}
		/*  Compatibilidad con la forma vieja: --host rci3 --puerto 23.    */
		if (n == "host")
		{
			const std::string v = valor("host");
			if (!o.error.empty()) return o;
			PartirUsuarioHost(v, &o.usuario, &o.host);
			continue;
		}

		o.error = "opcion desconocida: '" + a + "'";
		return o;
	}

	if (o.ayuda) return o;

	/*  -l gana sobre el usuario@host, que es lo que hace ssh: la opcion
	 *  explicita pisa a la que va pegada al host.                        */
	if (!usuarioDeOpcion.empty())
	{
		if (!o.usuario.empty() && o.usuario != usuarioDeOpcion)
		{
			o.avisos.push_back("se dieron dos usuarios ('" + o.usuario +
			                   "' y '" + usuarioDeOpcion + "'); vale el de -l");
		}
		o.usuario = usuarioDeOpcion;
	}

	if (o.host.empty())
	{
		o.error = "falta el host";
		return o;
	}

	Dados dados;
	dados.linemode = linemodeDado;
	dados.shell    = shellDado;
	dados.comando  = comandoDado;

	if (!puertoDado) o.puerto = 0;   /* el centinela que espera AplicarDefectos */
	AplicarDefectos(&o, dados);
	if (!o.error.empty()) return o;

	/*  Avisos no fatales: se dicen y se sigue. Ninguno impide conectar.  */
	if (o.transporte == Transporte::Telnet && comandoDado && !o.comando.empty())
	{
		o.avisos.push_back("-comando no se usa en telnet: TELSERV presenta "
		                   "su propio menu; se ignora");
	}
	if (o.transporte == Transporte::Telnet && !o.identidad.empty())
	{
		o.avisos.push_back("-i no se usa en telnet; se ignora");
	}
	/*  Ya no: adentro del canal ssh viaja el mismo telnet, con la misma
	 *  negociacion. LINEMODE y NAWS aplican en los dos transportes, y el
	 *  aviso que decia lo contrario era de cuando creiamos que ssh
	 *  reemplazaba a telnet y no solo al socket.                        */
	if (!transporteDado && o.puerto == 22)
	{
		o.avisos.push_back("el puerto 22 suele ser ssh, pero no se pidio -ssh; "
		                   "se conecta por telnet igual");
	}

	return o;
}

std::string Uso(const char *programa)
{
	std::string p = (programa != nullptr && *programa != '\0')
	              ? programa : "vt6530qt";
	return
		"Emulador de terminal 6530 para HP NonStop.\n"
		"\n"
		"  " + p + " [opciones] [usuario@]host\n"
		"\n"
		"transporte:\n"
		"  -ssh              SSH        (puerto 22, pide exec \"tacl\")\n"
		"  -telnet           telnet     (puerto 23, por defecto)\n"
		"  -P <puerto>       puerto, si no el del transporte\n"
		"\n"
		"sesion:\n"
		"  -l <usuario>      usuario; equivale a usuario@host.\n"
		"                    En ssh hace falta -- SSH autentica antes de que\n"
		"                    TACL arranque, no hay logon adentro como en\n"
		"                    telnet. Si falta, se usa el del sistema.\n"
		"  -comando <cmd>    el \"Remote command\" de PuTTY; en ssh, \"tacl\"\n"
		"  -i <archivo>      clave privada (ssh)\n"
		"  -tipo <tipo>      terminal-type a declarar (tn6530-8)\n"
		"  -tipo-pty <tipo>  TERM del pty-req de ssh (TN6530-8, en mayusculas).\n"
		"                    NO es lo mismo que -tipo: son dos campos, en dos\n"
		"                    protocolos distintos.\n"
		"\n"
		"la clave del host (ssh):\n"
		"  por defecto, un host que no esta en known_hosts se muestra con su\n"
		"  huella y se pregunta si aceptarlo; si se acepta, queda anotado en\n"
		"  ~/.ssh/known_hosts y no se vuelve a preguntar.\n"
		"  -aceptar-host-nuevo    aceptar y anotar sin preguntar (no\n"
		"                         interactivo)\n"
		"  -rechazar-host-nuevo   cortar sin preguntar\n"
		"  -known-hosts <ruta>    usar otro archivo\n"
		"  -timeout <seg>         espera para conectar (15)\n"
		"  una clave que CAMBIO corta siempre y no tiene bandera para\n"
		"  saltearla: es lo que se ve cuando alguien se mete en el medio.\n"
		"\n"
		"pantalla y diagnostico:\n"
		"  -eco auto|local|host   quien dibuja lo tecleado (auto)\n"
		"  -traza            volcar por consola lo que va y viene\n"
		"  -quedarse         no cerrar la ventana al cortar el host\n"
		"  -linemode         ofrecer y aceptar telnet LINEMODE. En ssh viene\n"
		"                    puesto: el host lo pide para armar la ventana 6530\n"
		"  -sin-linemode     apagarlo\n"
		"  -exec             pedir exec <comando> en vez de shell (ssh)\n"
		"  -naws             aceptar NAWS y mandar el tamano de la ventana\n"
		"  -shell            pedir shell en vez de exec. En ssh es lo normal:\n"
		"                    es lo que hace OutsideView y lo que deja la ventana\n"
		"                    6530 lista. Con -comando se usa exec.\n"
		"  -sin-filtro-eco   no descartar el eco que el host hace de lo que\n"
		"                    le mandamos. Por defecto se descarta: STN\n"
		"                    devuelve las secuencias AID y sus bytes\n"
		"                    terminan dibujados en la pantalla.\n"
		"  -pty-cocido       no pedir el pty de ssh en crudo. Por defecto se\n"
		"                    pide sin eco ni ISIG, que es lo que el modo\n"
		"                    bloque necesita.\n"
		"                    (por defecto se rechaza, como el emulador de\n"
		"                    referencia)\n"
		"\n"
		"ejemplos:\n"
		"  " + p + " rci3                       telnet al 23\n"
		"  " + p + " jaracena@rci3 -ssh         ssh al 22, shell, pty TN6530-8\n"
		"  " + p + " rci3 -ssh -comando viewsys\n";
}

} // namespace cli
