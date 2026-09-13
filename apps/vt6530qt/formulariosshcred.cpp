#include "formulariosshcred.h"
#include "ui_formulariosshcred.h"

#include <QLineEdit>

FormularioSSHcred::FormularioSSHcred(const QString &usuario, int intento,
                                     QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FormularioSSHcred)
{
    ui->setupUi(this);

    ui->usuario->setText(usuario);

    /*  Del segundo intento en adelante hay que decir que el anterior fallo, y
     *  decirlo en el titulo de la ventana, que es lo que se ve sin leer.
     *  Un dialogo identico al de la primera vez deja al usuario sin saber si
     *  la clave llego a mandarse o si le erro a la tecla de Aceptar.      */
    if (intento > 1)
    {
        setWindowTitle(tr("QT6530 - Clave incorrecta (intento %1)")
                           .arg(intento));
    }

    /*  El .ui ya trae echoMode = Password, pero se repite aca por escrito:
     *  si alguien reacomoda el formulario y lo pierde, la clave se dibuja en
     *  pantalla y nadie se da cuenta hasta que es tarde.                  */
    ui->clave->setEchoMode(QLineEdit::Password);
    ui->clave->setFocus();
}

FormularioSSHcred::~FormularioSSHcred()
{
    /*  Que no quede dando vueltas en el heap de Qt mas de lo necesario. No es
     *  blindaje -- QString copia y reasigna por su cuenta -- es higiene, la
     *  misma que hace el prompt de consola con su std::string.           */
    ui->clave->clear();
    delete ui;
}

bool FormularioSSHcred::Pedir(QWidget *padre, const QString &usuario,
                              int intento, QString *clave)
{
    if (clave == nullptr) return false;

    FormularioSSHcred dialogo(usuario, intento, padre);
    if (dialogo.exec() != QDialog::Accepted) return false;

    *clave = dialogo.ui->clave->text();
    return true;
}
