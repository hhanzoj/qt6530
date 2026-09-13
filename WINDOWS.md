# Compilar en Windows con MinGW

El proyecto ya es portable: no hay una sola llamada POSIX fuera de
`net/PosixTransport.cpp` y `apps/vt6530cli/`, y esos dos están detrás de un
`if(UNIX)` en el `CMakeLists.txt`. En Windows se compilan el núcleo, la capa
telnet, las pruebas, el reproductor de capturas y `vt6530qt`. El cliente de
consola no — usa `select()` y `unistd.h`, y en Windows su lugar lo ocupa la
aplicación Qt.

*(Verificado leyendo el código, no compilando: en este contenedor no hay
MinGW ni Qt para Windows. La auditoría cubrió cabeceras POSIX, colisiones con
macros de `windows.h` —`SendMessage`, `GetObject`, `min`/`max` y compañía—, y
el `typedef byte` frente a `std::byte`. No apareció ninguna.)*

---

## Opción A — MSYS2 (la más corta)

Todo desde un solo gestor de paquetes, sin cuenta de Qt.

1. Instalar MSYS2 desde <https://www.msys2.org>.

2. Abrir la terminal **UCRT64** (no la MSYS ni la MINGW32) e instalar:

```sh
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base \
  mingw-w64-ucrt-x86_64-qt6-tools \
  mingw-w64-ucrt-x86_64-libssh2
```

`libssh2` es para el transporte SSH y es opcional: sin ella todo compila
igual y `-ssh` avisa cómo instalarla.

3. Compilar, desde la misma terminal UCRT64:

```sh
cd /c/donde/lo/hayas/puesto/qt6530
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/vt6530qt.exe --host rci3 --puerto 23
```

Si además querés el Qt Creator de MSYS2:
`pacman -S mingw-w64-ucrt-x86_64-qt-creator`. Arranca ya con el kit UCRT64
configurado, que es la parte que suele fallar.

**Por qué UCRT64 y no MINGW64:** UCRT es el runtime de C moderno de Windows.
MINGW64 usa `msvcrt.dll`, que es de 1998 y tiene rarezas con `printf` y con
las locales. Para código nuevo, UCRT64.

---

## Opción B — instalador oficial de Qt

Si preferís el Qt Creator de The Qt Company:

1. Bajar el Qt Online Installer de <https://www.qt.io/download-qt-installer>
   (pide cuenta gratuita).

2. En el selector de componentes, dentro de la versión de Qt 6 que elijas,
   marcar **MinGW 64-bit** — es el kit de compilación. Y aparte, en
   *Developer and Designer Tools*, marcar el **MinGW compiler** propiamente
   dicho, que es un ítem separado y es el que suele quedar sin tildar. Qt 6.8
   y posteriores traen mingw-builds 13.1.0.

3. Elegir también **CMake** y **Ninja** de esa misma sección, salvo que ya los
   tengas.

4. En Qt Creator: **Archivo → Abrir archivo o proyecto…** y seleccionar el
   `CMakeLists.txt` de la raíz — no la carpeta. Después elegir el kit
   *Desktop Qt 6.x.x MinGW 64-bit*.

---

## Si Qt Creator no te lo tomó

Los tres motivos habituales, en orden de frecuencia:

**No hay ningún kit con MinGW.** Herramientas → Opciones → Kits. Si la lista
está vacía o los kits tienen un triángulo amarillo, falta el compilador o el
Qt. En la pestaña *Compiladores* tiene que aparecer un GCC de MinGW; en
*Versiones de Qt*, un `qmake.exe` de la carpeta `mingw_64`. Con el instalador
oficial esto pasa cuando se marcó el kit de Qt pero no el ítem *MinGW
compiler*, que va aparte.

**Se abrió la carpeta en vez del proyecto.** Qt Creator distingue entre abrir
un directorio y abrir un proyecto. Tiene que ser el archivo `CMakeLists.txt`.

**El proyecto abrió pero CMake falló.** Mirar la pestaña *General Messages*.
Si dice `Qt 6 no encontrado: se omite vt6530qtnet`, el CMake corrió pero sin
Qt: falta `CMAKE_PREFIX_PATH`. Se arregla en Proyectos → Build → CMake,
agregando:

```
-DCMAKE_PREFIX_PATH=C:/Qt/6.9.0/mingw_64
```

(con la ruta real de tu instalación). El proyecto está hecho para no romperse
cuando falta Qt —el núcleo y la capa telnet compilan igual— así que un build
"exitoso" sin `vt6530qt.exe` es exactamente este caso.

---

## Sin Qt Creator

CMake y Ninja alcanzan. Desde un `cmd` o PowerShell con MinGW y CMake en el
`PATH`:

```bat
cmake -S . -B build -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_PREFIX_PATH=C:/Qt/6.9.0/mingw_64
cmake --build build
ctest --test-dir build --output-on-failure
build\vt6530qt.exe --host rci3 --puerto 23
```

El `CMakeLists.txt` corre `windeployqt` después de enlazar, así que las DLL de
Qt y el plugin de plataforma quedan al lado del `.exe`. Sin eso el programa
arranca y muere sin decir nada — es el síntoma clásico de que falta
`platforms\qwindows.dll`.

---

## SSH en Windows

El transporte SSH estaba escrito contra POSIX de punta a punta. Todo lo que
sabe de sockets vive ahora en `net/Sockets.h`, que es la única parte del
proyecto que pregunta si esto es Windows. Para compilar no hay que hacer nada
especial: CMake enlaza `ws2_32` solo (y `crypt32`/`bcrypt` si libssh2 viene con
el backend WinCNG, que es como está armada en MSYS2), y `Makefile.portable` lo
detecta por `uname -s`.

Dos cosas no eran traducción sino cambio de forma, y son las que habrían
fallado en silencio:

- **El fracaso de un `connect` no bloqueante.** En POSIX el descriptor queda
  escribible y uno pregunta por `SO_ERROR`. En Winsock sale por `exceptfds`, y
  si no se mira esa lista el `select` vence por plazo: un rechazo inmediato se
  vería como «tiempo agotado» quince segundos después.
- **`select()` sin descriptores.** En POSIX es la forma corta de dormir; en
  Winsock devuelve `WSAEINVAL` al instante. `Close()` lo usaba para espaciar
  los reintentos de `channel_free`, así que ahí los veinte intentos habrían
  pasado en un suspiro y la sesión no se cerraría limpia.

Y una de tipos: un `SOCKET` de Windows es un `UINT_PTR` de 64 bits.
`Descriptor()` devolvía `int`, que lo trunca a un número que parece válido y no
lo es. Ahora devuelve un entero ancho, y `Ssh6530Session` se lo pasa a
`QSocketNotifier` como `qintptr`, que es justo lo que ese constructor pide.

**`vt6530cli` y `sonda_ssh` siguen siendo solo de Linux** (`if(UNIX)` en
`CMakeLists.txt`): usan `PosixTransport`, que todavía no pasó por el shim. La
aplicación Qt no los necesita —su telnet va por `QTcpSocket` y su SSH por
`Ssh6530Transport`—, así que en Windows compila y anda igual.

**Esto está verificado por compilador, no por ejecución.** No hay un Windows en
el entorno donde se escribió, así que la rama `_WIN32` se compila contra
cabeceras falsas (`tests/falso-win32`) como parte de
`make -f Makefile.portable test`. Eso verifica nombres, aridad y tipos; la
semántica de arriba sale de la documentación de Winsock. La primera compilación
de verdad en MSYS2 es la que manda.

---

## Detalles que pueden morder

**MSVC no está soportado hoy.** El código de 2007 usa `strcpy`, `strlen` sobre
literales y conversiones que MSVC rechaza o llena de avisos, y varios archivos
de `src/` están en latin-1, que MSVC interpreta distinto. Con MinGW nada de
eso molesta. Si algún día hace falta MSVC, es trabajo aparte y no chico.

**La consola.** `vt6530qt` se enlaza como aplicación de consola a propósito:
toma `--host` por línea de comandos y el log del núcleo va a `stderr`. Si
alguna vez molesta la ventana negra, se le agrega
`set_target_properties(vt6530qt PROPERTIES WIN32_EXECUTABLE TRUE)` — pero
entonces hay que darle otra salida al log.

**La fuente.** El widget pide una monoespaciada del sistema. En Windows suele
caer en Consolas, que anda bien. Si la grilla se ve despareja, es que tomó una
proporcional: se fuerza con `setTerminalFont()`.

**Los saltos de línea.** Los archivos de `src/` y `vt6530/` son los originales
de 2007, con CRLF en varios. Si usás Git, no dejes que `core.autocrlf` los
convierta de ida y vuelta: el `.esperado` de las capturas se compara byte a
byte.

---

Fuentes: [Qt Wiki — MinGW](https://wiki.qt.io/MinGW) ·
[MSYS2 qt6-base](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-qt6-base) ·
[Get and Install Qt](https://doc.qt.io/qt-6/get-and-install-qt.html)
