/*
 *  Capa de compatibilidad SPL -> STL.
 *
 *  StringBuffer es el tipo de SPL mas usado por el nucleo (65 llamadas).
 *  Se reimplementa sobre std::string, que admite NUL incrustados: el nucleo
 *  los usa en las secuencias AID (SOH ... ETX NUL) y luego pasa la longitud
 *  explicita a NotifyListener, asi que Length() debe contarlos.
 *
 *  Dos semanticas se conservan tal como estaban, a proposito:
 *
 *   - Insert() inserta y desplaza; NO sobrescribe. Es lo que hace que
 *     TextDisplay::WriteStatus() alargue la linea de estado mas alla de
 *     80 columnas (defecto 07 de la auditoria). Cambiarlo aqui ocultaria
 *     el bug en lugar de corregirlo donde corresponde.
 *   - SetLength() trunca, y al extender rellena con espacios, que es lo
 *     que necesita la linea de estado de 80 columnas para que SetCharAt()
 *     no quede fuera de rango.
 */
#ifndef _spl_compat_stringbuffer_h
#define _spl_compat_stringbuffer_h

#include <spl/types.h>
#include <spl/debug.h>

#include <string>
#include <cstring>
#include <cstdio>

class StringBuffer
{
	std::string m_buf;

public:
	StringBuffer() {}
	explicit StringBuffer(int capacity) { m_buf.reserve(capacity > 0 ? (size_t)capacity : 0); }
	StringBuffer(const StringBuffer &b) : m_buf(b.m_buf) {}
	~StringBuffer() {}

	StringBuffer &operator=(const StringBuffer &b) { m_buf = b.m_buf; return *this; }

	inline void Append(char ch) { m_buf.push_back(ch); }

	inline void Append(const char *str)
	{
		if (str != nullptr) m_buf.append(str);
	}

	inline void Append(const char *str, int len)
	{
		if (str != nullptr && len > 0) m_buf.append(str, (size_t)len);
	}

	/* Anexa la representacion decimal, como el Append(int) de SPL. */
	inline void Append(int val)
	{
		char tmp[24];
		std::snprintf(tmp, sizeof(tmp), "%d", val);
		m_buf.append(tmp);
	}

	inline void Append(const StringBuffer &b) { m_buf.append(b.m_buf); }

	/* SPL devolvia char* no constante y el nucleo se apoya en eso:
	 * TextDisplay::WriteBuffer/WriteDisplay reciben char*. Se ofrecen
	 * ambas versiones para no editar esas firmas todavia. */
	inline char *GetChars() { return m_buf.empty() ? const_cast<char *>("") : &m_buf[0]; }
	inline const char *GetChars() const { return m_buf.c_str(); }

	/*  ATENCION: la implementacion de SPL no esta disponible, asi que la
	 *  semantica de Trim() es una suposicion: aqui recorta espacios en
	 *  ambos extremos.
	 *
	 *  Importa. ProtectPage::ReadBuffer() la usa sobre el contenido de
	 *  cada campo antes de enviarlo al host, y ya emitio la direccion del
	 *  campo, de modo que recortar por la izquierda desplazaria los datos.
	 *  Lo esperable en un 6530 es suprimir solo los blancos finales.
	 *  Pendiente de contrastar con el manual en la fase 03; mientras tanto
	 *  la prueba trim_semantics deja el comportamiento a la vista. */
	inline void Trim()
	{
		size_t first = m_buf.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) { m_buf.clear(); return; }
		size_t last = m_buf.find_last_not_of(" \t\r\n");
		m_buf = m_buf.substr(first, last - first + 1);
	}

	inline int Length() const { return (int)m_buf.size(); }

	inline void SetLength(int len)
	{
		if (len < 0) len = 0;
		m_buf.resize((size_t)len, ' ');
	}

	/* Inserta desplazando el resto a la derecha (semantica original). */
	inline void Insert(int pos, const char *str)
	{
		if (str == nullptr) return;
		if (pos < 0) pos = 0;
		if ((size_t)pos > m_buf.size()) m_buf.resize((size_t)pos, ' ');
		m_buf.insert((size_t)pos, str);
	}

	inline void SetCharAt(int pos, char ch)
	{
		ASSERT(pos >= 0 && (size_t)pos < m_buf.size());
		if (pos >= 0 && (size_t)pos < m_buf.size()) m_buf[(size_t)pos] = ch;
	}

	inline char CharAt(int pos) const
	{
		ASSERT(pos >= 0 && (size_t)pos < m_buf.size());
		return ((size_t)pos < m_buf.size()) ? m_buf[(size_t)pos] : '\0';
	}

	inline char operator[](int pos) const { return CharAt(pos); }

	inline void Clear() { m_buf.clear(); }

	/* Vista sin copia, para el codigo nuevo de la capa de red y las pruebas. */
	inline const std::string &Str() const { return m_buf; }

	inline void CheckMem() const {}
	inline void ValidateMem() const {}
};

#endif
