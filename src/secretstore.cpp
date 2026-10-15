#include "secretstore.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QVariantMap>

namespace {
struct Secret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
QDBusArgument &operator<<(QDBusArgument &a, const Secret &s)
{ a.beginStructure(); a << s.session << s.parameters << s.value << s.contentType; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, Secret &s)
{ a.beginStructure(); a >> s.session >> s.parameters >> s.value >> s.contentType; a.endStructure(); return a; }
using Attributes = QMap<QString, QString>;
using Paths = QList<QDBusObjectPath>;
const QString service = QStringLiteral("org.freedesktop.secrets");
const QString root = QStringLiteral("/org/freedesktop/secrets");
using ReplyHandler = std::function<void(const QDBusMessage &)>;
void call(QObject *owner, const QString &path, const QString &interface, const QString &method,
          const QVariantList &arguments, ReplyHandler done)
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 20000), owner);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, owner, [watcher, done] {
        const QDBusMessage reply = watcher->reply(); watcher->deleteLater(); done(reply);
    });
}
QString failure(const QDBusMessage &reply)
{
    if (reply.type() != QDBusMessage::ErrorMessage) return {};
    return QStringLiteral("无法访问系统密钥环。请确认 Secret Service 已启动，并在“密码和密钥”中解锁登录密钥环后重试。");
}
}
Q_DECLARE_METATYPE(Secret)

void SecretStore::read(const QString &account, Completion done) { access(account, {}, 0, std::move(done)); }
void SecretStore::write(const QString &account, const QByteArray &password, Completion done) { access(account, password, 1, std::move(done)); }
void SecretStore::remove(const QString &account, Completion done) { access(account, {}, 2, std::move(done)); }

void SecretStore::access(const QString &account, const QByteArray &password, int operation, Completion done)
{
    qDBusRegisterMetaType<Secret>();
    qDBusRegisterMetaType<Attributes>();
    qDBusRegisterMetaType<Paths>();
    call(this, root, "org.freedesktop.Secret.Service", "OpenSession",
         {"plain", QVariant::fromValue(QDBusVariant(QString()))},
         [this, account, password, operation, done](const QDBusMessage &reply) {
        if (!failure(reply).isEmpty() || reply.arguments().size() != 2) { done({}, failure(reply).isEmpty() ? "密钥环返回了无效会话。" : failure(reply)); return; }
        const auto session = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
        const auto finish = [this, session, done](const QByteArray &value, const QString &error) {
            call(this, session.path(), "org.freedesktop.Secret.Session", "Close", {}, [](const QDBusMessage &) {});
            done(value, error);
        };
        const Attributes attributes {{"application", "dayline"}, {"account", account}, {"service", "icloud-caldav"}};
        call(this, root, "org.freedesktop.Secret.Service", "SearchItems", {QVariant::fromValue(attributes)},
             [this, attributes, password, operation, session, finish](const QDBusMessage &search) {
            if (!failure(search).isEmpty() || search.arguments().size() != 2) { finish({}, "无法查询系统密钥环，请解锁密钥环后重试。"); return; }
            const Paths unlocked = qdbus_cast<Paths>(search.arguments().at(0));
            const Paths locked = qdbus_cast<Paths>(search.arguments().at(1));
            if (!locked.isEmpty()) { finish({}, "日笺凭据所在密钥环已锁定。请在系统“密码和密钥”中解锁后重新连接。"); return; }
            if (operation == 0) {
                if (unlocked.isEmpty()) { finish({}, "未找到 iCloud 专用密码，请重新连接账户。"); return; }
                call(this, unlocked.first().path(), "org.freedesktop.Secret.Item", "GetSecret", {QVariant::fromValue(session)},
                     [finish](const QDBusMessage &secret) {
                    if (!failure(secret).isEmpty() || secret.arguments().size() != 1) { finish({}, "无法读取专用密码，请解锁系统密钥环后重试。"); return; }
                    finish(qdbus_cast<Secret>(secret.arguments().first()).value, {});
                });
                return;
            }
            if (operation == 2) {
                if (unlocked.isEmpty()) { finish({}, {}); return; }
                call(this, unlocked.first().path(), "org.freedesktop.Secret.Item", "Delete", {}, [finish](const QDBusMessage &deleted) {
                    if (!failure(deleted).isEmpty()) { finish({}, failure(deleted)); return; }
                    if (deleted.arguments().isEmpty() || qdbus_cast<QDBusObjectPath>(deleted.arguments().first()).path() != "/") {
                        finish({}, "密钥环要求确认删除凭据，请在系统密钥环中处理后重试。"); return;
                    }
                    finish({}, {});
                });
                return;
            }
            const Secret secret {session, {}, password, "text/plain; charset=utf8"};
            if (!unlocked.isEmpty()) {
                call(this, unlocked.first().path(), "org.freedesktop.Secret.Item", "SetSecret", {QVariant::fromValue(secret)},
                     [finish](const QDBusMessage &saved) { finish({}, failure(saved)); });
                return;
            }
            call(this, root, "org.freedesktop.Secret.Service", "ReadAlias", {"default"},
                 [this, attributes, secret, finish](const QDBusMessage &alias) {
                if (!failure(alias).isEmpty() || alias.arguments().isEmpty()) { finish({}, "没有可用的默认密钥环。请先在系统中创建并解锁登录密钥环。"); return; }
                const QString collection = qdbus_cast<QDBusObjectPath>(alias.arguments().first()).path();
                if (collection == "/") { finish({}, "没有默认密钥环，请在系统“密码和密钥”中创建登录密钥环。"); return; }
                QVariantMap properties {{"org.freedesktop.Secret.Item.Label", "Dayline · iCloud App 专用密码"},
                    {"org.freedesktop.Secret.Item.Attributes", QVariant::fromValue(attributes)}};
                call(this, collection, "org.freedesktop.Secret.Collection", "CreateItem",
                     {properties, QVariant::fromValue(secret), true}, [finish](const QDBusMessage &created) {
                    if (!failure(created).isEmpty() || created.arguments().size() != 2 ||
                        qdbus_cast<QDBusObjectPath>(created.arguments().at(1)).path() != "/") {
                        finish({}, "无法安全保存专用密码，请先解锁默认密钥环再连接。"); return;
                    }
                    finish({}, {});
                });
            });
        });
    });
}
