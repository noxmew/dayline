#pragma once

#include <QObject>

class QThread;
class UpdateWorker;

class UpdateChecker final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(QString packageName READ packageName NOTIFY changed)
    Q_PROPERTY(QString downloadedPath READ downloadedPath NOTIFY changed)
    Q_PROPERTY(qint64 packageSize READ packageSize NOTIFY changed)
    Q_PROPERTY(qint64 receivedBytes READ receivedBytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;
    bool configured() const { return m_configured; }
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    QString latestVersion() const { return m_version; }
    QString releaseNotes() const { return m_notes; }
    QString packageName() const { return m_name; }
    QString downloadedPath() const { return m_path; }
    qint64 packageSize() const { return m_size; }
    qint64 receivedBytes() const { return m_received; }
    qint64 totalBytes() const { return m_total; }
    bool busy() const { return m_state == "checking" || m_state == "downloading"; }

    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openDownloadFolder();

signals:
    void changed();
    void progressChanged();

private:
    UpdateWorker *m_worker;
    QThread *m_thread;
    bool m_configured = false;
    QString m_state = "idle", m_message, m_version, m_notes, m_name, m_path;
    qint64 m_size = 0, m_received = 0, m_total = 0;
};
