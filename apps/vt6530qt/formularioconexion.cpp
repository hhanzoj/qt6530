#include "formularioconexion.h"
#include "ui_formularioconexion.h"

#include <QDialogButtonBox>
#include <QIntValidator>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QStringList>

FormularioConexion::FormularioConexion(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FormularioConexion)
{
    ui->setupUi(this);

    /*  TELNET primero porque es el transporte por defecto del programa: lo
     *  que el formulario muestra al abrirse y lo que hace la linea de
     *  comandos sin banderas tienen que coincidir.                        */
    ui->protocolo->addItems(QStringList() << "TELNET" << "SSH");

    /*  El host acepta una IP o un nombre. El patron es el que escribiste;
     *  queda igual.                                                       */
    const QRegularExpression patronHost(
        R"(^(((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)|([a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?\.)*[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?)$)");
    ui->host->setValidator(
        new QRegularExpressionValidator(patronHost, this));

    ui->puerto->setValidator(new QIntValidator(1, 65535, this));

    /*  El largo del usuario sale de cli::kUsuarioLargoMaximo y no de un
     *  numero escrito aca: la forma de un usuario valido la decide la capa
     *  que tiene pruebas, no el formulario. La primera version tenia un
     *  maximo de 15 y no dejaba escribir 'nkaizen.jaracena', que tiene 16.
     *
     *  Aca solo se corta el largo mientras se tipea, que es cortesia; la
     *  validacion de verdad la hace cli::UsuarioValido() al aceptar.     */
    ui->usuario->setMaxLength(cli::kUsuarioLargoMaximo);

    /*  Aceptar no cierra solo: valida primero. Por eso la conexion
     *  accepted() -> accept() se saco del .ui.                            */
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &FormularioConexion::aceptar);

    /*  El puerto por defecto se aplica tambien al abrir, no solo cuando
     *  cambia el protocolo: si no, el campo arranca con lo que quedo escrito
     *  en el .ui y no con lo que dice el programa.                        */
    ponerPuertoPorDefecto();
    ajustarFilaDeUsuario();
    ui->puerto->setReadOnly(ui->checkBox->isChecked());
}

FormularioConexion::~FormularioConexion()
{
    delete ui;
}

void FormularioConexion::ponerPuertoPorDefecto()
{
    if (!ui->checkBox->isChecked()) return;

    const cli::Transporte t = (ui->protocolo->currentText() == "SSH")
                                  ? cli::Transporte::Ssh
                                  : cli::Transporte::Telnet;
    ui->puerto->setText(QString::number(cli::PuertoPorDefecto(t)));
}

void FormularioConexion::ajustarFilaDeUsuario()
{
    /*  Por telnet el usuario se teclea adentro de la sesion, en el logon de
     *  Guardian; pedirlo aca seria pedir algo que no se usa. Por ssh viaja en
     *  la autenticacion y tiene que estar antes de abrir el canal.        */
    const bool esSsh = (ui->protocolo->currentText() == "SSH");
    ui->usuario->setEnabled(esSsh);
    ui->label_4->setEnabled(esSsh);
}

void FormularioConexion::on_protocolo_currentIndexChanged(int index)
{
    (void)index;
    ponerPuertoPorDefecto();
    ajustarFilaDeUsuario();
}

void FormularioConexion::on_checkBox_toggled(bool marcado)
{
    /*  La version anterior decia "if (Qt::Unchecked)", que es "if (0)":
     *  siempre falso, siempre caia al else, y el puerto quedaba de solo
     *  lectura para siempre. Compilaba limpio, que es lo que lo hacia
     *  dificil de ver.                                                    */
    ui->puerto->setReadOnly(marcado);
    if (marcado) ponerPuertoPorDefecto();
}

void FormularioConexion::aceptar()
{
    cli::Opciones o;

    o.transporte = (ui->protocolo->currentText() == "SSH")
                       ? cli::Transporte::Ssh
                       : cli::Transporte::Telnet;
    o.host    = ui->host->text().trimmed().toStdString();
    o.usuario = ui->usuario->text().trimmed().toStdString();

    /*  0 es el centinela que AplicarDefectos entiende como "poneme el que
     *  corresponda". Si la casilla esta marcada no se lee el campo, asi que
     *  el defecto lo decide una sola vez cli::PuertoPorDefecto().        */
    o.puerto = ui->checkBox->isChecked() ? 0 : ui->puerto->text().toInt();

    if (o.host.empty())
    {
        QMessageBox::warning(this, windowTitle(),
                             tr("Falta el host."));
        ui->host->setFocus();
        return;
    }

    if (o.transporte == cli::Transporte::Ssh && !o.usuario.empty() &&
        !cli::UsuarioValido(o.usuario))
    {
        QMessageBox::warning(
            this, windowTitle(),
            tr("El usuario no tiene una forma que se pueda mandar.\n\n"
               "Se aceptan letras, digitos, punto, guion y guion bajo, "
               "hasta %1 caracteres.")
                .arg(cli::kUsuarioLargoMaximo));
        ui->usuario->setFocus();
        return;
    }

    /*  LA linea que no se puede olvidar. Los defectos de ssh salieron de una
     *  sesion que funciona contra el host y costaron una semana; sin esto el
     *  formulario arma una sesion que se queda muda y no hay nada roto a la
     *  vista. Es la misma llamada que hace el parseo de la linea de
     *  comandos.                                                          */
    cli::AplicarDefectos(&o);

    if (!o.error.empty())
    {
        QMessageBox::warning(this, windowTitle(),
                             QString::fromStdString(o.error));
        return;
    }

    m_opciones = o;
    accept();
}
