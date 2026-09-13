/*
 *  Capa de compatibilidad SPL -> STL.
 *
 *  TextDisplay guarda un color de frente y uno de fondo y solo llama a
 *  AsHexString() (desde ToHTML). Cuando llegue la capa Qt, este tipo se
 *  reemplaza por QColor y esta cabecera desaparece.
 */
#ifndef _spl_compat_color_h
#define _spl_compat_color_h

#include <spl/types.h>
#include <cstdio>

class Color
{
	byte m_r, m_g, m_b;

public:
	Color() : m_r(0), m_g(0), m_b(0) {}
	Color(byte r, byte g, byte b) : m_r(r), m_g(g), m_b(b) {}

	inline byte Red() const   { return m_r; }
	inline byte Green() const { return m_g; }
	inline byte Blue() const  { return m_b; }

	inline void Set(byte r, byte g, byte b) { m_r = r; m_g = g; m_b = b; }

	/* Escribe 6 digitos hexadecimales mas el terminador. El destino en
	 * TextDisplay::ToHTML es un char[20], asi que sobra sitio. */
	inline void AsHexString(char *out) const
	{
		std::snprintf(out, 7, "%02X%02X%02X", m_r, m_g, m_b);
	}

	inline void CheckMem() const {}
	inline void ValidateMem() const {}
};

#endif
