#include <QApplication>
#include <QFont>

#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("工业网关上位机"));
    app.setOrganizationName(QStringLiteral("IndustrialGateway"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));

    MainWindow window;
    window.show();
    return app.exec();
}

