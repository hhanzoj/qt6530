#include "PoliticaSsh.h"

namespace cli {

ssh6530::Politica PoliticaSshDesde(const Opciones &op)
{
	ssh6530::Politica p;

	p.usuario     = op.usuario;
	p.comando     = op.comando;
	p.claveArchivo = op.identidad;
	p.ptyCrudo    = op.ptyCrudo;
	p.usarShell   = op.shell;
	p.knownHosts  = op.knownHosts;

	/*  Quince segundos mirando una ventana que no aparece se parecen mucho a
	 *  un programa que no arranco. -timeout lo acorta.                    */
	if (op.timeoutSegundos > 0)
		p.timeoutConexionMs = op.timeoutSegundos * 1000;

	/*  El TERM del pty-req. -tipo-pty y -tipo son dos campos distintos, en
	 *  dos protocolos distintos: TN6530-8 en mayusculas en el pty-req de
	 *  ssh, tn6530-8 en minusculas en la subnegociacion de telnet. Si no se
	 *  dio -tipo-pty se usa el otro, que es lo que hacia antes de que se
	 *  supiera que eran dos cosas.                                        */
	p.terminalType = op.tipoPty.empty() ? op.tipoTerminal : op.tipoPty;

	switch (op.hostNuevo)
	{
		case HostNuevo::Aceptar:
			p.hostDesconocido = ssh6530::HostDesconocido::Aceptar;
			break;
		case HostNuevo::Rechazar:
			p.hostDesconocido = ssh6530::HostDesconocido::Rechazar;
			break;
		case HostNuevo::Preguntar:
		default:
			p.hostDesconocido = ssh6530::HostDesconocido::Preguntar;
			break;
	}

	return p;
}

} // namespace cli
