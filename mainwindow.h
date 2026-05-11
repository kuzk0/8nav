#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSet>
#include <QMap>
#include <QFileSystemWatcher>


QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class QListWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    private slots:
    void onSelectCsvFile();
    void onSaveButtonClicked();

    void onTypeCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onCmdCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);

    void onDirectoryChanged(const QString &path);
    void updateTypesActivity(); // обновляет активность всех тип

    private:
    Ui::MainWindow *ui;
    QString m_currentCsvPath;
    QString m_currentDirectory; // каталог, где лежит CSV
    QString m_lastSelectedType; // выбранный TYPE
    QString m_lastSelectedCmd; // отображаемое имя .cmd файла (без расширения)
    QSet<QString> m_allTypes; // все уникальные TYPE из CSV
    QMap<QString, bool> m_typeHasSubdir; // существует ли поддиректория
    QStringList findSubdirsByTypePrefix(const QString &typeValue) const;
    QStringList findCmdFilesInSubdirs(const QStringList &subdirs) const;
    bool hasSubdirWithPrefix(const QString &typeValue) const;

    void updateSaveButtonState();
    void showTxtFileContent();

    void loadAndDisplayTypes(const QString &csvFilePath);
    void refreshCmdListForType(const QString &typeValue);
    bool doesSubdirExist(const QString &typeValue) const;
    QStringList findCmdFilesInSubdir(const QString &typeValue) const;

    void saveLastDirectory() const;
    void loadLastDirectory();

    QFileSystemWatcher *m_watcher;
    void updateWatcherDir();

};

#endif // MAINWINDOW_H
