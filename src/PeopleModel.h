#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QStringList>

#include <functional>

class PeopleModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PubkeyRole = Qt::UserRole + 1,
        NameRole,
        PictureRole,
        NpubRole
    };

    explicit PeopleModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPubkeys(const QStringList& pubkeys);
    void setAuthor(const QString& pubkey, const QString& name, const QString& picture);
    void setNameLookup(const std::function<QString(const QString&)>& names);
    void setImageLookup(const std::function<QString(const QString&)>& images);
    void clear();

private:
    QStringList m_pubkeys;
    QHash<QString, QString> m_names;
    QHash<QString, QString> m_pictures;
    std::function<QString(const QString&)> m_nameLookup;
    std::function<QString(const QString&)> m_imageLookup;
};
