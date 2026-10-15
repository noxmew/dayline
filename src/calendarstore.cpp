#include "calendarstore.h"
#include "icloudsync.h"

#include <QDir>
#include <QColor>
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
}

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
    if (m_demo && newDatabase)
        seedPreview();
    reload();
    return m_error.isEmpty();
}

void CalendarStore::reload()
{
    QSqlQuery query(m_db);
    if (!query.exec("SELECT id,name,color,enabled FROM local_calendars ORDER BY sort_order,id")) {
        setError("无法读取本机日历分类。"); return;
    }
    QVariantList localCalendars;
    QSet<QString> enabled;
    while (query.next()) {
        localCalendars.append(QVariantMap {{"id",query.value(0)}, {"name",query.value(1)}, {"color",query.value(2)},
            {"enabled",query.value(3).toBool()}, {"cloud",false}});
        if (query.value(3).toBool()) enabled.insert(query.value(0).toString());
    }
    if (localCalendars.isEmpty()) { setError("本机日历分类为空，请检查本机数据。"); return; }
    if (!query.exec("SELECT e.id,e.title,e.date,e.all_day,e.start_time,e.end_time,e.calendar,e.notes,c.color,c.name,c.id "
                    "FROM events e LEFT JOIN local_calendars c ON c.id=e.calendar ORDER BY e.date,e.all_day DESC,e.start_time,e.title")) {
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
        event["timeLabel"] = query.value(3).toBool() ? "全天" : query.value(4).toString();
        event["timeRange"] = query.value(3).toBool() ? "全天日程" :
            query.value(4).toString() + " — " + query.value(5).toString();
        const QDate date = QDate::fromString(query.value(2).toString(), Qt::ISODate);
        event["dateLabel"] = QLocale("zh_CN").toString(date, "M月d日 ddd");
        events.append(event);
    }
    if (m_icloud) {
        const auto cloudEvents = m_icloud->events(m_month.addMonths(-1), m_month.addMonths(3));
        for (const auto &item : cloudEvents) {
            auto event = item.toMap();
            QString displayEnd = event.value("endDate", event.value("date")).toString();
            if (!event.value("allDay").toBool() && event.value("end").toString() == "00:00" && displayEnd > event.value("date").toString())
                displayEnd = QDate::fromString(displayEnd, Qt::ISODate).addDays(-1).toString(Qt::ISODate);
            event["displayEndDate"] = displayEnd;
            event["timeLabel"] = event.value("allDay").toBool() ? "全天" : event.value("start").toString();
            event["timeRange"] = event.value("allDay").toBool() ? "全天日程" : event.value("start").toString() + " — " + event.value("end").toString();
            event["dateLabel"] = QLocale("zh_CN").toString(QDate::fromString(event.value("date").toString(), Qt::ISODate), "M月d日 ddd");
            events.append(event);
        }
        std::stable_sort(events.begin(), events.end(), [](const QVariant &a, const QVariant &b) {
            const auto x = a.toMap(), y = b.toMap();
            if (x.value("date") != y.value("date")) return x.value("date").toString() < y.value("date").toString();
            if (x.value("allDay") != y.value("allDay")) return x.value("allDay").toBool();
            return x.value("start").toString() < y.value("start").toString();
        });
    }
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
    return (event.value("cloud").toBool() || m_enabled.contains(event.value("calendar").toString())) &&
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
    QVariantList result;
    for (const auto &value : m_localCalendars) {
        auto calendar = value.toMap();
        int count = 0;
        for (const auto &event : m_events)
            if (event.toMap().value("calendar") == calendar.value("id")) ++count;
        calendar["count"] = count;
        result.append(calendar);
    }
    return result;
}

QVariantList CalendarStore::calendars() const
{
    QVariantList result = localCalendars();
    if (m_icloud) for (const auto &value : m_icloud->calendars()) {
        auto calendar = value.toMap();
        if (!calendar.value("selected").toBool()) continue;
        int count = 0;
        for (const auto &event : m_events) if (event.toMap().value("calendar") == calendar.value("id")) ++count;
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
    if (id.startsWith("icloud:") && m_icloud) {
        for (const auto &value : m_icloud->calendars()) {
            const auto calendar = value.toMap();
            if (calendar.value("id").toString() == id) m_icloud->selectCalendar(id, !calendar.value("selected").toBool());
        }
        return;
    }
    clearError();
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
    for (const auto &value : m_events) {
        const auto event = value.toMap();
        if (event.value("id").toString() == id) return event.value("calendar").toString();
    }
    return {};
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
    const QString title = event.value("title").toString().trimmed();
    const QDate date = QDate::fromString(event.value("date").toString(), Qt::ISODate);
    const bool allDay = event.value("allDay").toBool();
    const QString start = event.value("start").toString();
    const QString end = event.value("end").toString();
    const QString calendar = event.value("calendar").toString();
    if (title.isEmpty()) { setError("给这件事起个名字吧。"); return false; }
    if (!date.isValid()) { setError("请输入有效日期，格式为 YYYY-MM-DD。"); return false; }
    if (!allDay && (!QTime::fromString(start, "HH:mm").isValid() ||
                    !QTime::fromString(end, "HH:mm").isValid() || start >= end)) {
        setError("请填写有效的时间，结束时间需晚于开始时间。"); return false;
    }
    const bool validCalendar = std::any_of(m_localCalendars.cbegin(), m_localCalendars.cend(), [&calendar](const QVariant &value) {
        return value.toMap().value("id").toString() == calendar;
    });
    if (!validCalendar) {
        setError("请选择一个有效的本机日历。"); return false;
    }
    QString id = event.value("id").toString();
    if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO events(id,title,date,all_day,start_time,end_time,calendar,notes) "
                  "VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                  "title=excluded.title,date=excluded.date,all_day=excluded.all_day,"
                  "start_time=excluded.start_time,end_time=excluded.end_time,calendar=excluded.calendar,notes=excluded.notes");
    for (const QVariant &value : QVariantList {id, title, date.toString(Qt::ISODate), allDay,
            allDay ? QString("") : start, allDay ? QString("") : end, calendar,
            event.value("notes", QString("")).toString()}) query.addBindValue(value);
    if (!query.exec()) { setError("保存失败：" + query.lastError().text()); return false; }
    m_selected = date;
    m_month = QDate(date.year(), date.month(), 1);
    reload();
    return true;
}

bool CalendarStore::deleteEvent(const QString &id)
{
    clearError();
    if (id.startsWith("icloud:") && m_icloud) {
        QString error;
        if (!m_icloud->deleteEvent(id, &error)) { setError(error); return false; }
        return true;
    }
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM events WHERE id=?");
    query.addBindValue(id);
    if (!query.exec()) { setError("删除失败：" + query.lastError().text()); return false; }
    reload();
    return true;
}

void CalendarStore::seedPreview()
{
    QSqlQuery query(m_db);
    struct Sample { int day; const char *title; const char *start; const char *end; const char *calendar; const char *notes; };
    const Sample samples[] = {
        {2,"留一天给自己","","","life","散散步，读几页喜欢的书。"},
        {5,"十月计划","09:30","10:30","work","整理这个月想完成的三件事。"},
        {7,"去看一场电影","19:00","21:00","life","在忙碌之外，也记得好好生活。"},
        {9,"收集秋日灵感","","","ideas","光线、街角和一些偶然的发现。"},
        {12,"产品方向讨论","10:00","11:00","work","让每一天的安排更清晰。"},
        {14,"给植物浇水","","","life","窗台上的绿意也需要照顾。"},
        {15,"日笺 · 设计碰头","10:00","11:00","work","聊聊月历的留白、配色与交互。"},
        {15,"散步，顺便买束花","17:30","18:15","life","去常去的花店，挑一束秋天。"},
        {15,"记下一个好点子","","","ideas","让日历像桌面上的一张小纸笺。"},
        {16,"本周小结","16:00","16:30","work","回顾完成的事情，也留意新的想法。"},
        {18,"和朋友吃早午餐","11:00","12:30","life","好好吃饭，慢慢聊天。"},
        {20,"日历交互评审","14:00","15:00","work","把日常操作做得自然一点。"},
        {22,"读完一本书","","","ideas","不赶进度，享受阅读。"},
        {24,"去公园看秋天","09:00","11:00","life","带上相机，走一条没走过的路。"},
        {27,"整理灵感清单","","","ideas","为下一个月留一点期待。"},
        {28,"月末项目回顾","15:00","16:00","work","记录收获，准备下一步。"}
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
