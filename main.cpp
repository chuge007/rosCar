#include "mainwindow.h"
#include <QApplication>
#include <QLoggingCategory>


int main(int argc, char *argv[])
{
    QLoggingCategory::setFilterRules("qt.modbus.lowlevel=false");



    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
