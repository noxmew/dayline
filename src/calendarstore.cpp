#include "calendarstore.h"
#include "icloudsync.h"
#include "icalresource.h"
#include "workdaycalendar.h"

#include <QDir>
#include <QColor>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTime>
#include <QUuid>
#include <algorithm>

CalendarStore::CalendarStore(bool demo, QObject *parent)
    : QObject(parent), m_demo(demo),
      m_month(QDate::currentDate().year(), QDate::currentDate().month(), 1),
      m_selected(QDate::currentDate()),
      m_settings("Dayline", demo ? "preview" : "dayline")
{
    connect(this, &CalendarStore::changed, this, &CalendarStore::selectionChanged);
    m_memoSaveTimer.setSingleShot(true);
    m_memoSaveTimer.setInterval(400);
    connect(&m_memoSaveTimer, &QTimer::timeout, this, [this] { saveMemo(); });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { saveMemo(); });
}

QString CalendarStore::workdayCoverage() const { return WorkdayCalendar::coverage(); }

bool CalendarStore::initialize()
{
    QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (m_demo)
        directory += "/preview";
    if (!QDir().mkpath(directory)) {
        setError("无法创建日历数据目录，请检查目录权限。");
        return false;
    }
    const QString databasePath = directory + "/calendar.sqlite";
    const bool newDatabase = !QFileInfo::exists(databasePath);
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(databasePath);
    if (!m_db.open()) {
        setError("无法打开本机日历数据库：" + m_db.lastError().text());
        return false;
    }
    QSqlQuery query(m_db);
    if (!query.exec("CREATE TABLE IF NOT EXISTS events ("
                    "id TEXT PRIMARY KEY, title TEXT NOT NULL, date TEXT NOT NULL, "
                    "all_day INTEGER NOT NULL, start_time TEXT NOT NULL, "
                    "end_time TEXT NOT NULL, calendar TEXT NOT NULL, notes TEXT NOT NULL)")) {
        setError(query.lastError().text());
        return false;
    }
    if (!query.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='local_calendars'")) {
        setError("无法读取本机日历分类。"); return false;
    }
    const bool createCalendars = !query.next();
    query.finish();
    if (createCalendars) {
        if (!m_db.transaction()) { setError("无法初始化本机日历分类。"); return false; }
        const bool created = query.exec("CREATE TABLE local_calendars (id TEXT PRIMARY KEY, name TEXT NOT NULL, color TEXT NOT NULL, enabled INTEGER NOT NULL, sort_order INTEGER NOT NULL)") &&
            query.exec("INSERT INTO local_calendars VALUES ('work','工作','#218572',1,0),('life','生活','#C28B45',1,1),('ideas','灵感','#8273B3',1,2)");
        if (!created || !m_db.commit()) {
            m_db.rollback(); setError("无法初始化本机日历分类。"); return false;
        }
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS event_schedules (event_id TEXT PRIMARY KEY, raw BLOB NOT NULL)")) {
        setError("无法初始化日程时间和重复规则：" + query.lastError().text()); return false;
    }
    if (!query.exec("CREATE TABLE IF NOT EXISTS memo ("
                    "id INTEGER PRIMARY KEY CHECK(id=1), content TEXT NOT NULL)")) {
        setError("无法初始化本机备忘录：" + query.lastError().text());
        return false;
    }
    if (!query.exec("SELECT content FROM memo WHERE id=1")) {
        setError("无法读取本机备忘录：" + query.lastError().text());
        return false;
    }
    if (query.next()) m_memo = query.value(0).toString();
    query.finish();
    if (m_demo && newDatabase)
        seedPreview();
    reload();
    return m_error.isEmpty();
}

void CalendarStore::setMemoEnabled(bool enabled)
{
    if (enabled == memoEnabled()) return;
    m_settings.setValue("memo/enabled", enabled);
    emit memoEnabledChanged();
}

void CalendarStore::setMemo(const QString &text)
{
    if (text == m_memo) return;
    m_memo = text.isEmpty() ? QStringLiteral("") : text;
    m_memoSavePending = true;
    m_memoError.clear();
    m_memoSaveTimer.start();
    emit memoChanged();
    emit memoSaveStateChanged();
}

bool CalendarStore::saveMemo()
{
    m_memoSaveTimer.stop();
    if (!m_memoSavePending) return true;
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO memo(id,content) VALUES(1,?) "
                  "ON CONFLICT(id) DO UPDATE SET content=excluded.content");
    query.addBindValue(m_memo);
    if (!query.exec()) {
        m_memoError = "无法保存备忘录：" + query.lastError().text();
        emit memoSaveStateChanged();
        return false;
    }
    m_memoSavePending = false;
    m_memoError.clear();
    emit memoSaveStateChanged();
    return true;
}

void CalendarStore::reload()
{
    QSqlQuery query(m_db);
    if (!query.exec("SELECT c.id,c.name,c.color,c.enabled,(SELECT COUNT(*) FROM events e WHERE e.calendar=c.id) FROM local_calendars c ORDER BY c.sort_order,c.id")) {
        setError("无法读取本机日历分类。"); return;
    }
    QVariantList localCalendars;
    QSet<QString> enabled;
    while (query.next()) {
        localCalendars.append(QVariantMap {{"id",query.value(0)}, {"name",query.value(1)}, {"color",query.value(2)},
            {"enabled",query.value(3).toBool()}, {"count",query.value(4).toInt()}, {"cloud",false}});
        if (query.value(3).toBool()) enabled.insert(query.value(0).toString());
    }
    if (localCalendars.isEmpty()) { setError("本机日历分类为空，请检查本机数据。"); return; }
    if (!query.exec("SELECT e.id,e.title,e.date,e.all_day,e.start_time,e.end_time,e.calendar,e.notes,c.color,c.name,c.id,s.raw "
                    "FROM events e LEFT JOIN local_calendars c ON c.id=e.calendar LEFT JOIN event_schedules s ON s.event_id=e.id "
                    "ORDER BY e.date,e.all_day DESC,e.start_time,e.title")) {
        setError(query.lastError().text());
        return;
    }
    QVariantList events;
    while (query.next()) {
        if (query.value(10).isNull()) { setError("日程所属的本机日历分类不存在，请检查本机数据。"); return; }
        QVariantMap event;
        event["id"] = query.value(0);
        event["title"] = query.value(1);
        event["date"] = query.value(2);
        event["allDay"] = query.value(3).toBool();
        event["start"] = query.value(4);
        event["end"] = query.value(5);
        event["calendar"] = query.value(6);
        event["notes"] = query.value(7);
        event["color"] = query.value(8);
        event["calendarName"] = query.value(9);
        event["endDate"] = event.value("date");
        event["recurring"] = false;
        event["recurrence"] = QVariantMap{{"frequency", "none"}};
        if (query.value(11).isNull()) events.append(event);
        else {
            const auto raw = query.value(11).toByteArray();
            if (ICalResource::uid(raw) != event.value("id").toString()) { setError("本机日程的系列标识不一致。"); return; }
            QString error;
            const auto scheduled = ICalResource::occurrences(raw, m_month.addMonths(-1), m_month.addMonths(3), &error);
            if (!error.isEmpty()) { setError("无法读取本机日程：" + error); return; }
            for (const auto &value : scheduled) {
                auto item = value.toMap();
                for (const auto &key : {"id", "calendar", "color", "calendarName"}) item[key] = event.value(key);
                events.append(item);
            }
        }
    }
    if (m_icloud) {
        for (const auto &value : m_icloud->calendars()) {
            const auto calendar = value.toMap();
            const QString id = calendar.value("id").toString();
            if (calendar.value("selected").toBool() && m_settings.value("calendars/visible/" + id, true).toBool())
                enabled.insert(id);
        }
        const auto cloudEvents = m_icloud->events(m_month.addMonths(-1), m_month.addMonths(3));
        events.append(cloudEvents);
    }
    for (auto &value : events) {
        auto event = value.toMap();
        const auto first = QDate::fromString(event.value("date").toString(), Qt::ISODate);
        const auto last = QDate::fromString(event.value("endDate").toString(), Qt::ISODate);
        const bool allDay = event.value("allDay").toBool();
        auto displayEnd = last;
        if (!allDay && event.value("end").toString() == "00:00" && last > first) displayEnd = last.addDays(-1);
        event["displayEndDate"] = displayEnd.toString(Qt::ISODate);
        event["timeLabel"] = allDay ? "全天" : event.value("start").toString();
        QString range = allDay ? QString("全天日程") : event.value("start").toString() + " — " + event.value("end").toString();
        if (last > first) range = allDay ? first.toString("M/d") + " — " + last.toString("M/d") + " · 全天"
            : first.toString("M/d") + " " + event.value("start").toString() + " — " + last.toString("M/d") + " " + event.value("end").toString();
        if (event.value("recurring").toBool()) range += " · 重复";
        event["timeRange"] = range;
        event["dateLabel"] = last > first ? first.toString("M月d日") + " — " + last.toString("M月d日") : QLocale("zh_CN").toString(first, "M月d日 ddd");
        value = event;
    }
    std::stable_sort(events.begin(), events.end(), [](const QVariant &a, const QVariant &b) {
            const auto x = a.toMap(), y = b.toMap();
            if (x.value("date") != y.value("date")) return x.value("date").toString() < y.value("date").toString();
            if (x.value("allDay") != y.value("allDay")) return x.value("allDay").toBool();
            return x.value("start").toString() < y.value("start").toString();
    });
    m_localCalendars = localCalendars;
    m_enabled = enabled;
    m_events = events;
    emit changed();
}

void CalendarStore::attachICloud(ICloudSync *sync)
{
    m_icloud = sync;
    connect(sync, &ICloudSync::dataChanged, this, &CalendarStore::reload);
    connect(sync, &ICloudSync::changed, this, &CalendarStore::changed);
    reload();
}

QString CalendarStore::monthLabel() const
{
    return QString("%1年%2月").arg(m_month.year()).arg(m_month.month());
}

QString CalendarStore::monthEnglish() const
{
    return QLocale("en_US").toString(m_month, "MMMM yyyy").toUpper();
}

QString CalendarStore::selectedHeading() const
{
    return QLocale("zh_CN").toString(m_selected, "yyyy年M月");
}

QString CalendarStore::selectedWeekday() const
{
    return QLocale("zh_CN").toString(m_selected, "dddd");
}

int CalendarStore::weekCount() const
{
    return (m_month.dayOfWeek() - 1 + m_month.daysInMonth() + 6) / 7;
}

bool CalendarStore::includes(const QVariantMap &event) const
{
    return m_enabled.contains(event.value("calendar").toString()) &&
           (m_search.isEmpty() || event.value("title").toString().contains(m_search, Qt::CaseInsensitive) ||
            event.value("notes").toString().contains(m_search, Qt::CaseInsensitive) ||
            event.value("calendarName").toString().contains(m_search, Qt::CaseInsensitive));
}

QVariantList CalendarStore::eventsOn(const QDate &date) const
{
    QVariantList result;
    for (const QVariant &value : m_events) {
        const QVariantMap event = value.toMap();
        const QString start = event.value("date").toString(), end = event.value("displayEndDate", event.value("endDate", start)).toString();
        if (start <= date.toString(Qt::ISODate) && end >= date.toString(Qt::ISODate) && includes(event))
            result.append(event);
    }
    return result;
}

QVariantList CalendarStore::monthDays() const
{
    QVariantList result;
    const QDate first = m_month.addDays(1 - m_month.dayOfWeek());
    for (int i = 0; i < weekCount() * 7; ++i) {
        const QDate date = first.addDays(i);
        result.append(QVariantMap {
            {"date", date.toString(Qt::ISODate)}, {"day", date.day()},
            {"inMonth", date.month() == m_month.month()},
            {"today", date == QDate::currentDate()},
            {"weekend", date.dayOfWeek() > 5}, {"events", eventsOn(date)}
        });
    }
    return result;
}

QVariantList CalendarStore::localCalendars() const
{
    return m_localCalendars;
}

QVariantList CalendarStore::calendars() const
{
    QVariantList result = localCalendars();
    if (m_icloud) for (const auto &value : m_icloud->calendars()) {
        auto calendar = value.toMap();
        if (!calendar.value("selected").toBool()) continue;
        int count = 0;
        for (const auto &event : m_events) if (event.toMap().value("calendar") == calendar.value("id")) ++count;
        calendar["enabled"] = m_enabled.contains(calendar.value("id").toString());
        calendar["count"] = count; result.append(calendar);
    }
    return result;
}

QVariantList CalendarStore::writableCalendars() const
{
    QVariantList result;
    for (const auto &value : m_localCalendars) {
        const auto calendar = value.toMap();
        result.append(QVariantMap {{"id",calendar.value("id")},{"name",calendar.value("name").toString() + " · 本机"}});
    }
    if (m_icloud) for (const auto &value : m_icloud->calendars()) {
        const auto calendar = value.toMap();
        if (calendar.value("selected").toBool() && calendar.value("writable").toBool() && calendar.value("available").toBool())
            result.append(QVariantMap {{"id", calendar.value("id")},{"name", calendar.value("name").toString() + " · iCloud"}});
    }
    return result;
}

QVariantList CalendarStore::selectedEvents() const { return eventsOn(m_selected); }

QVariantList CalendarStore::upcomingEvents() const
{
    QVariantList result;
    for (const QVariant &value : m_events) {
        const auto event = value.toMap();
        if (QDate::fromString(event.value("date").toString(), Qt::ISODate) > m_selected && includes(event)) {
            result.append(value);
            if (result.size() == 3) break;
        }
    }
    return result;
}

QVariantList CalendarStore::visibleEvents() const
{
    QVariantList result;
    for (const QVariant &value : m_events) {
        const auto event = value.toMap();
        const QDate date = QDate::fromString(event.value("date").toString(), Qt::ISODate);
        const auto end = QDate::fromString(event.value("displayEndDate", event.value("endDate", event.value("date"))).toString(), Qt::ISODate);
        if (includes(event) && (!m_search.isEmpty() || (date < m_month.addMonths(1) && end >= m_month)))
            result.append(value);
    }
    return result;
}

int CalendarStore::monthCount() const
{
    int count = 0;
    for (const QVariant &value : m_events) {
        const auto event = value.toMap();
        const QDate date = QDate::fromString(event.value("date").toString(), Qt::ISODate);
        const auto end = QDate::fromString(event.value("displayEndDate", event.value("endDate", event.value("date"))).toString(), Qt::ISODate);
        if (date < m_month.addMonths(1) && end >= m_month && includes(event)) ++count;
    }
    return count;
}

void CalendarStore::changeMonth(int offset) { m_month = m_month.addMonths(offset); reload(); }
void CalendarStore::goToday()
{
    m_selected = QDate::currentDate();
    m_month = QDate(m_selected.year(), m_selected.month(), 1);
    reload();
}
void CalendarStore::selectDate(const QString &iso)
{
    const QDate date = QDate::fromString(iso, Qt::ISODate);
    if (!date.isValid() || date == m_selected) return;
    m_selected = date;
    // Keep the date cells alive so clicks and double-clicks finish on the same item.
    emit selectionChanged();
}
void CalendarStore::toggleCalendar(const QString &id)
{
    clearError();
    if (id.startsWith("icloud:") && m_icloud) {
        for (const auto &value : m_icloud->calendars()) {
            const auto calendar = value.toMap();
            if (calendar.value("id").toString() != id || !calendar.value("selected").toBool()) continue;
            m_settings.setValue("calendars/visible/" + id, !m_enabled.contains(id));
            reload();
            return;
        }
        setError("此 iCloud 日历已不在当前过滤列表中。");
        return;
    }
    QSqlQuery query(m_db);
    query.prepare("UPDATE local_calendars SET enabled=1-enabled WHERE id=?"); query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1) { setError("无法更改本机日历的显示状态。"); return; }
    reload();
}

bool CalendarStore::saveCalendar(const QString &id, const QString &name, const QString &color)
{
    clearError();
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 30) { setError("分类名称请填写 1 到 30 个字。"); return false; }
    if (!QRegularExpression("^#[0-9a-fA-F]{6}$").match(color).hasMatch() || !QColor(color).isValid()) {
        setError("颜色请使用 #RRGGBB 格式。"); return false;
    }
    bool exists = id.isEmpty();
    for (const auto &value : m_localCalendars) {
        const auto calendar = value.toMap();
        if (calendar.value("id").toString() == id) exists = true;
        else if (calendar.value("name").toString().compare(trimmed, Qt::CaseInsensitive) == 0) {
            setError("已有同名的本机分类，请换一个名称。"); return false;
        }
    }
    if (!exists) { setError("此本机分类已不存在，请重新打开分类设置。"); return false; }
    QSqlQuery query(m_db);
    if (id.isEmpty()) {
        query.prepare("INSERT INTO local_calendars(id,name,color,enabled,sort_order) VALUES(?,?,?,1,(SELECT COALESCE(MAX(sort_order),-1)+1 FROM local_calendars))");
        query.addBindValue("local:" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    } else {
        query.prepare("UPDATE local_calendars SET name=?,color=? WHERE id=?");
    }
    query.addBindValue(trimmed); query.addBindValue(QColor(color).name());
    if (!id.isEmpty()) query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1) { setError("无法保存本机分类，请检查数据库权限。"); return false; }
    reload();
    return m_error.isEmpty();
}

bool CalendarStore::deleteCalendar(const QString &id, const QString &targetId)
{
    clearError();
    if (!m_db.transaction()) { setError("无法开始删除本机分类。"); return false; }
    auto reject = [this](const QString &message) { m_db.rollback(); setError(message); return false; };
    QSqlQuery query(m_db);
    if (!query.exec("SELECT id FROM local_calendars")) return reject("无法读取本机分类。");
    QSet<QString> ids;
    while (query.next()) ids.insert(query.value(0).toString());
    if (!ids.contains(id)) return reject("此本机分类已不存在。");
    if (ids.size() <= 1) return reject("至少需要保留一个本机分类。");
    query.prepare("SELECT COUNT(*) FROM events WHERE calendar=?"); query.addBindValue(id);
    if (!query.exec() || !query.next()) return reject("无法读取此分类中的日程。");
    const bool hasEvents = query.value(0).toInt() > 0;
    query.finish();
    if (hasEvents) {
        if (targetId == id || !ids.contains(targetId)) return reject("请选择另一个本机分类来保留这些日程。");
        query.prepare("UPDATE events SET calendar=? WHERE calendar=?"); query.addBindValue(targetId); query.addBindValue(id);
        if (!query.exec()) return reject("无法移动分类中的日程，原分类已保留。");
    }
    query.prepare("DELETE FROM local_calendars WHERE id=?"); query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1 || !m_db.commit()) return reject("无法删除本机分类，日程和原分类已保留。");
    reload();
    return m_error.isEmpty();
}

QString CalendarStore::eventCalendar(const QString &id) const
{
    QSqlQuery query(m_db);
    query.prepare("SELECT calendar FROM events WHERE id=?"); query.addBindValue(id);
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}
void CalendarStore::setSearchQuery(const QString &query)
{
    if (m_search == query.trimmed()) return;
    m_search = query.trimmed();
    emit changed();
}
void CalendarStore::setDarkMode(bool dark)
{
    if (dark == darkMode()) return;
    m_settings.setValue("appearance/dark", dark);
    emit darkModeChanged();
}
void CalendarStore::setError(const QString &message) { m_error = message; emit errorChanged(); }
void CalendarStore::clearError() { setError({}); }

bool CalendarStore::saveEvent(const QVariantMap &event)
{
    clearError();
    if (event.value("calendar").toString().startsWith("icloud:") && m_icloud) {
        QString error;
        if (!m_icloud->saveEvent(event, &error)) { setError(error); return false; }
        m_selected = QDate::fromString(event.value("date").toString(), Qt::ISODate);
        m_month = QDate(m_selected.year(), m_selected.month(), 1);
        reload(); return true;
    }
    const QString calendar = event.value("calendar").toString();
    const bool validCalendar = std::any_of(m_localCalendars.cbegin(), m_localCalendars.cend(), [&calendar](const QVariant &value) {
        return value.toMap().value("id").toString() == calendar;
    });
    if (!validCalendar) {
        setError("请选择一个有效的本机日历。"); return false;
    }
    QString id = event.value("id").toString();
    QSqlQuery query(m_db);
    QByteArray original;
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    else {
        query.prepare("SELECT e.calendar,s.raw FROM events e LEFT JOIN event_schedules s ON s.event_id=e.id WHERE e.id=?");
        query.addBindValue(id);
        if (!query.exec() || !query.next()) { setError("此日程已不存在，请关闭后重新打开。"); return false; }
        if (event.value("scope").toString() == "occurrence" && query.value(0).toString() != calendar) {
            setError("单次重复日程不能更换日历，请选择整个系列。"); return false;
        }
        original = query.value(1).toByteArray();
        if (!query.value(1).isNull() && (original.isEmpty() || ICalResource::uid(original) != id)) {
            setError("日程的系列数据无效，已停止保存。"); return false;
        }
        query.finish();
    }
    QString error;
    const auto raw = ICalResource::edit(original, event, id, &error);
    if (raw.isEmpty()) { setError(error); return false; }
    const auto master = ICalResource::fields(raw);
    if (master.isEmpty()) { setError("无法读取保存后的日程。"); return false; }
    const bool extended = master.value("date") != master.value("endDate") ||
        master.value("recurrence").toMap().value("frequency").toString() != "none" || !master.value("location").toString().isEmpty();
    if (!m_db.transaction()) { setError("无法开始保存日程。"); return false; }
    auto reject = [this](const QString &message) { m_db.rollback(); setError(message); return false; };
    query.prepare("INSERT INTO events(id,title,date,all_day,start_time,end_time,calendar,notes) "
                  "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                  "title=excluded.title,date=excluded.date,all_day=excluded.all_day,"
                  "start_time=excluded.start_time,end_time=excluded.end_time,calendar=excluded.calendar,notes=excluded.notes");
    for (const QVariant &value : QVariantList {id, master.value("title"), master.value("date"), master.value("allDay"),
            master.value("start"), master.value("end"), calendar, master.value("notes")}) {
        query.addBindValue(value.metaType().id() == QMetaType::QString && value.toString().isNull() ? QVariant(QStringLiteral("")) : value);
    }
    if (!query.exec()) return reject("保存失败：" + query.lastError().text());
    if (extended) {
        query.prepare("INSERT INTO event_schedules(event_id,raw) VALUES(?,?) ON CONFLICT(event_id) DO UPDATE SET raw=excluded.raw");
        query.addBindValue(id); query.addBindValue(raw);
    } else {
        query.prepare("DELETE FROM event_schedules WHERE event_id=?"); query.addBindValue(id);
    }
    if (!query.exec() || !m_db.commit()) return reject("无法保存日程的时间和重复规则。");
    m_selected = QDate::fromString(event.value("date").toString(), Qt::ISODate);
    m_month = QDate(m_selected.year(), m_selected.month(), 1);
    reload();
    return true;
}

bool CalendarStore::deleteEvent(const QString &id, const QString &recurrenceId)
{
    clearError();
    if (id.startsWith("icloud:") && m_icloud) {
        QString error;
        if (!m_icloud->deleteEvent(id, recurrenceId, &error)) { setError(error); return false; }
        return true;
    }
    QSqlQuery query(m_db);
    if (!m_db.transaction()) { setError("无法开始删除日程。"); return false; }
    auto reject = [this](const QString &message) { m_db.rollback(); setError(message); return false; };
    if (recurrenceId.isEmpty()) {
        query.prepare("DELETE FROM event_schedules WHERE event_id=?"); query.addBindValue(id);
        if (!query.exec()) return reject("无法删除日程的时间和重复规则。");
        query.prepare("DELETE FROM events WHERE id=?"); query.addBindValue(id);
    } else {
        query.prepare("SELECT raw FROM event_schedules WHERE event_id=?"); query.addBindValue(id);
        if (!query.exec() || !query.next()) return reject("此重复日程已不存在。");
        QString error;
        const auto raw = ICalResource::exclude(query.value(0).toByteArray(), recurrenceId, &error);
        query.finish();
        if (raw.isEmpty()) return reject(error);
        query.prepare("UPDATE event_schedules SET raw=? WHERE event_id=?"); query.addBindValue(raw); query.addBindValue(id);
    }
    if (!query.exec() || query.numRowsAffected() != 1 || !m_db.commit()) return reject("无法保存删除操作，请重新打开日程。");
    reload();
    return true;
}

void CalendarStore::seedPreview()
{
    QSqlQuery query(m_db);
    struct Sample { int day; const char *title; const char *start; const char *end; const char *calendar; const char *notes; };
    const Sample samples[] = {
        {2,"休息","","","life",""},
        {5,"本月计划","09:30","10:30","work","整理本月任务和截止日期。"},
        {7,"看电影","19:00","21:00","life","提前取票。"},
        {9,"整理照片","","","ideas","选出要打印的照片。"},
        {12,"产品讨论","10:00","11:00","work","确认下一版的功能范围。"},
        {14,"给植物浇水","","","life",""},
        {15,"设计讨论","10:00","11:00","work","确认月历布局和配色。"},
        {15,"散步买花","17:30","18:15","life","回家前去一趟花店。"},
        {15,"记录想法","","","ideas","整理桌面日历的使用建议。"},
        {16,"本周小结","16:00","16:30","work","整理进度和下周任务。"},
        {18,"和朋友吃饭","11:00","12:30","life","确认餐厅和人数。"},
        {20,"交互评审","14:00","15:00","work","检查新建、编辑和搜索流程。"},
        {22,"阅读","","","ideas","读完第三章。"},
        {24,"去公园","09:00","11:00","life","带上相机。"},
        {27,"整理笔记","","","ideas","按主题归类。"},
        {28,"项目回顾","15:00","16:00","work","检查本月进度。"}
    };
    if (!m_db.transaction()) { setError(m_db.lastError().text()); return; }
    for (const auto &sample : samples) {
        const QDate date(m_month.year(), m_month.month(), qMin(sample.day, m_month.daysInMonth()));
        query.prepare("INSERT INTO events VALUES(?,?,?,?,?,?,?,?)");
        for (const QVariant &value : QVariantList {
                QUuid::createUuid().toString(QUuid::WithoutBraces), QString::fromUtf8(sample.title),
                date.toString(Qt::ISODate), sample.start[0] == '\0', QString::fromUtf8(sample.start),
                QString::fromUtf8(sample.end), QString::fromUtf8(sample.calendar), QString::fromUtf8(sample.notes)})
            query.addBindValue(value);
        if (!query.exec()) { m_db.rollback(); setError(query.lastError().text()); return; }
    }
    if (!m_db.commit()) setError(m_db.lastError().text());
}
