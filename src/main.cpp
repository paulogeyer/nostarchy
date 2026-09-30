#include "AppController.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("nostarchy"));
    QCoreApplication::setApplicationName(QStringLiteral("nostarchy"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.loadFromModule("Nostarchy", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return QGuiApplication::exec();
}
