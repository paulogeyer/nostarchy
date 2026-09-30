#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QString>

class Theme : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString darkBackground READ darkBackground NOTIFY changed)
    Q_PROPERTY(QString darkerBackground READ darkerBackground NOTIFY changed)
    Q_PROPERTY(QString surface READ surface NOTIFY changed)
    Q_PROPERTY(QString card READ card NOTIFY changed)
    Q_PROPERTY(QString border READ border NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString bright READ bright NOTIFY changed)
    Q_PROPERTY(QString muted READ muted NOTIFY changed)
    Q_PROPERTY(QString faint READ faint NOTIFY changed)
    Q_PROPERTY(QString accent READ accent NOTIFY changed)
    Q_PROPERTY(QString accentText READ accentText NOTIFY changed)
    Q_PROPERTY(QString danger READ danger NOTIFY changed)
    Q_PROPERTY(QString ok READ ok NOTIFY changed)
    Q_PROPERTY(qreal wheelBoost READ wheelBoost NOTIFY changed)

public:
    explicit Theme(QObject* parent = nullptr);

    QString name() const;
    bool dark() const;
    QString background() const;
    QString darkBackground() const;
    QString darkerBackground() const;
    QString surface() const;
    QString card() const;
    QString border() const;
    QString text() const;
    QString bright() const;
    QString muted() const;
    QString faint() const;
    QString accent() const;
    QString accentText() const;
    QString danger() const;
    QString ok() const;
    qreal wheelBoost() const;

signals:
    void changed();

private:
    QString color(const QString& key, const QString& fallback) const;
    void reload();

    static qreal hyprOption(const QString& name, qreal fallback);
    QHash<QString, QString> m_colors;
    QString m_name;
    qreal m_wheelBoost = 4.0;
    QFileSystemWatcher m_watch;
};
