/*
 *  Capa de compatibilidad SPL -> STL.
 *
 *  Sustituye el subsistema de depuracion de SPL (~570 usos en el nucleo).
 *  SPL rastreaba cada asignacion del heap para detectar fugas; aqui las
 *  aserciones se apoyan en assert() y el rastreo de memoria desaparece,
 *  que es lo que hacia SPL en compilacion release de todos modos.
 *
 *  Con VT6530_STRICT_ASSERT definido, ASSERT aborta como en el original.
 *  Sin el, registra la falla y continua: util para reproducir capturas del
 *  host sin que una asercion heredada corte el analisis a mitad de camino.
 */
#ifndef _spl_compat_debug_h
#define _spl_compat_debug_h

#include <cassert>
#include <cstdio>

#ifdef __cplusplus
extern "C" {
#endif

/* Contador de aserciones fallidas, para que las pruebas puedan comprobar
 * que un buffer se proceso sin disparar ninguna. */
extern int spl_compat_assert_failures;
void spl_compat_assert_failed(const char *expr, const char *file, int line);

/* Cada sitio se reporta una vez y despues solo se cuenta: una asercion
 * repetida miles de veces tapa todo lo demas en la consola. */
void spl_compat_assert_reset(void);
int  spl_compat_assert_sites(void);

#ifdef __cplusplus
}
#endif

#if defined(VT6530_STRICT_ASSERT)
#  define ASSERT(x)          assert(x)
#else
#  define ASSERT(x)                                                        \
     do {                                                                  \
       if (!(x)) spl_compat_assert_failed(#x, __FILE__, __LINE__);         \
     } while (0)
#endif

/* SPL validaba que un puntero apuntara a un bloque vivo del tamano dado.
 * Sin el rastreador de heap solo queda comprobar que no sea nulo. */
#define ASSERT_PTR(p)              ASSERT((p) != nullptr)
#define ASSERT_MEM(p, size)        ASSERT((p) != nullptr)

/* Rastreo de fugas de SPL: sin equivalente, se anulan. */
#define DEBUG_NOTE_MEM_ALLOCATION(p)   ((void)0)
#define DEBUG_NOTE_MEM(p)              ((void)0)
#define DEBUG_VALIDATE()               ((void)0)
#define DEBUG_CLEAR_MEM_CHECK_POINTS() ((void)0)
#define DEBUG_DUMP_MEM_LEAKS()         ((void)0)
#define DEBUG_TEAR_DOWN(x)             ((void)0)

#endif
