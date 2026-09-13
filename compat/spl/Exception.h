/*
 *  Capa de compatibilidad SPL -> STL.
 *
 *  El nucleo lanza OutOfMemoryException al fallar un new (habito de 2007,
 *  cuando new devolvia NULL en algunos compiladores) y captura Exception
 *  por valor. Se conserva la jerarquia sobre std::exception.
 */
#ifndef _spl_compat_exception_h
#define _spl_compat_exception_h

#include <exception>
#include <string>

class Exception : public std::exception
{
protected:
	std::string m_msg;
public:
	Exception() : m_msg("Exception") {}
	explicit Exception(const char *msg) : m_msg(msg ? msg : "") {}
	virtual ~Exception() throw() {}

	inline const char *Message() const { return m_msg.c_str(); }
	virtual const char *what() const throw() { return m_msg.c_str(); }
};

class OutOfMemoryException : public Exception
{
public:
	OutOfMemoryException() : Exception("Out of memory") {}
};

class NotImplementedException : public Exception
{
public:
	NotImplementedException() : Exception("Not implemented") {}
};

class InvalidArgumentException : public Exception
{
public:
	InvalidArgumentException() : Exception("Invalid argument") {}
	explicit InvalidArgumentException(const char *msg) : Exception(msg) {}
};

#endif
