#include "LogStore.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>

#include <cstdio>

namespace {

bool noise(const QString& message) {
    return message.contains(QStringLiteral("QIODevice::read")) || message.contains(QStringLiteral("device not open"));
}

QString redact(QString text) {
    static const QRegularExpression secretPath(QStringLiteral("(wss?://[^/\\s]+)/[^\\s]{16,}"));
    return text.replace(secretPath, QStringLiteral("\\1/"));
}

void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& message) {
    if (noise(message))
        return;
    const QString clean = redact(message);
    const QByteArray line = clean.toLocal8Bit();
    std::fprintf(stderr, "%s\n", line.constData());
    if (LogStore* store = LogStore::instance())
        store->write(type, clean);
}

QString typeName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return QStringLiteral("log");
}

}

LogStore* LogStore::instance() {
    static LogStore store;
    return &store;
}

LogStore::LogStore(QObject* parent)
    : QObject(parent) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    QDir().mkpath(dir);
    m_path = dir + QStringLiteral("/nostarchy.log");
    QSettings settings;
    m_enabled = settings.value(QStringLiteral("storeLogs"), false).toBool();
    if (m_enabled)
        openFile();
}

void LogStore::install() {
    qInstallMessageHandler(messageHandler);
}

bool LogStore::enabled() const { return m_enabled; }

QString LogStore::path() const { return m_path; }

void LogStore::setEnabled(bool on) {
    if (m_enabled == on)
        return;
    m_enabled = on;
    QSettings settings;
    settings.setValue(QStringLiteral("storeLogs"), m_enabled);
    if (m_enabled) {
        openFile();
        write(QtInfoMsg, QStringLiteral("log storage enabled"));
    } else {
        write(QtInfoMsg, QStringLiteral("log storage disabled"));
        QMutexLocker lock(&m_mutex);
        m_file.close();
    }
    emit enabledChanged();
}

void LogStore::openFile() {
    QMutexLocker lock(&m_mutex);
    if (m_file.isOpen())
        return;
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        m_enabled = false;
}

void LogStore::trim() {
    if (!m_file.isOpen() || m_file.size() < 1024 * 1024)
        return;
    m_file.close();
    QFile existing(m_path);
    QByteArray tail;
    if (existing.open(QIODevice::ReadOnly)) {
        if (existing.size() > 256 * 1024)
            existing.seek(existing.size() - 256 * 1024);
        tail = existing.readAll();
    }
    existing.close();
    if (existing.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        existing.write(tail);
        existing.close();
    }
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
}

void LogStore::write(QtMsgType type, const QString& message) {
    if (!m_enabled)
        return;
    QMutexLocker lock(&m_mutex);
    if (!m_file.isOpen())
        return;
    trim();
    QTextStream out(&m_file);
    out << QDateTime::currentDateTime().toString(Qt::ISODate) << ' ' << typeName(type) << ' ' << message << '\n';
    out.flush();
}
