#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QFile>
#include <QTextCodec>
#include <QMessageBox>
#include <QDir>
#include <QListWidgetItem>
#include <QSettings>
#include <QTextEdit>
#include <QProcess>
#include <QStandardPaths>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_watcher(new QFileSystemWatcher(this))
{
    ui->setupUi(this);

    if (!m_watcher) {
        qDebug() << "ERROR: m_watcher is null";
        m_watcher = new QFileSystemWatcher(this);
    }
    connect(m_watcher,                  &QFileSystemWatcher::directoryChanged,  this,   &MainWindow::onDirectoryChanged);

    connect(ui->selectCsvButton,        &QPushButton::clicked,                  this,   &MainWindow::onSelectCsvFile);
    connect(ui->selectPhpButton,        &QPushButton::clicked,                  this,   &MainWindow::onSelectPhpFile);

    connect(ui->typeListWidget,         &QListWidget::currentItemChanged,       this,   &MainWindow::onTypeCurrentItemChanged);
    connect(ui->cmdFilesListWidget,     &QListWidget::currentItemChanged,       this,   &MainWindow::onCmdCurrentItemChanged);
    connect(ui->namesListWidget,        &QListWidget::currentItemChanged,       this,   &MainWindow::onNameCurrentItemChanged);

    ui->saveButton->setEnabled(false);
    connect(ui->saveButton,             &QPushButton::clicked,                  this,   &MainWindow::onSaveButtonClicked);

    loadLastDirectory();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onSelectCsvFile()
{
    QString startDir = m_currentDirectory.isEmpty() ? QDir::currentPath() : m_currentDirectory;
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Choose CSV-file KOI8-R"),
                                                    startDir,
                                                    tr("CSV files (*.csv *.CSV)"));

    if (fileName.isEmpty())
        return;

    m_currentCsvPath = fileName;
    m_currentDirectory = QFileInfo(fileName).absolutePath();
    ui->filePathLabel->setText(m_currentCsvPath);
    findPhpScriptInDirectory();
    updateWatcherDir();

    saveLastDirectory();
    loadAndDisplayTypes(fileName);

    ui->cmdFilesListWidget->clear();

    ui->numSpinBox->setValue(0);
}


void MainWindow::onSelectPhpFile()
{
    QString startDir = m_currentDirectory.isEmpty() ? QDir::currentPath() : m_currentDirectory;
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    tr("Choose PHP-file"),
                                                    startDir,
                                                    tr("PHP files (*.php *.PHP)"));

    if (fileName.isEmpty())
        return;

    m_currentPhpScriptPath = fileName;
    ui->phpPathLabel->setText(m_currentPhpScriptPath);


}


void MainWindow::loadAndDisplayTypes(const QString &csvFilePath)
{
    ui->typeListWidget->clear();
    m_allTypes.clear();
    m_typeHasSubdir.clear();
    m_csvRows.clear();
    m_rowsByType.clear();

    QFile file(csvFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось открыть файл:\n%1").arg(csvFilePath));
        return;
    }

    QTextCodec *codec = QTextCodec::codecForName("KOI8-R");
    if (!codec) {
        QMessageBox::critical(this, tr("Ошибка"), tr("Кодек KOI8-R не поддерживается."));
        file.close();
        return;
    }

    QByteArray rawData = file.readAll();
    file.close();

    QString decoded = codec->toUnicode(rawData);
    QStringList lines = decoded.split('\n', Qt::SkipEmptyParts);

    if (lines.isEmpty()) {
        ui->typeListWidget->addItem(tr("(файл пуст)"));
        return;
    }

    bool firstLine = true;
    for (const QString &line : lines) {
        if (firstLine) {
            firstLine = false;
            continue; // пропускаем заголовок
        }
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;

        // Разделитель ';'
        QStringList columns = trimmed.split(';');
        if (columns.size() < 3) continue; // нужно минимум три столбца: TYPE;NAME;FULLNAME

        QString type = columns[0].trimmed();
        QString name = columns[1].trimmed();
        QString fullName = columns[2].trimmed();

        if (type.isEmpty()) continue;

        // Сохраняем строку для дальнейшего использования
        CsvRow row{type, name, fullName};
        m_csvRows.append(row);
        m_rowsByType[type].append(row);
        m_allTypes.insert(type);
    }

    // Заполняем typeListWidget
    for (const QString &type : m_allTypes) {
        QListWidgetItem *item = new QListWidgetItem(type);
        item->setData(Qt::UserRole, type);
        ui->typeListWidget->addItem(item);
    }

    ui->typeListWidget->sortItems();
    updateTypesActivity();   // проверка существования поддиректорий

    statusBar()->showMessage(tr("Найдено %1 типов (активных: %2), строк данных: %3")
                                 .arg(m_allTypes.size())
                                 .arg(m_typeHasSubdir.values().count(true))
                                 .arg(m_csvRows.size()));
}


bool MainWindow::hasSubdirWithPrefix(const QString &typeValue) const
{
    if (m_currentDirectory.isEmpty())
        return false;
    QDir dir(m_currentDirectory);
    // Фильтр: только директории, начинающиеся с typeValue
    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subdirs) {
        if (sub.startsWith(typeValue + "_") || sub.compare(typeValue) == 0)
            return true;
    }
    return false;
}


QStringList MainWindow::findSubdirsByTypePrefix(const QString &typeValue) const
{
    QStringList result;
    if (m_currentDirectory.isEmpty())
        return result;
    QDir dir(m_currentDirectory);
    QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : subdirs) {
        if (sub.startsWith(typeValue + "_") || sub.compare(typeValue) == 0)
            result << sub;
    }
    return result;
}


void MainWindow::findPhpScriptInDirectory()
{
    QDir dir(m_currentDirectory);
    QStringList filters;
    filters << "*.php" << "*.PHP";
    QStringList phpFiles = dir.entryList(filters, QDir::Files);

    if (phpFiles.isEmpty()) {
        m_currentPhpScriptPath.clear();
        statusBar()->showMessage(tr("В каталоге не найден .php файл для запуска скрипта"), 5000);
        if (ui->saveButton)
            ui->saveButton->setEnabled(false);
    } else {
        phpFiles.sort(); // сортируем для предсказуемого выбора первого
        m_currentPhpScriptPath = dir.absoluteFilePath(phpFiles.first());
        ui->phpPathLabel->setText(m_currentPhpScriptPath);
        statusBar()->showMessage(tr("Найден PHP-скрипт: %1").arg(m_currentPhpScriptPath), 3000);
        if (ui->saveButton)
            ui->saveButton->setEnabled(true);
    }
}


void MainWindow::onTypeCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current) return;
    if (!(current->flags() & Qt::ItemIsSelectable)) {
        statusBar()->showMessage(tr("Тип '%1' недоступен (нет поддиректории).").arg(current->text()), 2000);
        ui->namesListWidget->clear();
        ui->cmdFilesListWidget->clear();
        return;
    }

    QString typeValue = current->text();
    m_lastSelectedType = typeValue;

    refreshNamesListForType(typeValue);   // новый список
    refreshCmdListForType(typeValue);     // существующий список .cmd файлов

    updateSaveButtonState();
}


void MainWindow::refreshCmdListForType(const QString &typeValue)
{
    ui->cmdFilesListWidget->clear();
    if (!m_typeHasSubdir.value(typeValue, false)) {
        return;
    }

    QStringList subdirs = findSubdirsByTypePrefix(typeValue);
    // Храним пары (поддиректория, имя_файла)
    QList<QPair<QString, QString>> cmdFiles;

    for (const QString &subdir : subdirs) {
        QDir subPath(m_currentDirectory + QDir::separator() + subdir);
        QStringList filters = {"*.cmd", "*.CMD"};
        QFileInfoList files = subPath.entryInfoList(filters, QDir::Files);
        for (const QFileInfo &fi : files) {
            cmdFiles.append(qMakePair(subdir, fi.fileName()));
        }
    }

    if (cmdFiles.isEmpty()) {
        return;
    }

    // Сортируем по имени файла
    std::sort(cmdFiles.begin(),
              cmdFiles.end(),
              [](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
                  return a.second < b.second;
              });

    for (const auto &pair : cmdFiles) {
        QString subdir = pair.first;
        QString fileName = pair.second;
        QString relativePath = subdir + "/"
                               + fileName; // полный путь относительно m_currentDirectory

        QListWidgetItem *item = new QListWidgetItem(fileName);
        item->setData(Qt::UserRole, relativePath); // сохраняем "Type_something/script.cmd"
        ui->cmdFilesListWidget->addItem(item);
    }

    updateSaveButtonState();
}


void MainWindow::refreshNamesListForType(const QString &typeValue)
{
    ui->namesListWidget->clear();
    QList<CsvRow> rows = m_rowsByType.value(typeValue);
    for (const CsvRow &row : rows) {
        QString displayText = QString("%1 (%2)").arg(row.name, row.fullName);
        QListWidgetItem *item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, row.name);   // храним только "имя"
        ui->namesListWidget->addItem(item);
    }
}


void MainWindow::onCmdCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (current) {
        // Сохраняем отображаемое имя без расширения
        m_lastSelectedCmd = current->text();
    } else {
        m_lastSelectedCmd.clear();
    }
    updateSaveButtonState();
}

void MainWindow::onNameCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (current) {
        // Сохраняем отображаемое имя без расширения
        m_lastSelectedName = current->text();
    } else {
        m_lastSelectedName.clear();
    }
    updateSaveButtonState();
}


void MainWindow::onSaveButtonClicked()
{
    saveLastDirectory();
    callPhpScript();
    statusBar()->showMessage(tr("Вызов сценария %1").arg(m_currentPhpScriptPath), 2000);
}


void MainWindow::updateSaveButtonState()
{
    bool typeSelected = false;
    if (ui->typeListWidget->currentItem()) {
        QListWidgetItem *typeItem = ui->typeListWidget->currentItem();
        if (typeItem->flags() & Qt::ItemIsSelectable) {
            typeSelected = true;
        }
    }

    bool cmdSelected = (ui->cmdFilesListWidget->currentItem() != nullptr);
    bool nameSelected = (ui->namesListWidget->currentItem() != nullptr);

    ui->saveButton->setEnabled(typeSelected && cmdSelected && nameSelected);
}


void MainWindow::saveLastDirectory() const
{
    if (m_currentDirectory.isEmpty()) return;
    QSettings settings("8nav", "App");
    settings.setValue("lastDirectory", m_currentDirectory);

    if (!m_currentCsvPath.isEmpty()) {
        settings.setValue("lastCsvPath", m_currentCsvPath);
    }

    if (!m_currentPhpScriptPath.isEmpty()) {
        settings.setValue("lastPhpScriptPath", m_currentPhpScriptPath);
    }
}


void MainWindow::loadLastDirectory()
{
    QSettings settings("8nav", "App");
    m_currentDirectory = settings.value("lastDirectory").toString();
    if (!m_currentDirectory.isEmpty() && QDir(m_currentDirectory).exists()) {
        statusBar()->showMessage(tr("Последняя рабочая директория: %1").arg(m_currentDirectory), 3000);
        updateWatcherDir();
    } else {
        m_currentDirectory.clear();
    }

    m_currentCsvPath = settings.value("lastCsvPath").toString();
    if (!m_currentCsvPath.isEmpty()) {
        ui->filePathLabel->setText(m_currentCsvPath);
        loadAndDisplayTypes(m_currentCsvPath);
        ui->cmdFilesListWidget->clear();

        statusBar()->showMessage(tr("Последний рабочий файл: %1").arg(m_currentCsvPath), 3000);
    } else {
        m_currentCsvPath.clear();
    }

    m_currentPhpScriptPath = settings.value("lastPhpScriptPath").toString();
    if (!m_currentPhpScriptPath.isEmpty() && QFileInfo(m_currentPhpScriptPath).exists()) {
        ui->phpPathLabel->setText(m_currentPhpScriptPath);

        statusBar()->showMessage(tr("Последний рабочий php - скрипт: %1").arg(m_currentPhpScriptPath), 3000);
    } else {
        m_currentPhpScriptPath.clear();
        findPhpScriptInDirectory();
    }
}


void MainWindow::updateTypesActivity()
{
    if (m_allTypes.isEmpty()) return;

    for (int i = 0; i < ui->typeListWidget->count(); ++i) {
        QListWidgetItem *item = ui->typeListWidget->item(i);
        QString type = item->text();
        bool exists = hasSubdirWithPrefix(type);
        m_typeHasSubdir[type] = exists;

        if (exists) {
            item->setFlags(item->flags() | Qt::ItemIsSelectable);
            item->setForeground(Qt::black);
        } else {
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            item->setForeground(Qt::gray);
        }
    }

    // Если текущий выбранный тип стал неактивным, сбрасываем выбор и очищаем второй список
    QListWidgetItem *currentTypeItem = ui->typeListWidget->currentItem();
    if (currentTypeItem && !(currentTypeItem->flags() & Qt::ItemIsSelectable)) {
        ui->typeListWidget->setCurrentItem(nullptr);
        ui->cmdFilesListWidget->clear();
        m_lastSelectedType.clear();
        m_lastSelectedCmd.clear();
        ui->saveButton->setEnabled(false);
    } else if (currentTypeItem && (currentTypeItem->flags() & Qt::ItemIsSelectable)) {
        // Обновляем список .cmd для текущего типа (на случай если поддиректории изменились)
        refreshCmdListForType(currentTypeItem->text());
    }

    updateSaveButtonState();
}


void MainWindow::onDirectoryChanged(const QString &path)
{
    Q_UNUSED(path);
    // При любых изменениях в родительской папке перепроверяем наличие поддиректорий
    updateTypesActivity();
}


void MainWindow::updateWatcherDir(){
    if (m_watcher) {
        // Убираем старую директорию, если есть
        if (!m_watcher->directories().isEmpty())
            m_watcher->removePaths(m_watcher->directories());
        // Добавляем новую
        if (!m_watcher->addPath(m_currentDirectory))
            qWarning() << "Failed to watch directory:" << m_currentDirectory;
        else
            qDebug() << "Watching directory:" << m_currentDirectory;
    }
}


void MainWindow::callPhpScript()
{

    if (QStandardPaths::findExecutable("php").isEmpty()) {
        QMessageBox::critical(this, tr("Error"), tr("PHP не установлен или не найден в PATH."));
        return;
    }


    QString terminal;

    if (!QStandardPaths::findExecutable("xterm").isEmpty())
        terminal = "xterm";
    else {
        QMessageBox::warning(this, tr("Error"), tr("Не найден терминал (xterm)."));
        return;
    }


    // 1. Получаем выбранный элемент из первого списка (например, typeListWidget)
    QListWidgetItem *selectedItem = ui->typeListWidget->currentItem();
    if (!selectedItem) {
        QMessageBox::warning(this, tr("Error"), tr("Не выбран элемент в списке типов."));
        return;
    }
    QString typeValue = selectedItem->text();

    // 2. Получаем выбранный элемент из второго списка (например, cmdFilesListWidget)
    QListWidgetItem *selectedName = ui->namesListWidget->currentItem();

    if (!selectedName) {
        QMessageBox::warning(this, tr("Error"), tr("Не выбрано имя объекта."));
        return;
    }

    QString nameValue = selectedName->data(Qt::UserRole).toString();
    if (nameValue.isEmpty())
        nameValue = selectedName->text();  // fallback


    // 3. Получаем выбранный элемент из второго списка (например, cmdFilesListWidget)
    QListWidgetItem *selectedCmd = ui->cmdFilesListWidget->currentItem();
    if (!selectedCmd) {
        QMessageBox::warning(this, tr("Error"), tr("Не выбран .cmd файл."));
        return;
    }

    QString cmdValue = selectedCmd->text();

    // 4. Получаем значение из спинбокса
    int spinValue = ui->numSpinBox->value();
    if(!spinValue) {
        QMessageBox::warning(this, tr("Error"), tr("Номер не может быть 0"));
        return;
    }

    // 5. Формируем аргументы для PHP-скрипта
    QStringList arguments;
    arguments << nameValue;
    arguments << typeValue;
    arguments << cmdValue;
    arguments << QString::number(spinValue);


    QString message = tr("Запуск PHP-скрипта со следующими аргументами:\n\n"
                         "Скрипт: %1\n"
                         "Имя: %2\n"
                         "Тип: %3\n"
                         "Команда: %4\n"
                         "Номер: %5\n\n"
                         "Продолжить?").arg(m_currentPhpScriptPath, nameValue, typeValue, cmdValue).arg(spinValue);

    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr("Run accepting"),
        message,
        QMessageBox::Yes | QMessageBox::No
        );

    if (reply != QMessageBox::Yes) {
        statusBar()->showMessage(tr("Запуск скрипта отменён"), 3000);
        return;
    }

    QString phpCmd = QString("php \"%1\" %2")
                         .arg(m_currentPhpScriptPath)
                         .arg(arguments.join(' '));


    QStringList terminalArgs;
    terminalArgs << "-e" << "bash" << "-c" << phpCmd + "; exec bash";

    // Запускаем терминал отдельно (detached)
    if (!QProcess::startDetached(terminal, terminalArgs)) {
        QMessageBox::critical(this, tr("Error"), tr("Не удалось запустить терминал."));
    }

}
