#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tap6530 -- proxy de captura para sesiones 6530 / TN6530-8.

Se interpone entre un emulador 6530 que ya funciona y el host NonStop, deja
pasar todo sin tocarlo y graba la sesion en cuatro archivos por conexion:

    <prefijo>-NNN.host.raw       bytes crudos host -> terminal
    <prefijo>-NNN.term.raw       bytes crudos terminal -> host
    <prefijo>-NNN.host.payload   host -> terminal ya sin telnet
    <prefijo>-NNN.term.payload   terminal -> host ya sin telnet
    <prefijo>-NNN.log            negociacion telnet anotada

Los .raw sirven para la fase 01 (escribir la negociacion telnet contra una
referencia real). Los .payload son exactamente lo que entra y sale del nucleo,
o sea lo que consume vt6530_replay y lo que alimenta el banco de pruebas.

Uso tipico:

    python3 tap6530.py --escucha 127.0.0.1:2323 --host nonstop.interno:1016

y despues se apunta el emulador a 127.0.0.1:2323 en vez de al host.

El puerto por defecto del codigo original es 1016, pero TELSERV en muchos
sitios escucha en el 23: conviene confirmar cual usa el emulador antes.

Sin dependencias externas. Python 3.7 o posterior.
"""

import argparse
import datetime
import os
import socket
import socketserver
import sys
import threading

# ---------------------------------------------------------------------------
#  Vocabulario de telnet
# ---------------------------------------------------------------------------

IAC = 255

COMANDOS = {
    236: "EOF", 237: "SUSP", 238: "ABORT", 239: "EOR",
    240: "SE",  241: "NOP",  242: "DM",    243: "BRK",
    244: "IP",  245: "AO",   246: "AYT",   247: "EC",
    248: "EL",  249: "GA",   250: "SB",    251: "WILL",
    252: "WONT", 253: "DO",  254: "DONT",  255: "IAC",
}

OPCIONES = {
    0: "BINARY", 1: "ECHO", 3: "SGA", 5: "STATUS", 6: "TIMING-MARK",
    19: "END-OF-RECORD", 24: "TERMINAL-TYPE", 31: "NAWS",
    32: "TERMINAL-SPEED", 33: "TOGGLE-FLOW-CONTROL", 34: "LINEMODE",
    36: "ENVIRON", 39: "NEW-ENVIRON",
}

SB = 250
SE = 240
EOR = 239
NEGOCIACION = (251, 252, 253, 254)   # WILL WONT DO DONT

LINEMODE = 34

# RFC 1184: subcomandos de LINEMODE y bits del modo.
LM_SUB = {1: "MODE", 2: "FORWARDMASK", 3: "SLC"}
LM_BITS = [(0x01, "EDIT"), (0x02, "TRAPSIG"), (0x04, "MODE_ACK"),
           (0x08, "SOFT_TAB"), (0x10, "LIT_ECHO")]

SLC_FUNC = {
    1: "SYNCH", 2: "BRK", 3: "IP", 4: "AO", 5: "AYT", 6: "EOR", 7: "ABORT",
    8: "EOF", 9: "SUSP", 10: "EC", 11: "EL", 12: "EW", 13: "RP", 14: "LNEXT",
    15: "XON", 16: "XOFF", 17: "FORW1", 18: "FORW2", 19: "MCL", 20: "MCR",
    21: "MCWL", 22: "MCWR", 23: "MCBOL", 24: "MCEOL", 25: "INSRT", 26: "OVER",
    27: "ECR", 28: "EWR", 29: "EBOL", 30: "EEOL",
}
SLC_NIVEL = {0: "NOSUPPORT", 1: "CANTCHANGE", 2: "VALUE", 3: "DEFAULT"}


def nombre_comando(b):
    return COMANDOS.get(b, "CMD-%d" % b)


def nombre_opcion(b):
    return OPCIONES.get(b, "OPT-%d" % b)


def legible(datos):
    """Bytes en forma inspeccionable: imprimibles tal cual, resto en hex."""
    out = []
    for b in datos:
        if 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append("<%02X>" % b)
    return "".join(out)


def hexdump(datos):
    """Hex puro. Para cuerpos binarios, donde mezclar ASCII confunde: en un
    cuerpo SLC el 0x2C se ve como ',' y parece texto cuando es un valor."""
    return " ".join("%02X" % b for b in datos)


def describe_slc(cuerpo):
    """Decodifica las ternas (funcion, banderas, valor) de un cuerpo SLC."""
    if len(cuerpo) % 3 != 0:
        return None
    partes = []
    for i in range(0, len(cuerpo), 3):
        func, flags, valor = cuerpo[i], cuerpo[i + 1], cuerpo[i + 2]
        nombre = SLC_FUNC.get(func, "func-%d" % func)
        nivel = SLC_NIVEL.get(flags & 0x03, "?")
        extra = []
        if flags & 0x80: extra.append("ACK")
        if flags & 0x40: extra.append("FLUSHIN")
        if flags & 0x20: extra.append("FLUSHOUT")
        marcas = ("+" + "+".join(extra)) if extra else ""
        partes.append("%s=%02X %s%s" % (nombre, valor, nivel, marcas))
    return "; ".join(partes)


def describe_linemode(cuerpo):
    """Decodifica un cuerpo de LINEMODE, o devuelve None si no se reconoce.

    Nunca inventa: si el subcomando no es MODE, FORWARDMASK o SLC, o si las
    ternas de SLC no cuadran, devuelve None y el log cae a hex crudo. Vale
    mas un volcado honesto que un decodificado inventado.
    """
    if not cuerpo:
        return None
    sub = cuerpo[0]
    resto = cuerpo[1:]

    if sub == 1 and len(resto) == 1:                     # MODE
        mascara = resto[0]
        activos = [n for bit, n in LM_BITS if mascara & bit]
        sobra = mascara & ~0x1F
        texto = "MODE %02X" % mascara
        if activos: texto += " (" + "|".join(activos) + ")"
        if sobra:   texto += " +bits desconocidos %02X" % sobra
        return texto

    if sub == 3:                                          # SLC
        d = describe_slc(resto)
        if d is not None:
            return "SLC  " + d
        return None

    if sub == 2:
        return "FORWARDMASK  " + hexdump(resto)

    return None


# ---------------------------------------------------------------------------
#  Separador de telnet
# ---------------------------------------------------------------------------

class FiltroTelnet(object):
    """Separa el flujo de telnet en payload de aplicacion y eventos de control.

    Es una maquina de estados porque los datos llegan partidos por TCP: una
    secuencia IAC puede quedar cortada entre dos lecturas. Justamente el tipo
    de reparto que dispara el defecto de ESC r en el nucleo.
    """

    ESPERA_DATOS = 0
    ESPERA_COMANDO = 1
    ESPERA_OPCION = 2
    DENTRO_SB = 3
    SB_VIO_IAC = 4

    def __init__(self, etiqueta, anotar, registrar=None):
        self.etiqueta = etiqueta
        self.anotar = anotar
        self.registrar = registrar or (lambda *a: None)
        self.estado = self.ESPERA_DATOS
        self.comando = None
        self.sb = bytearray()
        self.offset_payload = 0

    def procesar(self, datos):
        """Devuelve el payload de aplicacion contenido en 'datos'."""
        salida = bytearray()

        for b in datos:
            if self.estado == self.ESPERA_DATOS:
                if b == IAC:
                    self.estado = self.ESPERA_COMANDO
                else:
                    salida.append(b)

            elif self.estado == self.ESPERA_COMANDO:
                if b == IAC:
                    # IAC IAC -> un 0xFF literal del payload
                    salida.append(IAC)
                    self.estado = self.ESPERA_DATOS
                elif b in NEGOCIACION:
                    self.comando = b
                    self.estado = self.ESPERA_OPCION
                elif b == SB:
                    self.sb = bytearray()
                    self.estado = self.DENTRO_SB
                else:
                    # Comando de dos bytes: EOR, NOP, DM, GA...
                    marca = self.offset_payload + len(salida)
                    self.anotar("%s  IAC %s   (offset payload %d)"
                                % (self.etiqueta, nombre_comando(b), marca))
                    self.registrar("comando", self.etiqueta, b, None)
                    self.estado = self.ESPERA_DATOS

            elif self.estado == self.ESPERA_OPCION:
                self.anotar("%s  IAC %s %s"
                            % (self.etiqueta,
                               nombre_comando(self.comando),
                               nombre_opcion(b)))
                self.registrar("opcion", self.etiqueta, self.comando, b)
                self.estado = self.ESPERA_DATOS

            elif self.estado == self.DENTRO_SB:
                if b == IAC:
                    self.estado = self.SB_VIO_IAC
                else:
                    self.sb.append(b)

            elif self.estado == self.SB_VIO_IAC:
                if b == IAC:
                    self.sb.append(IAC)
                    self.estado = self.DENTRO_SB
                elif b == SE:
                    self._anotar_sb()
                    self.estado = self.ESPERA_DATOS
                else:
                    # IAC dentro de SB seguido de algo que no es SE ni IAC:
                    # el emisor no esta respetando el encuadre. Se registra.
                    self.anotar("%s  SB mal terminada: IAC %s"
                                % (self.etiqueta, nombre_comando(b)))
                    self.estado = self.DENTRO_SB

        self.offset_payload += len(salida)
        return bytes(salida)

    def _anotar_sb(self):
        if not self.sb:
            self.anotar("%s  IAC SB (vacia) IAC SE" % self.etiqueta)
            return
        opcion = self.sb[0]
        cuerpo = self.sb[1:]
        detalle = ""
        if opcion == 24 and cuerpo:                      # TERMINAL-TYPE
            accion = "IS" if cuerpo[0] == 0 else "SEND" if cuerpo[0] == 1 else "?"
            detalle = "  %s %s" % (accion, legible(cuerpo[1:]))
            if cuerpo[0] == 0:
                self.registrar("ttype", self.etiqueta, None,
                               bytes(cuerpo[1:]).decode("ascii", "replace"))
        elif opcion == 31 and len(cuerpo) >= 4:          # NAWS
            ancho = (cuerpo[0] << 8) | cuerpo[1]
            alto = (cuerpo[2] << 8) | cuerpo[3]
            detalle = "  %dx%d" % (ancho, alto)
        elif opcion == LINEMODE:
            d = describe_linemode(cuerpo)
            detalle = ("  " + d) if d else ("  sin decodificar: " + hexdump(cuerpo))
        else:
            # Hex, no ASCII: en un cuerpo binario los bytes imprimibles
            # sueltos hacen parecer texto lo que son valores.
            detalle = "  %s" % hexdump(cuerpo)
        self.anotar("%s  IAC SB %s%s IAC SE"
                    % (self.etiqueta, nombre_opcion(opcion), detalle))


# ---------------------------------------------------------------------------
#  Grabacion
# ---------------------------------------------------------------------------

class Sesion(object):
    def __init__(self, directorio, prefijo, numero):
        # Ruta absoluta: el proxy se corre desde cualquier directorio y una
        # ruta relativa deja al usuario buscando los archivos donde no estan.
        base = os.path.abspath(
            os.path.join(directorio, "%s-%03d" % (prefijo, numero)))
        self.base = base
        self.f_host_raw = open(base + ".host.raw", "wb")
        self.f_term_raw = open(base + ".term.raw", "wb")
        self.f_host_pay = open(base + ".host.payload", "wb")
        self.f_term_pay = open(base + ".term.payload", "wb")
        self.f_log = open(base + ".log", "w", encoding="utf-8")
        self.candado = threading.Lock()
        self.bytes_host = 0
        self.bytes_term = 0

        # Material para el resumen de la fase 01.
        self.dichos = {"host->term": [], "term->host": []}
        self.comandos = {}
        self.ttype = None
        self.cambios_modo = []
        self._sm_modo = 0
        self._sm_letra = None
        self._sm_offset = 0
        self._pos_host = 0

    def registrar(self, clase, etiqueta, a, b):
        with self.candado:
            if clase == "opcion":
                self.dichos.setdefault(etiqueta, []).append(
                    (nombre_comando(a), nombre_opcion(b)))
            elif clase == "comando":
                nombre = nombre_comando(a)
                self.comandos[nombre] = self.comandos.get(nombre, 0) + 1
            elif clase == "ttype" and b:
                self.ttype = b

    def _buscar_cambios_de_modo(self, payload):
        """Detecta SOH <A|B|C> ETX en el payload del host.

        Es como el 6530 cambia de modo en banda -- ANSI, Bloque o
        Conversacional -- sin que telnet se entere. Guardian lo atiende en el
        estado 5000, y saber si aparece decide si una captura sirve para
        probar el modo bloque.
        """
        for b in payload:
            if self._sm_modo == 0:
                if b == 0x01:
                    self._sm_modo = 1
                    self._sm_offset = self._pos_host
            elif self._sm_modo == 1:
                if b in (0x41, 0x42, 0x43):      # A, B, C
                    self._sm_letra = chr(b)
                    self._sm_modo = 2
                else:
                    self._sm_modo = 1 if b == 0x01 else 0
                    if b == 0x01:
                        self._sm_offset = self._pos_host
            elif self._sm_modo == 2:
                if b == 0x03:
                    self.cambios_modo.append((self._sm_offset, self._sm_letra))
                self._sm_modo = 0
            self._pos_host += 1

    def anotar(self, texto):
        marca = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        with self.candado:
            self.f_log.write("%s  %s\n" % (marca, texto))
            self.f_log.flush()
        print("  %s" % texto, file=sys.stderr)

    def escribir(self, sentido, crudo, payload):
        with self.candado:
            if sentido == "host":
                self.f_host_raw.write(crudo)
                self.f_host_pay.write(payload)
                self.f_host_raw.flush()
                self.f_host_pay.flush()
                self.bytes_host += len(crudo)
                self._buscar_cambios_de_modo(payload)
            else:
                self.f_term_raw.write(crudo)
                self.f_term_pay.write(payload)
                self.f_term_raw.flush()
                self.f_term_pay.flush()
                self.bytes_term += len(crudo)

    NOMBRE_MODO = {"A": "ANSI", "B": "bloque", "C": "conversacional"}

    def resumen(self):
        """Lo que la fase 01 necesita saber de esta sesion, en un bloque."""
        def dijo(etiqueta, comando, opcion):
            return (comando, opcion) in self.dichos.get(etiqueta, [])

        def acordada(opcion):
            """Una opcion queda activa si un lado la ofrece (WILL) y el otro
            la acepta (DO), en cualquiera de los dos sentidos."""
            return ((dijo("host->term", "WILL", opcion) and
                     dijo("term->host", "DO", opcion)) or
                    (dijo("term->host", "WILL", opcion) and
                     dijo("host->term", "DO", opcion)))

        L = []
        L.append("")
        L.append("=== resumen para la fase 01 ===")

        for etiqueta, quien in (("host->term", "el host  "),
                                ("term->host", "el termin")):
            pares = self.dichos.get(etiqueta, [])
            texto = ", ".join("%s %s" % (c, o) for c, o in pares) or "(nada)"
            L.append("  %s dijo: %s" % (quien, texto))

        L.append("  terminal-type declarado: %s" % (self.ttype or "(ninguno)"))

        for nombre, opcion in (("BINARY", "BINARY"),
                               ("END-OF-RECORD", "END-OF-RECORD"),
                               ("SGA", "SGA"),
                               ("ECHO", "ECHO"),
                               ("TERMINAL-TYPE", "TERMINAL-TYPE"),
                               ("NAWS", "NAWS"),
                               ("LINEMODE", "LINEMODE")):
            L.append("  %-16s acordada: %s"
                     % (nombre, "si" if acordada(opcion) else "no"))

        eor = self.comandos.get("EOR", 0)
        L.append("  marcadores IAC EOR: %d" % eor)

        if self.cambios_modo:
            partes = ["%s en offset %d" % (self.NOMBRE_MODO.get(l, l), off)
                      for off, l in self.cambios_modo]
            L.append("  cambios de modo en banda: %s" % "; ".join(partes))
        else:
            L.append("  cambios de modo en banda: ninguno "
                     "(la sesion no entro en modo bloque)")

        L.append("===============================")
        return "\n".join(L)

    def cerrar(self):
        self.anotar("sesion cerrada -- host %d bytes, terminal %d bytes"
                    % (self.bytes_host, self.bytes_term))
        texto = self.resumen()
        with self.candado:
            self.f_log.write(texto + "\n")
            self.f_log.flush()
        print(texto, file=sys.stderr)
        for f in (self.f_host_raw, self.f_term_raw,
                  self.f_host_pay, self.f_term_pay, self.f_log):
            try:
                f.close()
            except Exception:
                pass


# ---------------------------------------------------------------------------
#  Proxy
# ---------------------------------------------------------------------------

_contador = [0]
_candado_contador = threading.Lock()


def siguiente_numero():
    with _candado_contador:
        _contador[0] += 1
        return _contador[0]


def relevar(origen, destino, sesion, sentido, filtro, terminar):
    """Copia de un socket al otro grabando por el camino."""
    try:
        while True:
            datos = origen.recv(65536)
            if not datos:
                break
            payload = filtro.procesar(datos)
            sesion.escribir(sentido, datos, payload)
            destino.sendall(datos)
    except OSError:
        pass
    finally:
        terminar.set()
        for s in (origen, destino):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


class Manejador(socketserver.BaseRequestHandler):
    def handle(self):
        cfg = self.server.cfg
        numero = siguiente_numero()
        sesion = Sesion(cfg.salida, cfg.prefijo, numero)

        print("\n[sesion %03d] %s -> %s:%d   archivos: %s.*"
              % (numero, self.client_address[0], cfg.host_destino,
                 cfg.puerto_destino, sesion.base), file=sys.stderr)
        sesion.anotar("conexion desde %s hacia %s:%d"
                      % (self.client_address[0], cfg.host_destino,
                         cfg.puerto_destino))

        try:
            arriba = socket.create_connection(
                (cfg.host_destino, cfg.puerto_destino), timeout=cfg.espera)
            arriba.settimeout(None)
        except OSError as e:
            sesion.anotar("no se pudo conectar al host: %s" % e)
            sesion.cerrar()
            return

        self.request.settimeout(None)
        terminar = threading.Event()

        f_host = FiltroTelnet("host->term", sesion.anotar, sesion.registrar)
        f_term = FiltroTelnet("term->host", sesion.anotar, sesion.registrar)

        h1 = threading.Thread(target=relevar, daemon=True,
                              args=(arriba, self.request, sesion, "host",
                                    f_host, terminar))
        h2 = threading.Thread(target=relevar, daemon=True,
                              args=(self.request, arriba, sesion, "term",
                                    f_term, terminar))
        h1.start()
        h2.start()
        terminar.wait()
        h1.join(timeout=2)
        h2.join(timeout=2)

        try:
            arriba.close()
        except OSError:
            pass
        sesion.cerrar()


class Servidor(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def separar_destino(texto, puerto_por_defecto):
    if ":" in texto:
        host, _, puerto = texto.rpartition(":")
        return host, int(puerto)
    return texto, puerto_por_defecto


def main():
    ap = argparse.ArgumentParser(
        description="Proxy de captura para sesiones 6530 / TN6530-8.")
    ap.add_argument("--escucha", default="127.0.0.1:2323",
                    help="donde escuchar, host:puerto (por defecto 127.0.0.1:2323)")
    ap.add_argument("--host", required=True,
                    help="host NonStop de destino, host:puerto "
                         "(puerto por defecto 1016; TELSERV suele usar 23)")
    ap.add_argument("--salida", default="capturas",
                    help="directorio donde dejar los archivos (por defecto capturas)")
    ap.add_argument("--prefijo", default="sesion",
                    help="prefijo de los nombres de archivo (por defecto sesion)")
    ap.add_argument("--espera", type=float, default=10.0,
                    help="segundos de espera al conectar con el host")
    cfg = ap.parse_args()

    cfg.host_escucha, cfg.puerto_escucha = separar_destino(cfg.escucha, 2323)
    cfg.host_destino, cfg.puerto_destino = separar_destino(cfg.host, 1016)

    cfg.salida = os.path.abspath(cfg.salida)
    os.makedirs(cfg.salida, exist_ok=True)

    servidor = Servidor((cfg.host_escucha, cfg.puerto_escucha), Manejador)
    servidor.cfg = cfg

    print("tap6530 escuchando en %s:%d, reenviando a %s:%d"
          % (cfg.host_escucha, cfg.puerto_escucha,
             cfg.host_destino, cfg.puerto_destino), file=sys.stderr)
    print("los archivos van a %s" % cfg.salida, file=sys.stderr)
    print("apunta el emulador 6530 a %s:%d y trabaja normalmente."
          % (cfg.host_escucha, cfg.puerto_escucha), file=sys.stderr)
    print("Ctrl-C para terminar. Los archivos se escriben a medida que pasa "
          "el trafico.\n", file=sys.stderr)

    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        print("\nterminando.", file=sys.stderr)
    finally:
        servidor.shutdown()
        servidor.server_close()


if __name__ == "__main__":
    main()
