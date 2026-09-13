/*
 *  Vt6530Widget -- la pantalla del 6530 como QWidget.
 *
 *  Dibuja la grilla de 80x24 mas la linea de estado, y traduce el teclado.
 *  No sabe nada de red: observa un Vt6530Terminal y repinta cuando este
 *  avisa. Quien conecta el terminal con el socket es la ventana principal.
 *
 *  La plantilla del pintado sale de VtConsole::UpdateTerm() del paquete
 *  original de 2007, que ya recorria las celdas sucias y traducia atributos;
 *  y el mapeo de atributos a colores, de la rutina GDI que el autor dejo
 *  comentada en Page.h.
 */
#ifndef _vt6530_widget_h
#define _vt6530_widget_h

#include "../net/Vt6530Terminal.h"

#include <QColor>
#include <QFont>
#include <QSize>
#include <QString>
#include <QWidget>

class QTimer;

class Vt6530Widget : public QWidget, public ITerminalObserver
{
	Q_OBJECT

public:
	explicit Vt6530Widget(Vt6530Terminal *terminal, QWidget *parent = nullptr);
	~Vt6530Widget() override;

	/** Tamano natural: 80 columnas por 25 filas (24 mas la de estado). */
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override { return sizeHint(); }

	void setTerminalFont(const QFont &font);

	/* --- ITerminalObserver: llega desde el nucleo --- */
	void OnScreenChanged() override;
	void OnHostWaiting() override;
	void OnLineReset() override;
	void OnBell() override;

signals:
	/** El host termino de dibujar y espera entrada. */
	void hostWaiting();

	/** Se apreto una combinacion que todavia no se sabe traducir. La ventana
	 *  la muestra en la barra de estado en vez de tragarsela en silencio. */
	void unsupportedKey(const QString &motivo);

	/** Cada tecla que llega, y que se decidio hacer con ella.
	 *
	 *  Existe porque dos veces seguidas discutimos si una combinacion
	 *  "no funciona" sin saber siquiera si llegaba. El escritorio se queda
	 *  con varias -- Alt+F6 en GNOME --, y desde afuera eso se ve igual que
	 *  un mapeo mal hecho. Con esto se ve la diferencia en una linea. */
	void keyTrace(const QString &linea);

protected:
	void paintEvent(QPaintEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void onBlinkTimer();

private:
	void recalcMetrics();
	QRect cellRect(int col, int row) const;

	/** Traduce una tecla de Qt. Devuelve false si no la maneja. */
	bool dispatchKey(QKeyEvent *event);

	Vt6530Terminal *m_terminal;

	QFont   m_font;
	int     m_cellW = 8;
	int     m_cellH = 16;
	int     m_ascent = 12;
	int     m_originX = 0;
	int     m_originY = 0;

	QColor  m_background;
	QColor  m_foreground;
	QColor  m_dim;
	QColor  m_cursor;

	QTimer *m_blinkTimer;
	bool    m_blinkOn = true;
};

#endif
