#pragma once

#include "nostr/Event.h"

#include <QAbstractListModel>
#include <QSet>

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
        FollowedRole
    };

    explicit TimelineModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const NostrEvent& event, const QString& name);
    void setName(const QString& pubkey, const QString& name);
    void setFollows(const QSet<QString>& follows);
    void clear();
    void refreshTimes();

private:
    struct Note {
        QString eventId;
        QString pubkey;
        QString content;
        QString name;
        QString npub;
        qint64 createdAt = 0;
    };

    int indexOf(const QString& eventId) const;
    static bool comesBefore(const Note& a, const Note& b);

    QList<Note> m_notes;
    QSet<QString> m_follows;
};
