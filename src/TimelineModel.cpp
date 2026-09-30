#include "TimelineModel.h"

#include "crypto/Keys.h"

#include <QDateTime>

namespace {

QString formatAge(qint64 created) {
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 age = now - created;
    if (age < 5)
        return QStringLiteral("now");
    if (age < 60)
        return QString::number(age) + QStringLiteral("s");
    if (age < 3600)
        return QString::number(age / 60) + QStringLiteral("m");
    if (age < 86400)
        return QString::number(age / 3600) + QStringLiteral("h");
    if (age < 86400 * 7)
        return QString::number(age / 86400) + QStringLiteral("d");
    return QDateTime::fromSecsSinceEpoch(created).toString(QStringLiteral("MMM d"));
}

}

TimelineModel::TimelineModel(QObject* parent, bool newestFirst)
    : QAbstractListModel(parent)
    , m_newestFirst(newestFirst) {}

int TimelineModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return m_notes.size();
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_notes.size())
        return {};
    const Note& note = m_notes.at(index.row());
    switch (role) {
    case EventIdRole: return note.eventId;
    case PubkeyRole: return note.pubkey;
    case NameRole: return note.name.isEmpty() ? note.npub.left(12) : note.name;
    case ContentRole: return note.content;
    case TimeRole: return formatAge(note.createdAt);
    case NpubRole: return note.npub;
    case FollowedRole: return m_follows.contains(note.pubkey);
    case PictureRole: return note.picture.isEmpty() || !m_imageLookup ? QString() : m_imageLookup(note.picture);
    case RichContentRole: return note.rich.isEmpty() ? note.content : note.rich;
    default: return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const {
    return {
        {EventIdRole, "eventId"},
        {PubkeyRole, "pubkey"},
        {NameRole, "name"},
        {ContentRole, "content"},
        {TimeRole, "timeText"},
        {NpubRole, "npub"},
        {FollowedRole, "followed"},
        {PictureRole, "picture"},
        {RichContentRole, "richContent"},
    };
}

void TimelineModel::upsert(const NostrEvent& event, const QString& name, const QString& picture) {
    if (indexOf(event.id) >= 0)
        return;
    Note note;
    note.eventId = event.id;
    note.pubkey = event.pubkey;
    note.content = event.content;
    note.name = name;
    note.picture = picture;
    note.tags = event.tags;
    if (m_formatter)
        note.rich = m_formatter(note.content, note.tags);
    note.npub = Keys::npubFromHex(event.pubkey);
    note.createdAt = event.createdAt;

    int row = 0;
    while (row < m_notes.size() && comesBefore(m_notes.at(row), note))
        ++row;
    beginInsertRows(QModelIndex(), row, row);
    m_notes.insert(row, note);
    endInsertRows();
    if (m_notes.size() > 300) {
        beginRemoveRows(QModelIndex(), 300, m_notes.size() - 1);
        m_notes.resize(300);
        endRemoveRows();
    }
}

void TimelineModel::setAuthor(const QString& pubkey, const QString& name, const QString& picture) {
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes.at(i).pubkey != pubkey)
            continue;
        if (m_notes.at(i).name == name && m_notes.at(i).picture == picture)
            continue;
        m_notes[i].name = name;
        m_notes[i].picture = picture;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, {NameRole, PictureRole, RichContentRole});
    }
}

void TimelineModel::setFormatter(const std::function<QString(const QString&, const QVector<QStringList>&)>& formatter) {
    m_formatter = formatter;
    refreshMentions();
}

void TimelineModel::setImageLookup(const std::function<QString(const QString&)>& lookup) {
    m_imageLookup = lookup;
}

void TimelineModel::refreshPicture(const QString& remoteUrl) {
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes.at(i).picture != remoteUrl)
            continue;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, {PictureRole});
    }
}

void TimelineModel::refreshMentions() {
    if (m_notes.isEmpty() || !m_formatter)
        return;
    for (Note& note : m_notes)
        note.rich = m_formatter(note.content, note.tags);
    emit dataChanged(index(0), index(m_notes.size() - 1), {RichContentRole});
}

void TimelineModel::setFollows(const QSet<QString>& follows) {
    m_follows = follows;
    if (!m_notes.isEmpty()) {
        emit dataChanged(index(0), index(m_notes.size() - 1), {FollowedRole});
    }
}

void TimelineModel::clear() {
    if (m_notes.isEmpty())
        return;
    beginResetModel();
    m_notes.clear();
    endResetModel();
}

void TimelineModel::refreshTimes() {
    if (m_notes.isEmpty())
        return;
    emit dataChanged(index(0), index(m_notes.size() - 1), {TimeRole});
}

int TimelineModel::indexOf(const QString& eventId) const {
    for (int i = 0; i < m_notes.size(); ++i) {
        if (m_notes.at(i).eventId == eventId)
            return i;
    }
    return -1;
}

bool TimelineModel::comesBefore(const Note& a, const Note& b) const {
    if (a.createdAt != b.createdAt)
        return m_newestFirst ? a.createdAt > b.createdAt : a.createdAt < b.createdAt;
    return m_newestFirst ? a.eventId < b.eventId : a.eventId > b.eventId;
}
