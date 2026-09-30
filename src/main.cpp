#include "AppController.h"
#include "LogStore.h"
#include "Theme.h"

#include <QFont>
#include <QFontInfo>
#include <QGuiApplication>
#include <QImageReader>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QStandardPaths>

class AvatarImageProvider : public QQuickImageProvider {
public:
    AvatarImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_dir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images")) {}

    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override {
        const QStringList extensions = {QStringLiteral(".jpg"), QStringLiteral(".png"), QStringLiteral(".webp"), QStringLiteral(".gif")};
        for (const QString& ext : extensions) {
            QImageReader reader(m_dir + QLatin1Char('/') + id + ext);
            if (!reader.canRead())
                continue;
            if (requested.isValid() && requested.width() > 0 && requested.height() > 0)
                reader.setScaledSize(QSize(requested.width(), requested.height()));
            const QImage image = reader.read();
            if (image.isNull())
                continue;
            if (size)
                *size = image.size();
            return image;
        }
        return {};
    }

private:
    QString m_dir;
};

int main(int argc, char* argv[]) {
    QLoggingCategory::setFilterRules(QStringLiteral("qt.text.font*=false\nqt.qpa.fonts=false"));
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("nostarchy"));
    QCoreApplication::setApplicationName(QStringLiteral("nostarchy"));
    LogStore::instance()->install();
    QFont font(QStringLiteral("JetBrainsMono Nerd Font"));
    if (!QFontInfo(font).family().contains(QStringLiteral("JetBrains"), Qt::CaseInsensitive))
        font = QFont(QStringLiteral("Noto Sans"));
    font.setStyleStrategy(QFont::PreferOutline);
    app.setFont(font);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    Theme theme;
    AppController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("app"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("logs"), LogStore::instance());
    engine.addImageProvider(QStringLiteral("avatars"), new AvatarImageProvider);
    engine.loadFromModule("Nostarchy", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    return QGuiApplication::exec();
}
