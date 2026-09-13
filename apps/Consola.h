/*
 *  Consola -- leer del teclado, con y sin eco, en los dos sistemas.
 *
 *  POR QUE EXISTE
 *
 *  Estaba adentro de apps/vt6530qt/main.cpp, escrito solo para POSIX con un
 *  #else que decia "no hay consola para pedir la clave: use -i". En MinGW
 *  Q_OS_UNIX no esta definido, asi que en Windows ese #else era el unico
 *  camino: la aplicacion jamas podia pedir una clave, y lo informaba como si
 *  fuera un problema del entorno del usuario y no una parte que nunca se
 *  escribio.
 *
 *  Y estaba ahi adentro justamente porque main.cpp no lo compila nadie en el
 *  entorno donde se escribio este port. Aca si.
 *
 *  Apagar el eco es lo unico que cambia de verdad entre los dos: termios en
 *  POSIX, SetConsoleMode en Windows. Leer la linea es igual en los dos.
 */
#ifndef _vt6530_consola_h
#define _vt6530_consola_h

#include <string>

namespace consola {

/**
 *  true si hay una terminal de verdad del otro lado.
 *
 *  Importa para no quedarse esperando una respuesta que nadie puede dar:
 *  lanzada desde un icono del escritorio, la aplicacion no tiene a quien
 *  preguntarle.
 */
bool Hay();

/** Lee una linea del teclado. false si no se pudo (fin de archivo). */
bool LeerLinea(std::string *linea);

/**
 *  Lee una linea con el eco apagado, para una clave.
 *
 *  Si no se pudo apagar el eco, igual lee: mejor una clave visible que un
 *  programa que no deja entrar. Devuelve false solo si no pudo leer.
 */
bool LeerLineaSinEco(std::string *linea);

/**
 *  Interpreta una respuesta de si/no.
 *
 *  Solo un si explicito cuenta. Un ENTER de apuro no puede alcanzar para
 *  aceptar la clave de un host que nadie miro, asi que la cadena vacia es
 *  que no.
 */
bool EsSi(const std::string &respuesta);

} // namespace consola

#endif
