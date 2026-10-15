#include "widget.h"

#include <QApplication>
#include <form_phonecall.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Widget w;
//    Form_phonecall f;
//    f.show();
    w.show();
    return a.exec();
}
