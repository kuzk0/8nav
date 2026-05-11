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

    connect(ui->typeListWidget,         &QListWidget::currentItemChanged,       this,   &MainWindow::onTypeCurrentItemChanged);
    connect(ui->cmdFilesListWidget,     &QListWidget::currentItemChanged,       this,   &MainWindow::onCmdCurrentItemChanged);

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
                                                    tr("Выберите CSV-файл в KOI8-R"),
                                                    startDir,
                                                    tr("CSV файлы (*.csv *.CSV)"));

    if (fileName.isEmpty())
        return;

    m_currentCsvPath = fileName;
    m_currentDirectory = QFileInfo(fileName).absolutePath();
    ui->filePathLabel->setText(m_currentCsvPath);

    updateWatcherDir();

    saveLastDirectory();
    loadAndDisplayTypes(fileName);

    // Очищаем список .cmd файлов, так как тип ещё не выбран
    ui->cmdFilesListWidget->clear();
}

void MainWindow::loadAndDisplayTypes(const QString &csvFilePath)
{
    ui->typeListWidget->clear();
    m_allTypes.clear();
    m_typeHasSubdir.clear();

    QFile file(csvFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this,
                             tr("Ошибка"),
                             tr("Не удалось открыть файл:\n%1").arg(csvFilePath));
        return;
    }

    QTextCodec *codec = QTextCodec::codecForName("KOI8-R");
    if (!codec) {
        QMessageBox::critical(this, tr("Ошибка"), tr("Кодировка KOI8-R не поддерживается."));
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

        // Первый столбец (разделитель ';')
        QString type = trimmed.section(';', 0, 0);
        if (type.startsWith('"') && type.endsWith('"'))
            type = type.mid(1, type.length() - 2);
        type = type.trimmed();

        if (!type.isEmpty())
            m_allTypes.insert(type);
    }


    ui->typeListWidget->clear();
    for (const QString &type : m_allTypes) {
        QListWidgetItem *item = new QListWidgetItem(type);
        // Пока не знаем, есть ли поддиректория – установим временно
        item->setData(Qt::UserRole, type);
        ui->typeListWidget->addItem(item);
    }


    ui->typeListWidget->sortItems();

    updateTypesActivity();

    statusBar()->showMessage(tr("Найдено %1 типов, из них активных: %2")
                                 .arg(m_allTypes.size())
                                 .arg(m_typeHasSubdir.values().count(true)));
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

void MainWindow::onTypeCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (current && (current->flags() & Qt::ItemIsSelectable)) {
        m_lastSelectedType = current->text();
        refreshCmdListForType(m_lastSelectedType);
    } else {
        m_lastSelectedType.clear();
        ui->cmdFilesListWidget->clear(); // очищаем список команд
    }
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
        QString displayName = fileName;
        if (displayName.endsWith(".cmd", Qt::CaseInsensitive))
            displayName.chop(4); // убираем .cmd для отображения
        QString relativePath = subdir + "/"
                               + fileName; // полный путь относительно m_currentDirectory

        QListWidgetItem *item = new QListWidgetItem(displayName);
        item->setData(Qt::UserRole, relativePath); // сохраняем "Type_something/script.cmd"
        ui->cmdFilesListWidget->addItem(item);
    }

    updateSaveButtonState();
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


void MainWindow::onSaveButtonClicked()
{
    saveLastDirectory();
    showTxtFileContent();
    statusBar()->showMessage(tr("Настройки сохранены"), 2000);
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
    ui->saveButton->setEnabled(typeSelected && cmdSelected);
}


void MainWindow::showTxtFileContent()
{
    QListWidgetItem *cmdItem = ui->cmdFilesListWidget->currentItem();
    if (!cmdItem) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не выбран .cmd файл"));
        return;
    }

    QString fullCmdName = cmdItem->data(Qt::UserRole).toString();
    if (fullCmdName.isEmpty()) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось определить имя файла"));
        return;
    }

    // Формируем имя .txt файла (заменяем расширение)
    QString txtRelativePath = fullCmdName;
    if (txtRelativePath.endsWith(".cmd", Qt::CaseInsensitive))
        txtRelativePath.chop(4);
    txtRelativePath += ".txt";

    // Строим абсолютный путь с использованием QDir и разделителя
    QDir baseDir(m_currentDirectory);
    QString absoluteTxtPath = baseDir.filePath(txtRelativePath);
    absoluteTxtPath = QDir::cleanPath(absoluteTxtPath); // убираем лишние . и ..
    absoluteTxtPath = QDir::toNativeSeparators(absoluteTxtPath); // системный разделитель

    QFile txtFile(absoluteTxtPath);
    if (!txtFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось открыть файл:\n%1\nОшибка: %2")
                                 .arg(absoluteTxtPath, txtFile.errorString()));
        return;
    }

    QByteArray data = txtFile.readAll();
    txtFile.close();

    // Определение кодировки (UTF-8, затем KOI8-R)
    QString text;
    QTextCodec *codec = QTextCodec::codecForName("UTF-8");
    text = codec->toUnicode(data);
    if (text.contains(QChar::ReplacementCharacter)) {
        codec = QTextCodec::codecForName("KOI8-R");
        if (codec)
            text = codec->toUnicode(data);
        else
            text = QString::fromUtf8(data);
    }

    // Диалог с текстом
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Содержимое %1").arg(txtRelativePath));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QTextEdit *textEdit = new QTextEdit(&dialog);
    textEdit->setPlainText(text);
    textEdit->setReadOnly(true);
    textEdit->setMinimumSize(600, 400);
    layout->addWidget(textEdit);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(buttonBox);

    dialog.exec();
}


void MainWindow::saveLastDirectory() const
{
    if (m_currentDirectory.isEmpty()) return;
    QSettings settings("8nav", "App");
    settings.setValue("lastDirectory", m_currentDirectory);

    if (m_currentCsvPath.isEmpty()) return;
    settings.setValue("lastCsvPath", m_currentCsvPath);
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
