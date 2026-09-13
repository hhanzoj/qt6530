/*
 *  PoliticaSsh -- de cli::Opciones a ssh6530::Politica, en un solo lugar.
 *
 *  POR QUE EXISTE
 *
 *  Esta traduccion estaba copiada en dos lados -- apps/vt6530qt/main.cpp y
 *  tools/sonda_ssh.cpp -- y ya habia empezado a separarse: la sonda no
 *  seteaba varios campos que la aplicacion si, asi que las dos no probaban lo
 *  mismo. Justamente la sonda existe para reproducir lo que hace la
 *  aplicacion sin Qt en el medio; si arma la sesion distinto, no sirve para
 *  eso.
 *
 *  Y hay una segunda razon, mas incomoda: main.cpp no se puede compilar en el
 *  entorno donde se escribio este port, porque necesita Qt. Todo lo que viva
 *  ahi adentro esta verificado por lectura y nada mas -- dos veces ya se
 *  escaparon errores de compilacion tontos por eso, un nombre mal calificado
 *  y una variable renombrada --. Sacar la logica de main.cpp y traerla a un
 *  archivo sin Qt no es prolijidad: es la diferencia entre que lo mire un
 *  compilador o no.
 *
 *  La regla que queda: main.cpp arma ventanas y engancha senales. Cualquier
 *  decision se toma aca o en Opciones.cpp.
 */
#ifndef _vt6530_politica_ssh_h
#define _vt6530_politica_ssh_h

#include "Opciones.h"

#include <net/Ssh6530Transport.h>

namespace cli {

/**
 *  Arma la politica de ssh a partir de las opciones de la linea de comandos.
 *
 *  No toca el tamano de la ventana (80x24, que lo pone el llamador si le
 *  hace falta otro) ni las devoluciones de llamada, que son de quien las
 *  vaya a atender.
 */
ssh6530::Politica PoliticaSshDesde(const Opciones &op);

} // namespace cli

#endif
