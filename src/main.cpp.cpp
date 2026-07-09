#include <QApplication>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("MySunRemote");
    app.setOrganizationName("MySunRemote");

    MainWindow window;
    window.show();

    return app.exec();
}