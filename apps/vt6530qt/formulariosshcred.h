/*
 *  FormularioSSHcred -- la clave, cuando el transporte la pide.
 *
 *  NO se muestra antes de conectar, y eso es el punto. El transporte prueba
 *  primero la clave privada de -i, despues el agente, y solo si ninguno sirve
 *  llama a onPedirClave. Un dialogo que salga antes hace tipear una clave que
 *  en muchos casos no se usa nunca.
 *
 *  Tampoco pide el usuario: ese es configuracion y ya vino del formulario de
 *  conexion o de la linea de comandos. Aca se muestra de solo lectura, para
 *  que se vea de quien es la clave que se esta pidiendo.
 *
 *  La clave se devuelve por parametro y no se guarda en un miembro: mientras
 *  menos lugares la tengan, mejor. Al cerrarse, el campo se limpia.
 */
#ifndef FORMULARIOSSHCRED_H
#define FORMULARIOSSHCRED_H

#include <QDialog>
#include <QString>

namespace Ui {
class FormularioSSHcred;
}

class FormularioSSHcred : public QDialog
{
    Q_OBJECT

public:
    explicit FormularioSSHcred(const QString &usuario, int intento,
                               QWidget *parent = nullptr);
    ~FormularioSSHcred();

    /**
     *  Pide la clave y devuelve true si la dieron.
     *
     *  Es lo que se le pasa a Ssh6530Session::setPasswordCallback, y tiene
     *  la misma forma que el prompt de consola: el transporte pregunta, esto
     *  contesta. Devolver false cancela la autenticacion.
     *
     *  intento empieza en 1. Del segundo en adelante el dialogo dice que la
     *  clave anterior no sirvio: volver a mostrar el mismo cartel, igual que
     *  la primera vez, deja al usuario sin saber si llego a mandarse algo.
     */
    static bool Pedir(QWidget *padre, const QString &usuario, int intento,
                      QString *clave);

private:
    Ui::FormularioSSHcred *ui;
};

#endif // FORMULARIOSSHCRED_H
