/*
 *  FormularioConexion -- el dialogo que reemplaza a la linea de comandos.
 *
 *  Sale cuando el programa se lanza sin argumentos, al estilo de PuTTY.
 *
 *  LO QUE DEVUELVE ES UN cli::Opciones, Y NO UNOS QString
 *
 *  Esto no es un detalle de estilo. Hay un solo camino para configurar una
 *  sesion -- el struct cli::Opciones -- y por ahi pasan tanto la linea de
 *  comandos como este formulario. Si el dialogo armara la sesion por su
 *  cuenta, habria dos formas de configurarla y se irian separando; ya paso en
 *  este proyecto con la politica de ssh, que estaba copiada en main.cpp y en
 *  sonda_ssh.cpp y las copias dejaron de coincidir.
 *
 *  Por eso Aceptar() llama a cli::AplicarDefectos(), igual que el parseo: los
 *  defectos de ssh (puerto 22, pty TN6530-8, shell, LINEMODE ofrecido) son lo
 *  que hace que el host arranque TACL en una ventana 6530, y un formulario
 *  que los saltee vuelve a la sesion muda sin que nada parezca roto.
 *
 *  LA CLAVE NO ESTA ACA
 *
 *  A proposito. La clave no es configuracion: es un secreto de un solo uso
 *  que hace falta una vez, adentro de Connect(), y que no tiene por que vivir
 *  en un struct que se copia y se imprime en las trazas. La pide el
 *  transporte cuando la necesita, por onPedirClave, y la contesta
 *  FormularioSSHcred. Ademas asi no se pide cuando no hace falta: si andan la
 *  clave publica o el agente, nadie tipea nada.
 *
 *  El USUARIO si esta aca, porque si es configuracion: por ssh viaja en la
 *  autenticacion y tiene que estar antes de abrir el canal.
 */
#ifndef FORMULARIOCONEXION_H
#define FORMULARIOCONEXION_H

#include "../Opciones.h"

#include <QDialog>

namespace Ui {
class FormularioConexion;
}

class FormularioConexion : public QDialog
{
    Q_OBJECT

public:
    explicit FormularioConexion(QWidget *parent = nullptr);
    ~FormularioConexion();

    /** Lo que se cargo, ya con los defectos aplicados. Solo vale si exec()
     *  devolvio Accepted. */
    const cli::Opciones &opciones() const { return m_opciones; }

private slots:
    /*  Se llaman por nombre desde el .ui (connectSlotsByName). */
    void on_protocolo_currentIndexChanged(int index);
    void on_checkBox_toggled(bool marcado);

    /** Reemplaza a accept(): valida y recien entonces cierra. */
    void aceptar();

private:
    /** El puerto que corresponde al protocolo elegido. */
    void ponerPuertoPorDefecto();

    /** Muestra u oculta la fila del usuario segun el protocolo. */
    void ajustarFilaDeUsuario();

    Ui::FormularioConexion *ui;
    cli::Opciones m_opciones;
};

#endif // FORMULARIOCONEXION_H
