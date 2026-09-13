/*
 *  Pruebas de la linea de comandos.
 *
 *  El parseo no necesita Qt, y por eso vive aparte: asi se prueba con
 *  compilador en vez de a ojo. La aplicacion Qt no se puede compilar en el
 *  entorno donde se escribio este port, y todo lo que se pueda sacar de ahi
 *  y probar es una trampa menos.
 */

#include "../apps/Opciones.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace cli;

static int g_ok = 0, g_mal = 0;
static std::string g_prueba;

static void Check(bool cond, const std::string &que)
{
	if (cond) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLA  %s :: %s\n", g_prueba.c_str(), que.c_str());
}

static Opciones P(std::vector<std::string> args)
{
	return Parsear(args);
}

/*  Para la prueba del usuario por defecto hay que tocar el entorno, y ahi
 *  Windows y Unix no se ponen de acuerdo en el nombre de la funcion.     */
static void PonerEnv(const char *nombre, const char *valor)
{
#if defined(_WIN32)
	_putenv_s(nombre, valor);
#else
	setenv(nombre, valor, 1);
#endif
}

static void BorrarEnv(const char *nombre)
{
#if defined(_WIN32)
	_putenv_s(nombre, "");
#else
	unsetenv(nombre);
#endif
}

/* ------------------------------------------------------------------ */

static void Test_LoMinimo()
{
	g_prueba = "lo_minimo";
	const Opciones o = P({ "rci3" });
	Check(o.error.empty(), "un host solo alcanza");
	Check(o.host == "rci3", "el host se toma del argumento suelto");
	Check(o.transporte == Transporte::Telnet, "telnet por defecto");
	Check(o.puerto == 23, "y el 23 por defecto");
	Check(o.comando.empty(), "en telnet no hay comando");
	Check(o.usuario.empty(), "y no hay usuario");
}

static void Test_SshCambiaPuertoYComando()
{
	/*  Lo importante de esta: pedir -ssh cambia DOS cosas a la vez. El
	 *  puerto pasa a 22 y el comando pasa a "tacl", que es lo que hace
	 *  que STN arme un PTY 6530 en vez de dejarte en OSS.               */
	g_prueba = "ssh_cambia_puerto_y_comando";
	const Opciones o = P({ "rci3", "-ssh" });
	Check(o.error.empty(), "se acepta");
	Check(o.transporte == Transporte::Ssh, "queda en ssh");
	Check(o.puerto == 22, "el puerto por defecto pasa a 22");
	Check(o.comando == "tacl", "y el comando por defecto a tacl");
	std::printf("           -ssh -> puerto %d, comando \"%s\"\n",
	            o.puerto, o.comando.c_str());
}

static void Test_UsuarioArrobaHost()
{
	g_prueba = "usuario_arroba_host";
	const Opciones o = P({ "jaracena@rci3", "-ssh" });
	Check(o.usuario == "jaracena", "el usuario sale de antes de la arroba");
	Check(o.host == "rci3", "y el host de despues");

	/*  Un usuario Guardian lleva punto: nkaizen.jaracena@rci3. Se parte
	 *  por la ULTIMA arroba, asi que el punto no molesta.                */
	const Opciones g = P({ "nkaizen.jaracena@rci3" });
	Check(g.usuario == "nkaizen.jaracena", "el usuario Guardian queda entero");
	Check(g.host == "rci3", "y el host tambien");

	const Opciones malo = P({ "@rci3x", "-l", "" });
	Check(malo.error.empty() || !malo.error.empty(), "no revienta con arroba al principio");
}

static void Test_LaOpcionDeUsuarioGanaYAvisa()
{
	/*  Como en ssh: -l pisa al usuario pegado al host. Pero no se lo
	 *  traga en silencio, avisa -- y sigue, que es lo que uno quiere de
	 *  un aviso no fatal.                                                */
	g_prueba = "la_opcion_de_usuario_gana";
	const Opciones o = P({ "otro@rci3", "-l", "jaracena", "-ssh" });
	Check(o.error.empty(), "no es un error");
	Check(o.usuario == "jaracena", "gana el de -l");
	Check(o.avisos.size() == 1, "y avisa que habia dos");
	if (!o.avisos.empty())
		std::printf("           aviso: %s\n", o.avisos[0].c_str());
}

static void Test_LosDosGuionesSonLoMismo()
{
	/*  PuTTY usa un guion, el resto del proyecto venia con dos. No vale
	 *  la pena hacer elegir a nadie.                                     */
	g_prueba = "los_dos_guiones";
	const Opciones a = P({ "rci3", "-ssh", "-P", "2222" });
	const Opciones b = P({ "rci3", "--ssh", "--puerto", "2222" });
	Check(a.error.empty() && b.error.empty(), "las dos formas se aceptan");
	Check(a.transporte == b.transporte && a.puerto == b.puerto,
	      "y dan lo mismo");
	Check(a.puerto == 2222, "el -P explicito pisa al del transporte");
}

static void Test_LaFormaViejaSigueAndando()
{
	/*  --host rci3 --puerto 23 es como se venia invocando. Romperlo seria
	 *  gratuito.                                                          */
	g_prueba = "la_forma_vieja";
	const Opciones o = P({ "--host", "rci3", "--puerto", "23" });
	Check(o.error.empty(), "se sigue aceptando");
	Check(o.host == "rci3" && o.puerto == 23, "y significa lo mismo");
}

static void Test_ComandoExplicito()
{
	g_prueba = "comando_explicito";
	const Opciones o = P({ "rci3", "-ssh", "-comando", "viewsys" });
	Check(o.comando == "viewsys", "el comando explicito manda");

	/*  En telnet no hay comando que mandar: TELSERV presenta su menu. Se
	 *  avisa y se sigue, no se aborta.                                    */
	const Opciones t = P({ "rci3", "-telnet", "-comando", "tacl" });
	Check(t.error.empty(), "en telnet no es un error");
	Check(t.avisos.size() == 1, "pero avisa que no se usa");
}

static void Test_ErroresQueSiDetienen()
{
	g_prueba = "errores_que_detienen";

	Check(!P({}).error.empty(), "sin host es error");
	Check(!P({ "-ssh" }).error.empty(), "solo el transporte no alcanza");
	Check(!P({ "rci3", "-P" }).error.empty(), "-P sin valor es error");
	Check(!P({ "rci3", "-P", "cero" }).error.empty(), "puerto no numerico");
	Check(!P({ "rci3", "-P", "0" }).error.empty(), "puerto 0");
	Check(!P({ "rci3", "-P", "99999" }).error.empty(), "puerto fuera de rango");
	Check(!P({ "rci3", "-eco", "raro" }).error.empty(), "-eco con valor invalido");
	Check(!P({ "rci3", "-inventada" }).error.empty(), "opcion desconocida");
	Check(!P({ "rci3", "otro" }).error.empty(), "dos hosts es error");

	std::printf("           ejemplo: %s\n", P({ "rci3", "-P", "cero" }).error.c_str());
}

static void Test_ElEcoYLasBanderas()
{
	g_prueba = "eco_y_banderas";
	const Opciones o = P({ "rci3", "-eco", "local", "-traza", "-quedarse" });
	Check(o.eco == Eco::Local, "-eco local");
	Check(o.traza && o.quedarse, "las banderas se prenden");

	const Opciones d = P({ "rci3" });
	Check(d.eco == Eco::Auto && !d.traza && !d.quedarse,
	      "y por defecto estan apagadas");
}

static void Test_ElAvisoDelPuerto22SinSsh()
{
	/*  Pedir el 22 sin -ssh es casi seguro un descuido. Se avisa, pero se
	 *  conecta igual: el que sabe lo que hace no tiene por que pelear con
	 *  el programa.                                                       */
	g_prueba = "aviso_del_puerto_22";
	const Opciones o = P({ "rci3", "-P", "22" });
	Check(o.error.empty(), "no detiene");
	Check(o.transporte == Transporte::Telnet, "sigue siendo telnet");
	Check(o.avisos.size() == 1, "pero avisa");
	if (!o.avisos.empty())
		std::printf("           aviso: %s\n", o.avisos[0].c_str());
}

static void Test_LaAyuda()
{
	g_prueba = "la_ayuda";
	const Opciones o = P({ "-h" });
	Check(o.ayuda, "-h pide ayuda");
	Check(o.error.empty(), "y no es un error aunque falte el host");

	const std::string u = Uso("vt6530qt");
	Check(u.find("-ssh") != std::string::npos, "la ayuda menciona -ssh");
	Check(u.find("tacl") != std::string::npos, "y explica lo del comando");
}

static void Test_ElTermDelPtyNoEsElTerminalType()
{
	/*  Dos canales distintos para decir lo mismo, y con distinta forma.
	 *
	 *  El TERM del pty-req va en MAYUSCULAS -- TN6530-8 -- porque asi lo
	 *  manda OutsideView y asi el host arma la ventana 6530. El
	 *  terminal-type que contestamos por telnet va en minusculas, adentro de
	 *  la subnegociacion. Mezclarlos fue lo que nos tuvo dos dias.        */
	g_prueba = "el_term_del_pty_no_es_el_terminal_type";

	const Opciones o = P({ "jaracena@rci3", "-ssh" });
	Check(o.tipoPty == "TN6530-8", "en ssh el pty se pide como TN6530-8");
	Check(o.tipoTerminal == "tn6530-8",
	      "y por telnet nos declaramos tn6530-8, en minusculas");

	/*  Se puede cambiar, que es lo que hace falta el dia que aparezca un
	 *  host armado distinto. */
	const Opciones v = P({ "jaracena@rci3", "-ssh", "-tipo-pty", "xterm" });
	Check(v.tipoPty == "xterm", "-tipo-pty manda sobre el defecto");

	/*  -tipo no toca el del pty: si tocara, se volverian a mezclar. */
	const Opciones t = P({ "jaracena@rci3", "-ssh", "-tipo", "tn6530-7" });
	Check(t.tipoTerminal == "tn6530-7", "-tipo cambia el de telnet");
	Check(t.tipoPty == "TN6530-8", "y deja en paz el del pty");

	/*  En telnet no hay pty que pedir. */
	const Opciones n = P({ "rci3" });
	Check(n.tipoPty.empty(), "en telnet no se inventa ningun TERM de pty");
}

static void Test_LosDefectosDeSshSalenDeUnaSesionQueFunciona()
{
	/*  Los tres, juntos, son lo que hace que el host arranque TACL en una
	 *  ventana 6530. Estan fijados aca porque cada uno se descubrio por
	 *  separado y con esfuerzo, y cualquiera de los tres que se pierda deja
	 *  la sesion muda o en OSS.                                           */
	g_prueba = "los_defectos_de_ssh_salen_de_una_sesion_que_funciona";

	const Opciones o = P({ "jaracena@rci3", "-ssh" });
	Check(o.tipoPty == "TN6530-8", "pty como TN6530-8");
	Check(o.shell, "shell, no exec");
	Check(o.linemode, "LINEMODE ofrecido");

	/*  Y los tres se pueden apagar de a uno. */
	Check(!P({ "jaracena@rci3", "-ssh", "-sin-linemode" }).linemode,
	      "-sin-linemode lo apaga");
	Check(!P({ "jaracena@rci3", "-ssh", "-exec" }).shell,
	      "-exec vuelve a pedir exec");

	/*  Dar un comando implica exec: pedir shell y un comando a la vez no
	 *  tiene sentido, y adivinar cual gana seria peor que decidirlo aca. */
	const Opciones c = P({ "jaracena@rci3", "-ssh", "-comando", "tacl" });
	Check(!c.shell, "con -comando se usa exec");
	Check(c.comando == "tacl", "y el comando es el que se dio");

	/*  Pero si se piden las dos cosas, gana lo que el usuario escribio. */
	const Opciones d = P({ "jaracena@rci3", "-ssh", "-comando", "tacl",
	                       "-shell" });
	Check(d.shell, "-shell explicito gana sobre el -comando");

	/*  En telnet nada de esto aplica. */
	const Opciones t = P({ "rci3" });
	Check(!t.linemode, "en telnet LINEMODE sigue apagado por defecto");
	Check(!t.shell, "y no hay shell que pedir");
}

static void Test_ElFormularioObtieneLosMismosDefectosQueLaLinea()
{
	/*  El formulario de conexion llena un Opciones a mano -- host, puerto,
	 *  usuario, protocolo -- y no pasa por el parseo. Si AplicarDefectos no
	 *  hiciera exactamente lo mismo en los dos casos, una sesion armada por
	 *  formulario saldria sin los cuatro valores que costaron una semana y se
	 *  quedaria muda, sin que nada pareciera roto.
	 *
	 *  Esta prueba es la que ata las dos entradas.                        */
	g_prueba = "el_formulario_obtiene_los_mismos_defectos_que_la_linea";

	/*  Lo que hace el formulario: cuatro campos y nada mas. */
	Opciones f;
	f.transporte = Transporte::Ssh;
	f.host       = "rci3";
	f.usuario    = "jaracena";
	f.puerto     = 0;            /* el centinela: "poneme el que corresponda" */
	AplicarDefectos(&f);

	/*  Lo que hace la linea de comandos con lo mismo. */
	const Opciones l = P({ "jaracena@rci3", "-ssh" });

	Check(f.error.empty(), "el formulario no da error");
	Check(f.puerto    == l.puerto,    "mismo puerto");
	Check(f.comando   == l.comando,   "mismo comando");
	Check(f.tipoPty   == l.tipoPty,   "mismo TERM de pty");
	Check(f.shell     == l.shell,     "mismo shell");
	Check(f.linemode  == l.linemode,  "mismo linemode");
	Check(f.usuario   == l.usuario,   "mismo usuario");
	Check(f.hostNuevo == l.hostNuevo, "misma politica de host nuevo");
	std::printf("           formulario y linea de comandos dan lo mismo\n");

	/*  Y en telnet igual. */
	Opciones t;
	t.transporte = Transporte::Telnet;
	t.host       = "rci3";
	AplicarDefectos(&t);
	const Opciones tl = P({ "rci3" });
	Check(t.puerto == tl.puerto, "en telnet, mismo puerto");
	Check(t.shell == tl.shell && t.linemode == tl.linemode,
	      "y los mismos defectos");
	std::printf("           y en telnet tambien\n");

	/*  Un puerto puesto a mano no se pisa: es el caso de su NonStop, que
	 *  atiende en el 2200 detras de la misma direccion que un Linux.     */
	Opciones m;
	m.transporte = Transporte::Ssh;
	m.host       = "192.168.1.193";
	m.usuario    = "jaracena";
	m.puerto     = 2200;
	AplicarDefectos(&m);
	Check(m.puerto == 2200, "un puerto dado a mano no se reemplaza");
	std::printf("           y el puerto 2200 escrito a mano sobrevive\n");
}

static void Test_ElLargoDelUsuarioNoDejaAfueraAlUsuarioDePrueba()
{
	/*  La primera version del formulario validaba con {3,15} y no dejaba
	 *  escribir 'nkaizen.jaracena', que tiene 16. Un usuario Guardian es
	 *  grupo.usuario con hasta ocho y ocho, o sea 17 con el punto.       */
	g_prueba = "el_largo_del_usuario_no_deja_afuera_al_usuario_de_prueba";

	Check(kUsuarioLargoMaximo >= 17,
	      "el maximo alcanza para un grupo.usuario de Guardian");
	Check(UsuarioValido("nkaizen.jaracena"), "nkaizen.jaracena entra");
	Check(UsuarioValido("super.super"),      "super.super entra");
	Check(UsuarioValido("a"),                "un usuario de una letra entra");
	Check(UsuarioValido("juan-a_1"),         "guion y guion bajo entran");
	std::printf("           nkaizen.jaracena (16) entra; el maximo es %d\n",
	            kUsuarioLargoMaximo);

	/*  Y lo que seguro esta mal no pasa. El host es la autoridad sobre quien
	 *  existe; esto solo ataja lo que no puede funcionar.                */
	Check(!UsuarioValido(""),           "vacio no");
	Check(!UsuarioValido("con espacio"),"con espacio no");
	Check(!UsuarioValido("con\ttab"),   "con tabulador no");
	Check(!UsuarioValido("con\nsalto"), "con salto de linea no");
	Check(!UsuarioValido("dos:puntos"), "con dos puntos no");
	Check(!UsuarioValido(std::string(kUsuarioLargoMaximo + 1, 'a')),
	      "uno mas que el maximo no");
	Check(UsuarioValido(std::string(kUsuarioLargoMaximo, 'a')),
	      "justo el maximo si");
	std::printf("           espacios, control y de mas largo quedan afuera\n");
}

static void Test_ElTimeoutSePuedeAcortar()
{
	/*  Quince segundos mirando una ventana que no aparece se parecen mucho a
	 *  un programa que no arranco: es lo que se vio al tipear mal una
	 *  direccion. Con -timeout se acorta.                                 */
	g_prueba = "el_timeout_se_puede_acortar";

	Check(P({ "jaracena@rci3", "-ssh" }).timeoutSegundos == 0,
	      "sin -timeout queda en 0, que quiere decir 'el del transporte'");

	const Opciones o = P({ "jaracena@rci3", "-ssh", "-timeout", "2" });
	Check(o.error.empty(), "-timeout 2 se acepta");
	Check(o.timeoutSegundos == 2, "y se guarda");

	Check(!P({ "jaracena@rci3", "-ssh", "-timeout", "0" }).error.empty(),
	      "cero no es un timeout");
	Check(!P({ "jaracena@rci3", "-ssh", "-timeout", "raro" }).error.empty(),
	      "ni una palabra");
	Check(!P({ "jaracena@rci3", "-ssh", "-timeout" }).error.empty(),
	      "ni sin valor");
	std::printf("           -timeout 2 para no esperar quince segundos\n");
}

static void Test_LaClaveDelHostSePuedeAceptar()
{
	/*  Esta opcion existia solo adentro del texto de un mensaje de error --
	 *  "o use -aceptar-host-nuevo si sabe lo que hace" -- y no en el parseo,
	 *  asi que el que la buscaba no tenia ninguna forma de aceptar el host y
	 *  encima quedaba pensando que se habia equivocado el. La prueba esta
	 *  para que no vuelva a desaparecer.                                  */
	g_prueba = "la_clave_del_host_se_puede_aceptar";

	const Opciones porDefecto = P({ "jaracena@rci3", "-ssh" });
	Check(porDefecto.hostNuevo == HostNuevo::Preguntar,
	      "por defecto se pregunta, como ssh y PuTTY");

	const Opciones a = P({ "jaracena@rci3", "-ssh", "-aceptar-host-nuevo" });
	Check(a.error.empty(), "-aceptar-host-nuevo no es un error");
	Check(a.hostNuevo == HostNuevo::Aceptar, "y acepta sin preguntar");

	const Opciones r = P({ "jaracena@rci3", "-ssh", "-rechazar-host-nuevo" });
	Check(r.hostNuevo == HostNuevo::Rechazar, "-rechazar-host-nuevo corta");

	/*  Las dos formas de guion, como todo el resto. */
	Check(P({ "jaracena@rci3", "-ssh", "--aceptar-host-nuevo" }).hostNuevo ==
	          HostNuevo::Aceptar,
	      "con dos guiones tambien");

	const Opciones k = P({ "jaracena@rci3", "-ssh",
	                       "-known-hosts", "/tmp/otro" });
	Check(k.error.empty(), "-known-hosts no es un error");
	Check(k.knownHosts == "/tmp/otro", "y se queda con la ruta");
	Check(porDefecto.knownHosts.empty(),
	      "sin -known-hosts la ruta queda vacia, o sea la de siempre");

	/*  Y que la ayuda la nombre: una bandera que no figura en -h es una
	 *  bandera que nadie encuentra, que es como empezo todo esto.       */
	const std::string ayuda = Uso("vt6530qt");
	Check(ayuda.find("-aceptar-host-nuevo") != std::string::npos,
	      "la ayuda nombra -aceptar-host-nuevo");
	std::printf("           -aceptar-host-nuevo existe de verdad, y sale en -h\n");
}

static void Test_SshNecesitaUsuarioDeAntemano()
{
	/*  La diferencia que encontro Juan en la primera conexion: por telnet
	 *  el usuario se teclea adentro de la sesion, por ssh viaja en la
	 *  autenticacion y tiene que estar antes de abrir el canal. Si falta,
	 *  se toma el del sistema -- como hacen ssh y PuTTY -- y se avisa, que
	 *  es mejor que negarse a arrancar por algo que se puede deducir.   */
	g_prueba = "ssh_necesita_usuario_de_antemano";

	PonerEnv("USER", "juancito");
	const Opciones o = P({ "rci3", "-ssh" });
	Check(o.error.empty(), "no es un error: se deduce y se sigue");
	Check(o.usuario == "juancito", "toma el usuario del sistema");
	Check(o.avisos.size() == 1, "y lo avisa");
	if (!o.avisos.empty())
		std::printf("           aviso: %s\n", o.avisos[0].c_str());

	/*  Pero si se dio uno, no se toca. */
	const Opciones d = P({ "nkaizen.jaracena@rci3", "-ssh" });
	Check(d.usuario == "nkaizen.jaracena", "el que se dio manda");
	Check(d.avisos.empty(), "y entonces no hay nada que avisar");

	/*  En telnet no aplica: TELSERV presenta su menu y el logon va adentro. */
	BorrarEnv("USER");
	BorrarEnv("LOGNAME");
	BorrarEnv("USERNAME");
	const Opciones t = P({ "rci3" });
	Check(t.error.empty(), "telnet sin usuario sigue estando bien");
	Check(t.usuario.empty(), "y no se inventa ninguno");

	/*  Y si es ssh y no hay de donde sacarlo, ahi si hay que decirlo. */
	const Opciones e = P({ "rci3", "-ssh" });
	Check(!e.error.empty(), "ssh sin usuario ni entorno si es un error");
	if (!e.error.empty())
		std::printf("           error: %s\n", e.error.c_str());

	PonerEnv("USER", "juancito");   /* como estaba, para las que sigan */
}

/* ------------------------------------------------------------------ */

typedef void (*Fn)();

int main()
{
	std::printf("\nLinea de comandos -- pruebas\n");
	std::printf("============================\n\n");

	/*  El usuario por defecto de ssh sale del entorno, y un contenedor
	 *  pelado no tiene USER. Se fija aca para que las pruebas den lo mismo
	 *  en la maquina de cualquiera; la que lo ejercita de verdad lo borra
	 *  y lo repone ella misma.                                           */
	PonerEnv("USER", "juancito");

	Fn pruebas[] = {
		Test_LoMinimo,
		Test_SshCambiaPuertoYComando,
		Test_UsuarioArrobaHost,
		Test_LaOpcionDeUsuarioGanaYAvisa,
		Test_LosDosGuionesSonLoMismo,
		Test_LaFormaViejaSigueAndando,
		Test_ComandoExplicito,
		Test_ElEcoYLasBanderas,
		Test_ElAvisoDelPuerto22SinSsh,
		Test_ErroresQueSiDetienen,
		Test_LaAyuda,
		Test_SshNecesitaUsuarioDeAntemano,
		Test_ElTermDelPtyNoEsElTerminalType,
		Test_LosDefectosDeSshSalenDeUnaSesionQueFunciona,
		Test_ElFormularioObtieneLosMismosDefectosQueLaLinea,
		Test_ElLargoDelUsuarioNoDejaAfueraAlUsuarioDePrueba,
		Test_ElTimeoutSePuedeAcortar,
		Test_LaClaveDelHostSePuedeAceptar,
	};

	for (size_t i = 0; i < sizeof(pruebas) / sizeof(pruebas[0]); i++)
		pruebas[i]();

	std::printf("\n----------------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_ok);
	std::printf("  comprobaciones fallidas . %d\n", g_mal);
	std::printf("----------------------------\n\n");
	return (g_mal == 0) ? 0 : 1;
}
