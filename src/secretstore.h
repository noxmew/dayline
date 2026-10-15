#pragma once

#include <QByteArray>
#include <QObject>
#include <functional>

// Secret Service keeps credentials out of the calendar database and settings.
class SecretStore final : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(const QByteArray &, const QString &)>;
    explicit SecretStore(QObject *parent = nullptr) : QObject(parent) {}
    void read(const QString &account, Completion done);
    void write(const QString &account, const QByteArray &password, Completion done);
    void remove(const QString &account, Completion done);
private:
    void access(const QString &account, const QByteArray &password, int operation, Completion done);
};
