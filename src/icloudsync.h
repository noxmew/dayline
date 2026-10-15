#pragma once

#include "secretstore.h"
#include <QDate>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSqlDatabase>
#include <QTimer>
#include <QVariantList>
#include <functional>

class ICloudSync final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString account READ account NOTIFY changed)
    Q_PROPERTY(QString region READ region NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString lastSync READ lastSync NOTIFY changed)
    Q_PROPERTY(QString defaultCalendar READ defaultCalendar WRITE setDefaultCalendar NOTIFY changed)
    Q_PROPERTY(QVariantList calendars READ calendars NOTIFY changed)
    Q_PROPERTY(QVariantList conflicts READ conflicts NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
public:
    explicit ICloudSync(QObject *parent = nullptr);
    bool initialize(const QString &databasePath);
    bool connected() const { return m_connected; }
    bool busy() const { return m_busy; }
    QString account() const { return m_user; }
    QString region() const { return m_region; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString lastSync() const;
    QString defaultCalendar() const { return m_default; }
    QVariantList calendars() const;
    QVariantList conflicts() const;
    int pendingCount() const;
    QVariantList events(const QDate &from, const QDate &until) const;
    bool saveEvent(const QVariantMap &fields, QString *error);
    bool deleteEvent(const QString &id, QString *error);

    Q_INVOKABLE void connectAccount(const QString &account, const QString &password, const QString &region);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void selectCalendar(const QString &id, bool selected);
    Q_INVOKABLE void setDefaultCalendar(const QString &id);
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void resolveConflict(const QString &id, const QString &resolution);
    Q_INVOKABLE void clearError();

signals:
    void changed();
    void dataChanged();

private:
    struct Response { int status = 0; QByteArray body, etag; QUrl url; QString error; int retryAfter = 0; };
    using ResponseHandler = std::function<void(const Response &)>;
    using Completion = std::function<void(bool)>;
    void request(const QByteArray &method, const QUrl &url, const QByteArray &body,
                 const QMap<QByteArray, QByteArray> &headers, ResponseHandler done, int redirects = 0);
    void discover(Completion done);
    void discoverCalendars(Completion done);
    void synchronizeCalendars(const QStringList &ids, int index);
    void fetchChanges(const QString &calendar, const QList<QPair<QString, QString>> &resources, int index, Completion done);
    void uploadNext();
    void confirmWrite(const QString &href, const QString &operation, const QByteArray &confirmedEtag = {}, bool acknowledged = false);
    bool acceptRemote(const QString &calendar, const QString &href, const QByteArray &raw, const QString &etag);
    void completeSync();
    void fail(const QString &error, int retryAfter = 0);
    bool sql(const QString &statement, const QVariantList &values = {}) const;
    void loadAccount();
    void queueSync();
    static bool allowed(const QUrl &url);
    QString networkError(const Response &response) const;
    void setBusy(const QString &status);
private slots:
    void prepareForSleep(bool sleeping);
    void networkStateChanged(uint state);
private:
    QSqlDatabase m_db;
    QNetworkAccessManager m_network;
    SecretStore m_secrets;
    QTimer m_poll, m_retry;
    QByteArray m_password;
    QString m_user, m_region = "global", m_home, m_default, m_status = "尚未连接", m_error;
    bool m_connected = false, m_busy = false, m_authRequired = false;
    int m_failures = 0;
};
