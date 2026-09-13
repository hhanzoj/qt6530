#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Prueba de humo del proxy tap6530, sin host ni emulador.

Levanta un host de mentira que negocia telnet como lo hace TELSERV, arranca
el proxy, conecta un cliente de mentira que responde la negociacion, y
comprueba que el payload extraido coincida byte a byte con el original.

Sirve para confirmar que el proxy funciona en tu maquina antes de ponerlo
delante de una sesion real.

    python3 tools/prueba_tap.py
"""

import os
import socket
import subprocess
import sys
import tempfile
import threading
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
RAIZ = os.path.dirname(AQUI)

IAC = 255
WILL, WONT, DO, DONT = 251, 252, 253, 254
SB, SE, EOR = 250, 240, 239
BINARY, SGA, ENDREC, TTYPE, NAWS = 0, 3, 19, 24, 31


def puerto_libre():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def host_de_mentira(puerto, payload, listo):
    """Negocia telnet y manda el payload, como haria el NonStop."""
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", puerto))
    srv.listen(1)
    listo.set()
    c, _ = srv.accept()
    try:
        c.sendall(bytes([IAC, DO, TTYPE,  IAC, DO, NAWS,
                         IAC, WILL, ENDREC, IAC, DO, ENDREC,
                         IAC, WILL, SGA,   IAC, WILL, BINARY,
                         IAC, DO, BINARY]))
        c.sendall(bytes([IAC, SB, TTYPE, 1, IAC, SE]))   # TERMINAL-TYPE SEND
        time.sleep(0.5)
        try:
            c.recv(4096)
        except OSError:
            pass
        c.sendall(payload)
        c.sendall(bytes([IAC, EOR]))
        time.sleep(0.4)
    finally:
        c.close()
        srv.close()


def cliente_de_mentira(puerto, recibido):
    """Responde la negociacion como un emulador 6530 que funciona."""
    s = socket.create_connection(("127.0.0.1", puerto), timeout=5)
    try:
        time.sleep(0.3)
        s.recv(4096)
        s.sendall(bytes([IAC, WILL, TTYPE, IAC, WILL, NAWS,
                         IAC, DO, ENDREC,  IAC, WILL, ENDREC,
                         IAC, DO, SGA,     IAC, DO, BINARY,
                         IAC, WILL, BINARY]))
        s.sendall(bytes([IAC, SB, TTYPE, 0]) + b"tn6530-8" + bytes([IAC, SE]))
        s.sendall(bytes([IAC, SB, NAWS, 0, 80, 0, 24, IAC, SE]))
        time.sleep(0.8)
        s.settimeout(2)
        try:
            recibido.append(s.recv(65536))
        except OSError:
            pass
    finally:
        s.close()


def main():
    origen = os.path.join(RAIZ, "capturas", "sintetica-login.host.payload")
    if not os.path.exists(origen):
        print("falta %s -- correr antes:" % origen)
        print("    python3 tools/captura_sintetica.py --salida capturas")
        return 2
    payload = open(origen, "rb").read()

    p_host = puerto_libre()
    p_tap = puerto_libre()
    salida = tempfile.mkdtemp(prefix="prueba_tap_")

    print("host de mentira en el puerto %d" % p_host)
    print("proxy en el puerto %d" % p_tap)
    print("archivos en %s\n" % salida)

    listo = threading.Event()
    th = threading.Thread(target=host_de_mentira,
                          args=(p_host, payload, listo), daemon=True)
    th.start()
    listo.wait(timeout=5)

    tap = subprocess.Popen(
        [sys.executable, os.path.join(AQUI, "tap6530.py"),
         "--escucha", "127.0.0.1:%d" % p_tap,
         "--host", "127.0.0.1:%d" % p_host,
         "--salida", salida],
        stderr=subprocess.PIPE)
    time.sleep(1.0)

    recibido = []
    try:
        cliente_de_mentira(p_tap, recibido)
    except OSError as e:
        print("el cliente no pudo conectar: %s" % e)
        tap.terminate()
        return 1

    time.sleep(1.2)
    tap.terminate()
    try:
        tap.wait(timeout=3)
    except subprocess.TimeoutExpired:
        tap.kill()

    # ------------------------------------------------------------------
    problemas = 0

    def comprobar(ok, texto):
        nonlocal problemas
        print("  %s  %s" % ("OK    " if ok else "FALLA ", texto))
        if not ok:
            problemas += 1

    print("resultado:")

    ruta_payload = os.path.join(salida, "sesion-001.host.payload")
    ruta_log = os.path.join(salida, "sesion-001.log")

    comprobar(os.path.exists(ruta_payload), "se creo el archivo .host.payload")
    comprobar(os.path.exists(ruta_log), "se creo el archivo .log")

    if os.path.exists(ruta_payload):
        extraido = open(ruta_payload, "rb").read()
        comprobar(extraido == payload,
                  "el payload extraido coincide byte a byte con el original "
                  "(%d bytes)" % len(extraido))

    if os.path.exists(ruta_log):
        log = open(ruta_log, encoding="utf-8").read()
        comprobar("IAC SB TERMINAL-TYPE  IS tn6530-8" in log,
                  "el log registro el TERMINAL-TYPE del emulador")
        comprobar("NAWS  80x24" in log,
                  "el log registro el tamano de ventana")
        comprobar("IAC EOR" in log,
                  "el log ubico el marcador de fin de registro")

    comprobar(recibido and len(recibido[0]) > 0,
              "el cliente recibio los datos a traves del proxy")

    print()
    if problemas == 0:
        print("el proxy funciona. Ya lo podes usar con el host real:")
        print("    python3 tools/tap6530.py --escucha 127.0.0.1:2323 "
              "--host TUHOST:1016")
    else:
        print("%d comprobacion(es) fallaron. Los archivos quedaron en %s"
              % (problemas, salida))
        err = tap.stderr.read().decode("utf-8", "replace") if tap.stderr else ""
        if err:
            print("\nsalida del proxy:\n%s" % err)

    return 0 if problemas == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
