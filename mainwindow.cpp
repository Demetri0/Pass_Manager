#include "mainwindow.h"
#include <QDebug>
#include "cryptfiledevice.h"
#include "passwordgenerator.h"


#include <db/querysmanager.h>
#include <QHeaderView>

#include <QSqlRecord>
#include <QSqlResult>
#include <QString>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QMessageBox>
#include <QFileDialog>

namespace Options {
    const QString LAST_FILE_PATH("LastFilePath");
    const QString BUFFER_SIZE("ReadWriteBufferSize");
    const QString PASSWORD_HASH_CYCLES("PasswordHashCycles");

    const QString LANGUAGE("Language");
}

namespace DefaultValues {
    const size_t BUFFER_SIZE(51200);
    const int    PASSWORD_HASH_CYCLES(3);
}

/*! \~russian
 * \brief Метод для переключения страничной навигации
 * \param index - индекс необходимой страницы из перечисления PageIndex
 * \return Всегда истинный!
 */
bool MainWindow::setPage(PageIndex::PageIndex index)
{
    ui.StackedWidget->setCurrentIndex( index );
    if( index == PageIndex::MAIN ){
        ui.MainMenuBar->setVisible( true );
        ui.MainToolBar->setVisible( true );
    }else
        if(    index == PageIndex::FIRST
               || index == PageIndex::OPEN_FILE
               || index == PageIndex::NEW_FILE
               || index == PageIndex::LOCK){

            ui.MainMenuBar->setVisible( false );
            ui.MainToolBar->setVisible( false );
        }
    return true;
}

QString MainWindow::getTmpDbPath()
{
    return QStandardPaths::writableLocation( QStandardPaths::TempLocation )
            + QDir::separator()
            + QString::number( QDateTime::currentMSecsSinceEpoch() );
}

bool MainWindow::connectToDatabase(const QString &filePath)
{
    bool success = false;
    success = _db.open( filePath );
    success = success && QuerysManager::createTables();

    return success;
}

/*!
 * \brief Конструктор Lego
 * \param parent
 */
MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent)
{
    ui.setupUi(this);
    setPage( PageIndex::FIRST );
}

void MainWindow::closeEvent(QCloseEvent *){
    _db.close();
    if( _existsChanges && hasSaveChanges() ){
        _dbFileProcessing->saveEncryptFile();
    }
    _existsChanges = false;
    _db.remove();
}

/*!
 * \brief Деструктор Lego
 */
MainWindow::~MainWindow(){
    //    _db.close();
}

/*!
 * \brief Обработчик клика на кнопку Создания нового файла
 * переключает страницу в стак-виджете на страницу создания нового файла
 */
void MainWindow::on_PButton_First_NewFile_clicked()
{
    _trayIcon.setIcon( QIcon( ":/images/logotip.png" ) );
    _trayIcon.show();
    _trayIcon.showMessage("Hello", "Welcome To PassMan");
    setPage( PageIndex::NEW_FILE );
}

/*!
 * \brief Обработчик клика на кнопку возврата со страницы открытия файла
 * Переключает страницу в стак-виджете на страницу приветствия
 */
void MainWindow::on_PButton_Open_Cancel_clicked()
{
    setPage( PageIndex::FIRST );
}

/*!
 * \brief Обработчки клика на кнопку возврата со страницы создания нового файла
 * Переключает страницу в стак-видежете на страницу приветствия
 */
void MainWindow::on_PButton_New_Cancel_clicked()
{
    setPage( PageIndex::FIRST );
}

/*!
 * \brief Обработчик клика на кнопку открытия файла
 * Переключает страницу в стак-виджете на страницу открытия файла
 */
void MainWindow::on_PButton_First_OpenFile_clicked()
{
    setPage( PageIndex::OPEN_FILE );
    QSettings cfg;
    ui.LineEdit_Open_FilePath->setText( cfg.value( Options::LAST_FILE_PATH, "" ).toString() );
}

/*!
 * \brief Метод для скрытия колонок в таблице
 */
void MainWindow::HideColumns()
{
    ui.TableView_Main_Records->hideColumn( _TableModel.fieldIndex(DataTable::Fields::id) );
    ui.TableView_Main_Records->hideColumn( _TableModel.fieldIndex(DataTable::Fields::Resource) );
    ui.TableView_Main_Records->hideColumn( _TableModel.fieldIndex(DataTable::Fields::PassGroup) );
    ui.TableView_Main_Records->hideColumn( _TableModel.fieldIndex(DataTable::Fields::Description) );
    ui.TableView_Main_Records->hideColumn( _TableModel.fieldIndex(DataTable::Fields::CreateTime) );
    ui.TableView_Main_Records->hideColumn( _TableModel.fieldIndex(DataTable::Fields::PassLifeTime) );
}

/*!
 * \brief Метод делает последнюю колонку адаптивной
 */
void MainWindow::setAdaptiveLastColumn()
{
    ui.TableView_Main_Records->horizontalHeader()->setStretchLastSection(true);
}

/*!
 * \brief Метод производит настройка главной таблице
 * Устанавливает модель
 * Соеденяет сигналы и слоты
 * Скрывает ненужные колонки
 * Настраивает растяжение колонок
 */
void MainWindow::setMainTable()
{
    _TableModel.setTable(DataTable::tableName);
    _TableModel.select();
    connect( &_TableModel, SIGNAL(dataChanged(QModelIndex,QModelIndex,QVector<int>)),
             ui.TableView_Main_Records, SLOT(dataChanged(QModelIndex,QModelIndex,QVector<int>)) );
    ui.TableView_Main_Records->setModel( &_TableModel );
    HideColumns();
    setAdaptiveLastColumn();
}

/*!
 * \brief Обработчик клика на кнопку истинного открытия файла
 * Собственно берёт и дерзко его открывает!!!
 */
void MainWindow::updateSectionsList()
{
    QString sql = "SELECT %1 FROM %2";
    sql = sql.arg(DataTable::Fields::PassGroup, DataTable::tableName);
    QSqlQuery query( sql );
    if( query.exec() ){
        QSet<QString> groups;
        while( query.next() ){
            groups += query.value(DataTable::Fields::PassGroup).toString();
            qApp->processEvents();
        }
        ui.ComboBox_Main_Section->addItems( groups.toList() );
        ui.ComboBox_Edit_Group->addItems( groups.toList() );
    }
}

void MainWindow::on_PButton_Open_OpenFile_clicked()
{
    QSettings cfg;

    size_t bufferSize     = cfg.value( Options::BUFFER_SIZE, 51200).toUInt();
    QString achtungDbPath = getTmpDbPath();
    QString encDbPath     = ui.LineEdit_Open_FilePath->text();
    QByteArray password   = getPasswordHash( ui.LineEdit_Open_Password->text() );
    QByteArray salt       = ui.LineEdit_Open_Password->text().toUtf8().toHex();

    if( _dbFileProcessing ){
        qWarning() << "Чёта ты не в тот район забрёл...";
        delete _dbFileProcessing;
    }
    _dbFileProcessing = new DbFileProcessing(achtungDbPath, encDbPath, password, salt, bufferSize);
    if( ! _dbFileProcessing->openEncryptFile() ){
        ui.Label_Open_Error->setText( tr("Cannot open encrypted file") );
        return;
    }

    _passwordHash = password;
    connectToDatabase(achtungDbPath);
    setPage( PageIndex::MAIN );
    setMainTable();
    updateSectionsList();

    cfg.setValue( Options::LAST_FILE_PATH , encDbPath );
}

/*!
 * \brief Обработчик действия выход из приложения
 * Берёт и закрывает приложение, ни спросив них*я!
 */
void MainWindow::on_actionExit_triggered()
{
    qApp->exit(0);
}

/*!
 * \brief Обработчик действия добавления новой записи
 * Переключает страницу на страницу добавления/редактирования новой записи
 * устанавливает режим редактирования в false для _data
 */
void MainWindow::on_actionNewRecord_triggered()
{
    setPage( PageIndex::EDIT );
    _data.setEditMode(false);
    _existsChanges = true;
}

/*!
 * \brief Обработчик клика на кнопку генерации пароля
 * Вычисляет тип пароля в соответствии с установленными флажками
 * Генерирует пароль пользуясь классом PasswordGenerator
 * Устанавливает пароль в 2 LineEdit
 */
void MainWindow::on_PushButton_Edit_GeneratePassword_clicked()
{
    int type = 0;
    int length = 0;

    length = ui.SpinBox_Edit_PasswordLenght->value();

    if( ui.CheckBox_Edit_Pas_ChType_Lower->isChecked() )
        type |= PasswordGenerator::Lower;
    if( ui.CheckBox_Edit_Pas_ChType_Upper->isChecked() )
        type |= PasswordGenerator::Upper;
    if( ui.CheckBox_Edit_Pas_ChType_Underline->isChecked() )
        type |= PasswordGenerator::Underline;
    if( ui.CheckBox_Edit_Pas_ChType_Minus->isChecked() )
        type |= PasswordGenerator::Minus;
    if( ui.CheckBox_Edit_Pas_ChType_Special->isChecked() )
        type |= PasswordGenerator::Special;
    if( ui.CheckBox_Edit_Pas_ChType_Numbers->isChecked() )
        type |= PasswordGenerator::Numbers;



    QString password = PasswordGenerator::getPassword(type, length);

    ui.ProgressBar_Edit_PasswordQuality->setValue( PasswordGenerator::entropy(password) );
    ui.LineEdit_Edit_Password->setText( password );
    ui.LineEdit_Edit_ConfirmPassword->setText( password );
}

/*!
 * \brief Метод обрабатывает ввод пароля в поле ввода
 * Вычисляет и устанавливает в виджет прогресс-бара значение
 * энтропии посредством класса PasswordGenerator
 * \param password - пароль, текст в LineEdit
 */
void MainWindow::on_LineEdit_Edit_Password_textEdited(const QString &password)
{
    ui.ProgressBar_Edit_PasswordQuality->setValue( PasswordGenerator::entropy(password) );
}

/*!
 * \brief Метод обрабатывает кнопку-переключатель отображения/скрытия пароля
 * Переключает метод отображения в полях ввода и подтверждения пароля
 * \param checked - состяние кнопки-переключателя
 */
void MainWindow::on_ToolButton_Edit_Toogle_Password_toggled(bool checked)
{
    if( checked ){
        ui.LineEdit_Edit_Password->setEchoMode( QLineEdit::Normal );
        ui.LineEdit_Edit_ConfirmPassword->setEchoMode( QLineEdit::Normal );
    }else{
        ui.LineEdit_Edit_Password->setEchoMode( QLineEdit::Password );
        ui.LineEdit_Edit_ConfirmPassword->setEchoMode( QLineEdit::Password );
    }
}

void MainWindow::setDataFromUi()
{
    _data.setGroup( ui.ComboBox_Edit_Group->currentText() );
    _data.setResource( ui.LineEdit_Edit_Title->text() );
    _data.setUrl( ui.LineEdit_Edit_Url->text() );
    _data.setLogin( ui.LineEdit_Edit_Login->text() );
    _data.setPassword( ui.LineEdit_Edit_Password->text() );
    _data.setAnswer( ui.LineEdit_Edit_Answer->text() );
    _data.setCreateTime( QString::number( QDateTime::currentMSecsSinceEpoch() ) );
    _data.setPassLifeTime( QString::number( ui.DateTimeEdit_Edit_Pas_PassOutdate->dateTime().toMSecsSinceEpoch() ) );
    _data.setPhone( ui.LineEdit_Edit_Phone->text() );
    _data.setDescription( ui.PlainTextEdit_Edit_Comment->toPlainText() );
}

bool MainWindow::hasSaveChanges()
{
    QMessageBox::StandardButton btn = QMessageBox::question(this,
                                                            tr("Exit"),
                                                            tr("Save changes?"),
                                                            QMessageBox::Yes | QMessageBox::No);

    return (btn == QMessageBox::Yes);
}

bool MainWindow::isFieldsComplete_New()
{
    if( ui.LineEdit_New_FilePath->text().isEmpty() ){
        ui.Label_New_Error->setText( tr("Enter a new file path") );
        return false;
    }

    if( ui.LineEdit_New_Password->text() != ui.LineEdit_New_ConfirmPassword->text() ){
        ui.Label_New_Error->setText( tr("Paswords is not identical") );
        return false;
    }

    if(    ui.LineEdit_New_Password->text().isEmpty()
        || ui.LineEdit_New_ConfirmPassword->text().isEmpty() ){
        ui.Label_New_Error->setText( tr("Empty password is not allowed") );
        return false;
    }

    ui.Label_New_Error->clear();
    return true;
}

bool MainWindow::isFieldsComplete_Open()
{
    QString filePath = ui.LineEdit_Open_FilePath->text();
    if( filePath.isEmpty() ){
        ui.Label_Open_Error->setText( tr("Choose a file") );
        return false;
    }

    if( ! QFile::exists( filePath )
       || QFileInfo( filePath ).isDir() ){

        ui.Label_Open_Error->setText( tr("Choosen file is not exists") );
        return false;
    }

    if( ui.LineEdit_Open_Password->text().isEmpty() ){
        ui.Label_Open_Error->setText( tr("Empty password is not allowed") );
        return false;
    }

    ui.Label_Open_Error->clear();
    return true;
}

/*!
 * \brief Метод обрабатывает клик на кнопку сохранения записи
 * заполняет объект данными и вызывает метод
 * Data::save() - для сохранения данных в БД
 * Переключает страницу на PageIndex::MAIN
 */
void MainWindow::on_PushButton_Edit_Save_clicked()
{
    setDataFromUi();

    _data.save();
    _TableModel.select();
    setPage( PageIndex::MAIN );
}

/*!
 * \brief Метод обрабатывает нажатие на кнопку возврата со страницы редактирования/добавления записи
 * переключает страницу в стак-виджете на PageIndex::MAIN
 */
void MainWindow::on_PushButton_Edit_Cancel_clicked()
{
    setPage( PageIndex::MAIN );
}

void MainWindow::on_actionCreateDatabase_triggered()
{
    _db.close();
    if( _existsChanges && hasSaveChanges() ){
        _dbFileProcessing->saveEncryptFile();
    }
    _db.remove();
    _existsChanges = false;
    setPage( PageIndex::NEW_FILE );
}

void MainWindow::on_actionOpenDatabase_triggered()
{
    _db.close();
    if( _existsChanges && hasSaveChanges() ){
        _dbFileProcessing->saveEncryptFile();
    }
    _db.remove();
    _existsChanges = false;
    setPage( PageIndex::OPEN_FILE );
}

void MainWindow::on_actionDeleteRecord_triggered()
{
    /// \todo Поименовать поля
    /// \warning Нихера не работает!!!
    //    model->setHeaderData(0, Qt::Horizontal, tr("Name"));
    //    model->setHeaderData(1, Qt::Horizontal, tr("Salary"));
    QModelIndexList sel = ui.TableView_Main_Records->selectionModel()->selectedRows();
    int first = sel.first().row();
    int count = sel.last().row()
            - sel.first().row();


    _TableModel.removeRows(first, count);
    _TableModel.submitAll();
}

QByteArray MainWindow::getPasswordHash(const QString &password)
{
    QSettings cfg;
    QByteArray passwordHash = password.toUtf8();
    bool hashCyclessCastToIntSuccess = false;
    int hashCycles = cfg.value( Options::PASSWORD_HASH_CYCLES, DefaultValues::PASSWORD_HASH_CYCLES ).toInt(&hashCyclessCastToIntSuccess);
    if( (! hashCyclessCastToIntSuccess) || (hashCycles < 1) ){
        hashCycles = DefaultValues::PASSWORD_HASH_CYCLES;
    }
    for(int i = 0; i < hashCycles; ++i){
        passwordHash   = QCryptographicHash::hash( passwordHash, QCryptographicHash::Md5 );
    }

    return passwordHash;
}

void MainWindow::on_PButton_New_CreateDatabase_clicked()
{
    QSettings cfg;
    QByteArray password      = getPasswordHash( ui.LineEdit_New_Password->text() );
    QByteArray salt          = ui.LineEdit_New_Password->text().toUtf8().toHex();
    QString    achtungDbPath = getTmpDbPath();
    QString    encDbPath     = ui.LineEdit_New_FilePath->text();
    size_t     bufferSize    = cfg.value( Options::BUFFER_SIZE, 51200).toUInt();

    if( _dbFileProcessing ){
        qWarning() << "Чёта ты не в тот район забрёл...";
        delete _dbFileProcessing;
    }
    _dbFileProcessing = new DbFileProcessing(achtungDbPath, encDbPath, password, salt, bufferSize);
    connectToDatabase( achtungDbPath );
    _passwordHash = password;
    setMainTable();
    setPage( PageIndex::MAIN );

    cfg.setValue( Options::LAST_FILE_PATH , encDbPath );
}

void MainWindow::on_actionSaveDatabase_triggered()
{
    _dbFileProcessing->saveEncryptFile();
    _existsChanges = false;
}

void MainWindow::on_actionEditRecord_triggered()
{
    /// \todo write code here
    /// edit record
    _existsChanges = true;
}

void MainWindow::on_TButton_New_ChooseFile_clicked()
{
    ui.LineEdit_New_FilePath->setText(
                QFileDialog::getSaveFileName(this,
                                             tr("Create new file"),
                                             QStandardPaths::writableLocation( QStandardPaths::HomeLocation )
                                             ) );
}

void MainWindow::on_TButton_Open_ChooseFile_clicked()
{
    ui.LineEdit_Open_FilePath->setText(
                QFileDialog::getOpenFileName(this,
                                             tr("Choose a file"),
                                             QStandardPaths::writableLocation( QStandardPaths::HomeLocation )
                                             ) );
}

void MainWindow::on_LineEdit_New_ConfirmPassword_textEdited(const QString &)
{
    ui.PButton_New_CreateDatabase->setEnabled( isFieldsComplete_New() );
}

void MainWindow::on_LineEdit_New_Password_textEdited(const QString &)
{
    ui.PButton_New_CreateDatabase->setEnabled( isFieldsComplete_New() );
}

void MainWindow::on_LineEdit_New_FilePath_textEdited(const QString &)
{
    ui.PButton_New_CreateDatabase->setEnabled( isFieldsComplete_New() );
}

void MainWindow::on_LineEdit_Open_Password_textEdited(const QString &)
{
    ui.PButton_Open_OpenFile->setEnabled( isFieldsComplete_Open() );
}

void MainWindow::on_LineEdit_Open_FilePath_textChanged(const QString &)
{
    ui.PButton_Open_OpenFile->setEnabled( isFieldsComplete_Open() );
}

void MainWindow::on_TButton_Open_ShowPassword_toggled(bool checked)
{
    QLineEdit::EchoMode mode;
    mode = (checked)? QLineEdit::Normal : QLineEdit::Password;

    ui.LineEdit_Open_Password->setEchoMode( mode );
}

void MainWindow::on_TButton_New_ShowPassword_toggled(bool checked)
{
    QLineEdit::EchoMode mode;
    mode = (checked)? QLineEdit::Normal : QLineEdit::Password;

    ui.LineEdit_New_Password->setEchoMode( mode );
    ui.LineEdit_New_ConfirmPassword->setEchoMode( mode );
}

void MainWindow::on_TButton_Lock_ShowPassword_toggled(bool checked)
{
    QLineEdit::EchoMode mode;
    mode = (checked)? QLineEdit::Normal : QLineEdit::Password;

    ui.LineEdit_Lock_Password->setEchoMode( mode );
}

void MainWindow::on_actionLock_triggered()
{
    setPage( PageIndex::LOCK );
}

void MainWindow::on_PButton_Lock_Unclock_clicked()
{
    QByteArray password = getPasswordHash( ui.LineEdit_Lock_Password->text() );
    if( password == _passwordHash ){
        setPage( PageIndex::MAIN );
    }else{
        ui.Label_Lock_Error->setText( tr("Password is uncorrect") );
    }
}
