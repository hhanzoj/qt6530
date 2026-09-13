#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Genera capturas sinteticas para poder ejercitar vt6530_replay antes de tener
acceso al host.

No pretenden imitar a ninguna aplicacion en particular: solo arman secuencias
6530 validas que recorren los caminos que interesan. En cuanto haya capturas
reales de tools/tap6530.py, estas pasan a ser un piso minimo y nada mas.

    python3 tools/captura_sintetica.py --salida capturas
"""

import argparse
import os

ESC = 0x1B
SOH = 0x01
ETX = 0x03
ENQ = 0x05

# Direccion de cursor y de buffer: fila y columna viajan sesgadas +0x20.
FIJAR_CURSOR = 0x13
FIJAR_BUFFER = 0x11
INICIO_CAMPO = 0x1D


def cursor(fila, columna):
    """Direccion de CURSOR: donde queda el operador."""
    return bytes([FIJAR_CURSOR, 0x20 + fila, 0x20 + columna])


def buffer(fila, columna):
    """Direccion de BUFFER: donde escribe el host.

    En modo protegido son cosas distintas y el nucleo las trata como tales:
    Guardian manda el texto por WriteBuffer(), que usa la direccion de
    buffer, no la del cursor. Posicionar con 0x13 en modo protegido no mueve
    el punto de escritura -- es correcto, y conviene tenerlo presente al
    armar capturas a mano.
    """
    return bytes([FIJAR_BUFFER, 0x20 + fila, 0x20 + columna])


def campo(video=1, dato=0x60):
    """Inicio de campo. Los atributos viajan sesgados +0x20.

    video 1  = normal
    dato  0x60 = bit6 (formato) + bit5 (no protegido)
    dato  0x40 = bit6 solamente, o sea campo protegido
    """
    return bytes([INICIO_CAMPO, 0x20 + video, (0x20 + dato) & 0xFF])


def texto(s):
    return s.encode("ascii")


def modo_bloque():
    return bytes([SOH, ord('B'), ETX])


def entrar_protegido():
    return bytes([ESC, ord('W')])


def desbloquear_teclado():
    # ESC b deja al parser esperando CR LF (estado 10000).
    return bytes([ESC, ord('b'), 0x0D, 0x0A])


# ---------------------------------------------------------------------------

def pantalla_login():
    """Una pantalla de acceso con etiquetas protegidas y dos campos de entrada."""
    b = bytearray()
    b += modo_bloque()
    b += entrar_protegido()

    b += buffer(1, 24)
    b += texto("SISTEMA \\NONSTOP  --  ACCESO")

    b += buffer(3, 24)
    b += texto("----------------------------")

    b += buffer(7, 20)
    b += texto("USUARIO....:")
    b += campo(video=1, dato=0x60)          # campo de entrada
    b += buffer(7, 50)
    b += campo(video=1, dato=0x40)          # cierre: vuelve a protegido

    b += buffer(9, 20)
    b += texto("CLAVE......:")
    b += campo(video=1 | 8, dato=0x60)      # bit3 = invisible
    b += buffer(9, 50)
    b += campo(video=1, dato=0x40)

    b += buffer(13, 20)
    b += texto("F1 ENTRAR    F16 SALIR")

    b += buffer(22, 0)
    b += texto("Ingrese usuario y clave, luego F1.")

    b += cursor(7, 33)                      # el operador arranca en el campo
    b += desbloquear_teclado()
    b += bytes([ENQ])
    return bytes(b)


def pantalla_menu():
    """Un menu conversacional simple, sin modo protegido."""
    b = bytearray()
    b += bytes([SOH, ord('C'), ETX])        # modo conversacional
    b += texto("TACL 1> STATUS *, USER\r\n")
    b += texto("\r\n")
    b += texto("Process       Pri  PFR  %WT  Userid    Program file\r\n")
    b += texto("$Z0AB         148  R    001  255,255   \\SYS.$SYSTEM.SYS00.TACL\r\n")
    b += texto("$Z0AC         148  R    000  255,255   \\SYS.$SYSTEM.SYS00.TACL\r\n")
    b += texto("\r\n")
    b += texto("TACL 2> ")
    b += bytes([ENQ])
    return bytes(b)


def tabla_de_tipos():
    """Ejercita ESC r, la tabla de tipos de dato de 96 bytes.

    Reproducida con --trozos deja a la vista el defecto: el contador es una
    variable local de ProcessRemoteString y se reinicia en cada lectura, asi
    que repartida en dos segmentos nunca completa y el parser se come todo
    lo que sigue.
    """
    b = bytearray()
    b += modo_bloque()
    b += entrar_protegido()
    b += buffer(0, 0)
    b += texto("ANTES DE LA TABLA")
    b += bytes([ESC, ord('r')])
    b += bytes([0x41] * 96)                 # los 96 bytes de la tabla
    b += buffer(2, 0)
    b += texto("DESPUES DE LA TABLA")
    b += desbloquear_teclado()
    return bytes(b)


CAPTURAS = [
    ("sintetica-login.host.payload",  pantalla_login),
    ("sintetica-menu.host.payload",   pantalla_menu),
    ("sintetica-tabla.host.payload",  tabla_de_tipos),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--salida", default="capturas")
    cfg = ap.parse_args()

    os.makedirs(cfg.salida, exist_ok=True)
    for nombre, generar in CAPTURAS:
        ruta = os.path.join(cfg.salida, nombre)
        datos = generar()
        with open(ruta, "wb") as f:
            f.write(datos)
        print("%-36s %5d bytes" % (nombre, len(datos)))


if __name__ == "__main__":
    main()
