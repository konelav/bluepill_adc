#include "mainwindow.h"
#include <QApplication>

#include <libusb-1.0/libusb.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    struct libusb_context * ctx = NULL;
    if (libusb_init(&ctx))
    {
        qDebug("Can't start app: libusb_init() failed");
        return 1;
    }

    MainWindow w(ctx);
    w.show();

    return a.exec();
}
