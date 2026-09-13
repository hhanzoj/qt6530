cmake_minimum_required(VERSION 3.16)
project(vt6530 LANGUAGES CXX)

#
#  Fase 0 del port de libvt6530 0.2.6 a Qt 6.
#
#  Este archivo construye SOLO el nucleo: la logica del protocolo 6530, sin
#  Qt, sin sockets y sin la Standard Portable Library. La dependencia de SPL
#  se satisface con las cabeceras de compat/, que la reimplementan sobre la
#  biblioteca estandar de C++17.
#
#  Los archivos de src/ y vt6530/ son los originales de 2007, sin una sola
#  edicion. Eso es deliberado: mientras el nucleo no cambie, cualquier
#  diferencia de comportamiento se atribuye al shim y no al port.
#
#  Las fases siguientes agregan:
#    01  vt6530-net  -> Tn6530Session sobre QTcpSocket
#    02  vt6530-gui  -> Vt6530Widget sobre QWidget/QPainter
#

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# --------------------------------------------------------------------------
#  vt6530core
# --------------------------------------------------------------------------

add_library(vt6530core STATIC
  src/Cursor.cpp
  src/Page.cpp
  src/SharedProtocol.cpp
  src/ProtectPage.cpp
  src/TextDisplay.cpp
  src/Keys.cpp
  src/Guardian.cpp
  compat/spl_compat.cpp
)

target_include_directories(vt6530core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/compat
)

# El codigo es de 2007 y genera avisos por costumbres de la epoca. Se dejan
# encendidos a proposito: varios apuntan justo a los defectos de la auditoria.
# -Wno-unused-parameter evita el ruido de las interfaces virtuales.
if(MSVC)
  target_compile_options(vt6530core PRIVATE /W3)
else()
  target_compile_options(vt6530core PRIVATE
    -Wall -Wextra -Wno-unused-parameter -Wno-write-strings)
endif()

option(VT6530_STRICT_ASSERT
       "Abortar en las aserciones heredadas de SPL en vez de registrarlas" OFF)
if(VT6530_STRICT_ASSERT)
  target_compile_definitions(vt6530core PUBLIC VT6530_STRICT_ASSERT)
endif()

# --------------------------------------------------------------------------
#  Pruebas
# --------------------------------------------------------------------------

enable_testing()

add_executable(vt6530_tests tests/vt6530_tests.cpp)
target_link_libraries(vt6530_tests PRIVATE vt6530core)

add_test(NAME nucleo COMMAND vt6530_tests)

# --------------------------------------------------------------------------
#  vt6530_replay -- reproduce capturas del host contra el nucleo
# --------------------------------------------------------------------------
#
#  Convierte una sesion grabada con tools/tap6530.py en una prueba de
#  regresion: reproduce el payload, vuelca la pantalla y la compara contra un
#  archivo .esperado. Es lo que permite corregir el parser en la fase 03 sin
#  un host NonStop delante.

add_executable(vt6530_replay tools/replay.cpp)
target_link_libraries(vt6530_replay PRIVATE vt6530core)

# Algunas cadenas de GCC anteriores a la 9 necesitan enlazar stdc++fs aparte.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(vt6530_replay PRIVATE stdc++fs)
endif()

# --------------------------------------------------------------------------
#  vt6530net -- capa telnet TN6530-8 (fase 01)
# --------------------------------------------------------------------------
#
#  C++17 puro, sin Qt ni sockets: entran bytes del cable, salen el payload
#  para Guardian y los bytes de respuesta. La politica de negociacion sale de
#  capturas reales (ver capturas/) y se verifica contra ellas en
#  tests/telnet_tests.cpp.
#
#  El envoltorio Qt (net/Tn6530Session) y el respaldo POSIX se agregan aparte,
#  mas abajo, porque solo uno de los dos hace falta a la vez.

add_library(vt6530net STATIC net/Tn6530Telnet.cpp)
target_include_directories(vt6530net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
  target_compile_options(vt6530net PRIVATE -Wall -Wextra)
endif()

add_executable(vt6530_telnet_tests tests/telnet_tests.cpp)
target_link_libraries(vt6530_telnet_tests PRIVATE vt6530net)
add_test(NAME telnet COMMAND vt6530_telnet_tests)

# --------------------------------------------------------------------------
#  Transporte POSIX y cliente de consola
# --------------------------------------------------------------------------
#
#  El transporte de produccion es Qt (net/Tn6530Session, mas abajo). Este es
#  el respaldo sin Qt: sirve para probar contra el host desde una consola y
#  deja demostrado que la capa telnet no depende de Qt.

#  El pegamento va aparte del transporte: asi se puede probar el terminal
#  sin sockets, y los dos transportes -- POSIX y Qt -- comparten una sola
#  copia en vez de compilarlo cada uno por su cuenta.
add_library(vt6530term STATIC net/Vt6530Terminal.cpp)
target_link_libraries(vt6530term PUBLIC vt6530core vt6530net)

# --------------------------------------------------------------------------
#  vt6530opts -- la linea de comandos, al estilo PuTTY
# --------------------------------------------------------------------------
#
#  Sin Qt a proposito: el parseo es donde se cuelan los errores tontos y asi
#  se prueba con compilador en vez de a ojo. La aplicacion Qt no se puede
#  compilar en el entorno donde se escribio este port, asi que todo lo que se
#  pueda sacar de ahi y probar es una trampa menos.

add_library(vt6530opts STATIC apps/Opciones.cpp)
target_include_directories(vt6530opts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
  target_compile_options(vt6530opts PRIVATE -Wall -Wextra)
endif()

#  La traduccion de opciones a politica de ssh. Vivia adentro de main.cpp, que
#  necesita Qt: mudarla aca es lo que permite que la mire un compilador.
#
#  Necesita compat/ porque Ssh6530Transport.h incluye spl/term/Telnet.h, que
#  es donde vive IHostLink. Solo la cabecera: no arrastra libssh2 ni el
#  transporte, asi que las pruebas de aca siguen enlazando con nada.
target_sources(vt6530opts PRIVATE apps/PoliticaSsh.cpp apps/Consola.cpp)
target_include_directories(vt6530opts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat)

add_executable(vt6530_politica_tests tests/politica_ssh_tests.cpp)
target_link_libraries(vt6530_politica_tests PRIVATE vt6530opts)
add_test(NAME politica COMMAND vt6530_politica_tests)

#  La consola: apagar el eco para una clave, y el si/no. Tambien salio de
#  main.cpp, y por un motivo peor: estaba escrita solo para POSIX, asi que en
#  Windows la aplicacion no podia pedir una clave y lo informaba como si
#  faltara la consola. La rama del otro sistema se comprueba desde
#  Makefile.portable con las cabeceras falsas.
add_executable(vt6530_consola_tests tests/consola_tests.cpp)
target_link_libraries(vt6530_consola_tests PRIVATE vt6530opts)
add_test(NAME consola COMMAND vt6530_consola_tests)

add_executable(vt6530_opciones_tests tests/opciones_tests.cpp)
target_link_libraries(vt6530_opciones_tests PRIVATE vt6530opts)
add_test(NAME opciones COMMAND vt6530_opciones_tests)

#  El shim de sockets, por el camino nativo. La rama del otro sistema se
#  comprueba con las cabeceras falsas desde Makefile.portable: aca no, porque
#  con CMake no tiene sentido pedirle que compile como si fuera otro sistema.
add_executable(vt6530_sockets_tests tests/sockets_tests.cpp)
if(WIN32)
  target_link_libraries(vt6530_sockets_tests PRIVATE ws2_32)
endif()
add_test(NAME sockets COMMAND vt6530_sockets_tests)

add_executable(vt6530_terminal_tests tests/terminal_tests.cpp)
target_link_libraries(vt6530_terminal_tests PRIVATE vt6530term)
add_test(NAME terminal COMMAND vt6530_terminal_tests)

if(UNIX)
  add_library(vt6530posix STATIC net/PosixTransport.cpp)
  target_link_libraries(vt6530posix PUBLIC vt6530term)

  add_executable(vt6530cli apps/vt6530cli/main.cpp)
  target_link_libraries(vt6530cli PRIVATE vt6530posix vt6530opts vt6530ssh)

  #  Diagnostico: el mismo transporte ssh, sin Qt y con marcas de tiempo.
  #  Sirve para separar "el transporte no lee" de "el Qt no lo llama".
  add_executable(sonda_ssh tools/sonda_ssh.cpp)
  target_link_libraries(sonda_ssh PRIVATE vt6530ssh vt6530opts)
endif()

# --------------------------------------------------------------------------
#  vt6530ssh -- transporte SSH, opcional
# --------------------------------------------------------------------------
#
#  Se compila si esta libssh2. Si no, el archivo igual entra al build con un
#  cuerpo que dice, en castellano y con la linea de instalacion, que el
#  binario se armo sin ssh -- en vez de que -ssh falle con un error de enlace
#  incomprensible.
#
#  Se usa la cabecera nativa y se ajusta la linea de compilacion; nada de
#  redeclarar constantes de libssh2 a mano.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBSSH2 QUIET libssh2)
endif()
if(NOT LIBSSH2_FOUND)
  find_path(LIBSSH2_INCLUDE_DIRS libssh2.h)
  find_library(LIBSSH2_LIBRARIES NAMES ssh2 libssh2)
  if(LIBSSH2_INCLUDE_DIRS AND LIBSSH2_LIBRARIES)
    set(LIBSSH2_FOUND TRUE)
  endif()
endif()

#  Anotar la clave de un host en known_hosts. Aparte, sin libssh2 ni Qt: es
#  la parte que puede salir mal en silencio -- un base64 mal armado deja una
#  linea que ssh no reconoce nunca -- asi que tiene sus propias pruebas.
add_library(vt6530knownhosts STATIC net/KnownHosts.cpp)
target_include_directories(vt6530knownhosts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(vt6530_knownhosts_tests tests/knownhosts_tests.cpp)
target_link_libraries(vt6530_knownhosts_tests PRIVATE vt6530knownhosts)
add_test(NAME knownhosts COMMAND vt6530_knownhosts_tests)

add_library(vt6530ssh STATIC net/Ssh6530Transport.cpp)
target_link_libraries(vt6530ssh PUBLIC vt6530term vt6530knownhosts)

#  En Windows los sockets no viven en la libc: ws2_32 lo necesita nuestro
#  propio codigo (net/Sockets.h). crypt32 y bcrypt los pide libssh2 cuando va
#  con el backend WinCNG, que es como viene armada en MSYS2; con el backend
#  OpenSSL sobran, pero enlazar de mas no rompe nada y evita un error de
#  simbolos que no dice nada. Van PUBLIC para que los herede quien enlace esto.
if(WIN32)
  target_link_libraries(vt6530ssh PUBLIC ws2_32)
  if(LIBSSH2_FOUND)
    target_link_libraries(vt6530ssh PUBLIC crypt32 bcrypt)
  endif()
endif()

if(LIBSSH2_FOUND)
  message(STATUS "libssh2 encontrado: se compila el transporte ssh")
  target_compile_definitions(vt6530ssh PUBLIC VT6530_CON_SSH)
  target_include_directories(vt6530ssh PUBLIC ${LIBSSH2_INCLUDE_DIRS})
  target_link_libraries(vt6530ssh PUBLIC ${LIBSSH2_LIBRARIES})
else()
  message(STATUS "libssh2 no encontrado: -ssh avisara y no conectara "
                 "(dnf install libssh2-devel, o en MSYS2 "
                 "pacman -S mingw-w64-ucrt-x86_64-libssh2)")
endif()

# --------------------------------------------------------------------------
#  Qt 6 -- opcional
# --------------------------------------------------------------------------
#
#  Si Qt 6 esta presente se compila el transporte real. Si no, el resto del
#  proyecto compila igual: la capa telnet y el nucleo no dependen de Qt.

find_package(Qt6 QUIET COMPONENTS Core Network)
if(Qt6_FOUND)
  message(STATUS "Qt 6 encontrado: se compila vt6530qtnet")
  set(CMAKE_AUTOMOC ON)

  #  AUTOUIC corre uic sobre los .ui y genera los ui_*.h que incluyen los
  #  formularios. Hace falta desde que el programa se puede lanzar sin
  #  argumentos y sale el dialogo de conexion.
  set(CMAKE_AUTOUIC ON)
  add_library(vt6530qtnet STATIC net/Tn6530Session.cpp net/Ssh6530Session.cpp)
  target_link_libraries(vt6530qtnet PUBLIC vt6530term vt6530ssh
                                           Qt6::Core Qt6::Network)

  # ------------------------------------------------------------------------
  #  Fase 02 -- la interfaz
  # ------------------------------------------------------------------------
  #
  #  Necesita ademas Qt6::Widgets. Si esta Core y Network pero no Widgets, se
  #  compila la capa de red igual y se omite la GUI, en vez de fallar entera.

  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6Widgets_FOUND)
    message(STATUS "Qt 6 Widgets encontrado: se compila vt6530qt")

    add_library(vt6530gui STATIC gui/Vt6530Widget.cpp)
    target_link_libraries(vt6530gui PUBLIC vt6530qtnet Qt6::Widgets)

    #  Los formularios: se lanzan cuando no hay argumentos, al estilo PuTTY.
    #  Llenan un cli::Opciones y de ahi en adelante el camino es el mismo que
    #  el de la linea de comandos -- ver el comentario de main() --. La clave
    #  no pasa por el struct: la pide el transporte cuando la necesita.
    add_executable(vt6530qt
      apps/vt6530qt/main.cpp
      apps/vt6530qt/formularioconexion.cpp
      apps/vt6530qt/formularioconexion.h
      apps/vt6530qt/formularioconexion.ui
      apps/vt6530qt/formulariosshcred.cpp
      apps/vt6530qt/formulariosshcred.h
      apps/vt6530qt/formulariosshcred.ui)

    #  uic escribe los ui_*.h en el directorio de compilacion, asi que los
    #  #include "ui_formularioconexion.h" tienen que poder encontrarlos.
    target_include_directories(vt6530qt PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(vt6530qt PRIVATE vt6530gui vt6530opts)

    #  En Windows el ejecutable no encuentra las DLL de Qt por si solo.
    #  windeployqt las copia al lado del .exe, y ademas el plugin de
    #  plataforma (qwindows.dll), sin el cual la aplicacion arranca y muere
    #  en silencio. Se deja la consola a proposito (sin WIN32_EXECUTABLE):
    #  el programa toma --host por linea de comandos y el log va a stderr.
    if(WIN32)
      find_program(VT6530_WINDEPLOYQT windeployqt HINTS ${QT6_INSTALL_PREFIX}/bin)
      if(VT6530_WINDEPLOYQT)
        add_custom_command(TARGET vt6530qt POST_BUILD
          COMMAND "${VT6530_WINDEPLOYQT}" --no-translations
                  "$<TARGET_FILE:vt6530qt>"
          COMMENT "windeployqt: copiando las DLL de Qt junto al ejecutable")
      else()
        message(STATUS "windeployqt no encontrado: habra que copiar las DLL "
                       "de Qt a mano para correr vt6530qt fuera de Qt Creator")
      endif()
    endif()
  else()
    message(STATUS "Qt 6 Widgets no encontrado: se omite vt6530qt")
  endif()
else()
  message(STATUS "Qt 6 no encontrado: se omite vt6530qtnet "
                 "(el nucleo, la capa telnet y vt6530cli compilan igual)")
endif()

add_test(NAME capturas
         COMMAND vt6530_replay --verificar-todo
                 ${CMAKE_CURRENT_SOURCE_DIR}/capturas)
