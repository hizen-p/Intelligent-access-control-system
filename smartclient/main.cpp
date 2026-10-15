#include "widget.h"

#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Widget w;
    // 配置OpenGL
    QSurfaceFormat format;
    format.setVersion(2, 0);  // OpenGL ES 2.0
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setRenderableType(QSurfaceFormat::OpenGLES);
    QSurfaceFormat::setDefaultFormat(format);
    w.show();
    return a.exec();
}
