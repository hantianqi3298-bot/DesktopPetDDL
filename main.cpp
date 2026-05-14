#include "mainwindow.h"

#include <QApplication>

// 程序入口：创建 Qt 应用对象并显示桌宠主窗口。
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.show();

    return app.exec();
}