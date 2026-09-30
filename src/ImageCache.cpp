#include "ImageCache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kMaxBytes = 2 * 1024 * 1024;
constexpr int kMaxActive = 3;

}

ImageCache::ImageCache(QObject* parent)
    : QObject(parent) {
    m_dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/images");
    QDir().mkpath(m_dir);
}

QString ImageCache::keyFor(const QString& url) const {
    return QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString ImageCache::findExisting(const QString& key) const {
    const QString base = m_dir + QLatin1Char('/') + key;
    for (const char* ext : {".jpg", ".png", ".webp", ".gif"}) {
        const QString path = base + QLatin1String(ext);
        if (QFile::exists(path))
            return QStringLiteral("image://avatars/") + key;
    }
    return {};
}

QString ImageCache::lookup(const QString& url) const {
    if (url.isEmpty())
        return {};
    const auto it = m_entries.constFind(url);
    if (it != m_entries.cend()) {
        if (it->failed)
            return {};
        if (!it->fileUrl.isEmpty())
            return it->fileUrl;
    }
    const QString found = findExisting(keyFor(url));
    if (!found.isEmpty())
        m_entries[url].fileUrl = found;
    return found;
}

void ImageCache::prefetch(const QString& url) {
    if (url.isEmpty())
        return;
    if (!lookup(url).isEmpty()) {
        Entry& entry = m_entries[url];
        if (entry.fileUrl.isEmpty())
            entry.fileUrl = lookup(url);
        return;
    }
    Entry& entry = m_entries[url];
    if (entry.fetching || entry.failed)
        return;
    entry.fetching = true;
    m_queue.enqueue(url);
    pump();
}

void ImageCache::pump() {
    while (m_active < kMaxActive && !m_queue.isEmpty()) {
        const QString url = m_queue.dequeue();
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 nostarchy/0.1"));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        request.setTransferTimeout(12000);
        QNetworkReply* reply = m_net.get(request);
        ++m_active;
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
            if (received > kMaxBytes)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
            --m_active;
            QByteArray bytes;
            if (reply->isOpen() && reply->error() == QNetworkReply::NoError)
                bytes = reply->readAll();
            const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            const bool ok = reply->error() == QNetworkReply::NoError && !bytes.isEmpty() && bytes.size() <= kMaxBytes;
            reply->deleteLater();
            if (ok)
                finish(url, bytes, type);
            else {
                Entry& entry = m_entries[url];
                entry.fetching = false;
                entry.failed = true;
            }
            pump();
        });
    }
}

QString ImageCache::extensionFor(const QByteArray& bytes, const QString& contentType) {
    if (bytes.startsWith("\xFF\xD8\xFF"))
        return QStringLiteral(".jpg");
    if (bytes.startsWith("\x89PNG"))
        return QStringLiteral(".png");
    if (bytes.startsWith("GIF8"))
        return QStringLiteral(".gif");
    if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP")
        return QStringLiteral(".webp");
    const QString lower = contentType.toLower();
    if (lower.contains(QStringLiteral("jpeg")))
        return QStringLiteral(".jpg");
    if (lower.contains(QStringLiteral("png")))
        return QStringLiteral(".png");
    if (lower.contains(QStringLiteral("webp")))
        return QStringLiteral(".webp");
    if (lower.contains(QStringLiteral("gif")))
        return QStringLiteral(".gif");
    return {};
}

void ImageCache::finish(const QString& url, const QByteArray& bytes, const QString& contentType) {
    const QString ext = extensionFor(bytes, contentType);
    if (ext.isEmpty() || bytes.size() < 32) {
        Entry& entry = m_entries[url];
        entry.fetching = false;
        entry.failed = true;
        return;
    }
    const QString path = m_dir + QLatin1Char('/') + keyFor(url) + ext;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        Entry& entry = m_entries[url];
        entry.fetching = false;
        entry.failed = true;
        return;
    }
    m_entries[url].fileUrl = QStringLiteral("image://avatars/") + keyFor(url);
    m_entries[url].fetching = false;
    emit ready(url);
}
