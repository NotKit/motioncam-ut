#include "camera_bridge.h"

#include <QGuiApplication>
#include <QQuickView>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGLES);
    fmt.setVersion(2, 0);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    app.setApplicationName("motioncam");
    app.setOrganizationName("thekit");

    qmlRegisterType<CameraBridge>("MotionCam", 1, 0, "CameraBridge");

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (view.status() == QQuickView::Error)
        return 1;
    view.show();

    return app.exec();
}
