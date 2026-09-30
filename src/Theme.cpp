#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {

QString themeDir() {
    const QString state = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    const QString home = QDir::homePath();
    const QStringList candidates = {
        home + QStringLiteral("/.local/state/omarchy/current/theme"),
        state + QStringLiteral("/omarchy/current/theme"),
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path + QStringLiteral("/colors.toml")))
            return path;
    }
    return candidates.first();
}

}

qreal Theme::hyprOption(const QString& name, qreal fallback) {
    QProcess process;
    process.start(QStringLiteral("hyprctl"), {QStringLiteral("getoption"), name});
    if (!process.waitForFinished(800))
        return fallback;
    const auto match = QRegularExpression(QStringLiteral("float:\\s*([0-9.]+)")).match(QString::fromUtf8(process.readAllStandardOutput()));
    if (!match.hasMatch())
        return fallback;
    bool ok = false;
    const qreal value = match.captured(1).toDouble(&ok);
    return ok && value > 0.05 ? value : fallback;
}

Theme::Theme(QObject* parent)
    : QObject(parent) {
    const qreal touchpad = hyprOption(QStringLiteral("input:touchpad:scroll_factor"), 1.0);
    const qreal mouse = hyprOption(QStringLiteral("input:scroll_factor"), 1.0);
    m_wheelBoost = qBound(4.0, qMax(mouse, 1.5) / touchpad, 8.0);
    reload();
    const QString dir = themeDir();
    m_watch.addPath(dir);
    m_watch.addPath(dir + QStringLiteral("/colors.toml"));
    m_watch.addPath(QDir::homePath() + QStringLiteral("/.local/state/omarchy/current"));
    connect(&m_watch, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        m_watch.addPath(path);
        reload();
    });
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, [this] {
        const QString colors = themeDir() + QStringLiteral("/colors.toml");
        if (!m_watch.files().contains(colors))
            m_watch.addPath(colors);
        reload();
    });
}

void Theme::reload() {
    QHash<QString, QString> next;
    QFile file(themeDir() + QStringLiteral("/colors.toml"));
    if (file.open(QIODevice::ReadOnly)) {
        const QRegularExpression lineRe(QStringLiteral("^\\s*([A-Za-z0-9_]+)\\s*=\\s*\"(#(?:[0-9A-Fa-f]{6}|[0-9A-Fa-f]{8}))\""));
        const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            const auto match = lineRe.match(line);
            if (match.hasMatch())
                next.insert(match.captured(1), match.captured(2));
        }
    }
    QFile nameFile(QDir::homePath() + QStringLiteral("/.local/state/omarchy/current/theme.name"));
    QString themeName;
    if (nameFile.open(QIODevice::ReadOnly))
        themeName = QString::fromUtf8(nameFile.readAll()).trimmed();
    if (next == m_colors && themeName == m_name)
        return;
    m_colors = next;
    m_name = themeName;
    emit changed();
}

QString Theme::color(const QString& key, const QString& fallback) const {
    return m_colors.value(key, fallback);
}

QString Theme::name() const { return m_name; }
bool Theme::dark() const { return color(QStringLiteral("mode"), QStringLiteral("dark")) != QStringLiteral("light"); }
QString Theme::background() const { return color(QStringLiteral("background"), QStringLiteral("#0d171c")); }
QString Theme::darkBackground() const { return color(QStringLiteral("dark_background"), background()); }
QString Theme::darkerBackground() const { return color(QStringLiteral("darker_background"), darkBackground()); }
QString Theme::surface() const { return color(QStringLiteral("lighter_background"), QStringLiteral("#172830")); }
QString Theme::card() const { return color(QStringLiteral("selection"), surface()); }
QString Theme::border() const { return color(QStringLiteral("muted"), QStringLiteral("#5a6e6a")); }
QString Theme::text() const { return color(QStringLiteral("foreground"), QStringLiteral("#e6ddd0")); }
QString Theme::bright() const { return color(QStringLiteral("bright_foreground"), text()); }
QString Theme::muted() const { return color(QStringLiteral("dark_foreground"), QStringLiteral("#6b7d78")); }
QString Theme::faint() const { return color(QStringLiteral("muted"), muted()); }
QString Theme::accent() const { return color(QStringLiteral("accent"), QStringLiteral("#d4a84a")); }
QString Theme::accentText() const { return darkerBackground(); }
QString Theme::danger() const { return color(QStringLiteral("red"), QStringLiteral("#d45c4a")); }
QString Theme::ok() const { return color(QStringLiteral("green"), QStringLiteral("#4a9a68")); }
qreal Theme::wheelBoost() const { return m_wheelBoost; }
