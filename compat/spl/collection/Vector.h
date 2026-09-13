/*
 *  Capa de compatibilidad SPL -> STL.
 *
 *  El nucleo usa cuatro metodos de Vector<T>: Add, Count, ElementAt y Clear.
 *  Se reimplementa sobre std::vector conservando esos nombres para no tocar
 *  los archivos originales.
 */
#ifndef _spl_compat_vector_h
#define _spl_compat_vector_h

#include <spl/types.h>
#include <spl/debug.h>
#include <spl/Memory.h>

#include <vector>

template <typename T>
class Vector
{
	std::vector<T> m_items;

public:
	Vector() {}
	Vector(const Vector<T> &v) : m_items(v.m_items) {}
	~Vector() {}

	Vector<T> &operator=(const Vector<T> &v) { m_items = v.m_items; return *this; }

	inline void Add(const T &item) { m_items.push_back(item); }

	inline int Count() const { return (int)m_items.size(); }

	inline T &ElementAt(int index)
	{
		ASSERT(index >= 0 && (size_t)index < m_items.size());
		return m_items[(size_t)index];
	}

	inline const T &ElementAt(int index) const
	{
		ASSERT(index >= 0 && (size_t)index < m_items.size());
		return m_items[(size_t)index];
	}

	inline void Clear() { m_items.clear(); }

	inline bool IsEmpty() const { return m_items.empty(); }

	inline void CheckMem() const {}
	inline void ValidateMem() const {}
};

#endif
