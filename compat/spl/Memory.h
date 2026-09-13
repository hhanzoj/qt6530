/*
 *  Capa de compatibilidad SPL -> STL.
 *
 *  IMemoryValidate era la interfaz que SPL exigia a todo objeto que quisiera
 *  participar del rastreo de heap. El nucleo la hereda en Page, TextDisplay,
 *  Mode y Guardian, asi que la interfaz debe existir; su implementacion, no.
 */
#ifndef _spl_compat_memory_h
#define _spl_compat_memory_h

#include <spl/debug.h>
/* Page.cpp lanza OutOfMemoryException sin incluir Exception.h: en SPL
 * llegaba de arrastre por esta cabecera. Se conserva ese arrastre. */
#include <spl/Exception.h>

class IMemoryValidate
{
public:
	virtual ~IMemoryValidate() {}

#if defined(DEBUG) || defined(_DEBUG)
	virtual void CheckMem() const = 0;
	virtual void ValidateMem() const = 0;
#else
	inline void CheckMem() const {}
	inline void ValidateMem() const {}
#endif
};

/* SPL exigia una ValidateType() por cada tipo almacenado en un Vector.
 * Se conserva como plantilla vacia para que las declaraciones del nucleo
 * (por ejemplo la de TextWatch) sigan compilando. */
template <typename T> inline void ValidateType(T &) {}

#endif
