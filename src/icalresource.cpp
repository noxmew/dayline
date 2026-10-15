#include "icalresource.h"

#include <QDateTime>
#include <QSet>
#include <QTimeZone>
#include <libical/ical.h>
#include <memory>

namespace {
using Component = std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)>;
Component parse(const QByteArray &raw) { return {icalparser_parse_string(raw.constData()), icalcomponent_free}; }
QString property(icalcomponent *event, icalproperty_kind kind)
{
    auto *p = icalcomponent_get_first_property(event, kind);
    if (!p) return {};
    switch (kind) {
    case ICAL_SUMMARY_PROPERTY: return QString::fromUtf8(icalproperty_get_summary(p));
    case ICAL_DESCRIPTION_PROPERTY: return QString::fromUtf8(icalproperty_get_description(p));
    case ICAL_LOCATION_PROPERTY: return QString::fromUtf8(icalproperty_get_location(p));
    case ICAL_UID_PROPERTY: return QString::fromUtf8(icalproperty_get_uid(p));
    default: return {};
    }
}
icaltimezone *componentTimezone(icalcomponent *root, icalcomponent *event, icalproperty_kind kind)
{
    auto *p = icalcomponent_get_first_property(event, kind);
    auto *tz = p ? icalproperty_get_first_parameter(p, ICAL_TZID_PARAMETER) : nullptr;
    if (!tz) return nullptr;
    const char *id = icalparameter_get_tzid(tz);
    auto *zone = icalcomponent_get_timezone(root, id);
    return zone ? zone : icaltimezone_get_builtin_timezone(id);
}
// Resolve every referenced zone before expansion. An unknown TZID must never
// silently become a floating time. Builtin definitions are attached only to
// this parsed display copy, leaving the stored calendar resource untouched.
bool prepareZones(icalcomponent *root)
{
    QSet<QString> ids;
    for (auto *event = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
        for (auto *p = icalcomponent_get_first_property(event, ICAL_ANY_PROPERTY); p;
             p = icalcomponent_get_next_property(event, ICAL_ANY_PROPERTY)) {
            if (auto *parameter = icalproperty_get_first_parameter(p, ICAL_TZID_PARAMETER))
                ids.insert(QString::fromUtf8(icalparameter_get_tzid(parameter)));
        }
    }
    for (const auto &id : ids) {
        const auto name = id.toUtf8();
        if (icalcomponent_get_timezone(root, name.constData())) continue;
        auto *zone = icaltimezone_get_builtin_timezone(name.constData());
        if (!zone || !icaltimezone_get_component(zone)) return false;
        auto *definition = icalcomponent_new_clone(icaltimezone_get_component(zone));
        auto *tzid = icalcomponent_get_first_property(definition, ICAL_TZID_PROPERTY);
        if (!tzid) { icalcomponent_free(definition); return false; }
        icalproperty_set_tzid(tzid, name.constData());
        icalcomponent_add_component(root, definition);
    }
    return true;
}
bool editable(icalcomponent *root, icalcomponent *event)
{
    if (!event || icalcomponent_isa(root) != ICAL_VCALENDAR_COMPONENT ||
        icalcomponent_count_components(root, ICAL_VEVENT_COMPONENT) != 1) return false;
    for (auto kind : {ICAL_RRULE_PROPERTY, ICAL_RDATE_PROPERTY, ICAL_EXDATE_PROPERTY,
                      ICAL_EXRULE_PROPERTY, ICAL_RECURRENCEID_PROPERTY, ICAL_ATTENDEE_PROPERTY,
                      ICAL_ORGANIZER_PROPERTY, ICAL_DURATION_PROPERTY})
        if (icalcomponent_get_first_property(event, kind)) return false;
    if (icalcomponent_get_first_property(root, ICAL_METHOD_PROPERTY)) return false;
    if (icalcomponent_count_errors(root)) return false;
    if (!icalcomponent_get_first_property(event, ICAL_DTSTART_PROPERTY) ||
        !icalcomponent_get_first_property(event, ICAL_DTEND_PROPERTY)) return false;
    const auto start = icalcomponent_get_dtstart(event);
    const auto end = icalcomponent_get_dtend(event);
    if (!icaltime_is_valid_time(start) || !icaltime_is_valid_time(end) || start.is_date != end.is_date) return false;
    for (auto kind : {ICAL_DTSTART_PROPERTY, ICAL_DTEND_PROPERTY}) {
        auto *p = icalcomponent_get_first_property(event, kind);
        if (icalproperty_get_first_parameter(p, ICAL_TZID_PARAMETER) && !componentTimezone(root, event, kind)) return false;
    }
    return true;
}
QDate date(icaltimetype t) { return QDate(t.year, t.month, t.day); }
QDateTime localTime(icalcomponent *root, icalcomponent *event, icaltimetype t, icalproperty_kind kind)
{
    if (t.is_date) return QDateTime(date(t), QTime(0, 0));
    if (icaltime_is_utc(t)) return QDateTime(date(t), QTime(t.hour, t.minute, t.second), Qt::UTC).toLocalTime();
    if (auto *zone = componentTimezone(root, event, kind))
        return QDateTime::fromSecsSinceEpoch(icaltime_as_timet_with_zone(t, zone)).toLocalTime();
    if (const auto *zone = icaltime_get_timezone(t))
        return QDateTime::fromSecsSinceEpoch(icaltime_as_timet_with_zone(t, zone)).toLocalTime();
    return QDateTime(date(t), QTime(t.hour, t.minute, t.second));
}
struct Expansion {
    QVariantList result;
    QVariantMap base;
    QDate from, until;
    bool allDay;
    icaltimezone *zone;
    bool floating;
    QSet<qint64> overrides;
    QSet<qint64> seen;
};
void occurrence(icalcomponent *, icaltime_span *span, void *context)
{
    auto *x = static_cast<Expansion *>(context);
    if (x->overrides.contains(span->start) || x->seen.contains(span->start)) return;
    x->seen.insert(span->start);
    QDateTime start = QDateTime::fromSecsSinceEpoch(span->start, Qt::UTC);
    QDateTime end = QDateTime::fromSecsSinceEpoch(span->end, Qt::UTC);
    if (x->allDay || x->floating) {
        const auto first = icaltime_from_timet_with_zone(span->start, x->allDay, x->zone);
        const auto last = icaltime_from_timet_with_zone(span->end, x->allDay, x->zone);
        start = QDateTime(date(first), QTime(first.hour, first.minute, first.second));
        end = QDateTime(date(last), QTime(last.hour, last.minute, last.second));
    } else { start = start.toLocalTime(); end = end.toLocalTime(); }
    if (start.date() >= x->until || end.date() < x->from) return;
    auto event = x->base;
    event["date"] = start.date().toString(Qt::ISODate);
    event["endDate"] = (x->allDay ? end.date().addDays(-1) : end.date()).toString(Qt::ISODate);
    event["allDay"] = x->allDay;
    event["start"] = x->allDay ? "" : start.time().toString("HH:mm");
    event["end"] = x->allDay ? "" : end.time().toString("HH:mm");
    x->result.append(event);
}
void replace(icalcomponent *event, icalproperty_kind kind, icalproperty *replacement)
{
    while (auto *old = icalcomponent_get_first_property(event, kind)) {
        icalcomponent_remove_property(event, old); icalproperty_free(old);
    }
    icalcomponent_add_property(event, replacement);
}
void updateText(icalcomponent *event, icalproperty_kind kind, QString value, bool trim = false)
{
    const QString previous = property(event, kind);
    if (previous == value) return;
    if (trim) value = value.trimmed();
    if (previous == value) return;
    auto *p = icalcomponent_get_first_property(event, kind);
    if (!p) {
        p = icalproperty_new(kind);
        icalcomponent_add_property(event, p);
    }
    // The alternate representation describes the old text. Keep other
    // parameters and any additional properties that the editor did not show.
    icalproperty_remove_parameter_by_kind(p, ICAL_ALTREP_PARAMETER);
    const auto text = value.toUtf8();
    switch (kind) {
    case ICAL_SUMMARY_PROPERTY: icalproperty_set_summary(p, text.constData()); break;
    case ICAL_DESCRIPTION_PROPERTY: icalproperty_set_description(p, text.constData()); break;
    case ICAL_LOCATION_PROPERTY: icalproperty_set_location(p, text.constData()); break;
    default: break;
    }
}
icalproperty *timeProperty(icalcomponent *root, icalcomponent *event, icalproperty_kind kind,
                          const QDate &day, const QTime &time, bool allDay)
{
    auto *old = icalcomponent_get_first_property(event, kind);
    const auto previous = old ? (kind == ICAL_DTSTART_PROPERTY ? icalproperty_get_dtstart(old) : icalproperty_get_dtend(old)) : icaltime_null_time();
    auto value = icaltime_null_time();
    if (allDay) {
        value.year = day.year(); value.month = day.month(); value.day = day.day(); value.is_date = 1;
    } else if (old && !previous.is_date && !icaltime_is_utc(previous) &&
               !icalproperty_get_first_parameter(old, ICAL_TZID_PARAMETER)) {
        value.year = day.year(); value.month = day.month(); value.day = day.day();
        value.hour = time.hour(); value.minute = time.minute(); value.second = time.second();
    } else {
        auto *zone = old && !previous.is_date ? componentTimezone(root, event, kind) : nullptr;
        value = icaltime_from_timet_with_zone(QDateTime(day, time).toSecsSinceEpoch(), 0,
                                              zone ? zone : icaltimezone_get_utc_timezone());
    }
    if (old && bool(previous.is_date) == allDay) {
        const auto displayed = localTime(root, event, previous, kind);
        if (displayed.date() == day && (allDay || displayed.time().toString("HH:mm") == time.toString("HH:mm")))
            value = previous; // Keep seconds when only text fields were edited.
    }
    auto *result = kind == ICAL_DTSTART_PROPERTY ? icalproperty_new_dtstart(value) : icalproperty_new_dtend(value);
    if (old) {
        for (auto *p = icalproperty_get_first_parameter(old, ICAL_ANY_PARAMETER); p;
             p = icalproperty_get_next_parameter(old, ICAL_ANY_PARAMETER)) {
            const auto type = icalparameter_isa(p);
            if (type == ICAL_VALUE_PARAMETER || (type == ICAL_TZID_PARAMETER && (allDay || previous.is_date))) continue;
            icalproperty_add_parameter(result, icalparameter_new_clone(p));
        }
    }
    return result;
}
QByteArray serialize(icalcomponent *root)
{
    char *serialized = icalcomponent_as_ical_string_r(root);
    if (!serialized) return {};
    const QByteArray result(serialized); icalmemory_free_buffer(serialized);
    return result;
}
}

QVariantList ICalResource::occurrences(const QByteArray &raw, const QDate &from, const QDate &until, QString *error)
{
    auto root = parse(raw);
    if (!root || icalcomponent_isa(root.get()) != ICAL_VCALENDAR_COMPONENT || icalcomponent_count_errors(root.get())) {
        if (error) *error = "无法完整解析日程；已保留原始内容并停止写入。";
        return {};
    }
    if (!prepareZones(root.get())) {
        if (error) *error = "日程引用了无法识别的时区，已保留原文并暂停展示和写入。";
        return {};
    }
    QVariantList result;
    QSet<qint64> overrides;
    icalcomponent *master = nullptr;
    for (auto *event = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT)) {
        if (!icalcomponent_get_first_property(event, ICAL_RECURRENCEID_PROPERTY)) {
            if (master) { if (error) *error = "一个日程资源含多个主事件，已保留原文。"; return {}; }
            master = event;
        }
        for (auto *p = icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY); p;
             p = icalcomponent_get_next_property(event, ICAL_RRULE_PROPERTY)) {
            const auto rule = icalproperty_get_rrule(p);
            if (rule.freq == ICAL_SECONDLY_RECURRENCE || rule.freq == ICAL_MINUTELY_RECURRENCE || rule.freq == ICAL_HOURLY_RECURRENCE) {
                if (error) *error = "当前预览版尚不展开小时、分钟或秒级重复日程，原始内容已保留。";
                return {};
            }
        }
        for (auto *p = icalcomponent_get_first_property(event, ICAL_RDATE_PROPERTY); p;
             p = icalcomponent_get_next_property(event, ICAL_RDATE_PROPERTY)) {
            if (!icaltime_is_null_time(icalproperty_get_rdate(p).period.start)) {
                if (error) *error = "当前预览版尚不展开带独立时段的重复日期，原始内容已保留。";
                return {};
            }
        }
    }
    for (auto *event = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT)) {
        auto rid = icalcomponent_get_recurrenceid(event);
        if (!icaltime_is_null_time(rid)) {
            auto *p = icalcomponent_get_first_property(event, ICAL_RECURRENCEID_PROPERTY);
            if (icalproperty_get_first_parameter(p, ICAL_RANGE_PARAMETER)) {
                if (error) *error = "当前预览版尚不展开影响此后系列的重复例外，原始内容已保留。";
                return {};
            }
            if (master && property(master, ICAL_UID_PROPERTY) != property(event, ICAL_UID_PROPERTY)) {
                if (error) *error = "重复日程的事件标识不一致，原始内容已保留。";
                return {};
            }
            auto *zone = componentTimezone(root.get(), event, ICAL_RECURRENCEID_PROPERTY);
            if (icaltime_is_utc(rid)) zone = icaltimezone_get_utc_timezone();
            else if (!rid.is_date && !zone && master) {
                zone = componentTimezone(root.get(), master, ICAL_DTSTART_PROPERTY);
                if (icaltime_is_utc(icalcomponent_get_dtstart(master))) zone = icaltimezone_get_utc_timezone();
            }
            overrides.insert(icaltime_as_timet_with_zone(rid, zone));
        }
    }
    for (auto *event = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT)) {
        if (icalcomponent_get_status(event) == ICAL_STATUS_CANCELLED) continue;
        const auto start = icalcomponent_get_dtstart(event);
        if (icaltime_is_null_time(start) || !icaltime_is_valid_time(start)) { if (error) *error = "日程没有有效开始时间，原始内容已保留。"; continue; }
        const bool readOnly = !editable(root.get(), event);
        QVariantMap base {{"title", property(event, ICAL_SUMMARY_PROPERTY)}, {"notes", property(event, ICAL_DESCRIPTION_PROPERTY)},
            {"location", property(event, ICAL_LOCATION_PROPERTY)}, {"uid", property(event, ICAL_UID_PROPERTY)},
            {"readOnly", readOnly}, {"readOnlyReason", readOnly ? "此日程含重复规则、会议邀请或当前不支持的结构，已保留原文并以只读方式展示。" : ""}};
        if (!icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY) &&
            !icalcomponent_get_first_property(event, ICAL_RDATE_PROPERTY)) {
            auto recurrenceStart = start;
            auto candidate = start;
            if (icalproperty_recurrence_is_excluded(event, &recurrenceStart, &candidate)) continue;
            const auto begin = localTime(root.get(), event, start, ICAL_DTSTART_PROPERTY);
            auto end = icalcomponent_get_dtend(event);
            if (!icalcomponent_get_first_property(event, ICAL_DTEND_PROPERTY)) {
                auto *duration = icalcomponent_get_first_property(event, ICAL_DURATION_PROPERTY);
                end = icaltime_add(start, duration ? icalproperty_get_duration(duration) : icaldurationtype_from_int(start.is_date ? 86400 : 0));
            }
            if (icaltime_is_null_time(end)) end = icaltime_add(start, icaldurationtype_from_int(start.is_date ? 86400 : 0));
            const auto finish = localTime(root.get(), event, end, ICAL_DTEND_PROPERTY);
            if (begin.date() >= until || finish.date() < from) continue;
            base["date"] = begin.date().toString(Qt::ISODate);
            base["endDate"] = (start.is_date ? finish.date().addDays(-1) : finish.date()).toString(Qt::ISODate);
            base["allDay"] = bool(start.is_date);
            base["start"] = start.is_date ? "" : begin.time().toString("HH:mm");
            base["end"] = start.is_date ? "" : finish.time().toString("HH:mm");
            result.append(base); continue;
        }
        auto *zone = componentTimezone(root.get(), event, ICAL_DTSTART_PROPERTY);
        Expansion expanded {{}, base, from, until, bool(start.is_date), zone,
            !icaltime_is_utc(start) && !zone, overrides, {}};
        const auto begin = icaltime_from_timet_with_zone(QDateTime(from.addDays(-1), QTime(0,0), Qt::UTC).toSecsSinceEpoch(), 0, icaltimezone_get_utc_timezone());
        const auto finish = icaltime_from_timet_with_zone(QDateTime(until.addDays(1), QTime(0,0), Qt::UTC).toSecsSinceEpoch(), 0, icaltimezone_get_utc_timezone());
        icalcomponent_foreach_recurrence(event, begin, finish, occurrence, &expanded);
        result.append(expanded.result);
    }
    return result;
}

QByteArray ICalResource::edit(const QByteArray &raw, const QVariantMap &fields, const QString &uid, QString *error)
{
    auto root = raw.isEmpty() ? Component(icalcomponent_new_vcalendar(), icalcomponent_free) : parse(raw);
    if (!root) { *error = "日程原始内容无法解析，不能写入。"; return {}; }
    auto *event = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT);
    if (!raw.isEmpty() && (!event || !editable(root.get(), event))) { *error = "当前日程只能查看，不能在日笺中修改。"; return {}; }
    if (!event) {
        icalcomponent_add_property(root.get(), icalproperty_new_version("2.0"));
        icalcomponent_add_property(root.get(), icalproperty_new_prodid("-//Dayline//Calendar//ZH-CN"));
        event = icalcomponent_new_vevent(); icalcomponent_add_component(root.get(), event);
        icalcomponent_add_property(event, icalproperty_new_uid(uid.toUtf8().constData()));
    }
    const bool allDay = fields.value("allDay").toBool();
    const QDate day = QDate::fromString(fields.value("date").toString(), Qt::ISODate);
    const QDate last = QDate::fromString(fields.value("endDate", day.toString(Qt::ISODate)).toString(), Qt::ISODate);
    const QTime start = QTime::fromString(fields.value("start").toString(), "HH:mm");
    const QTime end = QTime::fromString(fields.value("end").toString(), "HH:mm");
    if (fields.value("title").toString().trimmed().isEmpty() || !day.isValid() || !last.isValid() || last < day ||
        (!allDay && (!start.isValid() || !end.isValid() || !QDateTime(day, start).isValid() || !QDateTime(last, end).isValid() || QDateTime(last, end) <= QDateTime(day, start)))) {
        *error = "请填写标题和有效的日期、时间范围。"; return {};
    }
    // Preserve floating, UTC and TZID semantics, including untouched parameters.
    auto *startProperty = timeProperty(root.get(), event, ICAL_DTSTART_PROPERTY, day, start, allDay);
    auto *endProperty = timeProperty(root.get(), event, ICAL_DTEND_PROPERTY, allDay ? last.addDays(1) : last, end, allDay);
    replace(event, ICAL_DTSTART_PROPERTY, startProperty);
    replace(event, ICAL_DTEND_PROPERTY, endProperty);
    updateText(event, ICAL_SUMMARY_PROPERTY, fields.value("title").toString(), true);
    if (fields.contains("notes")) updateText(event, ICAL_DESCRIPTION_PROPERTY, fields.value("notes").toString());
    if (fields.contains("location")) updateText(event, ICAL_LOCATION_PROPERTY, fields.value("location").toString());
    const auto now = icaltime_from_timet_with_zone(QDateTime::currentSecsSinceEpoch(), 0, icaltimezone_get_utc_timezone());
    replace(event, ICAL_DTSTAMP_PROPERTY, icalproperty_new_dtstamp(now));
    replace(event, ICAL_LASTMODIFIED_PROPERTY, icalproperty_new_lastmodified(now));
    auto *sequence = icalcomponent_get_first_property(event, ICAL_SEQUENCE_PROPERTY);
    replace(event, ICAL_SEQUENCE_PROPERTY, icalproperty_new_sequence(sequence ? icalproperty_get_sequence(sequence) + 1 : 0));
    return serialize(root.get());
}

bool ICalResource::canEdit(const QByteArray &raw)
{
    auto root = parse(raw);
    return root && editable(root.get(), icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT));
}

QByteArray ICalResource::copy(const QByteArray &raw, const QString &uid, QString *error)
{
    auto root = parse(raw);
    auto *event = root ? icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT) : nullptr;
    if (uid.isEmpty() || !event || !editable(root.get(), event)) {
        if (error) *error = "此日程结构无法安全另存，原始内容已保留。";
        return {};
    }
    replace(event, ICAL_UID_PROPERTY, icalproperty_new_uid(uid.toUtf8().constData()));
    return serialize(root.get());
}

QString ICalResource::title(const QByteArray &raw)
{
    auto root = parse(raw);
    auto *event = root ? icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT) : nullptr;
    return event ? property(event, ICAL_SUMMARY_PROPERTY) : QStringLiteral("无法解析的日程");
}

QString ICalResource::uid(const QByteArray &raw)
{
    auto root = parse(raw);
    if (!root || icalcomponent_isa(root.get()) != ICAL_VCALENDAR_COMPONENT || icalcomponent_count_errors(root.get())) return {};
    QString result;
    for (auto *event = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT)) {
        const QString current = property(event, ICAL_UID_PROPERTY);
        if (current.isEmpty() || (!result.isEmpty() && result != current)) return {};
        result = current;
    }
    return result;
}
