#include "calendarstore.h"

#include <QDir>
#include <QLocale>
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
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(directory + "/calendar.sqlite");
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
    if (m_demo)
        seedPreview();
    reload();
    return m_error.isEmpty();
}

QString CalendarStore::calendarColor(const QString &id)
{
    if (id == "work") return "#218572";
    if (id == "life") return "#C28B45";
    return "#8273B3";
}

QString CalendarStore::calendarName(const QString &id)
{
    if (id == "work") return "工作";
    if (id == "life") return "生活";
    return "灵感";
}

void CalendarStore::reload()
{
    QSqlQuery query(m_db);
    if (!query.exec("SELECT id,title,date,all_day,start_time,end_time,calendar,notes "
                    "FROM events ORDER BY date, all_day DESC, start_time, title")) {
        setError(query.lastError().text());
        return;
    }
    m_events.clear();
    while (query.next()) {
        QVariantMap event;
        event["id"] = query.value(0);
        event["title"] = query.value(1);
        event["date"] = query.value(2);
        event["allDay"] = query.value(3).toBool();
        event["start"] = query.value(4);
        event["end"] = query.value(5);
        event["calendar"] = query.value(6);
        event["notes"] = query.value(7);
        event["color"] = calendarColor(query.value(6).toString());
        event["calendarName"] = calendarName(query.value(6).toString());
        event["timeLabel"] = query.value(3).toBool() ? "全天" : query.value(4).toString();
        event["timeRange"] = query.value(3).toBool() ? "全天日程" :
            query.value(4).toString() + " — " + query.value(5).toString();
        const QDate date = QDate::fromString(query.value(2).toString(), Qt::ISODate);
        event["dateLabel"] = QLocale("zh_CN").toString(date, "M月d日 ddd");
        m_events.append(event);
    }
    emit changed();
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
            event.value("notes").toString().contains(m_search, Qt::CaseInsensitive));
}

QVariantList CalendarStore::eventsOn(const QDate &date) const
{
    QVariantList result;
    for (const QVariant &value : m_events) {
        const QVariantMap event = value.toMap();
        if (event.value("date").toString() == date.toString(Qt::ISODate) && includes(event))
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
            {"today", date == QDate::currentDate()}, {"selected", date == m_selected},
            {"weekend", date.dayOfWeek() > 5}, {"events", eventsOn(date)}
        });
    }
    return result;
}

QVariantList CalendarStore::calendars() const
{
    QVariantList result;
    for (const QString &id : {QString("work"), QString("life"), QString("ideas")}) {
        int count = 0;
        for (const QVariant &value : m_events)
            if (value.toMap().value("calendar").toString() == id) ++count;
        result.append(QVariantMap {{"id", id}, {"name", calendarName(id)},
            {"color", calendarColor(id)}, {"enabled", m_enabled.contains(id)}, {"count", count}});
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
        if (includes(event) && (!m_search.isEmpty() || (date.year() == m_month.year() && date.month() == m_month.month())))
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
        if (date.year() == m_month.year() && date.month() == m_month.month() && includes(event)) ++count;
    }
    return count;
}

void CalendarStore::changeMonth(int offset) { m_month = m_month.addMonths(offset); emit changed(); }
void CalendarStore::goToday()
{
    m_selected = QDate::currentDate();
    m_month = QDate(m_selected.year(), m_selected.month(), 1);
    emit changed();
}
void CalendarStore::selectDate(const QString &iso)
{
    const QDate date = QDate::fromString(iso, Qt::ISODate);
    if (!date.isValid()) return;
    m_selected = date;
    emit changed();
}
void CalendarStore::toggleCalendar(const QString &id)
{
    if (m_enabled.contains(id)) m_enabled.remove(id);
    else m_enabled.insert(id);
    emit changed();
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
    if (calendar != "work" && calendar != "life" && calendar != "ideas") {
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
    if (!query.exec("SELECT COUNT(*) FROM events")) { setError(query.lastError().text()); return; }
    if (!query.next() || query.value(0).toInt() != 0) return;
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
