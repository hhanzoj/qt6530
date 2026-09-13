/*
 *  Capa de compatibilidad SPL -> STL para el nucleo de libvt6530.
 *
 *  Reemplaza <spl/types.h> de la Standard Portable Library (John Garrison,
 *  abandonada en 2012) por definiciones equivalentes en C++17 puro.
 *  El nucleo no se modifica: sigue incluyendo <spl/...> y esta cabecera
 *  ocupa ese lugar en el include path.
 */
#ifndef _spl_compat_types_h
#define _spl_compat_types_h

#include <cstdint>
#include <cstddef>

typedef unsigned char byte;

#ifndef NULL
#define NULL 0
#endif

#endif
