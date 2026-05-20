#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    qputenv("LC_ALL", "ru_RU.UTF-8");
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return a.exec();
}
