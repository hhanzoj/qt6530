/*
 *  knownhosts_tests -- que la linea que escribimos sea la que ssh entiende.
 *
 *  Esto se prueba y no se mira a ojo por una razon concreta: un base64 mal
 *  armado no falla, deja una linea que ssh no va a reconocer NUNCA, y el
 *  sintoma aparece recien la proxima vez que alguien conecta y el programa
 *  vuelve a preguntar por un host que ya habia aceptado. Sin esta prueba,
 *  ese error puede vivir meses.
 *
 *  Los vectores de base64 son los de la RFC 4648, que existen justamente para
 *  esto.
 */
#include "../net/KnownHosts.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_ok = 0;
static int g_mal = 0;

static void Comprobar(bool condicion, const char *que)
{
	if (condicion) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLO: %s\n", que);
}

static void ComprobarTexto(const std::string &dio, const std::string &esperado,
                           const char *que)
{
	if (dio == esperado) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLO: %s\n", que);
	std::printf("           esperado: [%s]\n", esperado.c_str());
	std::printf("           dio     : [%s]\n", dio.c_str());
}

static std::string B64(const char *texto)
{
	return conocidos::Base64((const unsigned char *)texto, std::strlen(texto));
}

/*  Los seis vectores de la RFC 4648. Cubren los tres restos posibles al
 *  dividir por tres, que es donde vive todo el relleno con '='.          */
static void Test_Base64ContraLaRfc4648()
{
	ComprobarTexto(B64(""),       "",         "base64 de nada");
	ComprobarTexto(B64("f"),      "Zg==",     "base64 de 'f'");
	ComprobarTexto(B64("fo"),     "Zm8=",     "base64 de 'fo'");
	ComprobarTexto(B64("foo"),    "Zm9v",     "base64 de 'foo'");
	ComprobarTexto(B64("foob"),   "Zm9vYg==", "base64 de 'foob'");
	ComprobarTexto(B64("fooba"),  "Zm9vYmE=", "base64 de 'fooba'");
	ComprobarTexto(B64("foobar"), "Zm9vYmFy", "base64 de 'foobar'");
	std::printf("           los siete vectores de la RFC 4648\n");
}

/*  Una clave de verdad empieza con el largo y el nombre del algoritmo, y
 *  tiene bytes altos: el alfabeto se recorre entero y se ve si algun
 *  corrimiento quedo con signo.                                          */
static void Test_Base64ConBytesAltos()
{
	const unsigned char clave[] = {
		0x00, 0x00, 0x00, 0x07, 's', 's', 'h', '-', 'r', 's', 'a',
		0xFF, 0xFE, 0x80, 0x7F, 0x00, 0x01
	};
	const std::string r = conocidos::Base64(clave, sizeof(clave));

	ComprobarTexto(r, "AAAAB3NzaC1yc2H//oB/AAE=", "base64 con bytes altos");

	bool soloDelAlfabeto = true;
	for (std::size_t i = 0; i < r.size(); i++)
	{
		const char c = r[i];
		const bool valido = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
		                    (c >= '0' && c <= '9') || c == '+' || c == '/' ||
		                    c == '=';
		if (!valido) soloDelAlfabeto = false;
	}
	Comprobar(soloDelAlfabeto, "el base64 usa solo el alfabeto de base64");
	std::printf("           con bytes >= 0x80 no se cuela ningun signo\n");
}

static void Test_ElPuertoNoEstandarVaEntreCorchetes()
{
	ComprobarTexto(conocidos::NombreDeHost("rci3", 22), "rci3",
	               "el puerto 22 va pelado");
	ComprobarTexto(conocidos::NombreDeHost("rci3", 2222), "[rci3]:2222",
	               "otro puerto va entre corchetes");
	std::printf("           rci3 en el 22, [rci3]:2222 en el 2222\n");
}

static void Test_LaLineaTieneLaFormaDeOpenssh()
{
	const unsigned char clave[] = { 'f', 'o', 'o', 'b', 'a', 'r' };

	ComprobarTexto(
		conocidos::Linea("rci3", 22, "ssh-rsa", clave, sizeof(clave)),
		"rci3 ssh-rsa Zm9vYmFy", "la linea completa");

	ComprobarTexto(
		conocidos::Linea("rci3", 2222, "ssh-ed25519", clave, sizeof(clave)),
		"[rci3]:2222 ssh-ed25519 Zm9vYmFy", "la linea con puerto");

	std::printf("           host tipo base64, separados por un espacio\n");

	/*  Sin tipo o sin clave no se arma nada. Es deliberado: escribir media
	 *  linea en known_hosts es peor que no escribir ninguna, porque ssh la
	 *  rechaza y nadie sabe por que.                                      */
	Comprobar(conocidos::Linea("rci3", 22, "", clave, sizeof(clave)).empty(),
	          "sin tipo de clave no se arma la linea");
	Comprobar(conocidos::Linea("rci3", 22, "ssh-rsa", clave, 0).empty(),
	          "sin bytes de clave no se arma la linea");
	Comprobar(conocidos::Linea("", 22, "ssh-rsa", clave, sizeof(clave)).empty(),
	          "sin host no se arma la linea");
	std::printf("           si falta algo no se escribe media linea\n");
}

/*  LA prueba que importa: agregar no puede tocar lo que ya estaba. */
static void Test_AgregarNoPisaLoQueYaEstaba()
{
	const std::string ruta = "/tmp/vt6530-known-hosts-prueba/known_hosts";
	std::remove(ruta.c_str());

	const std::string viejo =
		"# un comentario que libssh2 no conserva\n"
		"@cert-authority *.ejemplo.com ssh-rsa AAAA\n"
		"otrohost ssh-ed25519 BBBB\n";

	std::string error;

	/*  Primero: el archivo no existe todavia, ni la carpeta. */
	Comprobar(conocidos::Agregar(ruta, "primero ssh-rsa AAAA", &error),
	          "agrega aunque no existan ni el archivo ni la carpeta");

	std::remove(ruta.c_str());

	/*  Ahora con contenido previo. */
	std::FILE *f = std::fopen(ruta.c_str(), "wb");
	Comprobar(f != nullptr, "se pudo escribir el archivo de partida");
	if (f != nullptr)
	{
		std::fwrite(viejo.c_str(), 1, viejo.size(), f);
		std::fclose(f);
	}

	Comprobar(conocidos::Agregar(ruta, "rci3 ssh-rsa CCCC", &error),
	          "agrega la entrada nueva");

	std::string ahora;
	f = std::fopen(ruta.c_str(), "rb");
	if (f != nullptr)
	{
		char buf[1024];
		std::size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			ahora.append(buf, n);
		std::fclose(f);
	}

	ComprobarTexto(ahora, viejo + "rci3 ssh-rsa CCCC\n",
	               "lo viejo intacto y lo nuevo al final");
	std::printf("           el comentario y el @cert-authority siguen ahi\n");
}

/*  Un archivo sin salto al final es comun cuando alguien lo edito a mano. Sin
 *  cuidado, la entrada nueva se pega a la ultima y se pierden las dos.     */
static void Test_SiFaltaElSaltoFinalSeLoPone()
{
	const std::string ruta = "/tmp/vt6530-known-hosts-prueba/sin-salto";
	std::remove(ruta.c_str());

	std::FILE *f = std::fopen(ruta.c_str(), "wb");
	Comprobar(f != nullptr, "se pudo escribir el archivo sin salto");
	if (f != nullptr)
	{
		const char *viejo = "otrohost ssh-rsa AAAA";   /* sin \n */
		std::fwrite(viejo, 1, std::strlen(viejo), f);
		std::fclose(f);
	}

	std::string error;
	Comprobar(conocidos::Agregar(ruta, "rci3 ssh-rsa BBBB", &error),
	          "agrega sobre un archivo sin salto final");

	std::string ahora;
	f = std::fopen(ruta.c_str(), "rb");
	if (f != nullptr)
	{
		char buf[256];
		std::size_t n;
		while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
			ahora.append(buf, n);
		std::fclose(f);
	}

	ComprobarTexto(ahora, "otrohost ssh-rsa AAAA\nrci3 ssh-rsa BBBB\n",
	               "las dos entradas quedan en lineas distintas");
	std::printf("           sin salto final, se lo pone antes de agregar\n");
}

static void Test_UnaRutaImposibleNoRompeNada()
{
	std::string error;

	Comprobar(!conocidos::Agregar("", "rci3 ssh-rsa AAAA", &error),
	          "sin ruta devuelve false");
	Comprobar(!error.empty(), "y dice por que");

	error.clear();
	Comprobar(!conocidos::Agregar("/tmp/vt6530-x/known_hosts", "", &error),
	          "sin linea devuelve false");
	Comprobar(!error.empty(), "y tambien dice por que");
	std::printf("           los casos imposibles avisan, no revientan\n");
}

/*  Lo que el archivo tiene para un host, que es lo que separa "alguien se
 *  metio en el medio" de "tenes la clave de otro tipo anotada".           */
static void Test_SeVeQueHayEnElArchivoParaUnHost()
{
	const std::string ruta = "/tmp/vt6530-known-hosts-prueba/entradas";
	std::remove(ruta.c_str());

	const std::string archivo =
		"# comentario\n"
		"\n"
		"rci3 ssh-rsa AAAA\n"
		"rci3 ssh-ed25519 BBBB\n"
		"[rci3]:2200 ecdsa-sha2-nistp256 CCCC\n"
		"otrohost ssh-rsa DDDD\n"
		"rci3b ssh-rsa EEEE\n"                  /* empieza igual: no cuenta */
		"alias,rci3,otro ssh-dss FFFF\n"        /* varios nombres por linea */
		"@cert-authority rci3 ecdsa-sha2-nistp384 GGGG\n"  /* con marca */
		"|1|aBcD=|eFgH= ssh-rsa HHHH\n";        /* hasheada */

	std::FILE *f = std::fopen(ruta.c_str(), "wb");
	Comprobar(f != nullptr, "se pudo escribir el archivo de entradas");
	if (f != nullptr)
	{
		std::fwrite(archivo.c_str(), 1, archivo.size(), f);
		std::fclose(f);
	}

	int hasheadas = -1;
	const std::vector<conocidos::Entrada> hay =
		conocidos::EntradasDe(ruta, "rci3", &hasheadas);

	Comprobar(hay.size() == 5, "cinco entradas para rci3");
	Comprobar(hasheadas == 1, "y una hasheada, que se cuenta aparte");

	bool rsa = false, ed = false, ecdsa2200 = false, dss = false, ca = false;
	for (std::size_t i = 0; i < hay.size(); i++)
	{
		if (hay[i].nombre == "rci3" && hay[i].tipo == "ssh-rsa")     rsa = true;
		if (hay[i].nombre == "rci3" && hay[i].tipo == "ssh-ed25519") ed = true;
		if (hay[i].nombre == "[rci3]:2200" &&
		    hay[i].tipo == "ecdsa-sha2-nistp256")                    ecdsa2200 = true;
		if (hay[i].nombre == "rci3" && hay[i].tipo == "ssh-dss")     dss = true;
		if (hay[i].nombre == "rci3" &&
		    hay[i].tipo == "ecdsa-sha2-nistp384")                    ca = true;
	}
	Comprobar(rsa,       "la ssh-rsa");
	Comprobar(ed,        "la ssh-ed25519");
	Comprobar(ecdsa2200, "la del puerto 2200, con su nombre entre corchetes");
	Comprobar(dss,       "la que viene en una linea con varios nombres");
	Comprobar(ca,        "y la que lleva @cert-authority adelante, que corre\n                         los campos uno a la derecha");
	std::printf("           se ven los tipos y los puertos que ya estan\n");

	/*  Un nombre que solo empieza igual no cuenta: 'rci3b' no es 'rci3'. */
	bool coloOtro = false;
	for (std::size_t i = 0; i < hay.size(); i++)
		if (hay[i].nombre == "rci3b" || hay[i].nombre == "otrohost")
			coloOtro = true;
	Comprobar(!coloOtro, "ni rci3b ni otrohost se cuelan");

	/*  Y un host que no esta, no esta. */
	int h2 = -1;
	Comprobar(conocidos::EntradasDe(ruta, "nohay", &h2).empty(),
	          "un host que no figura da vacio");
	Comprobar(h2 == 1, "pero las hasheadas se siguen contando");
	std::printf("           un host ausente da vacio, no invento\n");

	/*  Un archivo que no existe no es un error: es que no hay nada. */
	int h3 = -1;
	Comprobar(conocidos::EntradasDe("/tmp/vt6530-no-existe-nunca", "rci3", &h3)
	              .empty(),
	          "un archivo que no existe da vacio");
	std::printf("           y un archivo inexistente tampoco revienta\n");
}

/*
 *  La regla que decide si un "no coincide" se puede degradar a "host nuevo".
 *
 *  El caso de verdad: dos maquinas detras de la misma direccion, una en el 22
 *  y otra en el 2200 por NAT. La entrada del 22 no dice nada sobre el 2200,
 *  pero libssh2 la encuentra igual y acusa de que la clave cambio.
 *
 *  Degradar eso es correcto SOLO si se puede demostrar que no hay entrada
 *  para este host Y este puerto. Con nombres hasheados o con comodines no se
 *  puede demostrar, y entonces no se degrada: de eso se trata Completa().
 */
static bool Escribir(const std::string &ruta, const std::string &texto)
{
	std::remove(ruta.c_str());
	std::FILE *f = std::fopen(ruta.c_str(), "wb");
	if (f == nullptr) return false;
	std::fwrite(texto.c_str(), 1, texto.size(), f);
	std::fclose(f);
	return true;
}

static void Test_CuandoSePuedeAfirmarQueNoEsta()
{
	const std::string base = "/tmp/vt6530-known-hosts-prueba/";

	/*  El caso real: el 22 es una maquina y el 2200 es otra, por NAT. */
	const std::string nat = base + "nat";
	Comprobar(Escribir(nat, "192.168.1.193 ssh-rsa AAAA\n"), "archivo del NAT");

	const conocidos::Lectura a = conocidos::Revisar(nat, "192.168.1.193", 2200);
	Comprobar(a.leido, "se leyo");
	Comprobar(a.Completa(), "lectura completa: sin hasheadas ni comodines");
	Comprobar(a.delHost.size() == 1, "esta la entrada del otro puerto");
	Comprobar(!a.exacta, "pero ninguna para [192.168.1.193]:2200");
	std::printf("           lo anotado del 22 no dice nada del 2200\n");

	/*  Y al reves: si la entrada de ESE puerto existe, hay que cortar. */
	const std::string mismo = base + "mismo";
	Comprobar(Escribir(mismo,
	                   "192.168.1.193 ssh-rsa AAAA\n"
	                   "[192.168.1.193]:2200 ssh-rsa BBBB\n"),
	          "archivo con los dos puertos");
	const conocidos::Lectura b = conocidos::Revisar(mismo, "192.168.1.193", 2200);
	Comprobar(b.exacta, "la entrada de este host y este puerto SI esta");
	std::printf("           si esta la del 2200, no se degrada nada\n");

	/*  El puerto 22 se escribe pelado: tambien cuenta como exacta. */
	const conocidos::Lectura c = conocidos::Revisar(nat, "192.168.1.193", 22);
	Comprobar(c.exacta, "en el 22 el nombre pelado cuenta como exacto");
	std::printf("           en el 22 el nombre va pelado, y cuenta igual\n");

	/*  Zonas ciegas: con un nombre hasheado no se puede afirmar la ausencia. */
	const std::string conHash = base + "hasheado";
	Comprobar(Escribir(conHash,
	                   "192.168.1.193 ssh-rsa AAAA\n"
	                   "|1|aBcD=|eFgH= ssh-rsa BBBB\n"),
	          "archivo con una hasheada");
	const conocidos::Lectura d = conocidos::Revisar(conHash, "192.168.1.193", 2200);
	Comprobar(!d.Completa(), "con una hasheada la lectura NO es completa");
	Comprobar(d.hasheadas == 1, "y se dice cuantas son");
	std::printf("           con nombres hasheados no se degrada: podria estar\n");

	/*  Un comodin puede alcanzar al host sin nombrarlo. */
	const std::string conComodin = base + "comodin";
	Comprobar(Escribir(conComodin, "192.168.1.* ssh-rsa AAAA\n"),
	          "archivo con un comodin");
	const conocidos::Lectura e = conocidos::Revisar(conComodin, "192.168.1.193", 2200);
	Comprobar(!e.Completa(), "con un comodin la lectura NO es completa");
	Comprobar(e.comodines == 1, "y se cuenta");
	std::printf("           con comodines tampoco: pueden alcanzar al host\n");

	/*  Lo que no se pudo leer no demuestra nada. */
	const conocidos::Lectura g =
		conocidos::Revisar("/tmp/vt6530-no-existe-nunca", "rci3", 22);
	Comprobar(!g.leido, "un archivo que no existe no se leyo");
	Comprobar(!g.Completa(), "y por lo tanto no es una lectura completa");
	std::printf("           lo que no se pudo leer no demuestra nada\n");
}

int main()
{
	std::printf("\nknown_hosts -- pruebas\n");
	std::printf("======================\n\n");

	Test_Base64ContraLaRfc4648();
	Test_Base64ConBytesAltos();
	Test_ElPuertoNoEstandarVaEntreCorchetes();
	Test_LaLineaTieneLaFormaDeOpenssh();
	Test_AgregarNoPisaLoQueYaEstaba();
	Test_SiFaltaElSaltoFinalSeLoPone();
	Test_UnaRutaImposibleNoRompeNada();
	Test_SeVeQueHayEnElArchivoParaUnHost();
	Test_CuandoSePuedeAfirmarQueNoEsta();

	std::printf("\n-----------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_ok);
	std::printf("  comprobaciones fallidas . %d\n", g_mal);
	std::printf("-----------------------\n\n");

	return g_mal == 0 ? 0 : 1;
}
