#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
negociacion.py -- vuelca la negociacion telnet de una captura .raw

inspeccionar.py trabaja sobre el payload, o sea sobre lo que queda DESPUES de
sacar telnet. Este mira la otra mitad: los IAC, incluidas las subnegociaciones
IAC SB ... IAC SE, que es donde LINEMODE transporta el modo de edicion.

    python3 tools/negociacion.py capturas/propio-001.host.raw
    python3 tools/negociacion.py capturas/*.raw

Los offsets son del archivo .raw, que NO coinciden con los del .payload: el
payload no lleva los bytes de negociacion. Para alinear, restar los bytes de
IAC que hayan pasado antes.
"""

import sys

IAC, SB, SE = 255, 250, 240

VERBO = {251: "WILL", 252: "WONT", 253: "DO", 254: "DONT"}

OPCION = {
    0: "BINARY", 1: "ECHO", 3: "SGA", 5: "STATUS", 6: "TIMING-MARK",
    24: "TERMINAL-TYPE", 25: "END-OF-RECORD", 31: "NAWS", 32: "TERMINAL-SPEED",
    33: "TOGGLE-FLOW-CONTROL", 34: "LINEMODE", 35: "X-DISPLAY-LOCATION",
    36: "ENVIRON", 39: "NEW-ENVIRON",
}

MANDO = {
    240: "SE", 241: "NOP", 242: "Data Mark", 243: "BRK", 244: "IP",
    245: "AO", 246: "AYT", 247: "EC", 248: "EL", 249: "GA",
}

#  RFC 1184.  MODE es el que importa para el eco: con EDIT puesto, la edicion
#  de linea la hace el terminal (y por lo tanto el eco es local); sin EDIT, va
#  caracter por caracter y el eco es asunto del host.
LM_SUB = {1: "MODE", 2: "FORWARDMASK", 3: "SLC"}
LM_MODE_BITS = [
    (0x01, "EDIT"),          # el terminal edita la linea localmente
    (0x02, "TRAPSIG"),
    (0x04, "MODE_ACK"),      # es un acuse, no una orden
    (0x08, "SOFT_TAB"),
    (0x10, "LIT_ECHO"),
]


def nombre_opcion(o):
    return OPCION.get(o, "opcion %d" % o)


def hexd(bs):
    return " ".join("%02X" % b for b in bs)


def modo_linemode(mascara):
    puestos = [n for bit, n in LM_MODE_BITS if mascara & bit]
    txt = "+".join(puestos) if puestos else "ninguno"
    if mascara & 0x01:
        txt += "   (edicion y eco LOCALES)"
    else:
        txt += "   (caracter por caracter: el eco es del host)"
    if mascara & 0x04:
        txt += "  [acuse]"
    return txt


def describir_sb(cuerpo):
    if not cuerpo:
        return "SB vacio"
    opt = cuerpo[0]
    resto = cuerpo[1:]
    nombre = nombre_opcion(opt)

    if opt == 34 and resto:                       # LINEMODE
        sub = LM_SUB.get(resto[0])
        if sub == "MODE" and len(resto) >= 2:
            return "SB LINEMODE MODE %02X   -> %s" % (resto[1],
                                                      modo_linemode(resto[1]))
        if sub:
            return "SB LINEMODE %s   %s" % (sub, hexd(resto[1:]))
        return "SB LINEMODE subcomando %d desconocido   %s" % (resto[0],
                                                               hexd(resto[1:]))

    if opt == 24 and resto:                       # TERMINAL-TYPE
        que = "IS" if resto[0] == 0 else ("SEND" if resto[0] == 1 else "?")
        txt = bytes(resto[1:]).decode("latin-1")
        return "SB TERMINAL-TYPE %s '%s'" % (que, txt)

    return "SB %s   %s" % (nombre, hexd(resto))


def volcar(ruta):
    datos = open(ruta, "rb").read()
    print("archivo ....... %s" % ruta)
    print("tamano ........ %d bytes\n" % len(datos))

    i, n = 0, len(datos)
    cuenta = {}
    hubo = False

    while i < n:
        if datos[i] != IAC:
            i += 1
            continue
        if i + 1 >= n:
            break
        c = datos[i + 1]

        if c == IAC:                              # IAC IAC = un 255 de datos
            i += 2
            continue

        if c in VERBO and i + 2 < n:
            o = datos[i + 2]
            linea = "%s %s" % (VERBO[c], nombre_opcion(o))
            print("%8d  %s" % (i, linea))
            cuenta[linea] = cuenta.get(linea, 0) + 1
            hubo = True
            i += 3
            continue

        if c == SB:
            j = i + 2
            cuerpo = []
            while j < n:
                if datos[j] == IAC and j + 1 < n and datos[j + 1] == SE:
                    break
                if datos[j] == IAC and j + 1 < n and datos[j + 1] == IAC:
                    cuerpo.append(IAC)
                    j += 2
                    continue
                cuerpo.append(datos[j])
                j += 1
            desc = describir_sb(cuerpo)
            print("%8d  %s" % (i, desc))
            clave = desc.split("   ")[0]
            cuenta[clave] = cuenta.get(clave, 0) + 1
            hubo = True
            i = j + 2
            continue

        if c in MANDO:
            print("%8d  %s" % (i, MANDO[c]))
            cuenta[MANDO[c]] = cuenta.get(MANDO[c], 0) + 1
            hubo = True
            i += 2
            continue

        i += 2

    if not hubo:
        print("  (sin negociacion telnet en este archivo)")
        return

    print("\nresumen:")
    for k in sorted(cuenta, key=lambda x: (-cuenta[x], x)):
        print("  %-52s %3d" % (k, cuenta[k]))

    print("\n  Para el asunto del eco, lo que decide es SB LINEMODE MODE:")
    print("  con el bit EDIT puesto, la edicion de linea y el eco son del")
    print("  terminal; sin EDIT, van caracter por caracter y el eco lo hace")
    print("  el host -- que es como se oculta una clave.")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for k, ruta in enumerate(sys.argv[1:]):
        if k:
            print("\n" + "-" * 68 + "\n")
        volcar(ruta)
    return 0


if __name__ == "__main__":
    sys.exit(main())
