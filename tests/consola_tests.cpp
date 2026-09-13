/*
 *  consola_tests -- el si/no, y que la rama Windows exista.
 *
 *  Dos cosas distintas se prueban aca, con distinto alcance.
 *
 *  La primera es EsSi(), que es una decision de seguridad chiquita pero real:
 *  si acepta demasiado, un ENTER de apuro registra la clave de un host que
 *  nadie miro. Eso se prueba de verdad, con datos.
 *
 *  La segunda es que apps/Consola.cpp COMPILE por los dos caminos. El motivo
 *  es concreto: esto vivia adentro de apps/vt6530qt/main.cpp, escrito solo
 *  para POSIX, con un #else que en Windows decia "no hay consola para pedir
 *  la clave: use -i". No era cierto -- la consola estaba ahi -- y encima
 *  sonaba a problema del usuario. Como main.cpp necesita Qt, nadie lo
 *  compilaba y nadie lo veia. Aca si.
 *
 *  Leer del teclado de verdad no se puede probar sin un teclado, asi que de
 *  Hay(), LeerLinea() y LeerLineaSinEco() solo se verifica que existan y
 *  enlacen. Es poco, y es mucho mas que antes.
 */
#include "../apps/Consola.h"

#include <cstdio>
#include <string>

static int g_ok = 0;
static int g_mal = 0;

static void Comprobar(bool condicion, const char *que)
{
	if (condicion) { g_ok++; return; }
	g_mal++;
	std::printf("    FALLO: %s\n", que);
}

static void Test_SoloUnSiExplicitoAcepta()
{
	Comprobar(consola::EsSi("si"),  "'si'");
	Comprobar(consola::EsSi("s"),   "'s'");
	Comprobar(consola::EsSi("yes"), "'yes'");
	Comprobar(consola::EsSi("y"),   "'y'");
	Comprobar(consola::EsSi("SI"),  "'SI' en mayusculas");
	Comprobar(consola::EsSi("Yes"), "'Yes'");
	std::printf("           si, s, yes, y -- en cualquier caja\n");

	/*  Lo que llega de una terminal trae de todo alrededor. */
	Comprobar(consola::EsSi("  si  "), "con espacios a los costados");
	Comprobar(consola::EsSi("si\r"),   "con el CR de una terminal de Windows");
	Comprobar(consola::EsSi("\tsi\n"), "con tabulador y salto");
	std::printf("           los espacios y el CR no estorban\n");
}

static void Test_LoDemasEsQueNo()
{
	/*  ESTA es la que importa. Aceptar de mas aca significa registrar la
	 *  clave de un host que nadie miro, que es exactamente lo que la
	 *  pregunta existe para evitar.                                      */
	Comprobar(!consola::EsSi(""),       "la cadena vacia, o sea un ENTER solo");
	Comprobar(!consola::EsSi(" "),      "un espacio solo");
	Comprobar(!consola::EsSi("\n"),     "un salto solo");
	Comprobar(!consola::EsSi("no"),     "'no'");
	Comprobar(!consola::EsSi("n"),      "'n'");
	Comprobar(!consola::EsSi("quizas"), "cualquier otra cosa");
	Comprobar(!consola::EsSi("sip"),    "'sip' no es 'si'");
	Comprobar(!consola::EsSi("sin"),    "'sin' tampoco");
	Comprobar(!consola::EsSi("s i"),    "'s i' con un espacio en el medio");
	std::printf("           un ENTER de apuro NO acepta nada\n");
}

/*  Referencia a las tres que tocan el teclado, para que el compilador tenga
 *  que generarlas por los dos caminos. No se llaman: sin teclado se
 *  quedarian esperando.                                                   */
static void TocarLasQueNecesitanTeclado()
{
	if (std::string("no").empty())   /* nunca */
	{
		std::string s;
		(void)consola::Hay();
		(void)consola::LeerLinea(&s);
		(void)consola::LeerLineaSinEco(&s);
	}
}

int main()
{
	std::printf("\nConsola -- pruebas\n");
	std::printf("==================\n\n");

	TocarLasQueNecesitanTeclado();
	Test_SoloUnSiExplicitoAcepta();
	Test_LoDemasEsQueNo();

	std::printf("\n-------------------\n");
	std::printf("  comprobaciones OK ....... %d\n", g_ok);
	std::printf("  comprobaciones fallidas . %d\n", g_mal);
	std::printf("-------------------\n\n");

	return g_mal == 0 ? 0 : 1;
}
