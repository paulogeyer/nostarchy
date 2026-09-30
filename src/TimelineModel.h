#pragma once

#include "nostr/Event.h"

#include <QAbstractListModel>
#include <QSet>

#include <functional>

class TimelineModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        EventIdRole = Qt::UserRole + 1,
        PubkeyRole,
        NameRole,
        ContentRole,
        TimeRole,
        NpubRole,
        FollowedRole,
        PictureRole,
        RichContentRole
    };

    explicit TimelineModel(QObject* parent = nullptr, bool newestFirst = true);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const NostrEvent& event, const QString& name, const QString& picture = {});
    void setAuthor(const QString& pubkey, const QString& name, const QString& picture);
    void setFollows(const QSet<QString>& follows);
    void clear();
    void refreshTimes();
    void refreshMentions();
    void refreshPicture(const QString& remoteUrl);
    void setFormatter(const std::function<QString(const QString&, const QVector<QStringList>&)>& formatter);
    void setImageLookup(const std::function<QString(const QString&)>& lookup);

private:
    struct Note {
        QString eventId;
        QString pubkey;
        QString content;
        QString name;
        QString npub;
        QString picture;
        QString rich;
        QVector<QStringList> tags;
        qint64 createdAt = 0;
    };

    int indexOf(const QString& eventId) const;
    bool comesBefore(const Note& a, const Note& b) const;

    QList<Note> m_notes;
    QSet<QString> m_follows;
    bool m_newestFirst = true;
    std::function<QString(const QString&, const QVector<QStringList>&)> m_formatter;
    std::function<QString(const QString&)> m_imageLookup;
};
