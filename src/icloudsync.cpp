#include "icloudsync.h"
#include "icalresource.h"

#include <QColor>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDateTime>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
struct DavItem { QString href; QMap<QString, QString> properties; QSet<QString> types, privileges; bool calendar = false, collection = false, events = true; };
QList<DavItem> parseDav(const QByteArray &body, QString *error)
{
    QXmlStreamReader xml(body);
    QList<DavItem> result;
    if (!xml.readNextStartElement()) { *error = "iCloud 日历响应无法完整解析，已保留本机数据。"; return {}; }
    if (xml.name() != QStringLiteral("multistatus") || xml.namespaceUri() != QStringLiteral("DAV:")) {
        *error = "iCloud 返回了无效的日历响应。"; return {};
    }
    while (xml.readNextStartElement()) {
        if (xml.name() != QStringLiteral("response")) { xml.skipCurrentElement(); continue; }
        DavItem item;
        while (xml.readNextStartElement()) {
            if (xml.name() == QStringLiteral("href")) item.href = xml.readElementText();
            else if (xml.name() == QStringLiteral("propstat")) {
                QMap<QString, QString> properties;
                QSet<QString> types, privileges;
                QString status;
                bool componentsSeen = false, hasEvents = false;
                while (xml.readNextStartElement()) {
                    if (xml.name() == QStringLiteral("status")) status = xml.readElementText();
                    else if (xml.name() == QStringLiteral("prop")) {
                        while (xml.readNextStartElement()) {
                            const QString name = xml.name().toString();
                            if (name == "resourcetype" || name == "current-user-privilege-set" || name == "supported-calendar-component-set") {
                                if (name == "supported-calendar-component-set") componentsSeen = true;
                                int depth = 1;
                                while (depth && !xml.atEnd()) {
                                    xml.readNext();
                                    if (xml.isStartElement()) {
                                        ++depth;
                                        if (name == "resourcetype") types.insert(xml.name().toString());
                                        if (name == "current-user-privilege-set") privileges.insert(xml.name().toString());
                                        if (name == "supported-calendar-component-set" && xml.name() == QStringLiteral("comp") &&
                                            xml.attributes().value("name") == QStringLiteral("VEVENT")) hasEvents = true;
                                    } else if (xml.isEndElement()) --depth;
                                }
                            } else properties[name] = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
                        }
                    } else xml.skipCurrentElement();
                }
                if (status.contains(" 200 ")) {
                    item.properties.insert(properties); item.types.unite(types); item.privileges.unite(privileges);
                    if (componentsSeen) item.events = hasEvents;
                } else if (!status.contains(" 404 ")) { *error = "iCloud 未能完整返回日历属性，已停止本轮同步。"; return {}; }
            } else if (xml.name() == QStringLiteral("status")) {
                const auto status = xml.readElementText();
                if (!status.contains(" 200 ")) { *error = "iCloud 返回了不完整的资源清单，已停止本轮同步。"; return {}; }
            } else xml.skipCurrentElement();
        }
        item.calendar = item.types.contains("calendar"); item.collection = item.types.contains("collection");
        if (item.href.isEmpty()) { *error = "iCloud 日历响应缺少资源地址。"; return {}; }
        result.append(item);
    }
    // readNextStartElement() passes EndDocument while looking for a sibling root.
    // Finish the single multistatus document without reading beyond its end.
    while (!xml.atEnd()) xml.readNext();
    if (xml.hasError()) { *error = "iCloud 日历响应无法完整解析，已保留本机数据。"; return {}; }
    return result;
}
QByteArray propfind(const QByteArray &properties)
{ return "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:a=\"http://apple.com/ns/ical/\"><d:prop>" + properties + "</d:prop></d:propfind>"; }
QString calendarId(const QString &url) { return "icloud:" + QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex().left(24)); }
QString eventId(const QString &href) { return "icloud:" + QString::fromLatin1(QCryptographicHash::hash(href.toUtf8(), QCryptographicHash::Sha256).toHex()); }
QString cleanColor(QString value) { if (value.size() == 9) value.truncate(7); return QColor(value).isValid() ? QColor(value).name() : "#218572"; }
QString normalized(const QUrl &url) { return url.adjusted(QUrl::NormalizePathSegments | QUrl::RemoveFragment).toString(QUrl::FullyEncoded); }
QString details(const QByteArray &raw)
{
    if (!ICalResource::canEdit(raw)) return "含只读结构或无法完整解析，原始内容已保留。";
    const auto items = ICalResource::occurrences(raw, QDate(1,1,1), QDate(9999,12,31));
    if (items.isEmpty()) return {};
    const auto e = items.first().toMap();
    return e.value("date").toString() + " " + (e.value("allDay").toBool() ? QString("全天") : e.value("start").toString() + "—" + e.value("end").toString()) +
        "\n结束日期：" + e.value("endDate").toString() + "\n地点：" + e.value("location").toString() + "\n备注：" + e.value("notes").toString();
}
}

ICloudSync::ICloudSync(QObject *parent) : QObject(parent), m_secrets(this)
{
    m_poll.setInterval(60000);
    connect(&m_poll, &QTimer::timeout, this, &ICloudSync::sync);
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &ICloudSync::sync);
    QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "PrepareForSleep", this, SLOT(prepareForSleep(bool)));
    QDBusConnection::systemBus().connect("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager", "org.freedesktop.NetworkManager", "StateChanged", this, SLOT(networkStateChanged(uint)));
}

void ICloudSync::prepareForSleep(bool sleeping) { if (!sleeping) sync(); }
void ICloudSync::networkStateChanged(uint state) { if (state >= 60) sync(); }

bool ICloudSync::initialize(const QString &databasePath)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "dayline-icloud");
    m_db.setDatabaseName(databasePath);
    if (!m_db.open()) { m_error = "无法打开 iCloud 本机缓存。"; return false; }
    if (!sql("PRAGMA busy_timeout=5000") ||
        !sql("CREATE TABLE IF NOT EXISTS icloud_account (id INTEGER PRIMARY KEY CHECK(id=1), username TEXT NOT NULL, region TEXT NOT NULL, home TEXT NOT NULL, default_calendar TEXT NOT NULL DEFAULT '', last_sync TEXT NOT NULL DEFAULT '')") ||
        !sql("CREATE TABLE IF NOT EXISTS icloud_calendars (id TEXT PRIMARY KEY, url TEXT NOT NULL, name TEXT NOT NULL, color TEXT NOT NULL, writable INTEGER NOT NULL, selected INTEGER NOT NULL DEFAULT 0, available INTEGER NOT NULL DEFAULT 1)") ||
        !sql("CREATE TABLE IF NOT EXISTS icloud_resources (href TEXT PRIMARY KEY, id TEXT UNIQUE NOT NULL, calendar TEXT NOT NULL, uid TEXT NOT NULL DEFAULT '', etag TEXT NOT NULL DEFAULT '', raw BLOB NOT NULL, baseline BLOB NOT NULL DEFAULT X'', pending TEXT NOT NULL DEFAULT '', uncertain INTEGER NOT NULL DEFAULT 0, conflict INTEGER NOT NULL DEFAULT 0, remote_raw BLOB NOT NULL DEFAULT X'', remote_etag TEXT NOT NULL DEFAULT '', error TEXT NOT NULL DEFAULT '')")) {
        m_error = "无法创建 iCloud 缓存表。"; return false;
    }
    loadAccount();
    m_poll.start();
    if (m_connected) QTimer::singleShot(1500, this, &ICloudSync::sync);
    return true;
}
bool ICloudSync::sql(const QString &statement, const QVariantList &values) const
{
    QSqlQuery query(m_db); query.prepare(statement);
    for (const auto &v : values) {
        if (v.metaType().id() == QMetaType::QString && v.toString().isNull()) query.addBindValue(QStringLiteral(""));
        else if (v.metaType().id() == QMetaType::QByteArray && v.toByteArray().isNull()) query.addBindValue(QByteArray(""));
        else query.addBindValue(v);
    }
    return query.exec();
}
void ICloudSync::loadAccount()
{
    QSqlQuery q(m_db); q.exec("SELECT username,region,home,default_calendar FROM icloud_account WHERE id=1");
    m_connected = q.next();
    if (m_connected) { m_user = q.value(0).toString(); m_region = q.value(1).toString(); m_home = q.value(2).toString(); m_default = q.value(3).toString(); m_status = "等待同步"; }
    else { m_user.clear(); m_region = "global"; m_home.clear(); m_default.clear(); m_status = "尚未连接"; }
}
QString ICloudSync::lastSync() const
{
    QSqlQuery q(m_db); q.exec("SELECT last_sync FROM icloud_account WHERE id=1");
    return q.next() ? q.value(0).toString() : QString();
}
int ICloudSync::pendingCount() const
{
    QSqlQuery q(m_db); q.exec("SELECT COUNT(*) FROM icloud_resources WHERE pending<>''");
    return q.next() ? q.value(0).toInt() : 0;
}
QVariantList ICloudSync::calendars() const
{
    QVariantList list; QSqlQuery q(m_db);
    q.exec("SELECT id,name,color,writable,selected,available FROM icloud_calendars ORDER BY name");
    while (q.next()) list.append(QVariantMap {{"id", q.value(0)}, {"name", q.value(1)}, {"color", q.value(2)},
        {"writable", q.value(3).toBool()}, {"selected", q.value(4).toBool()}, {"enabled", q.value(4).toBool()}, {"available", q.value(5).toBool()}, {"cloud", true}});
    return list;
}
QVariantList ICloudSync::conflicts() const
{
    QVariantList list; QSqlQuery q(m_db);
    q.exec("SELECT r.id,r.raw,r.remote_raw,r.remote_etag,r.pending,c.name,r.error,r.conflict FROM icloud_resources r JOIN icloud_calendars c ON c.id=r.calendar WHERE r.conflict=1 OR r.error<>''");
    while (q.next()) list.append(QVariantMap {{"id", q.value(0)}, {"localTitle", ICalResource::title(q.value(1).toByteArray())},
        {"remoteTitle", q.value(3).toString() == "@deleted" ? "云端已删除" : ICalResource::title(q.value(2).toByteArray())},
        {"deleted", q.value(3).toString() == "@deleted"}, {"deleting", q.value(4).toString() == "delete"},
        {"calendarName", q.value(5)}, {"error", q.value(6)}, {"resolvable",q.value(7).toBool()}, {"isConflict",q.value(7).toBool()},
        {"localDetails", details(q.value(1).toByteArray())}, {"remoteDetails", q.value(3).toString() == "@deleted" ? "" : details(q.value(2).toByteArray())}});
    return list;
}
QVariantList ICloudSync::events(const QDate &from, const QDate &until) const
{
    QVariantList list; QSqlQuery q(m_db);
    q.prepare("SELECT r.id,r.raw,r.pending,r.conflict,c.id,c.name,c.color,c.writable,c.available FROM icloud_resources r JOIN icloud_calendars c ON c.id=r.calendar WHERE c.selected=1 AND r.pending<>'delete'"); q.exec();
    while (q.next()) {
        auto items = ICalResource::occurrences(q.value(1).toByteArray(), from, until);
        for (auto &item : items) {
            auto event = item.toMap();
            event["id"] = q.value(0); event["calendar"] = q.value(4); event["calendarName"] = q.value(5); event["color"] = q.value(6);
            event["cloud"] = true;
            event["syncState"] = q.value(3).toBool() ? "需处理" : q.value(2).toString().isEmpty() ? "已同步" : "待同步";
            if (!q.value(7).toBool() || !q.value(8).toBool() || q.value(3).toBool()) {
                event["readOnly"] = true; event["readOnlyReason"] = q.value(3).toBool() ? "此日程存在同步冲突，请先在 iCloud 设置中处理。" : "此日历只读或暂时不可用。";
            }
            list.append(event);
        }
    }
    return list;
}
bool ICloudSync::allowed(const QUrl &url)
{
    static const QRegularExpression host("^(?:caldav|p[0-9]+-caldav)\\.icloud\\.com(?:\\.cn)?$");
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() && (url.port() == -1 || url.port() == 443) && host.match(url.host().toLower()).hasMatch();
}
void ICloudSync::request(const QByteArray &method, const QUrl &url, const QByteArray &body,
                         const QMap<QByteArray, QByteArray> &headers, ResponseHandler done, int redirects)
{
    if (!allowed(url)) { Response r; r.error = "iCloud 返回了未经许可的服务地址，已停止连接。"; done(r); return; }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    request.setRawHeader("User-Agent", "Dayline/0.1");
    request.setRawHeader("Authorization", "Basic " + (m_user.toUtf8() + ':' + m_password).toBase64());
    request.setRawHeader("Content-Type", method == "PUT" ? "text/calendar; charset=utf-8" : "application/xml; charset=utf-8");
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) request.setRawHeader(it.key(), it.value());
    auto *reply = m_network.sendCustomRequest(request, method, body);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 bytes, qint64) { if (bytes > 32 * 1024 * 1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, method, body, headers, done, redirects] {
        Response r; r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.url = reply->url(); r.etag = reply->rawHeader("ETag"); r.body = reply->readAll();
        const QByteArray retry = reply->rawHeader("Retry-After");
        bool seconds = false; r.retryAfter = retry.toInt(&seconds);
        if (!seconds && !retry.isEmpty()) r.retryAfter = qMax(0, int(QDateTime::currentDateTimeUtc().secsTo(QDateTime::fromString(QString::fromLatin1(retry), Qt::RFC2822Date))));
        const auto target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (r.status >= 300 && r.status < 400 && !target.isEmpty()) {
            const auto destination = r.url.resolved(target); reply->deleteLater();
            if (redirects >= 5 || !allowed(destination)) { r.error = "iCloud 服务重定向无效，已停止连接。"; done(r); return; }
            this->request(method, destination, body, headers, done, redirects + 1); return;
        }
        if (reply->error() != QNetworkReply::NoError && r.status == 0) r.error = "网络或 HTTPS 连接失败。请检查联网状态和系统时间；本机修改已保留。";
        if (r.status == 401) { m_authRequired = true; m_password.clear(); m_retry.stop(); }
        reply->deleteLater(); done(r);
    });
}
QString ICloudSync::networkError(const Response &r) const
{
    if (!r.error.isEmpty()) return r.error;
    if (r.status == 401) return "iCloud 授权失败或已失效。请重新生成 App 专用密码并连接；本机修改已保留。";
    if (r.status == 403) return "iCloud 拒绝访问，请确认账户地区、日历权限和 App 专用密码。";
    if (r.status == 429 || r.status == 503) return "iCloud 暂时限制请求，日笺会稍后重试；本机修改已保留。";
    return QString("iCloud 请求未完成（HTTP %1），本机内容已保留。").arg(r.status);
}
void ICloudSync::setBusy(const QString &status) { m_busy = true; m_status = status; m_error.clear(); emit changed(); }
void ICloudSync::clearError() { m_error.clear(); emit changed(); }
void ICloudSync::fail(const QString &error, int retryAfter)
{
    m_busy = false; m_error = error; m_status = "需要处理";
    if (m_connected && !m_password.isEmpty() && !m_authRequired) {
        ++m_failures; const int delay = qMax(retryAfter, qMin(900, 15 * (1 << qMin(m_failures, 6))));
        m_retry.start(int(qMin(qint64(delay) * 1000, qint64(2147483647))));
    }
    emit changed(); emit dataChanged();
}
void ICloudSync::connectAccount(const QString &account, const QString &password, const QString &region)
{
    if (m_busy) return;
    const QString user = account.trimmed();
    if (!user.contains('@') || password.trimmed().isEmpty() || (region != "global" && region != "china")) {
        fail("请输入 Apple 账户、App 专用密码并选择账户地区。"); return;
    }
    if (m_connected && (user.compare(m_user, Qt::CaseInsensitive) || region != m_region)) { fail("请先处理待同步内容并断开当前账户，再连接其他账户或地区。"); return; }
    m_user = user; m_password = password.trimmed().toUtf8(); m_region = region; m_authRequired = false;
    m_home.clear(); m_retry.stop(); setBusy("正在发现 iCloud 日历…");
    discover([this](bool success) {
        if (!success) {
            const QString error = m_error, status = m_status;
            if (m_connected) { loadAccount(); m_error = error; m_status = status; emit changed(); }
            return;
        }
        m_secrets.write(m_user, m_password, [this](const QByteArray &, const QString &error) {
            if (!error.isEmpty()) { m_password.clear(); fail(error); return; }
            if (!sql("INSERT INTO icloud_account(id,username,region,home) VALUES(1,?,?,?) ON CONFLICT(id) DO UPDATE SET username=excluded.username,region=excluded.region,home=excluded.home", {m_user,m_region,m_home})) { fail("无法保存 iCloud 账户配置。"); return; }
            m_connected = true; m_busy = false; m_status = "已连接，请选择同步日历"; m_failures = 0;
            emit changed(); sync();
        });
    });
}
void ICloudSync::discover(Completion done)
{
    const QUrl endpoint(m_region == "china" ? "https://caldav.icloud.com.cn/" : "https://caldav.icloud.com/");
    request("PROPFIND", endpoint, propfind("<d:current-user-principal/>"), {{"Depth","0"}}, [this, done](const Response &r) {
        if (r.status != 207) { if (r.status == 401) m_password.clear(); fail(networkError(r), r.retryAfter); done(false); return; }
        QString error; const auto rows = parseDav(r.body, &error);
        const QString principal = rows.isEmpty() ? QString() : rows.first().properties.value("current-user-principal");
        if (!error.isEmpty() || principal.isEmpty()) { fail(error.isEmpty() ? "iCloud 未返回账户身份，请检查账户地区。" : error); done(false); return; }
        request("PROPFIND", r.url.resolved(QUrl(principal)), propfind("<c:calendar-home-set/>"), {{"Depth","0"}}, [this, done](const Response &home) {
            if (home.status != 207) { fail(networkError(home), home.retryAfter); done(false); return; }
            QString error; const auto rows = parseDav(home.body, &error);
            const QString path = rows.isEmpty() ? QString() : rows.first().properties.value("calendar-home-set");
            const QUrl url = home.url.resolved(QUrl(path));
            if (!error.isEmpty() || path.isEmpty() || !allowed(url)) { fail("无法发现有效的 iCloud 日历集合，请检查账户地区。" ); done(false); return; }
            m_home = normalized(url); discoverCalendars(done);
        });
    });
}
void ICloudSync::discoverCalendars(Completion done)
{
    request("PROPFIND", QUrl(m_home), propfind("<d:resourcetype/><d:displayname/><a:calendar-color/><d:current-user-privilege-set/><c:supported-calendar-component-set/>"), {{"Depth","1"}},
            [this, done](const Response &r) {
        if (r.status != 207) { if (r.status == 401) m_password.clear(); fail(networkError(r), r.retryAfter); done(false); return; }
        QString error; const auto rows = parseDav(r.body, &error);
        if (!error.isEmpty()) { fail(error); done(false); return; }
        bool complete = false;
        for (const auto &row : rows)
            complete |= row.collection && normalized(r.url.resolved(QUrl(row.href))) == normalized(r.url);
        if (!complete) { fail("iCloud 没有返回完整的日历集合，已保留原缓存。"); done(false); return; }
        if (!m_db.transaction()) { fail("无法更新日历缓存。"); done(false); return; }
        bool ok = sql("UPDATE icloud_calendars SET available=0");
        for (const auto &row : rows) {
            if (!row.calendar || !row.events) continue;
            const QUrl url = r.url.resolved(QUrl(row.href));
            if (!allowed(url) || !url.path().endsWith('/')) { ok = false; break; }
            const bool writable = row.privileges.contains("all") || row.privileges.contains("write") ||
                (row.privileges.contains("write-content") && row.privileges.contains("bind") && row.privileges.contains("unbind"));
            const QString address = normalized(url);
            ok = ok && sql("INSERT INTO icloud_calendars(id,url,name,color,writable) VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,color=excluded.color,writable=excluded.writable,available=1",
                {calendarId(address), address, row.properties.value("displayname", "iCloud 日历"), cleanColor(row.properties.value("calendar-color")), writable});
        }
        if (!ok || !m_db.commit()) { m_db.rollback(); fail("日历清单无效或无法保存，已保留原缓存。"); done(false); return; }
        emit changed(); done(true);
    });
}
void ICloudSync::sync()
{
    if (!m_connected || m_busy) return;
    if (m_authRequired) { m_error = "iCloud 授权已暂停，请重新生成 App 专用密码并重新授权。"; emit changed(); return; }
    if (m_retry.isActive()) { m_status = QString("网络重试等待中，约 %1 秒后自动同步").arg((m_retry.remainingTime() + 999) / 1000); emit changed(); return; }
    if (m_password.isEmpty()) {
        setBusy("正在读取系统密钥环…");
        m_secrets.read(m_user, [this](const QByteArray &password, const QString &error) {
            m_busy = false;
            if (!error.isEmpty()) { fail(error); return; }
            m_password = password; sync();
        }); return;
    }
    setBusy("正在同步…");
    discoverCalendars([this](bool success) {
        if (!success) return;
        QStringList ids; for (const auto &value : calendars()) {
            const auto c = value.toMap(); if (c.value("selected").toBool() && c.value("available").toBool()) ids.append(c.value("id").toString());
        }
        synchronizeCalendars(ids, 0);
    });
}
void ICloudSync::synchronizeCalendars(const QStringList &ids, int index)
{
    if (index == ids.size()) { uploadNext(); return; }
    const QString id = ids.at(index);
    QSqlQuery q(m_db); q.prepare("SELECT url FROM icloud_calendars WHERE id=? AND available=1"); q.addBindValue(id); q.exec();
    if (!q.next()) { fail("选中的日历已不可访问，本机修改已保留。"); return; }
    const QUrl url(q.value(0).toString());
    request("PROPFIND", url, propfind("<d:getetag/><d:resourcetype/>"), {{"Depth","1"}}, [this, id, ids, index](const Response &r) {
        if (r.status != 207) { if (r.status == 401) m_password.clear(); fail(networkError(r), r.retryAfter); return; }
        QString error; const auto rows = parseDav(r.body, &error);
        if (!error.isEmpty()) { fail(error); return; }
        bool complete = false;
        for (const auto &row : rows)
            complete |= row.collection && normalized(r.url.resolved(QUrl(row.href))) == normalized(r.url);
        if (!complete) { fail("iCloud 资源清单缺少日历集合本身，已停止同步并保留原缓存。"); return; }
        QSet<QString> seen; QList<QPair<QString, QString>> changed;
        for (const auto &row : rows) {
            if (row.collection) continue;
            const auto resource = r.url.resolved(QUrl(row.href));
            const QString href = normalized(resource), etag = row.properties.value("getetag");
            if (!allowed(resource) || !href.startsWith(normalized(r.url)) || etag.isEmpty() || seen.contains(href)) { fail("资源清单不完整或包含无效地址，已停止同步。"); return; }
            seen.insert(href);
            QSqlQuery existing(m_db); existing.prepare("SELECT etag,uncertain FROM icloud_resources WHERE href=?"); existing.addBindValue(href); existing.exec();
            if (!existing.next() || existing.value(0).toString() != etag || existing.value(1).toBool()) changed.append({href,etag});
        }
        if (!m_db.transaction()) { fail("无法更新 iCloud 本机缓存。"); return; }
        QSqlQuery cached(m_db); cached.prepare("SELECT href,pending FROM icloud_resources WHERE calendar=?"); cached.addBindValue(id); cached.exec();
        bool ok = true;
        while (cached.next()) {
            const QString href = cached.value(0).toString(), pending = cached.value(1).toString();
            if (seen.contains(href) || pending == "create") continue;
            if (pending == "update") ok = ok && sql("UPDATE icloud_resources SET conflict=1,remote_raw=X'',remote_etag='@deleted',uncertain=0 WHERE href=?", {href});
            else ok = ok && sql("DELETE FROM icloud_resources WHERE href=?", {href});
        }
        if (!ok || !m_db.commit()) { m_db.rollback(); fail("无法保存远端变更，本机内容已保留。"); return; }
        fetchChanges(id, changed, 0, [this, ids, index](bool ok) { if (ok) synchronizeCalendars(ids, index + 1); });
    });
}
void ICloudSync::fetchChanges(const QString &calendar, const QList<QPair<QString, QString>> &resources, int index, Completion done)
{
    if (index == resources.size()) { emit dataChanged(); done(true); return; }
    const auto item = resources.at(index);
    request("GET", QUrl(item.first), {}, {}, [this, calendar, resources, index, item, done](const Response &r) {
        if (r.status != 200 || r.etag.isEmpty()) { fail(networkError(r), r.retryAfter); done(false); return; }
        if (!acceptRemote(calendar, item.first, r.body, QString::fromUtf8(r.etag))) {
            fail("无法保存云端日程，本机内容已保留。"); done(false); return;
        }
        fetchChanges(calendar, resources, index + 1, done);
    });
}
bool ICloudSync::acceptRemote(const QString &calendar, const QString &href, const QByteArray &raw, const QString &etag)
{
    QSqlQuery q(m_db); q.prepare("SELECT raw,pending,uncertain FROM icloud_resources WHERE href=?"); q.addBindValue(href); q.exec();
    const bool exists = q.next();
    QString parseError; const auto parsed = ICalResource::occurrences(raw, QDate::currentDate().addYears(-1), QDate::currentDate().addYears(2), &parseError);
    const QString uid = parsed.isEmpty() ? QString() : parsed.first().toMap().value("uid").toString();
    if (exists && !q.value(1).toString().isEmpty()) {
        if (q.value(2).toBool() && q.value(1).toString() != "delete" && q.value(0).toByteArray() == raw) {
            return sql("UPDATE icloud_resources SET raw=?,baseline=?,etag=?,pending='',uncertain=0,conflict=0,remote_raw=X'',remote_etag='',error=? WHERE href=?", {raw,raw,etag,parseError,href});
        } else if (q.value(2).toBool()) {
            QSqlQuery baseline(m_db); baseline.prepare("SELECT baseline,etag FROM icloud_resources WHERE href=?"); baseline.addBindValue(href); baseline.exec();
            if (baseline.next() && baseline.value(0).toByteArray() == raw && baseline.value(1).toString() == etag)
                return sql("UPDATE icloud_resources SET uncertain=0 WHERE href=?", {href});
        }
        return sql("UPDATE icloud_resources SET conflict=1,remote_raw=?,remote_etag=?,uncertain=0 WHERE href=?", {raw,etag,href});
    } else return sql("INSERT INTO icloud_resources(href,id,calendar,uid,etag,raw,baseline,error) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(href) DO UPDATE SET etag=excluded.etag,raw=excluded.raw,baseline=excluded.baseline,error=excluded.error,conflict=0,remote_raw=X'',remote_etag=''",
        {href,eventId(href),calendar,uid,etag,raw,raw,parseError});
}
void ICloudSync::uploadNext()
{
    QSqlQuery q(m_db);
    q.exec("SELECT r.href,r.raw,r.etag,r.pending,r.uncertain FROM icloud_resources r JOIN icloud_calendars c ON c.id=r.calendar WHERE r.pending<>'' AND r.conflict=0 AND r.error='' AND c.selected=1 AND c.available=1 AND c.writable=1 LIMIT 1");
    if (!q.next()) { completeSync(); return; }
    const QString href = q.value(0).toString(), operation = q.value(3).toString();
    if (q.value(4).toBool()) { confirmWrite(href, operation); return; }
    const QByteArray raw = q.value(1).toByteArray(), etag = q.value(2).toByteArray();
    if (operation != "create" && etag.isEmpty()) { fail("日程缺少云端版本，已停止写入以保护云端内容。"); return; }
    QMap<QByteArray, QByteArray> headers;
    headers.insert(operation == "create" ? "If-None-Match" : "If-Match", operation == "create" ? QByteArray("*") : etag);
    // Persist uncertainty before dispatch so a crash or timeout is reconciled before any retry.
    if (!sql("UPDATE icloud_resources SET uncertain=1 WHERE href=?", {href})) { fail("无法记录待同步状态，已停止网络写入。"); return; }
    request(operation == "delete" ? "DELETE" : "PUT", QUrl(href), operation == "delete" ? QByteArray() : raw, headers,
            [this, href, operation](const Response &r) {
        if ((r.status >= 200 && r.status < 300) || (operation == "delete" && r.status == 404)) {
            if (operation == "delete") {
                if (!sql("DELETE FROM icloud_resources WHERE href=?", {href})) { fail("无法确认本机删除状态，稍后会重新核对云端。"); return; }
                uploadNext();
            } else confirmWrite(href, operation, r.etag, true);
            return;
        }
        if (r.status == 412 || r.status == 409) { confirmWrite(href, operation); return; }
        if (r.status >= 400 && r.status < 500 && r.status != 408 && r.status != 429)
            sql("UPDATE icloud_resources SET uncertain=0 WHERE href=?", {href});
        if (r.status == 401) m_password.clear();
        fail(networkError(r), r.retryAfter);
    });
}
void ICloudSync::confirmWrite(const QString &href, const QString &operation, const QByteArray &confirmedEtag, bool acknowledged)
{
    request("GET", QUrl(href), {}, {}, [this, href, operation, confirmedEtag, acknowledged](const Response &r) {
        QSqlQuery q(m_db); q.prepare("SELECT raw,baseline,etag FROM icloud_resources WHERE href=?"); q.addBindValue(href); q.exec();
        if (!q.next()) { uploadNext(); return; }
        if (r.status == 404) {
            bool ok = operation == "delete" ? sql("DELETE FROM icloud_resources WHERE href=?", {href}) :
                operation == "create" && !acknowledged ? sql("UPDATE icloud_resources SET uncertain=0 WHERE href=?", {href}) :
                sql("UPDATE icloud_resources SET conflict=1,remote_raw=X'',remote_etag='@deleted',uncertain=0 WHERE href=?", {href});
            if (!ok) { fail("无法保存远端确认状态。"); return; }
            uploadNext(); return;
        }
        if (r.status != 200 || r.etag.isEmpty()) { fail(networkError(r), r.retryAfter); return; }
        const auto local = q.value(0).toByteArray();
        // Servers may normalize harmless serialization details. An unrecognized difference remains a conflict.
        bool ok = false;
        if (operation != "delete" && (local == r.body || (!confirmedEtag.isEmpty() && confirmedEtag == r.etag))) {
            ok = sql("UPDATE icloud_resources SET raw=?,baseline=?,etag=?,pending='',uncertain=0,conflict=0,remote_raw=X'',remote_etag='' WHERE href=?", {r.body,r.body,QString::fromUtf8(r.etag),href});
        } else if (operation != "create" && q.value(2).toByteArray() == r.etag && q.value(1).toByteArray() == r.body) {
            ok = sql("UPDATE icloud_resources SET uncertain=0 WHERE href=?", {href});
        } else {
            ok = sql("UPDATE icloud_resources SET conflict=1,remote_raw=?,remote_etag=?,uncertain=0 WHERE href=?", {r.body,QString::fromUtf8(r.etag),href});
        }
        if (!ok) { fail("无法保存同步确认状态，本机内容已保留。"); return; }
        emit dataChanged(); uploadNext();
    });
}
void ICloudSync::completeSync()
{
    if (!sql("UPDATE icloud_account SET last_sync=? WHERE id=1", {QDateTime::currentDateTime().toString(Qt::ISODate)})) { fail("无法保存同步时间。"); return; }
    m_busy = false; m_failures = 0; m_retry.stop();
    m_status = !conflicts().isEmpty() ? "有日程需要处理" : pendingCount() ? "部分修改待同步，请检查日历选择和权限" : "已同步";
    bool selected = false; for (const auto &c : calendars()) selected |= c.toMap().value("selected").toBool();
    if (!selected) m_status = "已连接，请选择同步日历";
    emit changed(); emit dataChanged();
}
void ICloudSync::selectCalendar(const QString &id, bool selected)
{
    if (m_busy) return;
    if (!selected) {
        QSqlQuery q(m_db); q.prepare("SELECT COUNT(*) FROM icloud_resources WHERE calendar=? AND pending<>''"); q.addBindValue(id); q.exec();
        if (q.next() && q.value(0).toInt()) { fail("此日历仍有待同步内容，请同步或处理冲突后再取消选择。"); return; }
    }
    if (!sql("UPDATE icloud_calendars SET selected=? WHERE id=?", {selected,id})) { fail("无法保存日历选择。"); return; }
    if (!selected && m_default == id) setDefaultCalendar({});
    if (selected && m_default.isEmpty()) setDefaultCalendar(id);
    emit changed(); emit dataChanged(); if (selected) queueSync();
}
void ICloudSync::setDefaultCalendar(const QString &id)
{
    if (!id.isEmpty()) {
        QSqlQuery q(m_db); q.prepare("SELECT 1 FROM icloud_calendars WHERE id=? AND selected=1 AND writable=1 AND available=1"); q.addBindValue(id); q.exec();
        if (!q.next()) return;
    }
    if (sql("UPDATE icloud_account SET default_calendar=? WHERE id=1", {id})) { m_default = id; emit changed(); }
}
void ICloudSync::queueSync() { QTimer::singleShot(800, this, &ICloudSync::sync); }
bool ICloudSync::saveEvent(const QVariantMap &fields, QString *error)
{
    const QString calendar = fields.value("calendar").toString(), id = fields.value("id").toString();
    QSqlQuery c(m_db); c.prepare("SELECT url FROM icloud_calendars WHERE id=? AND selected=1 AND writable=1 AND available=1"); c.addBindValue(calendar); c.exec();
    if (!m_connected || !c.next()) { *error = "请选择一个已连接、可写的 iCloud 日历。"; return false; }
    const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces) + "@dayline";
    QString href = normalized(QUrl(c.value(0).toString()).resolved(QUrl(uid + ".ics")));
    QByteArray original;
    QString pending = "create", storedUid = uid;
    if (!id.isEmpty()) {
        QSqlQuery q(m_db); q.prepare("SELECT href,raw,pending,conflict,calendar,uid,uncertain FROM icloud_resources WHERE id=?"); q.addBindValue(id); q.exec();
        if (!q.next() || q.value(3).toBool() || q.value(4).toString() != calendar || q.value(6).toBool()) { *error = "日程正在同步、存在冲突或所属日历不匹配，请稍后重试。"; return false; }
        href = q.value(0).toString(); original = q.value(1).toByteArray(); storedUid = q.value(5).toString();
        pending = q.value(2).toString() == "create" ? "create" : "update";
    }
    const auto raw = ICalResource::edit(original, fields, storedUid, error);
    if (raw.isEmpty()) return false;
    // Local content and the outgoing operation are one atomic persistent record.
    if (!m_db.transaction()) { *error = "无法开始保存。"; return false; }
    bool ok = id.isEmpty() ? sql("INSERT INTO icloud_resources(href,id,calendar,uid,raw,pending) VALUES(?,?,?,?,?,?)", {href,eventId(href),calendar,storedUid,raw,pending}) :
        sql("UPDATE icloud_resources SET raw=?,pending=?,error='' WHERE id=?", {raw,pending,id});
    if (!ok || !m_db.commit()) { m_db.rollback(); *error = "无法保存日程及待同步状态。"; return false; }
    emit dataChanged(); emit changed(); queueSync(); return true;
}
bool ICloudSync::deleteEvent(const QString &id, QString *error)
{
    QSqlQuery q(m_db); q.prepare("SELECT r.pending,r.uncertain,r.conflict,r.raw,c.writable,c.available FROM icloud_resources r JOIN icloud_calendars c ON c.id=r.calendar WHERE r.id=?"); q.addBindValue(id); q.exec();
    if (!q.next() || q.value(1).toBool() || q.value(2).toBool() || !q.value(4).toBool() || !q.value(5).toBool()) { *error = "日程正在同步、存在冲突或日历不可写，暂不能删除。"; return false; }
    if (!ICalResource::canEdit(q.value(3).toByteArray())) { *error = "此日程含当前不支持的结构，只能查看。"; return false; }
    const bool ok = q.value(0).toString() == "create" ? sql("DELETE FROM icloud_resources WHERE id=?", {id}) : sql("UPDATE icloud_resources SET pending='delete' WHERE id=?", {id});
    if (!ok) { *error = "无法保存删除操作。"; return false; }
    emit dataChanged(); emit changed(); queueSync(); return true;
}
void ICloudSync::resolveConflict(const QString &id, const QString &resolution)
{
    if (m_busy) return;
    QSqlQuery q(m_db); q.prepare("SELECT href,calendar,raw,remote_raw,remote_etag,pending FROM icloud_resources WHERE id=? AND conflict=1"); q.addBindValue(id); q.exec();
    if (!q.next()) return;
    const bool deleted = q.value(4).toString() == "@deleted";
    bool ok = false;
    if (resolution == "remote") {
        ok = deleted ? sql("DELETE FROM icloud_resources WHERE id=?", {id}) :
            sql("UPDATE icloud_resources SET raw=remote_raw,baseline=remote_raw,etag=remote_etag,pending='',conflict=0,remote_raw=X'',remote_etag='',error='' WHERE id=?", {id});
    } else if (resolution == "local" && !deleted) {
        if (!ICalResource::canEdit(q.value(3).toByteArray())) {
            fail("云端日程现在含只读结构，不能直接覆盖。请采用云端，或将本机内容另存为新日程。" ); return;
        }
        // The next write still uses If-Match against the version displayed in the conflict panel.
        ok = sql("UPDATE icloud_resources SET baseline=remote_raw,etag=remote_etag,pending=CASE WHEN pending='delete' THEN 'delete' ELSE 'update' END,conflict=0,remote_raw=X'',remote_etag='',error='' WHERE id=?", {id});
    } else if (resolution == "copy") {
        QSqlQuery calendar(m_db); calendar.prepare("SELECT url FROM icloud_calendars WHERE id=? AND selected=1 AND writable=1 AND available=1"); calendar.addBindValue(q.value(1)); calendar.exec();
        if (!calendar.next()) { fail("原日历目前不可写，已保留本机内容。" ); return; }
        const QString uid = QUuid::createUuid().toString(QUuid::WithoutBraces) + "@dayline";
        QString error; const QByteArray raw = ICalResource::copy(q.value(2).toByteArray(), uid, &error);
        if (raw.isEmpty()) { fail(error); return; }
        const QString href = normalized(QUrl(calendar.value(0).toString()).resolved(QUrl(uid + ".ics")));
        if (!m_db.transaction()) { fail("无法保存日程副本。" ); return; }
        if (!sql("INSERT INTO icloud_resources(href,id,calendar,uid,raw,pending) VALUES(?,?,?,?,?,'create')", {href,eventId(href),q.value(1),uid,raw})) {
            m_db.rollback(); fail("无法保存日程副本。" ); return;
        }
        ok = deleted ? sql("DELETE FROM icloud_resources WHERE id=?", {id}) :
            sql("UPDATE icloud_resources SET raw=remote_raw,baseline=remote_raw,etag=remote_etag,pending='',conflict=0,remote_raw=X'',remote_etag='',error='' WHERE id=?", {id});
        if (!ok || !m_db.commit()) { m_db.rollback(); fail("无法完整保存冲突处理结果。" ); return; }
    }
    if (!ok) { fail("无法保存冲突处理结果。" ); return; }
    emit dataChanged(); emit changed(); queueSync();
}
void ICloudSync::disconnectAccount()
{
    if (m_busy || !m_connected) return;
    if (pendingCount()) { fail("仍有待同步的新增、修改或删除。请先同步或处理冲突，再断开账户。"); return; }
    setBusy("正在断开账户…");
    m_secrets.remove(m_user, [this](const QByteArray &, const QString &error) {
        if (!error.isEmpty()) { fail(error); return; }
        if (!m_db.transaction()) { fail("无法清理本机缓存。"); return; }
        if (!sql("DELETE FROM icloud_resources") || !sql("DELETE FROM icloud_calendars") || !sql("DELETE FROM icloud_account") || !m_db.commit()) { m_db.rollback(); fail("无法清理本机缓存。"); return; }
        m_password.clear(); m_retry.stop(); m_busy = false; m_error.clear(); loadAccount(); emit changed(); emit dataChanged();
    });
}
