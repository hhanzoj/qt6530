#include "Vt6530Terminal.h"

#include <cstring>

Vt6530Terminal::Vt6530Terminal(IHostLink *link, int paginas, int columnas,
                               int filas)
:	m_link(link), m_obs(nullptr), m_listeners()
{
	m_listeners.Add(this);

	m_display  = new TextDisplay(paginas, columnas, filas);
	m_keys     = new Keys();
	m_keys->SetListener(this);
	m_guardian = new Guardian(&m_listeners, m_display, m_keys, m_link);
}

Vt6530Terminal::~Vt6530Terminal()
{
	delete m_guardian;
	delete m_keys;
	delete m_display;
}

/* ------------------------------------------------------------------ */

void Vt6530Terminal::FeedFromHost(const char *data, int len)
{
	if (data == nullptr || len <= 0) return;

	m_guardian->ProcessRemoteString(data, len);

	/* El nucleo marca la pantalla como sucia pero no avisa solo; la
	 * fachada original consultaba NeedsRepaint() despues de cada lectura. */
	if (m_display->NeedsRepaint())
	{
		m_display->SetRePaint(false);
		Notify();
	}
}

void Vt6530Terminal::Notify()
{
	if (m_obs) m_obs->OnScreenChanged();
}

void Vt6530Terminal::TypeChar(int ascii, bool shift, bool ctrl, bool alt)
{
	m_keys->KeyTyped(ascii, shift, ctrl, alt);
	if (m_display->NeedsRepaint())
	{
		m_display->SetRePaint(false);
		Notify();
	}
}

void Vt6530Terminal::PressSpecial(int spc, bool shift, bool ctrl, bool alt)
{
	/*  Las de navegacion son locales: mueven el cursor en la pantalla y no
	 *  salen al host. Hasta la fase 03 reenviaban el codigo de la tecla
	 *  anterior, porque KeyReleased no asignaba m_pressedKey en esos casos.
	 *
	 *  SPC_BREAK sigue sin tener caso en el switch de KeyReleased: llega
	 *  hasta aca y no hace nada. Queda pendiente saber que deberia mandar. */
	m_keys->KeyReleased(spc, shift, ctrl, alt);
	if (m_display->NeedsRepaint())
	{
		m_display->SetRePaint(false);
		Notify();
	}
}

bool Vt6530Terminal::FunctionKey(int numero)
{
	/*  Las dieciseis del 6530. F13..F16 se agregaron en la fase 03; hasta
	 *  entonces esta funcion las rechazaba, y VIEWSYS sale con F16.        */
	static const int mapa[16] = {
		SPC_F1,  SPC_F2,  SPC_F3,  SPC_F4,  SPC_F5,  SPC_F6,
		SPC_F7,  SPC_F8,  SPC_F9,  SPC_F10, SPC_F11, SPC_F12,
		SPC_F13, SPC_F14, SPC_F15, SPC_F16
	};
	if (numero < 1 || numero > 16) return false;
	PressSpecial(mapa[numero - 1]);
	return true;
}

std::string Vt6530Terminal::ScreenText() const
{
	std::string out;
	Page *page = m_display->GetDisplayPage();
	const int filas = m_display->GetNumRows();
	const int cols  = m_display->GetNumColumns();

	for (int r = 0; r < filas; r++)
	{
		std::string linea;
		for (int c = 0; c < cols; c++)
		{
			char ch = page->GetCell(c, r)->Get();
			linea.push_back(((unsigned char)ch >= 0x20 &&
			                 (unsigned char)ch < 0x7F) ? ch : ' ');
		}
		while (!linea.empty() && linea[linea.size() - 1] == ' ')
			linea.erase(linea.size() - 1);
		out += linea;
		out += "\n";
	}
	return out;
}

/*
 *  INSERTAR Y BORRAR LINEA DESDE EL TECLADO -- NO EN MODO PROTEGIDO
 *
 *  Estas dos son el camino del TECLADO: Ctrl+Insertar y Ctrl+Suprimir en el
 *  widget. El camino del HOST es otro y no se toca -- Guardian llama a
 *  LineDown() y DeleteLine() directamente al interpretar ESC L y ESC M, y si
 *  el host manda ESC L sabra por que --.
 *
 *  Lo que hacen abajo es correr las 24 filas ENTERAS, sin mirar campos ni
 *  proteccion. En conversacional esta bien: la pantalla es texto libre. En
 *  modo protegido no: ahi la pantalla es un formulario que armo la
 *  aplicacion, y correr las filas rompe la correspondencia entre lo que el
 *  host cree que hay y lo que se ve.
 *
 *  El sintoma era ese: en VIEWSYS, Ctrl+Suprimir borraba NUESTRA pantalla
 *  -- solo la nuestra -- y de ahi en mas lo dibujado y el modelo del host
 *  eran dos cosas distintas.
 *
 *  LO QUE FALTA MEDIR
 *
 *  Que no haya que correr la pantalla es seguro. Que hay que hacer EN SU
 *  LUGAR, no: el manual dice que un campo protegido no se puede modificar,
 *  pero VIEWSYS documenta Ctrl+Insertar y Ctrl+Suprimir como "Reset
 *  MAXIMUMs" y "Exit", que son funciones de la aplicacion -- o sea que
 *  podria haber que mandarle algo al host --. Eso se contesta capturando un
 *  cliente que funcione, no razonando. Hasta entonces no se hace nada, que
 *  es lo unico que seguro no esta mal.
 */
void Vt6530Terminal::InsertLine()
{
	if (m_display->GetProtectMode()) return;
	m_display->LineDown();
	if (m_display->NeedsRepaint()) { m_display->SetRePaint(false); Notify(); }
}

void Vt6530Terminal::DeleteLine()
{
	if (m_display->GetProtectMode()) return;
	m_display->DeleteLine();
	if (m_display->NeedsRepaint()) { m_display->SetRePaint(false); Notify(); }
}

void Vt6530Terminal::SetLocalEcho(bool prendido)
{
	if (prendido) m_display->SetEchoOn();
	else          m_display->SetEchoOff();
}

bool Vt6530Terminal::LocalEcho() const { return m_display->GetEchoOn(); }

bool Vt6530Terminal::IsBlockMode() const   { return m_display->IsBlockMode(); }
bool Vt6530Terminal::IsProtectMode() const { return m_display->GetProtectMode(); }

/* ------------------------------------------------------------------ */
/*  Eventos que llegan del nucleo                                      */
/* ------------------------------------------------------------------ */

void Vt6530Terminal::Vt6530_OnConnect()    {}
void Vt6530Terminal::Vt6530_OnDisconnect() {}

void Vt6530Terminal::Vt6530_OnResetLine()
{
	if (m_obs) m_obs->OnLineReset();
}

void Vt6530Terminal::Vt6530_OnEnquire()
{
	if (m_obs) m_obs->OnHostWaiting();
}

void Vt6530Terminal::Vt6530_OnDisplayChanged()
{
	Notify();
}

void Vt6530Terminal::Vt6530_OnError(const char *mensaje)
{
	if (m_obs) m_obs->OnError(mensaje ? mensaje : "");
}

void Vt6530Terminal::Vt6530_OnDebug(const char *mensaje)
{
	if (m_obs) m_obs->OnTrace(mensaje ? mensaje : "");
}

void Vt6530Terminal::Vt6530_OnRecv34(const char *, const char *, const int) {}
void Vt6530Terminal::Vt6530_OnTextWatch(const char *, const int) {}

/* ------------------------------------------------------------------ */
/*  Salida del teclado hacia el host                                   */
/* ------------------------------------------------------------------ */

void Vt6530Terminal::KeyMappedKey(const char *s, int len)
{
	if (m_link && s != nullptr && len > 0)
		m_link->SendRaw((const byte *)s, len);
}

void Vt6530Terminal::KeyCommand(const char c)
{
	/*  El nucleo llama a esto para los comandos locales -- edicion en la
	 *  pantalla, no envio al host. Guardian::ExecLocalCommand decide si
	 *  ademas hay que mandar algo, segun el modo. */
	m_guardian->ExecLocalCommand(c);

	if (m_display->NeedsRepaint())
	{
		m_display->SetRePaint(false);
		Notify();
	}
}

int Vt6530Terminal::KeyGetPage()    { return m_display->GetCurrentPage(); }
int Vt6530Terminal::KeyGetCursorX() { return m_display->GetCursorCol(); }
int Vt6530Terminal::KeyGetCursorY() { return m_display->GetCursorRow(); }

void Vt6530Terminal::KeyGetStartFieldASCII(StringBuffer *sb)
{
	m_display->GetStartFieldASCII(sb);
}
