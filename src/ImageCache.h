#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QQueue>
#include <QString>

class ImageCache : public QObject {
    Q_OBJECT
public:
    explicit ImageCache(QObject* parent = nullptr);

    QString lookup(const QString& url) const;
    void prefetch(const QString& url);

signals:
    void ready(const QString& url);

private:
    struct Entry {
        QString fileUrl;
        bool fetching = false;
    };

    QString keyFor(const QString& url) const;
    QString findExisting(const QString& key) const;
    void pump();
    void finish(const QString& url, const QByteArray& bytes, const QString& contentType);
    static QString extensionFor(const QByteArray& bytes, const QString& contentType);

    QNetworkAccessManager m_net;
    QString m_dir;
    mutable QHash<QString, Entry> m_entries;
    QQueue<QString> m_queue;
    int m_active = 0;
};
