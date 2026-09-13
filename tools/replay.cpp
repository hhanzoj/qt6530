/*
 *  vt6530_replay -- reproduce una captura contra el nucleo.
 *
 *  Toma un archivo .host.payload de los que graba tools/tap6530.py, se lo
 *  entrega a Guardian::ProcessRemoteString() y vuelca la pantalla resultante
 *  junto con lo que el terminal habria respondido.
 *
 *  Sirve para tres cosas:
 *
 *    - mirar que hizo el parser con una pantalla real, sin host ni Qt;
 *    - convertir una captura en una prueba de regresion, generando un
 *      archivo .esperado y comparandolo despues (--generar / --verificar);
 *    - reproducir el reparto de TCP con --trozos, que es como se dispara el
 *      defecto de ESC r: 96 bytes que no caen en un solo segmento.
 *
 *  Uso:
 *      vt6530_replay captura.host.payload
 *      vt6530_replay captura.host.payload --trozos 64
 *      vt6530_replay captura.host.payload --generar
 *      vt6530_replay --verificar-todo capturas/
 */

#include <spl/Log.h>
#include <spl/StringBuffer.h>
#include <spl/collection/Vector.h>
#include <spl/term/Telnet.h>

#include <vt6530/TextDisplay.h>
#include <vt6530/Keys.h>
#include <vt6530/Guardian.h>
#include <vt6530/TermEventListener.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

/* ---------------------------------------------------------------------- */

class SumideroDeSalida : public IHostLink
{
public:
	std::string enviado;
	virtual void SendRaw(const byte *data, int len)
	{
		if (data != nullptr && len > 0)
			enviado.append((const char *)data, (size_t)len);
	}
};

class ContadorDeEventos : public Vt6530EventListener
{
public:
	int enquire = 0, resetLine = 0, displayChanged = 0, error = 0;

	virtual void Vt6530_OnConnect() {}
	virtual void Vt6530_OnDisconnect() {}
	virtual void Vt6530_OnResetLine() { resetLine++; }
	virtual void Vt6530_OnEnquire() { enquire++; }
	virtual void Vt6530_OnDisplayChanged() { displayChanged++; }
	virtual void Vt6530_OnError(const char *) { error++; }
	virtual void Vt6530_OnDebug(const char *) {}
	virtual void Vt6530_OnRecv34(const char *, const char *, const int) {}
	virtual void Vt6530_OnTextWatch(const char *, const int) {}
};

class ColectorDeLog : public ILogSink
{
public:
	std::vector<std::string> lineas;
	virtual void OnLogLine(SplLogLevel nivel, const std::string &linea)
	{
		const char *n = (nivel == CLOG_ERROR) ? "ERROR"
		              : (nivel == CLOG_WARN)  ? "WARN"
		              : (nivel == CLOG_DEBUG) ? "DEBUG" : "INFO";
		lineas.push_back(std::string(n) + "  " + linea);
	}
};

/* ---------------------------------------------------------------------- */

static std::string Legible(const std::string &crudo)
{
	std::string out;
	char tmp[8];
	for (size_t i = 0; i < crudo.size(); i++)
	{
		unsigned char c = (unsigned char)crudo[i];
		if (c >= 0x20 && c < 0x7F) out.push_back((char)c);
		else { std::snprintf(tmp, sizeof(tmp), "<%02X>", c); out += tmp; }
	}
	return out;
}

/** Una letra por celda que resume sus atributos, para ver la estructura de
 *  campos de un vistazo sin leer el texto.
 *
 *  El inicio de campo NO tapa al video inverso: en el 6530 la celda de inicio
 *  lleva los atributos del campo que sigue, asi que un campo en inverso
 *  empieza con una celda que es las dos cosas. Taparlo hacia invisibles las
 *  barras de una pantalla como la de VIEWSYS. */
static char LetraDeAtributo(PageCell *cell)
{
	const bool campo = cell->IsStartField();
	if (cell->IsReverse())    return campo ? '[' : 'R';
	if (campo)                return '|';
	if (cell->IsInvis())      return 'i';
	if (cell->IsUnderline())  return '_';
	if (cell->IsBlinking())   return '*';
	if (cell->IsUnprotect())  return '.';
	return ' ';
}

/** Un caracter para el volcado de pantalla.
 *
 *  Un espacio en video inverso se dibuja como un bloque solido en la
 *  terminal, y las aplicaciones lo usan para las barras: VIEWSYS dibuja asi
 *  los graficos de cada CPU. Imprimirlo como espacio hace desaparecer del
 *  volcado justo la parte que el usuario ve. */
static char CaracterDePantalla(PageCell *cell)
{
	const unsigned char ch = (unsigned char)cell->Get();
	const bool imprimible = (ch >= 0x20 && ch < 0x7F);

	if (cell->IsReverse() && (!imprimible || ch == ' ')) return '#';
	return imprimible ? (char)ch : '.';
}

static bool LeerArchivo(const fs::path &ruta, std::string &fuera)
{
	std::ifstream in(ruta, std::ios::binary);
	if (!in) return false;
	std::ostringstream ss;
	ss << in.rdbuf();
	fuera = ss.str();
	return true;
}

/* ---------------------------------------------------------------------- */

struct Opciones
{
	int  trozos = 0;      /* 0 = todo de una vez */
	bool atributos = false;
	bool mostrarLog = true;
	int  desde = 0;       /* recorte del payload; 0/0 = entero */
	int  hasta = 0;
};

/** Recorta el payload al rango pedido.
 *
 *  Hace falta porque una captura real recorre varios modos: la de VIEWSYS
 *  entra en modo bloque en el offset 805 y vuelve a conversacional en el
 *  4852. Reproducirla entera termina mostrando el TACL del final, no la
 *  pantalla que interesa. Los offsets salen de tools/inspeccionar.py. */
static std::string Recortar(const std::string &payload, const Opciones &op)
{
	if (op.desde <= 0 && op.hasta <= 0) return payload;
	size_t ini = (op.desde > 0) ? (size_t)op.desde : 0;
	size_t fin = (op.hasta > 0) ? (size_t)op.hasta : payload.size();
	if (ini >= payload.size()) return std::string();
	if (fin > payload.size()) fin = payload.size();
	if (fin <= ini) return std::string();
	return payload.substr(ini, fin - ini);
}

/** Reproduce la captura y devuelve el volcado como texto. */
static std::string Reproducir(const std::string &payload, const Opciones &op)
{
	ColectorDeLog log;
	Log::SetSink(&log);
	Log::SetQuiet(true);

	SumideroDeSalida salida;
	ContadorDeEventos eventos;
	Vector<Vt6530EventListener *> escuchas;
	escuchas.Add(&eventos);

	TextDisplay display(2, 80, 24);
	Keys teclas;
	Guardian guardian(&escuchas, &display, &teclas, &salida);

	int fallosPrevios = spl_compat_assert_failures;

	if (op.trozos <= 0)
	{
		guardian.ProcessRemoteString(payload.data(), (int)payload.size());
	}
	else
	{
		size_t pos = 0;
		while (pos < payload.size())
		{
			size_t n = std::min((size_t)op.trozos, payload.size() - pos);
			guardian.ProcessRemoteString(payload.data() + pos, (int)n);
			pos += n;
		}
	}

	int fallos = spl_compat_assert_failures - fallosPrevios;

	std::ostringstream o;
	const int cols = display.GetNumColumns();
	const int filas = display.GetNumRows();

	o << "bytes de entrada ...... " << payload.size();
	if (op.desde > 0 || op.hasta > 0)
		o << "  (recorte " << op.desde << ".." << op.hasta << ")";
	o << "\n";
	o << "entregados en ......... "
	  << (op.trozos <= 0 ? "una sola lectura"
	                     : ("trozos de " + std::to_string(op.trozos) + " bytes"))
	  << "\n";
	o << "modo .................. "
	  << (display.GetProtectMode() ? "bloque protegido"
	      : display.GetBlockMode() ? "bloque" : "conversacional") << "\n";
	o << "cursor ................ fila " << display.GetCursorRow()
	  << ", columna " << display.GetCursorCol() << "\n";
	o << "eventos ............... enquire " << eventos.enquire
	  << ", reset de linea " << eventos.resetLine
	  << ", error " << eventos.error << "\n";
	o << "aserciones heredadas .. " << fallos << "\n";
	o << "\n";

	/* Regla de columnas cada diez. */
	o << "     +";
	for (int c = 0; c < cols; c++) o << ((c % 10 == 0) ? '+' : '-');
	o << "+          ( # = espacio en video inverso, o sea bloque solido )\n";

	Page *page = display.GetDisplayPage();
	for (int r = 0; r < filas; r++)
	{
		char num[8];
		std::snprintf(num, sizeof(num), "%3d  |", r);
		o << num;
		for (int c = 0; c < cols; c++)
			o << CaracterDePantalla(page->GetCell(c, r));
		o << "|\n";
	}
	o << "     +";
	for (int c = 0; c < cols; c++) o << ((c % 10 == 0) ? '+' : '-');
	o << "+\n";

	/* La linea de estado se guarda aparte de las paginas. */
	const StringBuffer *estado = display.GetStatusLine();
	o << "\nlinea de estado (" << estado->Length() << " columnas):\n";
	o << "  [" << Legible(estado->Str().substr(0, 80)) << "]\n";

	if (op.atributos)
	{
		o << "\natributos  ( | inicio de campo   [ inicio de campo en inverso"
		     "   R reverso\n"
		     "               . no protegido   _ subrayado   * parpadeo"
		     "   i invisible )\n";
		o << "     +";
		for (int c = 0; c < cols; c++) o << ((c % 10 == 0) ? '+' : '-');
		o << "+\n";
		for (int r = 0; r < filas; r++)
		{
			char num[8];
			std::snprintf(num, sizeof(num), "%3d  |", r);
			o << num;
			for (int c = 0; c < cols; c++)
				o << LetraDeAtributo(page->GetCell(c, r));
			o << "|\n";
		}
		o << "     +";
		for (int c = 0; c < cols; c++) o << ((c % 10 == 0) ? '+' : '-');
		o << "+\n";
	}

	o << "\nrespuesta del terminal (" << salida.enviado.size() << " bytes):\n";
	if (salida.enviado.empty()) o << "  (ninguna)\n";
	else                        o << "  " << Legible(salida.enviado) << "\n";

	if (op.mostrarLog)
	{
		o << "\nlog del nucleo (" << log.lineas.size() << " lineas):\n";
		if (log.lineas.empty()) o << "  (vacio)\n";
		for (size_t i = 0; i < log.lineas.size(); i++)
			o << "  " << log.lineas[i] << "\n";
	}

	Log::SetSink(nullptr);
	Log::SetQuiet(false);
	return o.str();
}

/* ---------------------------------------------------------------------- */

/** Lee del propio .esperado con que opciones se genero.
 *
 *  Una captura real no se reproduce entera: la de VIEWSYS solo tiene sentido
 *  entre los offsets 805 y 4852. Si el rango vive en la linea de comandos, el
 *  .esperado queda inservible para --verificar-todo, que corre sin
 *  argumentos. Guardarlo en el archivo hace que cada captura lleve encima sus
 *  propios parametros y la suite pueda tener capturas heterogeneas.
 *
 *  El volcado ya imprime las dos lineas que hacen falta:
 *      bytes de entrada ...... 4047  (recorte 805..4852)
 *      entregados en ......... trozos de 64 bytes
 */
static Opciones OpcionesDelEsperado(const std::string &referencia,
                                    const Opciones &base)
{
	Opciones op = base;
	std::istringstream in(referencia);
	std::string linea;

	for (int n = 0; n < 4 && std::getline(in, linea); n++)
	{
		size_t p = linea.find("(recorte ");
		if (p != std::string::npos)
		{
			int a = 0, b = 0;
			if (std::sscanf(linea.c_str() + p, "(recorte %d..%d)", &a, &b) == 2)
			{
				op.desde = a;
				op.hasta = b;
			}
		}
		p = linea.find("trozos de ");
		if (p != std::string::npos)
		{
			int t = 0;
			if (std::sscanf(linea.c_str() + p, "trozos de %d", &t) == 1)
				op.trozos = t;
		}
	}

	/*  El bloque de atributos no esta en la cabecera sino mas abajo, asi que
	 *  se busca su encabezado en todo el archivo. */
	op.atributos = referencia.find("atributos  ( | inicio de campo")
	             != std::string::npos;

	return op;
}

static int VerificarUna(const fs::path &captura, const Opciones &op, bool generar)
{
	std::string payload;
	if (!LeerArchivo(captura, payload))
	{
		std::cerr << "no se pudo leer " << captura << "\n";
		return 2;
	}

	std::string volcado = Reproducir(Recortar(payload, op), op);

	fs::path esperado = captura;
	esperado += ".esperado";

	if (generar)
	{
		std::ofstream out(esperado, std::ios::binary);
		out << volcado;
		std::cout << "generado  " << esperado.filename().string() << "\n";
		return 0;
	}

	std::string referencia;
	if (!LeerArchivo(esperado, referencia))
	{
		/*  Todavia no fijada. No es una diferencia: es una captura que
		 *  entro al directorio y nadie reviso aun. Se informa y se sigue,
		 *  para que dejar caer una captura nueva no ponga la suite en rojo
		 *  antes de que alguien la haya mirado. */
		std::cout << "SIN FIJAR  " << captura.filename().string()
		          << "   (correr con --generar)\n";
		return 2;
	}

	/*  Se rehace el volcado con las opciones que el propio .esperado
	 *  declara, para que --verificar-todo reproduzca cada captura como fue
	 *  generada sin tener que repetir los argumentos. */
	const Opciones opRef = OpcionesDelEsperado(referencia, op);
	if (opRef.desde != op.desde || opRef.hasta != op.hasta ||
	    opRef.trozos != op.trozos || opRef.atributos != op.atributos)
	{
		volcado = Reproducir(Recortar(payload, opRef), opRef);
	}

	if (referencia == volcado)
	{
		std::cout << "OK      " << captura.filename().string() << "\n";
		return 0;
	}

	std::cout << "DIFIERE " << captura.filename().string() << "\n";

	/* Primera linea distinta, que suele bastar para ubicar el cambio. */
	std::istringstream a(referencia), b(volcado);
	std::string la, lb;
	int n = 1;
	for (;;)
	{
		bool hayA = (bool)std::getline(a, la);
		bool hayB = (bool)std::getline(b, lb);
		if (!hayA && !hayB) break;
		if (!hayA) la = "(fin del archivo)";
		if (!hayB) lb = "(fin del archivo)";
		if (la != lb)
		{
			std::cout << "        primera diferencia en la linea " << n << "\n";
			std::cout << "        esperado: " << la << "\n";
			std::cout << "        obtenido: " << lb << "\n";
			break;
		}
		n++;
	}
	return 1;
}

static int VerificarTodo(const fs::path &directorio, const Opciones &op, bool generar)
{
	if (!fs::exists(directorio))
	{
		std::cerr << "no existe el directorio " << directorio << "\n";
		return 2;
	}

	std::vector<fs::path> capturas;
	int omitidas = 0;
	for (const auto &e : fs::directory_iterator(directorio))
	{
		if (!e.is_regular_file()) continue;
		std::string nombre = e.path().filename().string();
		if (nombre.size() <= 8 ||
		    nombre.compare(nombre.size() - 8, 8, ".payload") != 0)
			continue;

		/*  Los .term.payload son lo que el TERMINAL le manda al host:
		 *  teclas, secuencias AID, lecturas de bloque. Guardian no los
		 *  interpreta -- para eso esta tools/inspeccionar.py --, asi que no
		 *  tiene sentido reproducirlos contra el nucleo. */
		if (nombre.find(".term.") != std::string::npos)
		{
			omitidas++;
			continue;
		}
		capturas.push_back(e.path());
	}
	std::sort(capturas.begin(), capturas.end());

	if (capturas.empty())
	{
		std::cout << "no hay capturas .payload en " << directorio.string()
		          << " -- todavia no hay nada que verificar\n";
		return 0;
	}

	int malas = 0, sinFijar = 0;
	for (const auto &c : capturas)
	{
		const int rc = VerificarUna(c, op, generar);
		if (rc == 1) malas++;
		else if (rc == 2) sinFijar++;
	}

	std::cout << "\n" << capturas.size() << " captura(s), " << malas
	          << " con diferencias";
	if (sinFijar) std::cout << ", " << sinFijar << " sin fijar";
	if (omitidas) std::cout << "   (" << omitidas
	                        << " .term.payload omitida(s): son del terminal "
	                           "al host, se leen con inspeccionar.py)";
	std::cout << "\n";

	/*  Solo las diferencias reales fallan. Una captura sin fijar es trabajo
	 *  pendiente, no una regresion. */
	return (malas == 0) ? 0 : 1;
}

/* ---------------------------------------------------------------------- */

static void Uso()
{
	std::cout <<
	"vt6530_replay -- reproduce una captura 6530 contra el nucleo\n"
	"\n"
	"  vt6530_replay <archivo.payload> [opciones]\n"
	"  vt6530_replay --verificar-todo <directorio> [opciones]\n"
	"\n"
	"opciones:\n"
	"  --trozos N        entregar el payload en trozos de N bytes, para\n"
	"                    reproducir el reparto de TCP (defecto: todo junto)\n"
	"  --desde N         empezar en el offset N del payload\n"
	"  --hasta N         terminar en el offset N; util para aislar el tramo\n"
	"                    de modo bloque de una captura que recorre varios\n"
	"  --atributos       agregar el mapa de atributos por celda\n"
	"  --sin-log         omitir el log del nucleo en el volcado\n"
	"  --generar         escribir el archivo .esperado en vez de comparar\n"
	"  --verificar       comparar contra el .esperado y salir con codigo\n"
	"\n"
	"sin --generar ni --verificar, muestra el volcado por pantalla.\n";
}

int main(int argc, char **argv)
{
	Opciones op;
	std::string objetivo;
	bool generar = false, verificar = false, todo = false;

	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		if (a == "--trozos" && i + 1 < argc)      op.trozos = std::atoi(argv[++i]);
		else if (a == "--desde" && i + 1 < argc)  op.desde  = std::atoi(argv[++i]);
		else if (a == "--hasta" && i + 1 < argc)  op.hasta  = std::atoi(argv[++i]);
		else if (a == "--atributos")              op.atributos = true;
		else if (a == "--sin-log")                op.mostrarLog = false;
		else if (a == "--generar")                generar = true;
		else if (a == "--verificar")              verificar = true;
		else if (a == "--verificar-todo" && i + 1 < argc)
		                                          { todo = true; objetivo = argv[++i]; }
		else if (a == "-h" || a == "--help")      { Uso(); return 0; }
		else if (!a.empty() && a[0] == '-')       { Uso(); return 2; }
		else                                      objetivo = a;
	}

	if (objetivo.empty()) { Uso(); return 2; }

	if (todo) return VerificarTodo(objetivo, op, generar);
	if (generar || verificar) return VerificarUna(objetivo, op, generar);

	std::string payload;
	if (!LeerArchivo(objetivo, payload))
	{
		std::cerr << "no se pudo leer " << objetivo << "\n";
		return 2;
	}
	std::cout << Reproducir(Recortar(payload, op), op);
	return 0;
}
