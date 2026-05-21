#include "camera_bridge.h"
#include "burst_preview_provider.h"

#include <QGuiApplication>
#include <QQmlEngine>
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

    // Register the burst image provider before loading QML so that
    // "image://burst/..." sources in PostProcessView.qml resolve immediately.
    auto* burstProvider = new BurstPreviewProvider();
    view.engine()->addImageProvider(QStringLiteral("burst"), burstProvider);

    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (view.status() == QQuickView::Error)
        return 1;

    // Wire the provider to the bridge once the root object exists.
    // CameraBridge is created by QML; find it via the root item.
    auto* root = view.rootObject();
    if (root) {
        auto* bridge = root->findChild<CameraBridge*>();
        if (bridge)
            bridge->setBurstPreviewProvider(burstProvider);
    }

    view.show();

    return app.exec();
}
