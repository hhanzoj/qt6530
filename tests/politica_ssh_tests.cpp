/*
 *  politica_ssh_tests -- que las opciones lleguen a la politica de ssh.
 *
 *  Esta traduccion vivia adentro de apps/vt6530qt/main.cpp, que necesita Qt y
 *  por lo tanto no se compila en el entorno donde se escribio este port. Dos
 *  errores de compilacion tontos se escaparon por ahi -- una variable
 *  renombrada y un enum sin calificar --, asi que se mudo a un archivo sin Qt
 *  y esto es lo que la mira.
 *
 *  Lo que se prueba no es "que copie los campos", que seria una prueba que no
 *  sirve para nada: es que los campos que costaron trabajo lleguen enteros.
 *  Cada uno de estos se descubrio con una sesion contra el host.
 */
#include "../apps/PoliticaSsh.h"

#include <cstdio>
#include <string>
#include <vector>

using cli::Opciones;
using cli::Parsear;
using cli::PoliticaSshDesde;

static int g_ok = 0;
static int g_mal = 0;

static void Check(bool condicion, const char *que)
{
	if (condicion) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLO: %s\n", que);
}

static ssh6530::Politica P(const std::vector<std::string> &args)
{
	return PoliticaSshDesde(Parsear(args));
}

static void Test_LoQueCostoLlegaEntero()
{
	const ssh6530::Politica p = P({ "jaracena@rci3", "-ssh" });

	Check(p.usuario == "jaracena", "el usuario, que en ssh va antes del canal");
	Check(p.terminalType == "TN6530-8",
	      "el TERM del pty-req, en mayusculas");
	Check(p.usarShell, "shell y no exec");
	Check(p.ptyCrudo, "el pty en crudo: sin eco y sin ISIG");
	Check(p.hostDesconocido == ssh6530::HostDesconocido::Preguntar,
	      "un host nuevo se pregunta");
	Check(p.anotarHostNuevo, "y si se acepta, se anota");
	std::printf("           TN6530-8, shell, pty crudo, preguntar y anotar\n");
}

static void Test_LosDosTiposSonDosCampos()
{
	/*  -tipo es el terminal-type de telnet; -tipo-pty es el TERM del pty-req
	 *  de ssh. Creer que eran uno solo costo un dia entero de diagnostico
	 *  equivocado, asi que lo que importa comprobar es que estan separados
	 *  de verdad.                                                         */
	const Opciones o =
		Parsear({ "jaracena@rci3", "-ssh", "-tipo", "abc", "-tipo-pty", "XYZ" });
	Check(PoliticaSshDesde(o).terminalType == "XYZ",
	      "a la politica de ssh va el -tipo-pty");
	Check(o.tipoTerminal == "abc",
	      "y el -tipo queda para la negociacion telnet, sin tocarse");

	/*  Y en ssh, cambiar SOLO el -tipo no mueve el TERM del pty: la opcion
	 *  tiene su propio valor por defecto. Esto es lo contrario de lo que
	 *  pasaba cuando los dos campos salian del mismo lugar, y es la razon
	 *  de que aquella vez pareciera que el TERM era el culpable.          */
	const Opciones q = Parsear({ "jaracena@rci3", "-ssh", "-tipo", "abc" });
	Check(PoliticaSshDesde(q).terminalType == "TN6530-8",
	      "-tipo solo no cambia el TERM del pty");
	Check(q.tipoTerminal == "abc", "pero si cambia el terminal-type de telnet");

	/*  En cambio sin -ssh no hay default de pty, y ahi si se cae al otro:
	 *  es el camino que usa la sonda cuando se le arma la politica a mano. */
	const Opciones t = Parsear({ "rci3", "-tipo", "abc" });
	Check(PoliticaSshDesde(t).terminalType == "abc",
	      "sin -ssh, y sin -tipo-pty, se usa el -tipo");

	std::printf("           -tipo y -tipo-pty son dos campos, no uno\n");
}

static void Test_LaClaveDelHostLlegaALaPolitica()
{
	Check(P({ "jaracena@rci3", "-ssh", "-aceptar-host-nuevo" })
	          .hostDesconocido == ssh6530::HostDesconocido::Aceptar,
	      "-aceptar-host-nuevo llega hasta el transporte");
	Check(P({ "jaracena@rci3", "-ssh", "-rechazar-host-nuevo" })
	          .hostDesconocido == ssh6530::HostDesconocido::Rechazar,
	      "-rechazar-host-nuevo tambien");
	Check(P({ "jaracena@rci3", "-ssh", "-known-hosts", "/tmp/kh" })
	          .knownHosts == "/tmp/kh",
	      "y la ruta del archivo");
	std::printf("           la bandera no se queda en el parseo\n");
}

static void Test_ElComandoYElShellNoSePisan()
{
	const ssh6530::Politica c =
		P({ "jaracena@rci3", "-ssh", "-comando", "viewsys" });
	Check(!c.usarShell, "con -comando se pide exec");
	Check(c.comando == "viewsys", "y el comando viaja");

	const ssh6530::Politica s = P({ "jaracena@rci3", "-ssh" });
	Check(s.usarShell, "sin -comando, shell");
	std::printf("           -comando implica exec, y el comando llega\n");
}

static void Test_LaIdentidadYElPtyCocido()
{
	Check(P({ "jaracena@rci3", "-ssh", "-i", "/tmp/clave" }).claveArchivo ==
	          "/tmp/clave",
	      "-i llega como claveArchivo");
	Check(!P({ "jaracena@rci3", "-ssh", "-pty-cocido" }).ptyCrudo,
	      "-pty-cocido apaga el pty crudo");
	std::printf("           -i y -pty-cocido\n");
}

int main()
{
	std::printf("\nPolitica de ssh -- pruebas\n");
	std::printf("==========================\n\n");

	Test_LoQueCostoLlegaEntero();
	Test_LosDosTiposSonDosCampos();
	Test_LaClaveDelHostLlegaALaPolitica();
	Test_ElComandoYElShellNoSePisan();
	Test_LaIdentidadYElPtyCocido();

	std::printf("\n---------------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_ok);
	std::printf("  comprobaciones fallidas . %d\n", g_mal);
	std::printf("---------------------------\n\n");

	return g_mal == 0 ? 0 : 1;
}
