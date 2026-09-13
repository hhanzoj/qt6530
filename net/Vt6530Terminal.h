/*
 *  Vt6530Terminal -- reemplazo de la fachada Vt6530 original.
 *
 *  La clase Vt6530 de 2007 mezclaba cuatro cosas: el socket, telnet, el
 *  parser y el despacho de eventos. Aca queda solo lo ultimo: junta
 *  TextDisplay, Keys y Guardian, y traduce entre las interfaces del nucleo y
 *  quien lo use. El transporte entra por constructor.
 *
 *  Sin Qt: la capa Qt observa esto y convierte las llamadas de
 *  ITerminalObserver en senales. Asi el terminal se puede probar sin GUI.
 */
#ifndef _vt6530_terminal_h
#define _vt6530_terminal_h

#include <spl/collection/Vector.h>
#include <spl/term/Telnet.h>

#include <vt6530/TextDisplay.h>
#include <vt6530/Keys.h>
#include <vt6530/Guardian.h>
#include <vt6530/TermEventListener.h>
#include <vt6530/MappedKeyListener.h>

#include <string>

/**
 *  Lo que el contenedor -- consola, widget Qt -- necesita saber.
 *
 *  Es el Vt6530EventListener del nucleo con nombres razonables y sin las
 *  entradas que nunca se usaron.
 */
class ITerminalObserver
{
public:
	virtual ~ITerminalObserver() {}

	/** La pantalla cambio: hay que repintar. */
	virtual void OnScreenChanged() {}

	/** El host termino de dibujar y espera entrada. */
	virtual void OnHostWaiting() {}

	/** Reset de linea. En el TELSERV capturado este es el turno de palabra
	 *  del modo conversacional: llega un EOT por cada linea de salida, y no
	 *  aparece ni un solo ENQ en toda la sesion. */
	virtual void OnLineReset() {}

	virtual void OnBell() {}
	virtual void OnError(const std::string &mensaje) { (void)mensaje; }
	virtual void OnTrace(const std::string &mensaje) { (void)mensaje; }
};

/**
 *  Un terminal 6530 completo, menos el transporte.
 */
class Vt6530Terminal : public Vt6530EventListener, public MappedKeyListener
{
public:
	/** link recibe lo que el terminal manda al host. No toma posesion. */
	explicit Vt6530Terminal(IHostLink *link, int paginas = 2,
	                        int columnas = 80, int filas = 24);
	virtual ~Vt6530Terminal();

	void SetObserver(ITerminalObserver *obs) { m_obs = obs; }

	/** Bytes que llegaron del host, ya sin telnet. */
	void FeedFromHost(const char *data, int len);

	/*  El teclado tiene DOS entradas, no una, y hay que elegir bien.
	 *
	 *  Las constantes SPC_* del nucleo van de 0 a 29 y pisan el rango de
	 *  los codigos de control ASCII: SPC_F1 vale 0, SPC_UP vale 24. Un
	 *  entero solo no alcanza para distinguir "el usuario apreto F1" de
	 *  "el usuario tecleo NUL", asi que el que llama tiene que decirlo.
	 *
	 *  Ademas los dos caminos hacen cosas distintas dentro del nucleo:
	 *  KeyReleased arma la secuencia AID de las teclas de funcion, y
	 *  KeyTyped resuelve por tabla. Mandar un CR por KeyReleased no hace
	 *  nada -- 13 no es ninguna constante SPC_ y cae fuera del switch. */

	/** Un caracter tecleado, incluidos los de control: CR, BS, TAB. */
	void TypeChar(int ascii, bool shift = false, bool ctrl = false,
	              bool alt = false);

	/** Una tecla especial, con una constante SPC_* de vt6530/Keys.h. */
	void PressSpecial(int spc, bool shift = false, bool ctrl = false,
	                  bool alt = false);

	/** Tecla de funcion, de 1 a 16. Devuelve false fuera de ese rango. */
	bool FunctionKey(int numero);

	/*  Eco local. En conversacional, si el host hace el eco (telnet WILL
	 *  ECHO) hay que apagarlo: es lo que hace que una clave no aparezca en
	 *  pantalla, porque el host simplemente deja de devolverla. Lo maneja
	 *  quien tenga el transporte, que es el unico que ve la negociacion.
	 *  En modo bloque no aplica: ahi el eco es siempre local.             */
	void SetLocalEcho(bool prendido);
	bool LocalEcho() const;

	/*  Edicion local de linea. No hay codigo de protocolo para estas dos:
	 *  en el 6530 son teclas del terminal, y el host se entera recien
	 *  cuando se le manda el bloque. Por eso se exponen aparte y no como
	 *  un SPC_ mas.                                                      */
	void InsertLine();
	void DeleteLine();

	inline TextDisplay *Display() { return m_display; }
	inline Keys *Keyboard()       { return m_keys; }

	/** La pantalla visible como texto, una fila por linea. */
	std::string ScreenText() const;

	bool IsBlockMode() const;
	bool IsProtectMode() const;

	/* --- Vt6530EventListener --- */
	virtual void Vt6530_OnConnect();
	virtual void Vt6530_OnDisconnect();
	virtual void Vt6530_OnResetLine();
	virtual void Vt6530_OnEnquire();
	virtual void Vt6530_OnDisplayChanged();
	virtual void Vt6530_OnError(const char *mensaje);
	virtual void Vt6530_OnDebug(const char *mensaje);
	virtual void Vt6530_OnRecv34(const char *op, const char *params, const int len);
	virtual void Vt6530_OnTextWatch(const char *txt, const int codigo);

	/* --- MappedKeyListener: por aca sale lo que el teclado produce --- */
	virtual void KeyMappedKey(const char *s, int len);
	virtual void KeyCommand(const char c);
	virtual int  KeyGetPage();
	virtual int  KeyGetCursorX();
	virtual int  KeyGetCursorY();
	virtual void KeyGetStartFieldASCII(StringBuffer *sb);

private:
	IHostLink        *m_link;
	ITerminalObserver *m_obs;

	TextDisplay *m_display;
	Keys        *m_keys;
	Guardian    *m_guardian;
	Vector<Vt6530EventListener *> m_listeners;

	void Notify();
};

#endif
