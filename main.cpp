#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    a.setStyle("Fusion");

    MainWindow w;
    w.setWindowTitle("Server Control");
    w.showMaximized();

    return a.exec();
}
