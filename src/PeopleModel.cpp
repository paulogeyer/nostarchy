#include "PeopleModel.h"

#include "crypto/Keys.h"

PeopleModel::PeopleModel(QObject* parent)
    : QAbstractListModel(parent) {}

int PeopleModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return m_pubkeys.size();
}

QVariant PeopleModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_pubkeys.size())
        return {};
    const QString pubkey = m_pubkeys.at(index.row());
    switch (role) {
    case PubkeyRole: return pubkey;
    case NameRole: {
        const QString stored = m_names.value(pubkey);
        if (!stored.isEmpty())
            return stored;
        if (m_nameLookup) {
            const QString looked = m_nameLookup(pubkey);
            if (!looked.isEmpty())
                return looked;
        }
        const QString npub = Keys::npubFromHex(pubkey);
        return npub.isEmpty() ? pubkey.left(8) : npub.left(12);
    }
    case PictureRole: {
        const QString remote = m_pictures.value(pubkey);
        if (remote.isEmpty() || !m_imageLookup)
            return {};
        return m_imageLookup(remote);
    }
    case NpubRole: return Keys::npubFromHex(pubkey);
    default: return {};
    }
}

QHash<int, QByteArray> PeopleModel::roleNames() const {
    return {
        {PubkeyRole, "pubkey"},
        {NameRole, "name"},
        {PictureRole, "picture"},
        {NpubRole, "npub"},
    };
}

void PeopleModel::setPubkeys(const QStringList& pubkeys) {
    beginResetModel();
    m_pubkeys.clear();
    for (const QString& pubkey : pubkeys) {
        if (pubkey.size() == 64 && !m_pubkeys.contains(pubkey))
            m_pubkeys.append(pubkey);
    }
    endResetModel();
}

void PeopleModel::setAuthor(const QString& pubkey, const QString& name, const QString& picture) {
    if (!name.isEmpty())
        m_names.insert(pubkey, name);
    if (!picture.isEmpty())
        m_pictures.insert(pubkey, picture);
    for (int i = 0; i < m_pubkeys.size(); ++i) {
        if (m_pubkeys.at(i) != pubkey)
            continue;
        const QModelIndex idx = index(i);
        emit dataChanged(idx, idx, {NameRole, PictureRole});
    }
}

void PeopleModel::setNameLookup(const std::function<QString(const QString&)>& names) {
    m_nameLookup = names;
}

void PeopleModel::setImageLookup(const std::function<QString(const QString&)>& images) {
    m_imageLookup = images;
}

void PeopleModel::clear() {
    if (m_pubkeys.isEmpty())
        return;
    beginResetModel();
    m_pubkeys.clear();
    endResetModel();
}
