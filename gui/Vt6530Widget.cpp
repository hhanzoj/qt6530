#include "Vt6530Widget.h"

#include <QApplication>
#include <QChar>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QString>
#include <QTimer>

/* Filas visibles: 24 de la pagina mas la linea de estado. */
static const int FILAS_ESTADO = 1;

Vt6530Widget::Vt6530Widget(Vt6530Terminal *terminal, QWidget *parent)
:	QWidget(parent),
	m_terminal(terminal),
	m_background(0x0C, 0x14, 0x1A),
	m_foreground(0xD8, 0xE0, 0xC8),
	m_dim(0x6A, 0x78, 0x68),
	m_cursor(0xE8, 0xB0, 0x40),
	m_blinkTimer(new QTimer(this))
{
	setFocusPolicy(Qt::StrongFocus);
	setAttribute(Qt::WA_OpaquePaintEvent);     /* pintamos cada pixel */
	setAutoFillBackground(false);

	m_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	m_font.setPointSize(12);
	m_font.setStyleHint(QFont::Monospace);
	m_font.setFixedPitch(true);
	recalcMetrics();

	if (m_terminal) m_terminal->SetObserver(this);

	/*  El parpadeo es un atributo del 6530, no un adorno: hay aplicaciones
	 *  que lo usan para senalar un campo en error. Medio segundo es lo que
	 *  hacian las terminales. */
	connect(m_blinkTimer, &QTimer::timeout, this, &Vt6530Widget::onBlinkTimer);
	m_blinkTimer->start(500);
}

Vt6530Widget::~Vt6530Widget()
{
	if (m_terminal) m_terminal->SetObserver(nullptr);
}

/* ------------------------------------------------------------------ */

void Vt6530Widget::recalcMetrics()
{
	const QFontMetrics fm(m_font);
	m_cellW  = fm.horizontalAdvance(QLatin1Char('W'));
	if (m_cellW <= 0) m_cellW = fm.averageCharWidth();
	if (m_cellW <= 0) m_cellW = 8;
	m_cellH  = fm.height();
	m_ascent = fm.ascent();
}

void Vt6530Widget::setTerminalFont(const QFont &font)
{
	m_font = font;
	m_font.setFixedPitch(true);
	recalcMetrics();
	updateGeometry();
	update();
}

QSize Vt6530Widget::sizeHint() const
{
	const int cols  = m_terminal ? m_terminal->Display()->GetNumColumns() : 80;
	const int filas = (m_terminal ? m_terminal->Display()->GetNumRows() : 24)
	                + FILAS_ESTADO;
	return QSize(cols * m_cellW, filas * m_cellH);
}

void Vt6530Widget::resizeEvent(QResizeEvent *)
{
	/*  La grilla no se estira: 80x24 es parte del protocolo, no una
	 *  preferencia. Se centra en el espacio disponible. */
	const QSize nat = sizeHint();
	m_originX = (width()  > nat.width())  ? (width()  - nat.width())  / 2 : 0;
	m_originY = (height() > nat.height()) ? (height() - nat.height()) / 2 : 0;
}

QRect Vt6530Widget::cellRect(int col, int row) const
{
	return QRect(m_originX + col * m_cellW, m_originY + row * m_cellH,
	             m_cellW, m_cellH);
}

void Vt6530Widget::onBlinkTimer()
{
	m_blinkOn = !m_blinkOn;
	update();
}

/* ------------------------------------------------------------------ */
/*  Pintado                                                            */
/* ------------------------------------------------------------------ */

void Vt6530Widget::paintEvent(QPaintEvent *event)
{
	QPainter p(this);
	p.fillRect(event->rect(), m_background);

	if (!m_terminal) return;

	TextDisplay *display = m_terminal->Display();
	Page *page = display->GetDisplayPage();
	if (page == nullptr) return;

	const int filas = display->GetNumRows();
	const int cols  = display->GetNumColumns();

	p.setFont(m_font);

	for (int r = 0; r < filas; r++)
	{
		for (int c = 0; c < cols; c++)
		{
			PageCell *cell = page->GetCell(c, r);
			const QRect celda = cellRect(c, r);

			/*  Video inverso: intercambia frente y fondo. Es lo que dibuja
			 *  las barras de VIEWSYS, que son espacios en inverso -- si se
			 *  pintara solo el caracter, el grafico no se veria. */
			const bool inverso = cell->IsReverse();
			QColor fondo  = inverso ? m_foreground : m_background;
			QColor frente = inverso ? m_background : m_foreground;

			if (fondo != m_background) p.fillRect(celda, fondo);

			/*  Invisible: la celda ocupa lugar pero no muestra su contenido.
			 *  Se usa para los campos de clave. */
			if (cell->IsInvis()) continue;

			/*  Parpadeo: en la mitad apagada no se dibuja el caracter, pero
			 *  el fondo del inverso queda, como en la terminal real. */
			if (cell->IsBlinking() && !m_blinkOn) continue;

			const unsigned char ch = (unsigned char)cell->Get();
			if (ch >= 0x20 && ch < 0x7F && ch != ' ')
			{
				p.setPen(frente);
				p.drawText(celda, Qt::AlignLeft | Qt::AlignVCenter,
				           QString(QLatin1Char((char)ch)));
			}

			if (cell->IsUnderline())
			{
				p.setPen(frente);
				const int y = celda.bottom() - 1;
				p.drawLine(celda.left(), y, celda.right(), y);
			}
		}
	}

	/* --- cursor --- */
	{
		/*  GetCursorRow/Col devuelven la posicion mas uno. */
		const int cr = display->GetCursorRow() - 1;
		const int cc = display->GetCursorCol() - 1;
		if (cr >= 0 && cr < filas && cc >= 0 && cc < cols)
		{
			QRect celda = cellRect(cc, cr);
			celda.setTop(celda.bottom() - 2);
			p.fillRect(celda, m_cursor);
		}
	}

	/* --- linea de estado, debajo de la pagina --- */
	{
		/*  La linea de estado crece mas alla de 80 columnas por un defecto
		 *  conocido de WriteStatus() -- usa Insert(), que desplaza en vez de
		 *  sobrescribir --, asi que se recorta al ancho de la pantalla. */
		std::string texto = display->GetStatusLine()->Str();
		if ((int)texto.size() > cols) texto.resize((size_t)cols);

		QRect banda(m_originX, m_originY + filas * m_cellH,
		            cols * m_cellW, m_cellH);
		p.fillRect(banda, m_dim.darker(220));
		p.setPen(m_dim.lighter(180));
		p.drawText(banda, Qt::AlignLeft | Qt::AlignVCenter,
		           QString::fromLatin1(texto.c_str(), (int)texto.size()));
	}
}

/* ------------------------------------------------------------------ */
/*  Teclado                                                            */
/* ------------------------------------------------------------------ */

/*  Nombre legible de una tecla, para la traza. QKeySequence hace el trabajo
 *  y ademas conoce los nombres locales, que es mas de lo que uno escribiria
 *  a mano.                                                                */
static QString NombreDeTecla(const QKeyEvent *e)
{
	const QString n = QKeySequence(e->key() | (int)e->modifiers()).toString();
	return n.isEmpty() ? QStringLiteral("(sin nombre)") : n;
}

bool Vt6530Widget::dispatchKey(QKeyEvent *event)
{
	if (!m_terminal) return false;

	const int key = event->key();

	/*  Los modificadores solos no se trazan. Apretar Ctrl genera su propio
	 *  evento -- "Ctrl+Control" -- y al soltar otro, asi que buscar una
	 *  combinacion entre ese ruido es incomodo justo cuando uno necesita
	 *  leer rapido.                                                       */
	if (key == Qt::Key_Control || key == Qt::Key_Alt ||
	    key == Qt::Key_Shift   || key == Qt::Key_Meta ||
	    key == Qt::Key_AltGr   || key == Qt::Key_CapsLock)
	{
		return false;
	}

	/*  La traza sale SIEMPRE por la senal; quien la escucha decide. Lo que
	 *  importa es que una tecla que el escritorio se comio no aparece aca,
	 *  y esa ausencia es la respuesta.                                    */
	emit keyTrace(QStringLiteral("tecla %1 (key=0x%2, mods=0x%3)")
	              .arg(NombreDeTecla(event))
	              .arg(key, 0, 16)
	              .arg((int)event->modifiers(), 0, 16));

	const bool ctrl  = event->modifiers().testFlag(Qt::ControlModifier);
	const bool alt   = event->modifiers().testFlag(Qt::AltModifier);
	const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);

	/*  F11 a F16 desde un teclado de PC.
	 *
	 *  Un teclado comun llega hasta F12 y el 6530 usa dieciseis. La
	 *  convencion es Alt+Fn = F(n+10), y es la que usa el propio VIEWSYS:
	 *  su encabezado dice "EXIT - F16 | Alt-F6", o sea que ofrece Alt+F6
	 *  como forma de mandar F16 desde un teclado que no la tiene.
	 *
	 *  Confirmado por captura: al salir de VIEWSYS con Alt+F6, el emulador
	 *  comercial mando 01 4F 21 20 20 03 00 -- el codigo de F16 PLANO, sin
	 *  ningun modificador. La traduccion la hace el emulador, no el
	 *  protocolo.
	 *
	 *  PERO el escritorio se queda con varias de esas combinaciones antes de
	 *  que lleguen aca. GNOME usa Alt+F5, Alt+F6 y Alt+F7 para manejar
	 *  ventanas, y KDE tiene las suyas. Comprobado en vivo: al apretar Alt+F6
	 *  para salir de VIEWSYS, la traza no muestra NINGUN "=> term" -- la
	 *  tecla no llego nunca. No hay nada que el programa pueda hacer contra
	 *  eso salvo ofrecer otro camino.
	 *
	 *  Por eso Ctrl+Fn hace lo mismo que Alt+Fn, con la misma cuenta:
	 *
	 *      Alt+F1..F6   -> F11..F16     (la convencion, cuando el escritorio
	 *                                    la deja pasar)
	 *      Ctrl+F1..F6  -> F11..F16     (igual, y nadie se la roba)
	 *
	 *  Antes Ctrl+F1..F4 daba F13..F16, que no seguia ninguna cuenta y habia
	 *  que memorizar aparte. Dos atajos para lo mismo pueden convivir; dos
	 *  cuentas distintas para lo mismo, no.
	 *
	 *  No se usa Shift, porque Shift+Fn ya significa otra cosa en el 6530: la
	 *  tabla shiftFn tiene sus propios codigos AID.                        */
	/*  Alt+F4 NO: es cerrar la ventana, en Windows y en casi todo escritorio
	 *  de Linux. Mapearla a F14 dejaba la aplicacion sin forma obvia de
	 *  salir -- se la comia el widget y el gestor de ventanas nunca la
	 *  veia. F14 sigue disponible por Ctrl+F4.                            */
	if (alt && key == Qt::Key_F4)
		return false;

	if ((alt || ctrl) && key >= Qt::Key_F1 && key <= Qt::Key_F6)
	{
		const int n = (key - Qt::Key_F1 + 1) + 10;
		emit keyTrace(QStringLiteral("  -> tecla de funcion F%1").arg(n));
		return m_terminal->FunctionKey(n);
	}

	/*  Y si el teclado si las produce, tambien. */
	if (!ctrl && !alt && key >= Qt::Key_F1 && key <= Qt::Key_F16)
	{
		const int n = key - Qt::Key_F1 + 1;
		emit keyTrace(QStringLiteral("  -> tecla de funcion F%1").arg(n));
		return m_terminal->FunctionKey(n);
	}

	/*  Alt sobre una tecla de funcion mas alla de F6 no tiene traduccion
	 *  conocida, y el nucleo tampoco la manda: Keys::KeyAction, en la rama
	 *  de alt con fn, hace return sin escribir nada. Se avisa en vez de
	 *  tragarsela en silencio.                                            */
	if (alt && key >= Qt::Key_F7 && key <= Qt::Key_F16)
	{
		emit unsupportedKey(QStringLiteral(
			"Alt+F%1 no tiene traduccion conocida (Alt+F1 a Alt+F6, o "
			"Ctrl+F1 a Ctrl+F6, mandan F11 a F16; F14 solo por Ctrl+F4).")
			.arg(key - Qt::Key_F1 + 1));
		return true;
	}

	/*  Teclas especiales -> PressSpecial con las constantes SPC_*.
	 *
	 *  Las de navegacion son locales: el nucleo mueve el cursor en la
	 *  pantalla y no manda nada al host. Arreglado en la fase 03 -- antes
	 *  una flecha reenviaba lo que hubiera mapeado la tecla anterior.
	 *
	 *  Pause -> SPC_BREAK es la excepcion: no tiene caso en el switch de
	 *  Keys::KeyReleased, asi que hoy no hace nada. Se deja mapeada para
	 *  que el dia que se sepa que deberia mandar, alcance con tocar el
	 *  nucleo.                                                            */
	/*  Ctrl+Insertar y Ctrl+Suprimir: insertar y borrar linea.
	 *
	 *  Van aparte de las SPC_ a proposito. En el 6530 son teclas del
	 *  terminal -- la edicion pasa en la pantalla y el host se entera
	 *  recien cuando se le manda el bloque --, asi que no hay codigo de
	 *  protocolo que mandar y no corresponde inventarle uno.             */
	if (ctrl && key == Qt::Key_Insert) { m_terminal->InsertLine(); return true; }
	if (ctrl && key == Qt::Key_Delete) { m_terminal->DeleteLine(); return true; }

	switch (key)
	{
		case Qt::Key_Up:       m_terminal->PressSpecial(SPC_UP);       return true;
		case Qt::Key_Down:     m_terminal->PressSpecial(SPC_DOWN);     return true;
		case Qt::Key_Left:     m_terminal->PressSpecial(SPC_LEFT);     return true;
		case Qt::Key_Right:    m_terminal->PressSpecial(SPC_RIGHT);    return true;
		case Qt::Key_Home:     m_terminal->PressSpecial(SPC_HOME);     return true;
		case Qt::Key_End:      m_terminal->PressSpecial(SPC_END);      return true;
		case Qt::Key_Insert:   m_terminal->PressSpecial(SPC_INS);      return true;
		case Qt::Key_Delete:   m_terminal->PressSpecial(SPC_DEL);      return true;
		case Qt::Key_PageUp:   m_terminal->PressSpecial(SPC_PGUP);     return true;
		case Qt::Key_PageDown: m_terminal->PressSpecial(SPC_PGDN);     return true;
		case Qt::Key_Pause:    m_terminal->PressSpecial(SPC_BREAK);    return true;
		case Qt::Key_Print:    m_terminal->PressSpecial(SPC_PRINTSCR); return true;
		default: break;
	}

	/*  Caracteres, incluidos los de control. Van por TypeChar: mandarlos
	 *  por PressSpecial no haria nada, porque 8, 9, 10 y 13 no son ninguna
	 *  constante SPC_ y caen fuera del switch de KeyReleased. */
	switch (key)
	{
		case Qt::Key_Return:
		case Qt::Key_Enter:     m_terminal->TypeChar(13); return true;
		case Qt::Key_Backspace: m_terminal->TypeChar(8);  return true;
		case Qt::Key_Tab:       m_terminal->TypeChar(9);  return true;
		case Qt::Key_Escape:    m_terminal->TypeChar(27); return true;
		default: break;
	}

	const QString texto = event->text();
	if (texto.isEmpty()) return false;

	bool alguno = false;
	for (QChar qc : texto)
	{
		const auto u = qc.unicode();
		if (u > 0xFF) continue;                 /* el 6530 es de 8 bits */
		m_terminal->TypeChar((int)u, shift, ctrl, alt);
		alguno = true;
	}
	return alguno;
}

void Vt6530Widget::keyPressEvent(QKeyEvent *event)
{
	if (dispatchKey(event))
	{
		event->accept();
		update();
		return;
	}
	QWidget::keyPressEvent(event);
}

/* ------------------------------------------------------------------ */
/*  Eventos del terminal                                               */
/* ------------------------------------------------------------------ */

void Vt6530Widget::OnScreenChanged()
{
	update();
}

void Vt6530Widget::OnHostWaiting()
{
	update();
	emit hostWaiting();
}

void Vt6530Widget::OnLineReset()
{
	update();
}

void Vt6530Widget::OnBell()
{
	/*  Hoy no llega: Guardian invoca TextDisplay::Bell(), que en el nucleo
	 *  original no avisa a nadie. Queda enganchado para cuando la fase 04 lo
	 *  conecte -- el 6530 usa la campana para senalar errores de campo.    */
	QApplication::beep();
}
