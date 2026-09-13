#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
tap6530ssh -- proxy de captura para sesiones 6530 sobre SSH.

Hermano de tap6530.py, que hace lo mismo con telnet. Se pone en el medio entre
un emulador que YA funciona -- OutsideView, por ejemplo -- y el host NonStop,
descifra los dos lados, deja pasar todo sin tocarlo y graba la sesion.

    <prefijo>-NNN.host.raw       todo lo que paso por el canal, host -> term
    <prefijo>-NNN.term.raw       todo lo que paso por el canal, term -> host
    <prefijo>-NNN.host.payload   host -> terminal, ya sin telnet
    <prefijo>-NNN.term.payload   terminal -> host, ya sin telnet
    <prefijo>-NNN.log            el saludo, y sobre todo las PETICIONES

Los .payload son exactamente lo que entra y sale del nucleo, o sea lo que
consumen tools/inspeccionar.py y vt6530_replay: una sesion de VIEWSYS
capturada asi se puede reproducir y meter en el banco de pruebas igual que
las de telnet.

POR QUE EXISTE

Con telnet alcanzaba con mirar el cable. Con SSH no: va cifrado, y lo que hace
falta saber no son los bytes de la aplicacion sino las peticiones del canal,
que viajan adentro del tunel. La pregunta concreta es una:

    como le dice un emulador que funciona, a STN, que es un terminal 6530

Porque el TERM del pty-req no alcanza. Con "tn6530-8" STN asigna la ventana y
no engancha ninguna aplicacion; con "xterm" arranca TACL pero la ventana queda
declarada como ANSI. Un emulador comercial hace las dos cosas a la vez, y este
proxy muestra COMO, porque anota:

    - pty-req: el TERM, el tamano, y los MODOS de terminal en crudo
    - env:     las variables que pide antes de arrancar (TERM puede ir aca)
    - exec / shell / subsystem: que le pide correr
    - window-change y cualquier otra peticion del canal

Esas cinco lineas del .log son todo el objetivo del programa. El payload es
un premio: sirve para vt6530_replay y para el banco de pruebas, igual que las
capturas de telnet.

CUIDADO -- ESTO VE LA CLAVE

El proxy termina la sesion SSH del emulador y abre otra contra el host, asi
que por definicion ve en claro lo que el emulador manda, la clave incluida, y
la necesita para poder autenticarse arriba. Es un hombre en el medio hecho a
proposito contra un host propio, que es la unica forma de ver esto.

De ahi que:

    - el .term.payload va a tener TODO lo tecleado, clave incluida. Es el
      mismo aviso que capturas/README.md da para las capturas de telnet.
    - conviene usar una clave de descarte y cambiarla despues.
    - no dejar estos archivos en un repositorio ni mandarlos adjuntos.

Con --sin-texto se graban los .payload del lado del terminal con los bytes
imprimibles reemplazados por puntos: se pierde el texto y se conserva la
estructura, que para mirar un pty-req alcanza y sobra.

LA CLAVE DEL HOST

Hay DOS rechazos distintos y conviene no confundirlos.

1. "no acceptable host key", en el saludo. No es que el emulador desconfie de
   nuestra clave: es que no entiende NINGUNO de los tipos que le ofrecemos.
   Un emulador de esta epoca suele querer ssh-dss, y paramiko con una clave
   RSA ofrece rsa-sha2-512, rsa-sha2-256 y ssh-rsa. Por eso el proxy genera
   ahora una clave de cada tipo que sepa hacer -- rsa, ecdsa, dss -- y las
   ofrece todas. Si aun asi falla, el registro de paramiko (--registro) trae
   la lista que el cliente pide, y ahi se ve que le falta.

2. El emulador acepta el tipo pero desconfia de la clave, porque no es la que
   tenia guardada del host real. Eso es inevitable -- es lo que hace un proxy
   -- y se resuelve apuntando el emulador a 127.0.0.1 en otro puerto: para el
   es un host nuevo y no hay nada guardado que contradecir. Borrar la clave
   guardada de rci3 tambien anda, pero despues queda la del proxy anotada
   bajo el nombre del host real, que muerde la proxima vez.

Las claves del proxy se guardan en disco para que no cambien entre corridas.

REQUISITOS

    pip install paramiko

USO

    python3 tap6530ssh.py --escucha 127.0.0.1:2222 --host rci3:22

y despues se apunta el emulador a 127.0.0.1 puerto 2222.
"""

import argparse
import datetime
import os
import socket
import sys
import threading
import time
import traceback

#  El filtro de telnet del proxy de telnet, tal cual. Adentro del canal ssh
#  viaja telnet cuando el TERM es 6530 -- STN abre con IAC WILL ECHO --, asi
#  que sin sacarlo los .payload no los puede leer ni inspeccionar.py ni
#  vt6530_replay. Reusar el que ya esta probado evita tener dos maquinas de
#  estados de telnet que se vayan separando con el tiempo.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
try:
    from tap6530 import FiltroTelnet
except Exception as _e:
    FiltroTelnet = None
    sys.stderr.write("sin tap6530.py al lado: los .payload van a salir con "
                     "telnet adentro (%s)\n" % _e)

try:
    import paramiko
    import paramiko.common          # para cMSG_CHANNEL_REQUEST
except ImportError:
    sys.stderr.write(
        "hace falta paramiko:  pip install paramiko\n"
        "(en Fedora/RHEL tambien esta como python3-paramiko)\n")
    raise SystemExit(2)


# ---------------------------------------------------------------------------
#  Modos de terminal del pty-req (RFC 4254 seccion 8)
# ---------------------------------------------------------------------------
#
#  El campo "modes" del pty-req es una lista de pares (codigo, valor de 32
#  bits) terminada por un codigo 0. Decodificarlo es la mitad del valor de
#  este proxy: ahi se ve si el emulador pide el terminal en crudo, si deja el
#  eco prendido, y que hace con el ETX.

NOMBRE_MODO = {
    1: "VINTR", 2: "VQUIT", 3: "VERASE", 4: "VKILL", 5: "VEOF",
    6: "VEOL", 7: "VEOL2", 8: "VSTART", 9: "VSTOP", 10: "VSUSP",
    11: "VDSUSP", 12: "VREPRINT", 13: "VWERASE", 14: "VLNEXT",
    15: "VFLUSH", 16: "VSWTCH", 17: "VSTATUS", 18: "VDISCARD",
    30: "IGNPAR", 31: "PARMRK", 32: "INPCK", 33: "ISTRIP", 34: "INLCR",
    35: "IGNCR", 36: "ICRNL", 37: "IUCLC", 38: "IXON", 39: "IXANY",
    40: "IXOFF", 41: "IMAXBEL", 42: "IUTF8",
    50: "ISIG", 51: "ICANON", 52: "XCASE", 53: "ECHO", 54: "ECHOE",
    55: "ECHOK", 56: "ECHONL", 57: "NOFLSH", 58: "TOSTOP", 59: "IEXTEN",
    60: "ECHOCTL", 61: "ECHOKE", 62: "PENDIN",
    70: "OPOST", 71: "OLCUC", 72: "ONLCR", 73: "OCRNL", 74: "ONOCR",
    75: "ONLRET",
    90: "CS7", 91: "CS8", 92: "PARENB", 93: "PARODD",
    128: "TTY_OP_ISPEED", 129: "TTY_OP_OSPEED",
}


def describe_modos(crudo):
    """Devuelve una lista de lineas legibles con los modos del pty-req."""
    if not crudo:
        return ["    (vacio: el cliente no pidio ningun modo)"]

    lineas = []
    i = 0
    datos = bytes(crudo)
    while i < len(datos):
        codigo = datos[i]
        i += 1
        if codigo == 0:
            break
        if i + 4 > len(datos):
            lineas.append("    (truncado en el codigo %d)" % codigo)
            break
        valor = int.from_bytes(datos[i:i + 4], "big")
        i += 4
        nombre = NOMBRE_MODO.get(codigo, "desconocido-%d" % codigo)
        lineas.append("    %-16s = %d" % (nombre, valor))
    if not lineas:
        lineas.append("    (lista vacia)")
    return lineas


def legible(datos):
    """Imprimible tal cual, el resto en <hexa>. Igual que en tap6530."""
    out = []
    for b in datos:
        if 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            out.append("<%02X>" % b)
    return "".join(out)


# ---------------------------------------------------------------------------
#  La sesion: archivos y anotaciones
# ---------------------------------------------------------------------------

class Sesion(object):

    def __init__(self, directorio, prefijo, numero, sin_texto):
        os.makedirs(directorio, exist_ok=True)
        base = os.path.join(directorio, "%s-%03d" % (prefijo, numero))
        self.base = base
        self.sin_texto = sin_texto
        #  Reentrante a proposito. El filtro de telnet avisa de cada IAC
        #  llamando a anotar(), y anotar() toma este mismo candado: con un
        #  Lock comun eso es un abrazo mortal en el primer byte de
        #  negociacion. Igual el filtrado se hace FUERA del candado, asi que
        #  esto es cinturon ademas de tirantes.
        self.lock = threading.RLock()
        #  .raw es todo lo que paso por el canal; .payload es lo mismo sin
        #  telnet, que es lo que consumen inspeccionar.py y vt6530_replay.
        self.f_host_raw = open(base + ".host.raw", "wb")
        self.f_term_raw = open(base + ".term.raw", "wb")
        self.f_host = open(base + ".host.payload", "wb")
        self.f_term = open(base + ".term.payload", "wb")
        self.f_log = open(base + ".log", "w", encoding="utf-8")

        if FiltroTelnet is not None:
            self.filtro_host = FiltroTelnet("host", self.anotar)
            self.filtro_term = FiltroTelnet("term", self.anotar)
        else:
            self.filtro_host = self.filtro_term = None
        self.t0 = time.time()
        self.anotar("sesion abierta %s" %
                    datetime.datetime.now().isoformat(timespec="seconds"))

    def anotar(self, texto):
        with self.lock:
            self.f_log.write("[%7.3f] %s\n" % (time.time() - self.t0, texto))
            self.f_log.flush()
        sys.stdout.write("%s\n" % texto)
        sys.stdout.flush()

    def escribir(self, sentido, datos):
        if not datos:
            return

        #  El filtro va FUERA del candado. Adentro llama a anotar(), que lo
        #  toma, y eso trababa el relevo entero en el primer IAC: el .raw
        #  quedaba con los 279 bytes del banner de STN y el .payload en cero,
        #  con el emulador esperando un prompt que no iba a llegar nunca.
        #
        #  No hace falta proteger al filtro: hay uno por sentido y cada uno lo
        #  usa un solo hilo.
        filtro = self.filtro_host if sentido == "host" else self.filtro_term
        payload = bytes(filtro.procesar(datos)) if filtro else datos

        with self.lock:
            crudo = self.f_host_raw if sentido == "host" else self.f_term_raw
            crudo.write(datos)
            crudo.flush()

            if sentido == "host":
                self.f_host.write(payload)
                self.f_host.flush()
            else:
                if self.sin_texto:
                    payload = bytes((b if b < 0x20 or b >= 0x7F else 0x2E)
                                    for b in payload)
                self.f_term.write(payload)
                self.f_term.flush()

    def cerrar(self):
        self.anotar("sesion cerrada")
        with self.lock:
            for f in (self.f_host, self.f_term, self.f_log,
                      self.f_host_raw, self.f_term_raw):
                try:
                    f.close()
                except Exception:
                    pass


# ---------------------------------------------------------------------------
#  El lado servidor: lo que el emulador nos pide
# ---------------------------------------------------------------------------

class Servidor(paramiko.ServerInterface):
    """
    Acepta al emulador y ANOTA todo lo que pide.

    Aceptar cualquier autenticacion es a proposito: el objetivo no es
    controlar el acceso -- esto escucha en 127.0.0.1 -- sino quedarse con la
    clave para poder abrir la conexion de arriba con ella.
    """

    def __init__(self, sesion):
        self.sesion = sesion
        self.evento_canal = threading.Event()
        self.evento_pedido = threading.Event()
        self.usuario = None
        self.clave = None
        self.term = None
        self.ancho = 80
        self.alto = 24
        self.modos = b""
        self.pty_pedido = False
        self.entorno = {}
        self.comando = None       # exec
        self.shell = False

    # --- autenticacion ---

    def get_allowed_auths(self, username):
        return "password,keyboard-interactive,publickey"

    def check_auth_password(self, username, password):
        """
        Aceptar solo si la contrasena viene con algo adentro.

        Esto no es paranoia, es lo que hacia fallar el tramo de arriba. Varios
        clientes mandan primero una peticion de contrasena VACIA para tantear
        que metodos acepta el servidor. Aceptandola, el cliente da la sesion
        por autenticada y nunca manda la de verdad: nos quedabamos con la
        cadena vacia y con eso intentabamos entrar al host, que obviamente
        decia que no.

        Rechazando la vacia, el cliente sigue adelante y manda la buena.
        """
        self.usuario = username
        if not password:
            self.sesion.anotar(
                "auth: contrasena vacia (el cliente esta tanteando metodos); "
                "se rechaza para que mande la de verdad")
            return paramiko.AUTH_FAILED

        self.clave = password
        #  El largo si, el contenido no: esto queda en un archivo.
        self.sesion.anotar("auth: contrasena de %d caracteres, usuario '%s'"
                           % (len(password), username))
        return paramiko.AUTH_SUCCESSFUL

    def check_auth_interactive(self, username, submethods):
        self.usuario = username
        self.sesion.anotar("auth: keyboard-interactive, usuario '%s'" % username)
        return paramiko.InteractiveQuery(
            "", "", ("Password: ", False))

    def check_auth_interactive_response(self, responses):
        if responses and responses[0]:
            self.clave = responses[0]
            self.sesion.anotar("auth: respuesta de %d caracteres"
                               % len(responses[0]))
            return paramiko.AUTH_SUCCESSFUL
        self.sesion.anotar("auth: respuesta vacia; se rechaza")
        return paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        """
        No se puede reenviar: no tenemos la clave privada del emulador. Se
        acepta igual y despues se pide una contrasena por consola para el
        tramo de arriba, que es mejor que cortar aca sin explicar nada.
        """
        self.usuario = username
        self.sesion.anotar(
            "auth: clave publica (%s), usuario '%s' -- no se puede reenviar, "
            "se va a pedir la contrasena por consola" % (key.get_name(), username))
        return paramiko.AUTH_SUCCESSFUL

    # --- canal ---

    def check_channel_request(self, kind, chanid):
        self.sesion.anotar("canal pedido: %s" % kind)
        if kind == "session":
            return paramiko.OPEN_SUCCEEDED
        return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    # --- y estas son LAS que importan ---

    def check_channel_pty_request(self, channel, term, width, height,
                                  pixelwidth, pixelheight, modes):
        self.term = term.decode("latin-1") if isinstance(term, bytes) else term
        self.ancho = width or 80
        self.alto = height or 24
        self.modos = modes or b""
        self.pty_pedido = True

        self.sesion.anotar("*** pty-req ***")
        self.sesion.anotar("    TERM = '%s'" % self.term)
        self.sesion.anotar("    tamano = %dx%d  (pixeles %dx%d)"
                           % (self.ancho, self.alto, pixelwidth, pixelheight))
        self.sesion.anotar("    modos (%d bytes):" % len(self.modos))
        for linea in describe_modos(self.modos):
            self.sesion.anotar(linea)
        return True

    def check_channel_env_request(self, channel, name, value):
        n = name.decode("latin-1") if isinstance(name, bytes) else name
        v = value.decode("latin-1") if isinstance(value, bytes) else value
        self.entorno[n] = v
        self.sesion.anotar("*** env *** %s = '%s'" % (n, v))
        return True

    def check_channel_shell_request(self, channel):
        self.shell = True
        self.sesion.anotar("*** shell ***")
        self.evento_pedido.set()
        return True

    def check_channel_exec_request(self, channel, command):
        c = command.decode("latin-1") if isinstance(command, bytes) else command
        self.comando = c
        self.sesion.anotar("*** exec *** '%s'" % c)
        self.evento_pedido.set()
        return True

    def check_channel_subsystem_request(self, channel, name):
        self.sesion.anotar("*** subsystem *** '%s'" % name)
        self.evento_pedido.set()
        return False

    def check_channel_window_change_request(self, channel, width, height,
                                            pixelwidth, pixelheight):
        self.ancho, self.alto = width, height
        self.sesion.anotar("window-change: %dx%d" % (width, height))
        return True


# ---------------------------------------------------------------------------
#  El relevo
# ---------------------------------------------------------------------------

def relevar(origen, destino, sesion, sentido, terminar):
    """Mueve bytes de un canal al otro y los graba."""
    try:
        while not terminar.is_set():
            if origen.recv_ready():
                datos = origen.recv(4096)
                if not datos:
                    break
                sesion.escribir(sentido, datos)
                destino.sendall(datos)
                continue
            if origen.recv_stderr_ready():
                datos = origen.recv_stderr(4096)
                if datos:
                    sesion.anotar("[stderr %s] %s" % (sentido, legible(datos)))
                continue
            if origen.exit_status_ready() and not origen.recv_ready():
                break
            time.sleep(0.01)
    except Exception as e:
        sesion.anotar("relevo %s termino: %s" % (sentido, e))
    finally:
        terminar.set()


def pedir_pty_con_modos(canal, term, ancho, alto, modos, sesion):
    """
    pty-req hacia arriba con los modos EXACTOS que mando el emulador.

    Channel.get_pty() de paramiko manda la lista de modos vacia, sin forma de
    pasarle otra. Para un proxy eso no sirve: si el host se comporta distinto
    segun los modos -- y justo estamos investigando si se comporta distinto
    segun lo que se le pide --, mandarle una lista vacia cuando el emulador
    mando 246 bytes deja de ser un relevo y pasa a ser otra cosa.

    Asi que se arma el mensaje a mano. Son las mismas seis lineas que tiene
    get_pty() adentro, con los modos de verdad. Usa internos de paramiko
    (_send_user_message y compania), asi que si un dia cambian, se cae al
    get_pty() de siempre y se avisa: una captura con los modos equivocados es
    mejor que ninguna captura.
    """
    try:
        m = paramiko.Message()
        m.add_byte(paramiko.common.cMSG_CHANNEL_REQUEST)
        m.add_int(canal.remote_chanid)
        m.add_string("pty-req")
        m.add_boolean(True)
        m.add_string(term)
        m.add_int(ancho)
        m.add_int(alto)
        m.add_int(0)
        m.add_int(0)
        m.add_string(modos or bytes())
        canal._event_pending()
        canal.transport._send_user_message(m)
        canal._wait_for_event()
        sesion.anotar("arriba: pty-req TERM='%s' %dx%d con los %d bytes de "
                      "modos del emulador" % (term, ancho, alto, len(modos or b"")))
        return
    except Exception as e:
        sesion.anotar("no se pudo mandar el pty-req con modos (%s); se cae al "
                      "get_pty() comun, que manda la lista vacia" % e)

    canal.get_pty(term=term, width=ancho, height=alto)
    sesion.anotar("arriba: pty-req TERM='%s' %dx%d (SIN los modos originales)"
                  % (term, ancho, alto))


def conectar_arriba(args, usuario, clave, sesion):
    """
    La conexion de verdad contra el host, probando los dos metodos.

    Se usa Transport en vez de SSHClient a proposito: hace falta elegir el
    metodo de autenticacion a mano. rci3 acepta "password" con nuestro cliente
    y "keyboard-interactive" con el ssh de OpenSSH -- se ve en la linea STN46
    de cada sesion --, y cual de los dos anda depende de como este configurado
    el usuario. Probar los dos sale gratis y evita un "Authentication failed"
    que no dice cual falto.
    """
    t = paramiko.Transport((args.host, args.puerto))
    t.start_client(timeout=20)

    errores = []
    for intento in range(2):
        try:
            t.auth_password(usuario, clave)
            sesion.anotar("arriba: autenticado por contrasena")
            return t
        except Exception as e:
            errores.append("contrasena: %s" % e)

        try:
            t.auth_interactive(
                usuario,
                lambda titulo, instrucciones, pedidos: [clave for _ in pedidos])
            sesion.anotar("arriba: autenticado por keyboard-interactive")
            return t
        except Exception as e:
            errores.append("keyboard-interactive: %s" % e)

        if intento == 0:
            #  Puede que la que nos paso el emulador no sea la que el host
            #  quiere. Se pide una por consola antes de darse por vencido, que
            #  es mejor que cortar la captura entera por esto.
            sesion.anotar("arriba rechazo la contrasena del emulador (%s)"
                          % "; ".join(errores))
            import getpass
            nueva = getpass.getpass(
                "clave de %s en %s (Enter para abandonar): "
                % (usuario, args.host))
            if not nueva:
                break
            clave = nueva
            errores = []

    t.close()
    raise paramiko.AuthenticationException(
        "no se pudo autenticar arriba. %s" % "; ".join(errores))


def atender(cliente, direccion, args, numero, claves_host):
    sesion = Sesion(args.salida, args.prefijo, numero, args.sin_texto)
    sesion.anotar("conexion desde %s:%d" % direccion)

    arriba = None
    try:
        transporte = paramiko.Transport(cliente)
        aflojar(transporte, claves_host)
        for k in claves_host:
            transporte.add_server_key(k)

        servidor = Servidor(sesion)
        try:
            transporte.start_server(server=servidor)
        except paramiko.SSHException as e:
            texto = str(e)
            pista = ""
            if "host key" in texto:
                pista = ("    El emulador no entiende ninguno de los tipos de\n"
                         "    clave que ofrecemos (%s).\n"
                         "    El registro de paramiko lista los que EL pide."
                         % ", ".join(k.get_name() for k in claves_host))
            elif "got 34" in texto or "got 30" in texto:
                pista = ("    Es el group exchange: paramiko como servidor solo\n"
                         "    entiende la forma vieja de esa peticion. Ya no se\n"
                         "    ofrece; si vuelve a aparecer, mirar que kex se\n"
                         "    acordo en el registro.")
            elif "cipher" in texto or "kex" in texto:
                pista = ("    Falta un algoritmo viejo que esta version de\n"
                         "    paramiko ya no trae. Con paramiko 2.x hay mas:\n"
                         "    pip install 'paramiko<3'")
            sesion.anotar("el saludo fallo: %s\n%s" % (e, pista))
            return

        canal = transporte.accept(30)
        if canal is None:
            sesion.anotar("el emulador no abrio ningun canal")
            return

        # El pty-req y el exec llegan enseguida despues del canal.
        servidor.evento_pedido.wait(10)

        # --- ahora si, la conexion de verdad ---
        clave = servidor.clave
        if clave is None:
            sesion.anotar("el emulador no mando contrasena; se pide por consola")
            import getpass
            clave = getpass.getpass(
                "clave de %s en %s: " % (servidor.usuario, args.host))

        sesion.anotar("conectando arriba a %s:%d como '%s'"
                      % (args.host, args.puerto, servidor.usuario))

        arriba = conectar_arriba(args, servidor.usuario, clave, sesion)
        canal_arriba = arriba.open_session()

        for nombre, valor in servidor.entorno.items():
            try:
                canal_arriba.set_environment_variable(nombre, valor)
                sesion.anotar("env reenviado: %s" % nombre)
            except Exception as e:
                sesion.anotar("env '%s' rechazado arriba: %s" % (nombre, e))

        if servidor.pty_pedido:
            pedir_pty_con_modos(canal_arriba, servidor.term or "vt100",
                                servidor.ancho, servidor.alto,
                                servidor.modos, sesion)

        if servidor.comando is not None:
            canal_arriba.exec_command(servidor.comando)
            sesion.anotar("arriba: exec '%s'" % servidor.comando)
        else:
            canal_arriba.invoke_shell()
            sesion.anotar("arriba: shell")

        canal.settimeout(0.0)
        canal_arriba.settimeout(0.0)

        terminar = threading.Event()
        h1 = threading.Thread(target=relevar,
                              args=(canal, canal_arriba, sesion, "term", terminar))
        h2 = threading.Thread(target=relevar,
                              args=(canal_arriba, canal, sesion, "host", terminar))
        h1.daemon = h2.daemon = True
        h1.start()
        h2.start()
        while not terminar.is_set():
            time.sleep(0.1)

    except Exception:
        sesion.anotar("error:\n" + traceback.format_exc())
    finally:
        try:
            if arriba is not None:
                arriba.close()
        except Exception:
            pass
        try:
            cliente.close()
        except Exception:
            pass
        sesion.cerrar()


# ---------------------------------------------------------------------------

#  Algoritmos viejos que paramiko sabe hacer pero no ofrece por defecto.
#
#  Un emulador comercial de terminales 6530 no es un cliente moderno. El
#  primer intento fallo con "no acceptable host key": OutsideView y paramiko
#  no compartian UN SOLO algoritmo de clave de host, porque paramiko con una
#  clave RSA ofrece rsa-sha2-512, rsa-sha2-256 y ssh-rsa, y un cliente de esa
#  epoca suele querer ssh-dss.
#
#  La respuesta no es adivinar cual quiere sino ofrecerle todos los que
#  tenemos y ampliar tambien el intercambio de claves y los cifrados, que es
#  donde se traba lo siguiente. Esto es un proxy de diagnostico contra un host
#  propio en 127.0.0.1: aflojar la criptografia aca no le abre la puerta a
#  nadie, y es la unica forma de que un cliente de 2005 se deje mirar.

CLAVES_PREFERIDAS = (
    "ssh-ed25519",
    "ecdsa-sha2-nistp256",
    "rsa-sha2-512",
    "rsa-sha2-256",
    "ssh-rsa",
    "ssh-dss",
)


def revivir_sha1():
    """
    Devolverle a paramiko la capacidad de firmar y verificar con ssh-rsa.

    Paramiko 5 saco SHA-1 de RSAKey.HASHES, con razon: ssh-rsa esta muerto y
    nadie deberia usarlo en 2026. Pero este proxy no elige con quien habla.
    De un lado hay un emulador de 2016 que ofrece ssh-rsa primero, y del otro
    un NonStop cuyo servidor SSH ofrece ssh-rsa y ssh-dss y nada mas. El nivel
    criptografico lo fijan ellos; nosotros solo miramos en el medio.

    Sin esto la unica salida es bajar a paramiko 2.x, que arrastra una
    version vieja de cryptography y suele pelearse con el resto del sistema.
    Esto son dos lineas y no toca nada instalado.

    Devuelve True si hizo falta y se pudo.
    """
    try:
        tabla = getattr(paramiko.RSAKey, "HASHES", None)
        if tabla is None or "ssh-rsa" in tabla:
            return False
        from cryptography.hazmat.primitives import hashes
        tabla["ssh-rsa"] = hashes.SHA1
        tabla.setdefault("ssh-rsa-cert-v01@openssh.com", hashes.SHA1)
        return True
    except Exception as e:
        sys.stdout.write("no se pudo revivir ssh-rsa: %s\n" % e)
        return False


def rsa_sha1_firmable():
    """
    Si esta version de paramiko puede firmar con ssh-rsa (o sea SHA-1).

    Paramiko 5 saco SHA-1 de RSAKey.HASHES. Ofrecer igual "ssh-rsa" no da un
    error de negociacion -- eso seria facil de leer -- sino que la negociacion
    SALE BIEN y despues revienta al firmar:

        Kex: diffie-hellman-group16-sha512
        HostKey: ssh-rsa
        ...
        KeyError: 'ssh-rsa'   en rsakey.py, sign_ssh_data

    Y es el caso mas probable con un cliente viejo, porque en SSH el algoritmo
    de clave de host lo elige el ORDEN DEL CLIENTE, no el nuestro: OutsideView
    2016 pone ssh-rsa primero y se lleva puesto lo unico que no podemos hacer.
    Asi que se pregunta antes y, si no se puede, no se ofrece.
    """
    try:
        return "ssh-rsa" in getattr(paramiko.RSAKey, "HASHES", {})
    except Exception:
        return False

#  SIN group-exchange, y esto no es una preferencia: es un defecto conocido
#  del lado servidor de paramiko.
#
#  El segundo intento fallo con "Expecting packet from (30,), got 34". El 30 es
#  KEXDH_INIT y el 34 es KEX_DH_GEX_REQUEST: acordaron group exchange, el
#  emulador mando la peticion en su forma moderna (RFC 4419) y el servidor de
#  paramiko solo espera la vieja. Como ninguno de los dos lados es nuestro,
#  la unica salida es no ofrecer group-exchange y quedarnos con los grupos
#  fijos, que paramiko si atiende bien como servidor.
KEX_PREFERIDOS = (
    "curve25519-sha256",
    "curve25519-sha256@libssh.org",
    "ecdh-sha2-nistp256",
    "ecdh-sha2-nistp384",
    "ecdh-sha2-nistp521",
    "diffie-hellman-group16-sha512",
    "diffie-hellman-group14-sha256",
    "diffie-hellman-group14-sha1",
    "diffie-hellman-group1-sha1",
)

CIFRADOS_PREFERIDOS = (
    "aes256-ctr", "aes192-ctr", "aes128-ctr",
    "aes256-cbc", "aes192-cbc", "aes128-cbc",
    "3des-cbc",
)


def crear_claves_de_host(base):
    """
    Todas las claves de host que paramiko sepa generar en esta instalacion.

    Se guardan para que no cambien entre corridas: una clave distinta cada vez
    obliga al emulador a volver a aceptarla, que es justo la friccion que uno
    esta tratando de sacarse de encima.

    Las que no esten disponibles se saltean y se avisa. Con que quede una que
    el cliente entienda, alcanza.
    """
    intentos = [
        ("rsa",   lambda: paramiko.RSAKey.generate(2048),
                  lambda r: paramiko.RSAKey(filename=r)),
        ("ecdsa", lambda: paramiko.ECDSAKey.generate(),
                  lambda r: paramiko.ECDSAKey(filename=r)),
        #  DSS ya no esta en paramiko 3.x. Se deja el intento porque en
        #  paramiko 2.x si existe y es justo lo que quiere un cliente viejo;
        #  si no esta, se avisa y se sigue con las demas.
        ("dss",   lambda: paramiko.DSSKey.generate(1024),
                  lambda r: paramiko.DSSKey(filename=r)),
    ]

    sin_sha1 = not rsa_sha1_firmable()
    if sin_sha1:
        sys.stdout.write(
            "esta version de paramiko no firma con ssh-rsa (le sacaron SHA-1);\n"
            "  no se va a ofrecer esa clave. Si el cliente SOLO acepta ssh-rsa,\n"
            "  la salida es  pip install 'paramiko<3'\n")

    claves = []
    for nombre, generar, leer in intentos:
        if nombre == "rsa" and sin_sha1:
            continue
        ruta = "%s_%s" % (base, nombre)
        try:
            if os.path.exists(ruta):
                claves.append(leer(ruta))
            else:
                k = generar()
                k.write_private_key_file(ruta)
                sys.stdout.write("clave de host %s creada en %s\n"
                                 % (nombre, ruta))
                claves.append(k)
        except Exception as e:
            sys.stdout.write("sin clave %s (%s); se sigue con las demas\n"
                             % (nombre, e))

    if not claves:
        raise SystemExit("no se pudo crear ninguna clave de host")
    sys.stdout.write("algoritmos de clave de host ofrecidos: %s\n"
                     % ", ".join(algoritmos_de(claves)))
    return claves


def algoritmos_de(claves_host):
    """
    Los nombres de algoritmo que podemos firmar DE VERDAD, en el orden de
    CLAVES_PREFERIDAS.

    Ofrecer un algoritmo sin tener la clave es un error silencioso y caro: en
    SSH el algoritmo de clave de host lo elige el orden del CLIENTE, asi que
    si el ofrece ssh-dss segundo y nosotros lo anunciamos sin tenerlo, se
    acuerda ssh-dss y recien se rompe despues. Por eso esta lista sale de las
    claves que efectivamente se crearon, no de una constante.

    Una clave RSA firma con tres nombres distintos -- ssh-rsa, rsa-sha2-256 y
    rsa-sha2-512 -- y cuales se pueden depende de la version de paramiko, asi
    que ese caso se mira aparte.
    """
    tengo = set()
    for k in claves_host:
        nombre = k.get_name()
        tengo.add(nombre)
        if nombre == "ssh-rsa":
            for variante in ("rsa-sha2-256", "rsa-sha2-512"):
                try:
                    if variante in getattr(paramiko.RSAKey, "HASHES", {}):
                        tengo.add(variante)
                except Exception:
                    pass
            if not rsa_sha1_firmable():
                tengo.discard("ssh-rsa")
    return tuple(a for a in CLAVES_PREFERIDAS if a in tengo)


def aflojar(transporte, claves_host):
    """Ofrecer tambien los algoritmos viejos. Ver el comentario de arriba."""
    disponibles = set()
    try:
        disponibles = set(paramiko.Transport._preferred_kex)
    except Exception:
        pass

    transporte._preferred_keys = algoritmos_de(claves_host)
    #  De kex y cifrados se dejan solo los que esta version de paramiko tiene;
    #  nombrar uno que no existe hace fallar la negociacion entera.
    kex = tuple(k for k in KEX_PREFERIDOS if not disponibles or k in disponibles)
    if kex:
        transporte._preferred_kex = kex
    try:
        cif = tuple(c for c in CIFRADOS_PREFERIDOS
                    if c in paramiko.Transport._cipher_info)
        if cif:
            transporte._preferred_ciphers = cif
    except Exception:
        pass


def separar_destino(texto, puerto_por_defecto):
    if ":" in texto:
        h, p = texto.rsplit(":", 1)
        return h, int(p)
    return texto, puerto_por_defecto


def main():
    ap = argparse.ArgumentParser(
        description="Proxy de captura para sesiones 6530 sobre SSH.")
    ap.add_argument("--escucha", default="127.0.0.1:2222",
                    help="donde escuchar (127.0.0.1:2222)")
    ap.add_argument("--host", required=True,
                    help="host NonStop real, opcionalmente con :puerto")
    ap.add_argument("--salida", default="capturas",
                    help="directorio de las capturas (capturas)")
    ap.add_argument("--prefijo", default="ssh",
                    help="prefijo de los archivos (ssh)")
    ap.add_argument("--clave-host", default="tap6530ssh_host",
                    help="base de los archivos de clave del proxy")
    ap.add_argument("--registro", default="tap6530ssh.paramiko.log",
                    help="registro detallado de paramiko; ahi se ve que "
                         "algoritmos pide el cliente. '-' lo apaga")
    ap.add_argument("--sin-sha1", action="store_true",
                    help="no rehabilitar ssh-rsa en paramiko. Con esto puesto "
                         "hace falta pip install 'paramiko<3'")
    ap.add_argument("--sin-texto", action="store_true",
                    help="grabar el lado del terminal sin los caracteres "
                         "imprimibles, para no dejar la clave en el archivo")
    args = ap.parse_args()

    args.host, args.puerto = separar_destino(args.host, 22)
    esc_host, esc_puerto = separar_destino(args.escucha, 2222)

    #  Antes de cualquier otra cosa: sin esto no se puede ni atender al
    #  emulador ni conectarse al host, porque los dos hablan ssh-rsa.
    if not args.sin_sha1:
        if revivir_sha1():
            print("ssh-rsa (SHA-1) rehabilitado en paramiko: lo piden tanto el "
                  "emulador como el host.\n  --sin-sha1 lo deja como estaba.")

    if args.registro and args.registro != "-":
        paramiko.util.log_to_file(args.registro, level="DEBUG")
        print("registro de paramiko en %s" % args.registro)

    claves_host = crear_claves_de_host(args.clave_host)

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((esc_host, esc_puerto))
    s.listen(5)

    print("escuchando en %s:%d, reenviando a %s:%d"
          % (esc_host, esc_puerto, args.host, args.puerto))
    print("apunta el emulador a %s puerto %d" % (esc_host, esc_puerto))
    if not args.sin_texto:
        print("AVISO: el .term.payload va a tener todo lo tecleado, la clave "
              "incluida. --sin-texto lo evita.")

    numero = 1
    try:
        while True:
            cliente, direccion = s.accept()
            h = threading.Thread(target=atender,
                                 args=(cliente, direccion, args, numero,
                                       claves_host))
            h.daemon = True
            h.start()
            numero += 1
    except KeyboardInterrupt:
        print("\ncortado")


if __name__ == "__main__":
    main()
