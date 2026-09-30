#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

class LogStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    static LogStore* instance();

    void install();
    bool enabled() const;
    void setEnabled(bool on);
    QString path() const;
    void write(QtMsgType type, const QString& message);

signals:
    void enabledChanged();

private:
    explicit LogStore(QObject* parent = nullptr);
    void openFile();
    void trim();

    bool m_enabled = false;
    QString m_path;
    QFile m_file;
    QMutex m_mutex;
};
