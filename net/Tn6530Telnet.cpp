#include "Tn6530Telnet.h"

#include <cstdio>
#include <cstring>

namespace tn6530 {

/* ------------------------------------------------------------------ */
/*  Nombres                                                            */
/* ------------------------------------------------------------------ */

const char *OptionName(unsigned char option)
{
	switch (option)
	{
		case OPT_BINARY:        return "BINARY";
		case OPT_ECHO:          return "ECHO";
		case OPT_SGA:           return "SGA";
		case OPT_END_OF_RECORD: return "END-OF-RECORD";
		case OPT_TERMINAL_TYPE: return "TERMINAL-TYPE";
		case OPT_NAWS:          return "NAWS";
		case OPT_LINEMODE:      return "LINEMODE";
		default:                return "OPT-?";
	}
}

const char *CommandName(unsigned char command)
{
	switch (command)
	{
		case WILL: return "WILL";
		case WONT: return "WONT";
		case DO:   return "DO";
		case DONT: return "DONT";
		case SB:   return "SB";
		case SE:   return "SE";
		case EOR:  return "EOR";
		case NOP:  return "NOP";
		case GA:   return "GA";
		case IAC:  return "IAC";
		default:   return "CMD-?";
	}
}

/* ------------------------------------------------------------------ */

Tn6530Telnet::Tn6530Telnet(const Policy &policy) : m_policy(policy) {}

void Tn6530Telnet::Trace(const std::string &texto)
{
	if (m_ev.onTrace) m_ev.onTrace(texto);
}

void Tn6530Telnet::SendRawBytes(const unsigned char *data, int len)
{
	if (m_ev.onSend && data != nullptr && len > 0) m_ev.onSend(data, len);
}

void Tn6530Telnet::SendCommand(unsigned char command, unsigned char option)
{
	const unsigned char buf[3] = { IAC, command, option };
	SendRawBytes(buf, 3);

	char linea[96];
	std::snprintf(linea, sizeof(linea), "-> IAC %s %s",
	              CommandName(command), OptionName(option));
	Trace(linea);
}

void Tn6530Telnet::Start()
{
	/*  Ofrecer TERMINAL-TYPE, sin esperar a que nos lo pidan.
	 *
	 *  Aca habia un "Nada. El host abre; nosotros respondemos", con el
	 *  argumento de que adelantarse agrega idas y vueltas que el host no
	 *  pidio. Una captura de OutsideView contra rci3 -- tomada con
	 *  tools/tap6530ssh.py -- lo desmiente:
	 *
	 *      host  IAC WILL ECHO / WILL SGA / DO NAWS
	 *      term  IAC DO SGA / WONT NAWS / WILL TERMINAL-TYPE
	 *      host  IAC DO TERMINAL-TYPE
	 *      host  IAC SB TERMINAL-TYPE SEND IAC SE
	 *      term  IAC SB TERMINAL-TYPE IS tn6530-8 IAC SE
	 *
	 *  El host NO pregunta el tipo de terminal hasta que el cliente lo
	 *  ofrece. En nuestras sesiones por ssh ese DO TERMINAL-TYPE no aparecia
	 *  nunca, justamente porque nunca ofreciamos nada: el host se quedaba sin
	 *  saber que somos un 6530.
	 *
	 *  Por telnet no se notaba porque ese TELSERV manda DO TERMINAL-TYPE por
	 *  su cuenta. Dos servicios del mismo host, dos costumbres distintas, y
	 *  la conclusion sacada de uno solo era incompleta.
	 *
	 *  El estado se anota antes de mandar, para que el DO TERMINAL-TYPE que
	 *  viene despues no dispare un WILL repetido.                          */
	m_opt[OPT_TERMINAL_TYPE].usKnown = true;
	m_opt[OPT_TERMINAL_TYPE].us      = true;
	SendCommand(WILL, OPT_TERMINAL_TYPE);

	/*  LINEMODE tambien lo ofrece OutsideView, y de ahi salen las peticiones
	 *  de lectura de HP con el bit de eco. Detras de la politica, porque en
	 *  esa misma captura el host y el emulador se lo ofrecen y se lo retiran
	 *  varias veces, y no quiero esa danza prendida por defecto sin
	 *  entenderla.                                                         */
	if (m_policy.acceptLinemode)
	{
		m_opt[OPT_LINEMODE].usKnown = true;
		m_opt[OPT_LINEMODE].us      = true;
		SendCommand(WILL, OPT_LINEMODE);
	}

	Trace("sesion telnet iniciada; ofrecido TERMINAL-TYPE");
}

/* ------------------------------------------------------------------ */
/*  Politica                                                           */
/* ------------------------------------------------------------------ */

/** Opciones que estamos dispuestos a activar de nuestro lado (WILL). */
bool Tn6530Telnet::WantToEnableLocal(unsigned char option) const
{
	switch (option)
	{
		case OPT_TERMINAL_TYPE: return true;
		case OPT_BINARY:        return m_policy.acceptBinary;
		case OPT_END_OF_RECORD: return m_policy.acceptEndOfRecord;
		case OPT_SGA:           return m_policy.acceptSga;
		case OPT_NAWS:          return m_policy.acceptNaws;
		case OPT_LINEMODE:      return m_policy.acceptLinemode;
		/* Nunca hacemos eco nosotros: en modo conversacional lo hace el host. */
		case OPT_ECHO:          return false;
		default:                return false;
	}
}

/** Opciones que aceptamos que el host active de su lado (respondemos DO). */
bool Tn6530Telnet::WantToEnableRemote(unsigned char option) const
{
	switch (option)
	{
		case OPT_ECHO:          return m_policy.acceptRemoteEcho;
		case OPT_SGA:           return m_policy.acceptSga;
		case OPT_BINARY:        return m_policy.acceptBinary;
		case OPT_END_OF_RECORD: return m_policy.acceptEndOfRecord;
		default:                return false;
	}
}

/* ------------------------------------------------------------------ */
/*  Negociacion                                                        */
/* ------------------------------------------------------------------ */
/*
 *  Solo se responde cuando el estado de la opcion cambia. Responder a cada
 *  mensaje sin mirar el estado es como se arma un bucle de negociacion:
 *  WILL / DO / WILL / DO hasta que alguno corta.
 */

void Tn6530Telnet::OnWill(unsigned char option)
{
	OptionState &st = m_opt[option];
	const bool queremos = WantToEnableRemote(option);

	if (st.himKnown && st.him == queremos) return;   /* ya esta asi */

	st.himKnown = true;
	st.him = queremos;
	SendCommand(queremos ? DO : DONT, option);
}

void Tn6530Telnet::OnWont(unsigned char option)
{
	OptionState &st = m_opt[option];
	if (st.himKnown && !st.him) return;
	st.himKnown = true;
	st.him = false;
	SendCommand(DONT, option);
}

void Tn6530Telnet::OnDo(unsigned char option)
{
	OptionState &st = m_opt[option];
	const bool podemos = WantToEnableLocal(option);

	/*  LINEMODE no termina con el WILL, y hay que contestarlo AUNQUE el
	 *  estado no cambie.
	 *
	 *  Desde que Start() ofrece LINEMODE, el estado ya queda fijado antes de
	 *  que llegue el DO, asi que el "return" de abajo se lo comia y la
	 *  negociacion se cortaba ahi. En la captura de OutsideView el paso
	 *  siguiente es del cliente:
	 *
	 *      term  IAC SB LINEMODE MODE 01 (EDIT) IAC SE
	 *      host  IAC SB LINEMODE MODE 05 (EDIT|MODE_ACK) IAC SE
	 *      host  IAC SB LINEMODE 04 08 18 19 0D 00 EF E0 00   <- lectura
	 *
	 *  Y de esas peticiones de lectura sale el bit de eco que oculta la
	 *  clave. O sea que sin este MODE se pierde mas que un ida y vuelta. */
	if (option == OPT_LINEMODE && podemos && !m_linemodeModeEnviado)
	{
		if (!st.usKnown || !st.us)
		{
			st.usKnown = true;
			st.us = true;
			SendCommand(WILL, option);
		}
		SendLinemodeMode();
		return;
	}

	if (st.usKnown && st.us == podemos) return;

	st.usKnown = true;
	st.us = podemos;
	SendCommand(podemos ? WILL : WONT, option);

	/*  NAWS no termina con el WILL: la RFC 1073 dice que el tamano se manda
	 *  enseguida, sin que el host lo pida. Decir WILL y no mandarlo deja al
	 *  host esperando un dato que no va a llegar -- peor que haber dicho
	 *  WONT, porque ahi al menos sabe a que atenerse.                    */
	if (option == OPT_NAWS && podemos) SendNaws();
}

void Tn6530Telnet::SendLinemodeMode()
{
	/*  IAC SB LINEMODE MODE <mascara> IAC SE, de la RFC 1184.
	 *
	 *  MODE es el subcomando 1 y EDIT el bit 0x01: el terminal junta la linea
	 *  y la manda entera. Es exactamente lo que manda OutsideView -- MODE 01
	 *  -- y el host contesta MODE 05, o sea EDIT con el bit MODE_ACK.       */
	static const unsigned char kMODE = 1;
	static const unsigned char kEDIT = 0x01;

	const unsigned char b[7] = {
		IAC, SB, OPT_LINEMODE, kMODE, kEDIT, IAC, SE
	};
	SendRawBytes(b, 7);
	m_linemodeModeEnviado = true;
	Trace("-> SB LINEMODE MODE 01 (EDIT)");
}

void Tn6530Telnet::SendNaws()
{
	if (!WeWill(OPT_NAWS)) return;

	const int c = m_policy.columns;
	const int f = m_policy.rows;

	/*  El cuerpo se escapa como cualquier dato: un 255 partiria la
	 *  subnegociacion. Con 80x24 no pasa, pero un dia alguien va a poner
	 *  255 columnas y no quiero que ese sea el defecto.                 */
	const unsigned char cuerpo[4] = {
		(unsigned char)((c >> 8) & 0xFF), (unsigned char)(c & 0xFF),
		(unsigned char)((f >> 8) & 0xFF), (unsigned char)(f & 0xFF)
	};

	std::vector<unsigned char> b;
	b.push_back(IAC);
	b.push_back(SB);
	b.push_back(OPT_NAWS);
	for (int i = 0; i < 4; i++)
	{
		b.push_back(cuerpo[i]);
		if (cuerpo[i] == IAC) b.push_back(IAC);
	}
	b.push_back(IAC);
	b.push_back(SE);
	SendRawBytes(b.data(), (int)b.size());

	char linea[96];
	std::snprintf(linea, sizeof(linea), "-> SB NAWS %dx%d", c, f);
	Trace(linea);
}

void Tn6530Telnet::SetWindowSize(int columnas, int filas)
{
	if (columnas <= 0 || filas <= 0) return;
	if (columnas == m_policy.columns && filas == m_policy.rows) return;
	m_policy.columns = columnas;
	m_policy.rows    = filas;
	SendNaws();   /* no hace nada si NAWS no se negocio */
}

void Tn6530Telnet::OnDont(unsigned char option)
{
	OptionState &st = m_opt[option];
	if (st.usKnown && !st.us) return;
	st.usKnown = true;
	st.us = false;
	SendCommand(WONT, option);
}

bool Tn6530Telnet::WeWill(unsigned char option) const
{
	return m_opt[option].usKnown && m_opt[option].us;
}

bool Tn6530Telnet::HeWill(unsigned char option) const
{
	return m_opt[option].himKnown && m_opt[option].him;
}

void Tn6530Telnet::OnSubnegotiation()
{
	if (m_sb.empty()) return;
	const unsigned char option = m_sb[0];

	if (option == OPT_TERMINAL_TYPE && m_sb.size() >= 2 && m_sb[1] == 1)
	{
		/* SB TERMINAL-TYPE SEND -> respondemos IS <tipo> */
		std::vector<unsigned char> out;
		out.push_back(IAC);
		out.push_back(SB);
		out.push_back(OPT_TERMINAL_TYPE);
		out.push_back(0);                       /* IS */
		for (char c : m_policy.terminalType)
			out.push_back((unsigned char)c);
		out.push_back(IAC);
		out.push_back(SE);
		SendRawBytes(out.data(), (int)out.size());
		Trace("-> IAC SB TERMINAL-TYPE IS " + m_policy.terminalType + " IAC SE");
		return;
	}

	/*  Peticion de lectura de HP: LINEMODE subcomando 4.
	 *
	 *  No es de la RFC 1184. Llega aunque hayamos contestado WONT LINEMODE,
	 *  asi que no hace falta cambiar la negociacion para verla. Ver el
	 *  comentario de LineRead en el .h para el origen de este formato. */
	if (option == OPT_LINEMODE && m_sb.size() >= 9 && m_sb[1] == 0x04)
	{
		LineRead lectura;
		lectura.terminator = m_sb[5];
		lectura.maxBytes   = ((int)m_sb[6] << 8) | (int)m_sb[7];
		lectura.flags      = m_sb[8];
		lectura.echo       = (m_sb[8] & 0x40) != 0;

		char t[176];
		std::snprintf(t, sizeof(t),
		              "<- peticion de lectura: hasta %d bytes, terminador %02X, "
		              "banderas %02X -> eco %s",
		              lectura.maxBytes, (unsigned)lectura.terminator,
		              (unsigned)lectura.flags,
		              lectura.echo ? "SI" : "NO (clave)");
		Trace(t);

		if (m_ev.onLineRead) m_ev.onLineRead(lectura);
		return;
	}

	/*  LINEMODE MODE (subcomando 1) de la RFC 1184.
	 *
	 *  Importa mas de lo que parece. Con el bit EDIT puesto, el que junta la
	 *  linea y hace el eco es EL TERMINAL, no el host. O sea que un WILL ECHO
	 *  del host deja de significar lo que significaba: quien dibuja lo
	 *  tecleado somos nosotros. Ver HostEchoes().
	 *
	 *  El host contesta con MODE_ACK (0x04) puesto sobre lo que le pedimos;
	 *  la captura de OutsideView muestra MODE 05, o sea EDIT|MODE_ACK.     */
	if (option == OPT_LINEMODE && m_sb.size() >= 3 && m_sb[1] == 0x01)
	{
		m_linemodeMode = m_sb[2];
		m_linemodeModeConocido = true;

		char t[160];
		std::snprintf(t, sizeof(t),
		              "<- SB LINEMODE MODE %02X (%s%s%s) -> el eco lo hace %s",
		              (unsigned)m_linemodeMode,
		              (m_linemodeMode & 0x01) ? "EDIT" : "sin EDIT",
		              (m_linemodeMode & 0x02) ? "|TRAPSIG" : "",
		              (m_linemodeMode & 0x04) ? "|MODE_ACK" : "",
		              LinemodeEdit() ? "el terminal" : "el host");
		Trace(t);
		return;
	}

	char linea[128];
	std::snprintf(linea, sizeof(linea),
	              "<- IAC SB %s (%d bytes de cuerpo), sin respuesta",
	              OptionName(option), (int)m_sb.size() - 1);
	Trace(linea);
}

/* ------------------------------------------------------------------ */
/*  Entrada                                                            */
/* ------------------------------------------------------------------ */

void Tn6530Telnet::FlushPayload()
{
	if (m_payload.empty()) return;
	if (m_ev.onPayload) m_ev.onPayload(m_payload.data(), (int)m_payload.size());
	m_payload.clear();
}

void Tn6530Telnet::Feed(const unsigned char *data, int len)
{
	if (data == nullptr || len <= 0) return;

	for (int i = 0; i < len; i++)
	{
		const unsigned char b = data[i];

		switch (m_state)
		{
			case ST_DATA:
				if (b == IAC) m_state = ST_IAC;
				else          m_payload.push_back(b);
				break;

			case ST_IAC:
				if (b == IAC)
				{
					m_payload.push_back(IAC);   /* IAC IAC -> 0xFF literal */
					m_state = ST_DATA;
				}
				else if (b == WILL || b == WONT || b == DO || b == DONT)
				{
					m_negCommand = b;
					m_state = ST_OPTION;
				}
				else if (b == SB)
				{
					m_sb.clear();
					m_state = ST_SB;
				}
				else
				{
					/* Comando de dos bytes. EOR marca fin de registro, y el
					 * payload acumulado hasta aca es ese registro: hay que
					 * entregarlo antes de avisar. */
					if (b == EOR)
					{
						FlushPayload();
						Trace("<- IAC EOR");
						if (m_ev.onEndOfRecord) m_ev.onEndOfRecord();
					}
					else
					{
						char linea[64];
						std::snprintf(linea, sizeof(linea), "<- IAC %s",
						              CommandName(b));
						Trace(linea);
					}
					m_state = ST_DATA;
				}
				break;

			case ST_OPTION:
			{
				char linea[96];
				std::snprintf(linea, sizeof(linea), "<- IAC %s %s",
				              CommandName(m_negCommand), OptionName(b));
				Trace(linea);

				/* La negociacion se atiende fuera de banda respecto del
				 * payload: se entrega lo acumulado para no reordenar. */
				FlushPayload();

				switch (m_negCommand)
				{
					case WILL: OnWill(b); break;
					case WONT: OnWont(b); break;
					case DO:   OnDo(b);   break;
					case DONT: OnDont(b); break;
				}
				m_state = ST_DATA;
				break;
			}

			case ST_SB:
				if (b == IAC) m_state = ST_SB_IAC;
				else          m_sb.push_back(b);
				break;

			case ST_SB_IAC:
				if (b == IAC)
				{
					m_sb.push_back(IAC);
					m_state = ST_SB;
				}
				else if (b == SE)
				{
					FlushPayload();
					OnSubnegotiation();
					m_state = ST_DATA;
				}
				else
				{
					/* IAC dentro de SB seguido de algo que no es SE ni IAC:
					 * el host no respeta el encuadre. Se registra y se sigue. */
					char linea[96];
					std::snprintf(linea, sizeof(linea),
					              "<- SB mal terminada: IAC %s", CommandName(b));
					Trace(linea);
					m_state = ST_SB;
				}
				break;
		}
	}

	FlushPayload();
}

/* ------------------------------------------------------------------ */
/*  Salida                                                             */
/* ------------------------------------------------------------------ */

void Tn6530Telnet::SendPayload(const unsigned char *data, int len)
{
	if (data == nullptr || len <= 0) return;

	std::vector<unsigned char> out;
	out.reserve((size_t)len + 8);

	for (int i = 0; i < len; i++)
	{
		out.push_back(data[i]);
		/* El 0xFF de los datos siempre se dobla, se haya negociado BINARY
		 * o no: es lo que lo distingue de un IAC de control. */
		if (data[i] == IAC) out.push_back(IAC);
	}

	if (m_policy.autoEndOfRecord && EndOfRecordActive())
	{
		out.push_back(IAC);
		out.push_back(EOR);
	}

	SendRawBytes(out.data(), (int)out.size());
}

void Tn6530Telnet::SendEndOfRecord()
{
	if (!EndOfRecordActive()) return;
	const unsigned char buf[2] = { IAC, EOR };
	SendRawBytes(buf, 2);
	Trace("-> IAC EOR");
}

/* ------------------------------------------------------------------ */

std::string Tn6530Telnet::Summary() const
{
	static const unsigned char interesantes[] = {
		OPT_BINARY, OPT_ECHO, OPT_SGA, OPT_END_OF_RECORD,
		OPT_TERMINAL_TYPE, OPT_NAWS, OPT_LINEMODE
	};

	/*  Se informa el ESTADO de cada lado, no el verbo que se mando. Decir
	 *  "el host: DO" confunde: DO es lo que dijimos NOSOTROS para habilitar
	 *  su lado. Lo que importa es si la opcion quedo activa y en quien. */
	std::string s = "estado negociado:\n";
	char linea[160];
	for (unsigned char opt : interesantes)
	{
		const char *nuestro = m_opt[opt].usKnown
		                    ? (m_opt[opt].us ? "activo" : "rechazado")
		                    : "sin negociar";
		const char *suyo    = m_opt[opt].himKnown
		                    ? (m_opt[opt].him ? "activo" : "rechazado")
		                    : "sin negociar";
		std::snprintf(linea, sizeof(linea),
		              "  %-16s nuestro lado: %-13s lado del host: %s\n",
		              OptionName(opt), nuestro, suyo);
		s += linea;
	}
	return s;
}

} // namespace tn6530
