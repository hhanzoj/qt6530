/*
 *  Capa de compatibilidad SPL -> STL.
 *
 *  El nucleo llama a Log::WriteInfo/WriteWarn/WriteError con formato printf
 *  en 57 sitios, y buena parte de esos sitios SON el hallazgo de la
 *  auditoria: comandos ESC que solo se registran en el log en lugar de
 *  ejecutarse. Por eso el log no se anula: se captura.
 *
 *  SetSink() permite a las pruebas recolectar cada linea y afirmar sobre
 *  ella ("este buffer no debio dejar ningun WriteError"), que es como se
 *  detectan las regresiones del parser sin un host NonStop delante.
 */
#ifndef _spl_compat_log_h
#define _spl_compat_log_h

#include <string>

enum SplLogLevel
{
	CLOG_DEBUG = 0,
	CLOG_INFO  = 1,
	CLOG_WARN  = 2,
	CLOG_ERROR = 3
};

class ILogSink
{
public:
	virtual ~ILogSink() {}
	virtual void OnLogLine(SplLogLevel level, const std::string &line) = 0;
};

class Log
{
public:
	static void Init(const char *filename, int level);

	/* Redirige la salida; nullptr vuelve a stderr. No toma posesion. */
	static void SetSink(ILogSink *sink);
	static ILogSink *GetSink();

	/* Silencia la salida por stderr sin desactivar el sink. */
	static void SetQuiet(bool quiet);

	static void WriteDebug(const char *fmt, ...);
	static void WriteInfo(const char *fmt, ...);
	static void WriteWarn(const char *fmt, ...);
	static void WriteError(const char *fmt, ...);
};

#endif
