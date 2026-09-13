/*
 *  Implementacion de la capa de compatibilidad SPL -> STL.
 */
#include <spl/debug.h>
#include <spl/Log.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>

/* ------------------------------------------------------------------ */
/* Aserciones                                                          */
/* ------------------------------------------------------------------ */

int spl_compat_assert_failures = 0;

namespace {

/*  Una asercion que se repite miles de veces no informa: tapa. Cada sitio
 *  (archivo:linea) se reporta una vez y despues solo se cuenta. El total
 *  sigue en spl_compat_assert_failures, que las pruebas consultan.
 *
 *  Hizo falta en cuanto la GUI se conecto a un host real: una asercion de
 *  ProtectPage se disparaba varias veces por segundo y no dejaba leer nada
 *  mas en la consola. */
std::map<std::string, long> &SitiosDeAsercion()
{
	static std::map<std::string, long> sitios;
	return sitios;
}

} // namespace

void spl_compat_assert_failed(const char *expr, const char *file, int line)
{
	spl_compat_assert_failures++;

	char clave[512];
	std::snprintf(clave, sizeof(clave), "%s:%d", file ? file : "?", line);

	long &veces = SitiosDeAsercion()[clave];
	veces++;

	if (veces == 1)
	{
		std::fprintf(stderr, "ASSERT fallo: %s  (%s)\n",
		             expr ? expr : "?", clave);
	}
	else if (veces == 100)
	{
		std::fprintf(stderr, "ASSERT fallo: %s  (%s)  -- ya van 100, "
		                     "se deja de reportar este sitio\n",
		             expr ? expr : "?", clave);
	}
}

void spl_compat_assert_reset()
{
	SitiosDeAsercion().clear();
	spl_compat_assert_failures = 0;
}

int spl_compat_assert_sites()
{
	return (int)SitiosDeAsercion().size();
}

/* ------------------------------------------------------------------ */
/* Log                                                                 */
/* ------------------------------------------------------------------ */

namespace {

ILogSink *g_sink  = nullptr;
bool      g_quiet = false;

const char *LevelName(SplLogLevel level)
{
	switch (level)
	{
		case CLOG_DEBUG: return "DEBUG";
		case CLOG_INFO:  return "INFO ";
		case CLOG_WARN:  return "WARN ";
		case CLOG_ERROR: return "ERROR";
	}
	return "?????";
}

void Emit(SplLogLevel level, const char *fmt, std::va_list args)
{
	char buf[1024];
	std::vsnprintf(buf, sizeof(buf), fmt, args);

	if (g_sink != nullptr)
	{
		g_sink->OnLogLine(level, std::string(buf));
	}
	if (!g_quiet)
	{
		std::fprintf(stderr, "[%s] %s\n", LevelName(level), buf);
	}
}

} // namespace

void Log::Init(const char *, int) {}

void Log::SetSink(ILogSink *sink) { g_sink = sink; }
ILogSink *Log::GetSink()          { return g_sink; }
void Log::SetQuiet(bool quiet)    { g_quiet = quiet; }

#define SPL_LOG_BODY(level)          \
	std::va_list args;               \
	va_start(args, fmt);             \
	Emit(level, fmt, args);          \
	va_end(args);

void Log::WriteDebug(const char *fmt, ...) { SPL_LOG_BODY(CLOG_DEBUG) }
void Log::WriteInfo (const char *fmt, ...) { SPL_LOG_BODY(CLOG_INFO)  }
void Log::WriteWarn (const char *fmt, ...) { SPL_LOG_BODY(CLOG_WARN)  }
void Log::WriteError(const char *fmt, ...) { SPL_LOG_BODY(CLOG_ERROR) }
