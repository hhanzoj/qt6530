#include "Consola.h"

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <iostream>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#endif

namespace consola {

namespace {

#if defined(_WIN32)

/*  En Windows el eco lo gobierna la consola, no el descriptor: se apaga
 *  quitandole ENABLE_ECHO_INPUT al modo de la entrada estandar. Es el
 *  equivalente exacto de sacarle ECHO a termios, incluido el detalle de que
 *  hay que acordarse de reponerlo.                                        */
struct SinEco
{
	HANDLE entrada = INVALID_HANDLE_VALUE;
	DWORD  modo = 0;
	bool   cambiado = false;

	SinEco()
	{
		entrada = ::GetStdHandle(STD_INPUT_HANDLE);
		if (entrada == INVALID_HANDLE_VALUE) return;
		if (!::GetConsoleMode(entrada, &modo)) return;
		cambiado = (::SetConsoleMode(entrada, modo & ~(DWORD)ENABLE_ECHO_INPUT) != 0);
	}

	~SinEco()
	{
		if (cambiado) ::SetConsoleMode(entrada, modo);
	}
};

#else

struct SinEco
{
	struct termios antes;
	bool cambiado = false;

	SinEco()
	{
		if (::tcgetattr(STDIN_FILENO, &antes) != 0) return;
		struct termios mudo = antes;
		mudo.c_lflag &= ~(tcflag_t)ECHO;
		cambiado = (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &mudo) == 0);
	}

	~SinEco()
	{
		if (cambiado) ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &antes);
	}
};

#endif

} // namespace

bool Hay()
{
#if defined(_WIN32)
	/*  Si GetConsoleMode contesta, hay una consola. Si la entrada viene de un
	 *  archivo o de una tuberia, falla, que es justo lo que se quiere saber. */
	const HANDLE entrada = ::GetStdHandle(STD_INPUT_HANDLE);
	if (entrada == INVALID_HANDLE_VALUE) return false;
	DWORD modo = 0;
	return ::GetConsoleMode(entrada, &modo) != 0;
#else
	return ::isatty(STDIN_FILENO) == 1;
#endif
}

bool LeerLinea(std::string *linea)
{
	if (linea == nullptr) return false;
	return (bool)std::getline(std::cin, *linea);
}

bool LeerLineaSinEco(std::string *linea)
{
	if (linea == nullptr) return false;

	bool leyo = false;
	{
		SinEco apagado;
		leyo = (bool)std::getline(std::cin, *linea);
	}

	/*  El ENTER que el usuario apreto no se vio, asi que el cursor quedo al
	 *  final del prompt. Sin esto, lo que sigue se escribe pegado.        */
	std::fprintf(stderr, "\n");
	return leyo;
}

bool EsSi(const std::string &respuesta)
{
	std::string r = respuesta;

	while (!r.empty() &&
	       (r[r.size() - 1] == ' ' || r[r.size() - 1] == '\t' ||
	        r[r.size() - 1] == '\r' || r[r.size() - 1] == '\n'))
	{
		r.erase(r.size() - 1);
	}

	std::size_t i = 0;
	while (i < r.size() && (r[i] == ' ' || r[i] == '\t')) i++;
	r = r.substr(i);

	for (std::size_t j = 0; j < r.size(); j++)
		r[j] = (char)std::tolower((unsigned char)r[j]);

	return r == "si" || r == "s" || r == "yes" || r == "y";
}

} // namespace consola
