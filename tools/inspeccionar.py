#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Inspecciona un archivo .payload y muestra el flujo de comandos 6530.

vt6530_replay muestra la pantalla que RESULTA de una captura. Esto muestra
la captura misma: qué comandos manda el host, en qué orden y en qué offset.
Sirve para responder "¿esta sesión entró en modo bloque?", "¿qué comandos ESC
usa realmente esta aplicación?" y "¿qué son esos bytes raros del offset N?".

    python3 tools/inspeccionar.py capturas/sesion-001.host.payload
    python3 tools/inspeccionar.py capturas/sesion-001.host.payload --resumen

El tokenizador sigue la misma máquina de estados que Guardian.cpp para los
comandos cuyo formato está confirmado en el código. Donde no lo está, lo dice
y muestra el hex en vez de inventar una lectura.
"""

import argparse
import collections
import sys

# Comandos de un byte del estado 0 de Guardian.
CONTROL = {
    0x00: ("NUL", 0),
    0x01: ("SOH -- cambio de modo", 0),      # se trata aparte
    0x04: ("EOT -- reset de linea", 0),
    0x05: ("ENQ -- el host espera entrada", 0),
    0x07: ("BEL", 0),
    0x08: ("BS -- retroceso", 0),
    0x09: ("HT -- tabulador", 0),
    0x0A: ("LF -- avance de linea", 0),
    0x0D: ("CR -- retorno de carro", 0),
    0x0E: ("SO -- juego de caracteres G1", 0),
    0x0F: ("SI -- juego de caracteres G0", 0),
    0x11: ("DC1 -- fijar direccion de BUFFER", 2),
    0x13: ("DC3 -- fijar direccion de CURSOR", 2),
    0x1D: ("GS -- inicio de campo", 2),
}

# Comandos ESC y cuantos bytes de argumento consumen, segun el switch del
# estado 1 de Guardian.cpp. None = formato variable, no se interpreta.
ESC_CMD = {
    '0': ("print screen (cuerpo vacio en el nucleo)", 0),
    '1': ("fijar tabulador (cuerpo vacio)", 0),
    '2': ("borrar tabulador (cuerpo vacio)", 0),
    '3': ("borrar todos los tabuladores (cuerpo vacio)", 0),
    '6': ("fijar atributos de video", 1),
    '7': ("registro de condicion previa de video", 1),
    ';': ("mostrar pagina", 1),
    '?': ("leer configuracion de terminal", 0),
    '@': ("esperar un segundo", 0),
    'A': ("cursor arriba", 0),
    'C': ("cursor derecha", 0),
    'F': ("cursor al final", 0),
    'H': ("cursor al inicio", 0),
    'I': ("borrar memoria a espacios", "I"),   # 0 args, o 4 en modo protegido
    'J': ("borrar hasta fin de pagina", 0),
    'K': ("borrar hasta fin de linea", 0),
    'L': ("insertar linea (cuerpo vacio)", 0),
    'M': ("borrar linea (cuerpo vacio)", 0),
    'N': ("deshabilitar edicion local (solo log)", 0),
    'O': ("insertar caracter", 0),
    'P': ("borrar caracter", 0),
    'S': ("roll up (solo log)", 0),
    'T': ("roll down (llama a LineDown, vacio)", 0),
    'U': ("pagina abajo (solo log)", 0),
    'V': ("pagina arriba (solo log)", 0),
    'W': ("ENTRAR EN MODO PROTEGIDO", 0),
    'X': ("salir de modo protegido", 0),
    '^': ("leer estado del terminal", 0),
    '_': ("leer revision de firmware", 0),
    'a': ("leer direccion de cursor", 0),
    'b': ("desbloquear teclado", None),        # deja al parser esperando CR LF
    'c': ("bloquear teclado", 0),
    'd': ("simular tecla de funcion", 1),
    'f': ("desconectar modem (solo log)", 0),
    'i': ("tabulador hacia atras", 0),
    'o': ("escribir en linea de mensaje", None),   # hasta CR
    'p': ("fijar numero maximo de pagina", 1),
    'q': ("reinicializar", None),
    'r': ("definir tabla de tipos de dato", 96),
    'u': ("definir funcion de tecla Enter", None),
    'v': ("fijar configuracion de terminal", None),  # hasta CR
    'x': ("fijar config de dispositivo (solo log)", 0),
    'y': ("leer config de dispositivo (solo log)", 0),
    '{': ("escribir a archivo/dispositivo (solo log)", 0),
    '}': ("escribir/leer archivo/dispositivo (solo log)", 0),
    '<': ("leer buffer", 0),
    '=': ("leer con direccion", 4),
    '>': ("resetear los MDT", 0),
    '[': ("inicio de campo extendido", 3),
    ']': ("leer con direccion, todos los campos", None),
    ':': ("seleccionar pagina", 1),
    '-': ("secuencia CSI extendida", None),
}

MODOS = {'A': "ANSI", 'B': "BLOQUE", 'C': "CONVERSACIONAL"}

# ---------------------------------------------------------------------------
#  Sentido terminal -> host
#
#  Ahi SOH no anuncia un cambio de modo: abre una secuencia AID, la respuesta
#  del terminal a una tecla de atencion. El formato lo arma Keys::KeyAction en
#  modo protegido:
#
#      SOH  codigo  pagina+0x20  fila+0x20  col+0x21  ETX  NUL      (7 bytes)
#
#  Los codigos de tecla van 0x40 + (n-1), confirmado contra plainFn del nucleo
#  para F1..F12 y contra una captura real de VIEWSYS saliendo con F16 (0x4F).
# ---------------------------------------------------------------------------

def nombre_tecla(codigo):
    if 0x40 <= codigo <= 0x4F:
        n = codigo - 0x40 + 1
        aviso = "" if n <= 12 else "   <- el nucleo NO la implementa"
        return "F%d%s" % (n, aviso)
    if codigo == 0x56:                      # 'V'
        return "ENTER (modo protegido)"
    if codigo == 0x27:
        return "shift-F1"
    if 0x61 <= codigo <= 0x67:
        return "shift-F%d" % (codigo - 0x61 + 2)
    if codigo in (0x68, 0x69):
        return "shift-F%d" % (codigo - 0x68 + 9)
    if codigo in (0x6A, 0x6B):
        return "shift-F%d" % (codigo - 0x6A + 11)
    return "codigo de tecla desconocido"


CONTROL_TERM = {
    0x03: ("ETX -- fin de la secuencia", 0),
    0x04: ("EOT -- fin del bloque", 0),
    0x00: ("NUL", 0),
    0x0D: ("CR -- fin de linea tecleada", 0),
    0x11: ("DC1 -- direccion de campo en la lectura de bloque", 2),
}


def atributos_video(b):
    """Decodifica el byte de ESC 6 / ESC 7 como lo hace
    TextDisplay::DecodeVideoAttrs. El bit 3 es el que oculta una clave."""
    if b in (0x00, 0x20):
        return "normal"
    nombres = []
    if b & (1 << 0): nombres.append("normal")
    if b & (1 << 1): nombres.append("parpadeo")
    if b & (1 << 2): nombres.append("reverso")
    if b & (1 << 3): nombres.append("INVISIBLE")
    if b & (1 << 4): nombres.append("subrayado")
    return "+".join(nombres) if nombres else "ninguno"


def hexd(bs):
    return " ".join("%02X" % b for b in bs)


def visible(bs):
    return "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in bs)


def coord(b):
    """Las coordenadas viajan sesgadas +0x20."""
    return b - 0x20


def inspeccionar_term(datos):
    """Sentido terminal -> host: secuencias AID y lecturas de bloque."""
    eventos = []
    cuenta = collections.Counter()
    pos, n = 0, len(datos)
    texto = bytearray()
    inicio_texto = 0

    def volcar():
        nonlocal texto, inicio_texto
        if texto:
            eventos.append((inicio_texto, "texto", "%d bytes tecleados: %r"
                            % (len(texto), bytes(texto).decode("latin-1"))))
            cuenta["texto tecleado"] += 1
            texto = bytearray()

    while pos < n:
        b = datos[pos]

        if b > 31 and b != 0x7F:
            if not texto:
                inicio_texto = pos
            texto.append(b)
            pos += 1
            continue

        volcar()
        off = pos

        if b == 0x01 and pos + 6 < n and datos[pos + 5] == 0x03:
            cod, pag = datos[pos + 1], datos[pos + 2]
            fila, col = datos[pos + 3], datos[pos + 4]

            #  Page::GetStartFieldASCII escribe fila+0x20 y columna+0x21, y
            #  cuando no encuentra ningun campo no protegido en la pagina
            #  escribe dos espacios. O sea que 0x20 0x20 no es la posicion
            #  (0,-1): es el centinela de "esta pantalla no tiene campos de
            #  entrada", tipico de una aplicacion de solo consulta.
            if fila == 0x20 and col == 0x20:
                donde = "sin campos de entrada en la pagina"
            else:
                donde = "campo en fila %d, columna %d" % (fila - 0x20,
                                                          col - 0x21)

            eventos.append((off, "aid",
                            "SECUENCIA AID   tecla %s   pagina %d   %s"
                            % (nombre_tecla(cod), pag - 0x20, donde)))
            eventos.append((off, "aid", "                bytes: %s"
                            % hexd(datos[pos:pos + 7])))
            cuenta["secuencia AID"] += 1
            cuenta["tecla " + nombre_tecla(cod).split("   ")[0]] += 1
            pos += 7
            continue

        if b in CONTROL_TERM:
            nombre, nargs = CONTROL_TERM[b]
            if nargs == 2 and pos + 2 < n:
                eventos.append((off, "ctrl", "%-46s fila %d, columna %d"
                                % (nombre, datos[pos + 1] - 0x20,
                                   datos[pos + 2] - 0x21)))
                cuenta[nombre.split(" --")[0]] += 1
                pos += 3
                continue
            eventos.append((off, "ctrl", nombre))
            cuenta[nombre.split(" --")[0]] += 1
            pos += 1
            continue

        eventos.append((off, "raro", "byte de control sin manejar: %02X" % b))
        cuenta["byte sin manejar"] += 1
        pos += 1

    volcar()
    return eventos, cuenta


def inspeccionar(datos, mostrar_texto=True, limite=None):
    eventos = []
    cuenta = collections.Counter()
    pos = 0
    n = len(datos)
    texto = bytearray()
    inicio_texto = 0

    #  Se sigue el modo protegido porque ESC I cambia de forma segun el:
    #  cuatro argumentos con las esquinas del bloque en protegido, ninguno
    #  fuera. Sin esto, los cuatro bytes de coordenadas se leen como texto.
    protegido = False

    def volcar_texto():
        nonlocal texto, inicio_texto
        if texto:
            eventos.append((inicio_texto, "texto", "%d bytes: %r"
                            % (len(texto), bytes(texto).decode("latin-1"))))
            cuenta["texto"] += 1
            texto = bytearray()

    while pos < n:
        b = datos[pos]

        if b > 31 and b != 0x7F:
            if not texto:
                inicio_texto = pos
            texto.append(b)
            pos += 1
            continue

        volcar_texto()
        off = pos

        # ---- cambio de modo: SOH <letra> ETX -------------------------
        if b == 0x01:
            if pos + 2 < n and chr(datos[pos + 1]) in MODOS and datos[pos + 2] == 0x03:
                letra = chr(datos[pos + 1])
                eventos.append((off, "modo", "SOH %s ETX  ->  MODO %s"
                                % (letra, MODOS[letra])))
                cuenta["modo " + MODOS[letra]] += 1
                if letra == 'C':
                    protegido = False
                pos += 3
                continue
            sig = datos[pos + 1:pos + 6]
            eventos.append((off, "raro", "SOH sin la forma SOH <A|B|C> ETX; "
                                         "siguen: %s" % hexd(sig)))
            cuenta["SOH suelto"] += 1
            pos += 1
            continue

        # ---- ESC ------------------------------------------------------
        if b == 0x1B:
            if pos + 1 >= n:
                eventos.append((off, "raro", "ESC al final del archivo"))
                pos += 1
                continue
            c = chr(datos[pos + 1])
            info = ESC_CMD.get(c)
            if info is None:
                eventos.append((off, "esc", "ESC %s  -- NO reconocido por el "
                                            "nucleo (cae en el default)" % c))
                cuenta["ESC %s (no reconocido)" % c] += 1
                pos += 2
                continue
            desc, nargs = info
            if nargs == "I":
                #  ESC I lleva cuatro argumentos SOLO en modo protegido: las
                #  esquinas del bloque a borrar. Fuera de protegido no lleva
                #  ninguno. Se sigue el modo como lo hace Guardian, si no los
                #  cuatro bytes de coordenadas se leen como texto -- que es lo
                #  que pasaba con TEDIT, donde salia " * <" en el volcado.
                nargs = 4 if protegido else 0
            if nargs is None:
                sig = datos[pos + 2:pos + 10]
                eventos.append((off, "esc", "ESC %s  %s  [formato variable, "
                                            "siguen: %s]" % (c, desc, hexd(sig))))
                cuenta["ESC " + c] += 1
                pos += 2
                continue
            args = datos[pos + 2:pos + 2 + nargs]
            extra = ("  args %s" % hexd(args)) if nargs else ""
            if c in ("6", "7") and args:
                extra += "   -> %s" % atributos_video(args[0])
            if c == "I" and len(args) == 4:
                extra += ("   -> bloque (%d,%d) a (%d,%d)"
                          % (coord(args[0]), coord(args[1]),
                             coord(args[2]), coord(args[3])))
            eventos.append((off, "esc", "ESC %s  %s%s" % (c, desc, extra)))
            cuenta["ESC " + c] += 1
            if   c == 'W': protegido = True
            elif c == 'X': protegido = False
            pos += 2 + nargs
            continue

        # ---- controles de un byte, con o sin argumentos ----------------
        if b in CONTROL:
            nombre, nargs = CONTROL[b]
            if nargs == 2 and pos + 2 < n:
                a, c2 = datos[pos + 1], datos[pos + 2]
                if b == 0x1D:
                    detalle = ("video=%d dato=%d  (%s)"
                               % (coord(a), coord(c2),
                                  "NO PROTEGIDO" if (coord(c2) & 0x20) else "protegido"))
                else:
                    detalle = "fila %d, columna %d" % (coord(a), coord(c2))
                eventos.append((off, "ctrl", "%-38s %s" % (nombre, detalle)))
                cuenta[nombre.split(" --")[0]] += 1
                pos += 3
                continue
            eventos.append((off, "ctrl", nombre))
            cuenta[nombre.split(" --")[0]] += 1
            pos += 1
            continue

        eventos.append((off, "raro", "byte de control sin manejar: %02X" % b))
        cuenta["byte sin manejar"] += 1
        pos += 1

    volcar_texto()
    return eventos, cuenta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("archivo")
    ap.add_argument("--resumen", action="store_true",
                    help="solo el conteo, sin el listado")
    ap.add_argument("--sin-texto", action="store_true",
                    help="omitir los tramos de texto del listado")
    ap.add_argument("--desde", type=int, default=0, help="offset inicial")
    ap.add_argument("--hasta", type=int, default=0, help="offset final")
    ap.add_argument("--sentido", choices=["auto", "host", "term"], default="auto",
                    help="host->terminal o terminal->host; por defecto se "
                         "deduce del nombre del archivo")
    cfg = ap.parse_args()

    datos = open(cfg.archivo, "rb").read()

    sentido = cfg.sentido
    if sentido == "auto":
        sentido = "term" if ".term." in cfg.archivo else "host"

    if sentido == "term":
        eventos, cuenta = inspeccionar_term(datos)
    else:
        eventos, cuenta = inspeccionar(datos)

    print("archivo ....... %s" % cfg.archivo)
    print("tamano ........ %d bytes" % len(datos))
    print("sentido ....... %s" % ("terminal -> host" if sentido == "term"
                                  else "host -> terminal"))
    print()

    if not cfg.resumen:
        for off, clase, texto in eventos:
            if cfg.sin_texto and clase == "texto":
                continue
            if cfg.desde and off < cfg.desde:
                continue
            if cfg.hasta and off > cfg.hasta:
                continue
            print("  %6d  %s" % (off, texto))
        print()

    print("resumen:")
    for nombre, veces in sorted(cuenta.items(), key=lambda kv: (-kv[1], kv[0])):
        print("  %-46s %5d" % (nombre, veces))

    print()
    if sentido == "term":
        aids = cuenta.get("secuencia AID", 0)
        if aids:
            print("  -> %d secuencia(s) AID: el terminal mando la pantalla al "
                  "host." % aids)
        else:
            print("  -> sin secuencias AID: no se mando ninguna pantalla en "
                  "modo bloque.")
        return 0

    bloque = cuenta.get("modo BLOQUE", 0)
    protegido = cuenta.get("ESC W", 0)
    if bloque or protegido:
        print("  -> la sesion SI entra en modo bloque "
              "(SOH B ETX: %d, ESC W: %d)" % (bloque, protegido))
    else:
        print("  -> la sesion NO entra en modo bloque: sin SOH B ETX y sin ESC W.")
        print("     Sirve para el camino conversacional, no para el de campos.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        # Salida cortada por head/less: no es un error.
        try:
            sys.stdout.close()
        except Exception:
            pass
        sys.exit(0)
