#include "icalresource.h"
#include "workdaycalendar.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QTimeZone>
#include <libical/ical.h>
#include <memory>

namespace {
using Component = std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)>;
Component parse(const QByteArray &raw) { return {icalparser_parse_string(raw.constData()), icalcomponent_free}; }
icaltimetype zonedTime(time_t seconds, int isDate, const icaltimezone *zone)
{
    auto value = icaltime_from_timet_with_zone(seconds, isDate, zone);
    // Wall-time conversion and the native zone marker must agree before a
    // property or recurrence identifier is serialized.
    icaltime_set_timezone(&value, zone);
    return value;
}
icalcomponent *timezoneDefinition(icaltimezone *zone)
{
    auto *component = icaltimezone_get_component(zone);
    if (!component) return nullptr;
    auto *copy = icalcomponent_new_clone(component);
    // System tzdata's initial LMT observance can have a null DTSTART. Bound
    // the definition to representable calendar years so its initial offset
    // has a valid DTSTART, retaining all subsequent transitions and rules.
    icaltimezone_truncate_vtimezone(copy, icaltime_from_string("00010101T000000Z"), icaltime_null_time(), 0);
    return copy;
}
QVariantMap recurrenceFields(icalcomponent *root, icalcomponent *event, bool *supported = nullptr);
void recurrenceParameters(icalcomponent *master, icalproperty *property);
constexpr const char *WorkdaysProperty = "X-DAYLINE-WORKDAYS";
constexpr const char *WorkdayCountProperty = "X-DAYLINE-WORKDAY-COUNT";
constexpr const char *WorkdayParameter = "X-DAYLINE-WORKDAY";
QString extension(icalcomponent *event, const char *name)
{
    for (auto *p = icalcomponent_get_first_property(event, ICAL_X_PROPERTY); p;
         p = icalcomponent_get_next_property(event, ICAL_X_PROPERTY))
        if (qstricmp(icalproperty_get_x_name(p), name) == 0) return QString::fromUtf8(icalproperty_get_x(p));
    return {};
}
void removeExtension(icalcomponent *event, const char *name)
{
    QList<icalproperty *> properties;
    for (auto *p = icalcomponent_get_first_property(event, ICAL_X_PROPERTY); p;
         p = icalcomponent_get_next_property(event, ICAL_X_PROPERTY))
        if (qstricmp(icalproperty_get_x_name(p), name) == 0) properties.append(p);
    for (auto *p : properties) { icalcomponent_remove_property(event, p); icalproperty_free(p); }
}
void setExtension(icalcomponent *event, const char *name, const QString &value)
{
    removeExtension(event, name);
    auto *p = icalproperty_new_x(value.toUtf8().constData());
    icalproperty_set_x_name(p, name);
    icalcomponent_add_property(event, p);
}
bool workdayExclusion(icalproperty *property)
{
    for (auto *p = icalproperty_get_first_parameter(property, ICAL_X_PARAMETER); p;
         p = icalproperty_get_next_parameter(property, ICAL_X_PARAMETER))
        if (qstricmp(icalparameter_get_xname(p), WorkdayParameter) == 0 &&
            qstrcmp(icalparameter_get_x(p), "CN") == 0) return true;
    return false;
}
bool hasUserExceptions(icalcomponent *root, icalcomponent *master)
{
    if (icalcomponent_count_components(root, ICAL_VEVENT_COMPONENT) > 1) return true;
    const bool managed = extension(master, WorkdaysProperty) == "CN";
    for (auto *p = icalcomponent_get_first_property(master, ICAL_EXDATE_PROPERTY); p;
         p = icalcomponent_get_next_property(master, ICAL_EXDATE_PROPERTY))
        if (!managed || !workdayExclusion(p)) return true;
    return false;
}
void clearWorkdayExclusions(icalcomponent *event)
{
    QList<icalproperty *> properties;
    for (auto *p = icalcomponent_get_first_property(event, ICAL_EXDATE_PROPERTY); p;
         p = icalcomponent_get_next_property(event, ICAL_EXDATE_PROPERTY))
        if (workdayExclusion(p)) properties.append(p);
    for (auto *p : properties) { icalcomponent_remove_property(event, p); icalproperty_free(p); }
    removeExtension(event, WorkdaysProperty);
    removeExtension(event, WorkdayCountProperty);
}
icalcomponent *masterEvent(icalcomponent *root)
{
    icalcomponent *master = nullptr;
    for (auto *event = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
        if (icalcomponent_get_first_property(event, ICAL_RECURRENCEID_PROPERTY)) continue;
        if (master) return nullptr;
        master = event;
    }
    return master;
}
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
icaltimezone *propertyTimezone(icalcomponent *root, icalproperty *p)
{
    auto *tz = p ? icalproperty_get_first_parameter(p, ICAL_TZID_PARAMETER) : nullptr;
    if (!tz) return nullptr;
    const char *id = icalparameter_get_tzid(tz);
    auto *zone = icalcomponent_get_timezone(root, id);
    return zone ? zone : icaltimezone_get_builtin_timezone(id);
}
icaltimezone *componentTimezone(icalcomponent *root, icalcomponent *event, icalproperty_kind kind)
{
    return propertyTimezone(root, icalcomponent_get_first_property(event, kind));
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
        if (!zone) return false;
        auto *definition = timezoneDefinition(zone);
        if (!definition) return false;
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
        icalcomponent_get_first_property(root, ICAL_METHOD_PROPERTY) || icalcomponent_count_errors(root)) return false;
    bool supported = false;
    recurrenceFields(root, event, &supported);
    if (!supported) return false;
    const QString uid = property(event, ICAL_UID_PROPERTY);
    if (uid.isEmpty()) return false;
    for (auto *item = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT); item;
         item = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
        if (property(item, ICAL_UID_PROPERTY) != uid) return false;
        for (auto kind : {ICAL_RDATE_PROPERTY, ICAL_EXRULE_PROPERTY, ICAL_ATTENDEE_PROPERTY, ICAL_ORGANIZER_PROPERTY})
            if (icalcomponent_get_first_property(item, kind)) return false;
        auto *rid = icalcomponent_get_first_property(item, ICAL_RECURRENCEID_PROPERTY);
        if (rid && (icalproperty_get_first_parameter(rid, ICAL_RANGE_PARAMETER) ||
                    icalcomponent_get_first_property(item, ICAL_RRULE_PROPERTY) ||
                    !icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY))) return false;
        if (rid && icalcomponent_get_status(item) == ICAL_STATUS_CANCELLED) continue;
        if (!icalcomponent_get_first_property(item, ICAL_DTSTART_PROPERTY)) return false;
        const auto start = icalcomponent_get_dtstart(item);
        const auto end = icalcomponent_get_dtend(item);
        if (!icaltime_is_valid_time(start) || (!icaltime_is_null_time(end) &&
            (!icaltime_is_valid_time(end) || start.is_date != end.is_date))) return false;
        for (auto kind : {ICAL_DTSTART_PROPERTY, ICAL_DTEND_PROPERTY, ICAL_RECURRENCEID_PROPERTY, ICAL_EXDATE_PROPERTY}) {
            for (auto *p = icalcomponent_get_first_property(item, kind); p;
                 p = icalcomponent_get_next_property(item, kind)) {
                if (icalproperty_get_first_parameter(p, ICAL_TZID_PARAMETER) && !propertyTimezone(root, p)) return false;
            }
        }
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
icaltimetype eventEnd(icalcomponent *event)
{
    if (icalcomponent_get_first_property(event, ICAL_DTEND_PROPERTY)) return icalcomponent_get_dtend(event);
    const auto start = icalcomponent_get_dtstart(event);
    auto *duration = icalcomponent_get_first_property(event, ICAL_DURATION_PROPERTY);
    return icaltime_add(start, duration ? icalproperty_get_duration(duration) : icaldurationtype_from_int(start.is_date ? 86400 : 0));
}
icaltimezone *startZone(icalcomponent *root, icalcomponent *event)
{
    return icaltime_is_utc(icalcomponent_get_dtstart(event)) ? icaltimezone_get_utc_timezone()
        : componentTimezone(root, event, ICAL_DTSTART_PROPERTY);
}
QVariantMap eventFields(icalcomponent *root, icalcomponent *event)
{
    const auto start = icalcomponent_get_dtstart(event);
    const auto begin = localTime(root, event, start, ICAL_DTSTART_PROPERTY);
    const auto finish = localTime(root, event, eventEnd(event),
        icalcomponent_get_first_property(event, ICAL_DTEND_PROPERTY) ? ICAL_DTEND_PROPERTY : ICAL_DTSTART_PROPERTY);
    auto *tz = icalproperty_get_first_parameter(icalcomponent_get_first_property(event, ICAL_DTSTART_PROPERTY), ICAL_TZID_PARAMETER);
    return {{"title", property(event, ICAL_SUMMARY_PROPERTY)}, {"notes", property(event, ICAL_DESCRIPTION_PROPERTY)},
        {"location", property(event, ICAL_LOCATION_PROPERTY)}, {"uid", property(event, ICAL_UID_PROPERTY)},
        {"date", begin.date().toString(Qt::ISODate)},
        {"endDate", (start.is_date ? finish.date().addDays(-1) : finish.date()).toString(Qt::ISODate)},
        {"allDay", bool(start.is_date)}, {"timeZone", tz ? QString::fromUtf8(icalparameter_get_tzid(tz)) : icaltime_is_utc(start) ? QString("UTC") : QString::fromUtf8(QTimeZone::systemTimeZoneId())},
        {"start", start.is_date ? "" : begin.time().toString("HH:mm")},
        {"end", start.is_date ? "" : finish.time().toString("HH:mm")}};
}
QVariantMap recurrenceFields(icalcomponent *root, icalcomponent *event, bool *supported)
{
    QVariantMap result {{"frequency", "none"}, {"interval", 1}, {"weekdays", QVariantList{}},
        {"endMode", "never"}, {"until", ""}, {"count", 1}, {"weekStart", "MO"}, {"workdaysOnly", false}};
    if (supported) *supported = true;
    auto unsupported = [&] {
        if (supported) *supported = false;
        result["frequency"] = "custom";
        return result;
    };
    const QString workdays = extension(event, WorkdaysProperty);
    if (!workdays.isEmpty() && workdays != "CN") return unsupported();
    result["workdaysOnly"] = workdays == "CN";
    auto *p = icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY);
    if (!p) return workdays.isEmpty() ? result : unsupported();
    if (icalcomponent_count_properties(event, ICAL_RRULE_PROPERTY) != 1) return unsupported();
    QMap<QString, QString> parts;
    for (const auto &part : QString::fromUtf8(icalproperty_get_value_as_string(p)).split(';')) {
        const auto pair = part.split('=');
        if (pair.size() != 2 || parts.contains(pair[0])) return unsupported();
        parts[pair[0]] = pair[1];
    }
    const QSet<QString> allowed {"FREQ", "INTERVAL", "BYDAY", "WKST", "UNTIL", "COUNT", "BYMONTHDAY", "BYMONTH"};
    for (auto it = parts.cbegin(); it != parts.cend(); ++it) if (!allowed.contains(it.key())) return unsupported();
    const QMap<QString, QString> frequencies {{"DAILY", "daily"}, {"WEEKLY", "weekly"}, {"MONTHLY", "monthly"}, {"YEARLY", "yearly"}};
    if (!frequencies.contains(parts.value("FREQ"))) return unsupported();
    result["frequency"] = frequencies.value(parts.value("FREQ"));
    const int interval = parts.value("INTERVAL", "1").toInt();
    if (interval < 1 || interval > 999) return unsupported();
    result["interval"] = interval;
    const QStringList days {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
    if (parts.contains("WKST")) {
        if (!days.contains(parts["WKST"])) return unsupported();
        result["weekStart"] = parts["WKST"];
    }
    const auto start = icalcomponent_get_dtstart(event);
    if (parts.contains("BYMONTHDAY") && (parts["FREQ"] != "MONTHLY" && parts["FREQ"] != "YEARLY")) return unsupported();
    if (parts.contains("BYMONTHDAY") && parts["BYMONTHDAY"] != QString::number(start.day)) return unsupported();
    if (parts.contains("BYMONTH") && (parts["FREQ"] != "YEARLY" || parts["BYMONTH"] != QString::number(start.month))) return unsupported();
    if (parts.contains("BYDAY") && parts["FREQ"] != "WEEKLY") return unsupported();
    if (parts["FREQ"] == "WEEKLY") {
        QSet<int> selected;
        for (const auto &day : parts.value("BYDAY", days.value(date(start).dayOfWeek() - 1)).split(',')) {
            if (!days.contains(day)) return unsupported();
            selected.insert(days.indexOf(day) + 1);
        }
        QVariantList weekdays;
        for (int day = 1; day <= 7; ++day) if (selected.contains(day)) weekdays.append(day);
        result["weekdays"] = weekdays;
    }
    if (parts.contains("UNTIL") && parts.contains("COUNT")) return unsupported();
    if (parts.contains("COUNT")) {
        const int count = parts["COUNT"].toInt();
        if (count < 1 || count > 9999) return unsupported();
        result["endMode"] = "count"; result["count"] = count;
    } else if (parts.contains("UNTIL")) {
        const auto until = icaltime_from_string(parts["UNTIL"].toUtf8().constData());
        if (icaltime_is_null_time(until) || !icaltime_is_valid_time(until) || until.is_date != start.is_date) return unsupported();
        result["endMode"] = "until";
        result["until"] = localTime(root, event, until, ICAL_DTSTART_PROPERTY).date().toString(Qt::ISODate);
    }
    const QString workdayCount = extension(event, WorkdayCountProperty);
    if (result.value("workdaysOnly").toBool()) {
        if (result.value("endMode").toString() == "never") return unsupported();
        if (parts.contains("COUNT")) {
            bool ok = false;
            const int count = workdayCount.toInt(&ok);
            if (!ok || count < 1 || count > result.value("count").toInt()) return unsupported();
            result["count"] = count;
        } else if (!workdayCount.isEmpty()) return unsupported();
    } else if (!workdayCount.isEmpty()) return unsupported();
    return result;
}
struct Expansion {
    QVariantList result;
    QVariantMap base;
    QDate from, until;
    bool allDay;
    icaltimezone *zone;
    bool floating;
    bool recurring;
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
        const auto first = zonedTime(span->start, x->allDay, x->zone);
        const auto last = zonedTime(span->end, x->allDay, x->zone);
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
    if (x->recurring)
        event["recurrenceId"] = QString::fromUtf8(icaltime_as_ical_string(zonedTime(span->start, x->allDay, x->zone)));
    x->result.append(event);
}
void replace(icalcomponent *event, icalproperty_kind kind, icalproperty *replacement)
{
    while (auto *old = icalcomponent_get_first_property(event, kind)) {
        icalcomponent_remove_property(event, old); icalproperty_free(old);
    }
    icalcomponent_add_property(event, replacement);
}
void removeProperties(icalcomponent *event, icalproperty_kind kind)
{
    while (auto *p = icalcomponent_get_first_property(event, kind)) {
        icalcomponent_remove_property(event, p); icalproperty_free(p);
    }
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
    if (!old && kind == ICAL_DTEND_PROPERTY) old = icalcomponent_get_first_property(event, ICAL_DTSTART_PROPERTY);
    const auto previous = old ? (icalproperty_isa(old) == ICAL_DTSTART_PROPERTY ? icalproperty_get_dtstart(old) : icalproperty_get_dtend(old)) : icaltime_null_time();
    auto value = icaltime_null_time();
    if (allDay) {
        value.year = day.year(); value.month = day.month(); value.day = day.day(); value.is_date = 1;
    } else if (old && !previous.is_date && !icaltime_is_utc(previous) &&
               !icalproperty_get_first_parameter(old, ICAL_TZID_PARAMETER)) {
        value.year = day.year(); value.month = day.month(); value.day = day.day();
        value.hour = time.hour(); value.minute = time.minute(); value.second = time.second();
    } else {
        auto *zone = old && !previous.is_date ? componentTimezone(root, event, icalproperty_isa(old))
            : icaltimezone_get_builtin_timezone(QTimeZone::systemTimeZoneId().constData());
        value = zonedTime(QDateTime(day, time).toSecsSinceEpoch(), 0,
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
    if (!allDay && (!old || previous.is_date)) {
        const auto id = QTimeZone::systemTimeZoneId();
        if (auto *zone = icaltimezone_get_builtin_timezone(id.constData()); zone && zone != icaltimezone_get_utc_timezone())
            icalproperty_add_parameter(result, icalparameter_new_tzid(id.constData()));
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
bool ensureLocalZone(icalcomponent *root, QString *error)
{
    const auto id = QTimeZone::systemTimeZoneId();
    auto *zone = icaltimezone_get_builtin_timezone(id.constData());
    if (!zone) { *error = "无法识别系统时区，请先检查系统时区设置。"; return false; }
    if (zone == icaltimezone_get_utc_timezone() || icalcomponent_get_timezone(root, id.constData())) return true;
    auto *copy = timezoneDefinition(zone);
    if (!copy) { *error = "无法读取系统时区的夏令时规则。"; return false; }
    auto *tzid = icalcomponent_get_first_property(copy, ICAL_TZID_PROPERTY);
    if (!tzid) { icalcomponent_free(copy); *error = "系统时区定义缺少标识。"; return false; }
    icalproperty_set_tzid(tzid, id.constData());
    icalcomponent_add_component(root, copy);
    return true;
}
QVariantMap requestedRecurrence(const QVariantMap &fields)
{
    QVariantMap result {{"frequency", "none"}, {"interval", 1}, {"weekdays", QVariantList{}},
        {"endMode", "never"}, {"until", ""}, {"count", 1}, {"weekStart", "MO"}, {"workdaysOnly", false}};
    const auto requested = fields.value("recurrence").toMap();
    for (auto it = requested.cbegin(); it != requested.cend(); ++it) result[it.key()] = it.value();
    if (result.value("frequency").toString() != "weekly") result["weekdays"] = QVariantList{};
    if (result.value("endMode").toString() != "until") result["until"] = "";
    if (result.value("endMode").toString() != "count") result["count"] = 1;
    result["workdaysOnly"] = result.value("workdaysOnly").toBool();
    return result;
}
bool setRecurrence(icalcomponent *event, const QVariantMap &rule, QString *error)
{
    const QString frequency = rule.value("frequency").toString();
    if (frequency == "none") { removeProperties(event, ICAL_RRULE_PROPERTY); return true; }
    const QMap<QString, QString> frequencies {{"daily", "DAILY"}, {"weekly", "WEEKLY"}, {"monthly", "MONTHLY"}, {"yearly", "YEARLY"}};
    const int interval = rule.value("interval").toInt();
    if (!frequencies.contains(frequency) || interval < 1 || interval > 999) {
        *error = "请选择有效的重复频率和间隔（1 到 999）。"; return false;
    }
    QStringList parts {"FREQ=" + frequencies[frequency], "INTERVAL=" + QString::number(interval)};
    const auto start = icalcomponent_get_dtstart(event);
    const QStringList names {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
    const QString weekStart = rule.value("weekStart").toString();
    if (!names.contains(weekStart)) { *error = "重复日程的每周起始日无效。"; return false; }
    parts.append("WKST=" + weekStart);
    if (frequency == "weekly") {
        QSet<int> selected;
        for (const auto &value : rule.value("weekdays").toList()) {
            const int day = value.toInt();
            if (day < 1 || day > 7) { *error = "请选择有效的重复星期。"; return false; }
            selected.insert(day);
        }
        if (!selected.contains(date(start).dayOfWeek())) {
            *error = "请选择至少一个重复星期，并包含开始日期所在的星期。"; return false;
        }
        QStringList days;
        for (int day = 1; day <= 7; ++day) if (selected.contains(day)) days.append(names[day - 1]);
        parts.append("BYDAY=" + days.join(','));
    }
    const auto mode = rule.value("endMode").toString();
    if (mode == "count") {
        const int count = rule.value("count").toInt();
        if (count < 1 || count > 9999) { *error = "重复次数请填写 1 到 9999。"; return false; }
        parts.append("COUNT=" + QString::number(count));
    } else if (mode == "until") {
        const auto until = QDate::fromString(rule.value("until").toString(), Qt::ISODate);
        const auto first = QDate::fromString(rule.value("startDate").toString(), Qt::ISODate);
        if (!until.isValid() || until < first) { *error = "重复结束日期不能早于开始日期。"; return false; }
        icaltimetype value;
        if (start.is_date) value = icaltime_from_string(until.toString("yyyyMMdd").toUtf8().constData());
        else if (!icaltime_is_utc(start) && !icalproperty_get_first_parameter(
                     icalcomponent_get_first_property(event, ICAL_DTSTART_PROPERTY), ICAL_TZID_PARAMETER))
            value = icaltime_from_string((until.toString("yyyyMMdd") + "T235959").toUtf8().constData());
        else value = icaltime_from_timet_with_zone(QDateTime(until, QTime(23,59,59)).toSecsSinceEpoch(), 0, icaltimezone_get_utc_timezone());
        parts.append("UNTIL=" + QString::fromUtf8(icaltime_as_ical_string(value)));
    } else if (mode != "never") { *error = "请选择有效的重复结束方式。"; return false; }
    auto *property = icalproperty_new_from_string(("RRULE:" + parts.join(';')).toUtf8().constData());
    if (!property) { *error = "无法生成重复规则。"; return false; }
    replace(event, ICAL_RRULE_PROPERTY, property);
    return true;
}
bool setWorkdayExclusions(icalcomponent *root, icalcomponent *event, const QVariantMap &rule, QString *error)
{
    clearWorkdayExclusions(event);
    if (!rule.value("workdaysOnly").toBool()) return true;
    auto *property = icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY);
    const auto mode = rule.value("endMode").toString();
    if (!property || (mode != "until" && mode != "count")) {
        *error = "仅工作日日程需要指定结束日期或次数，不能设为永不结束。"; return false;
    }
    auto start = icalcomponent_get_dtstart(event);
    icaltime_set_timezone(&start, startZone(root, event));
    const auto first = localTime(root, event, start, ICAL_DTSTART_PROPERTY).date();
    const auto last = mode == "until" ? QDate::fromString(rule.value("until").toString(), Qt::ISODate) : first;
    if (!WorkdayCalendar::validateRange(first, last, error)) return false;
    auto recurrence = icalproperty_get_rrule(property);
    auto iterationRule = recurrence;
    const int requestedCount = rule.value("count").toInt();
    if (mode == "count") iterationRule.count = 0;
    using Iterator = std::unique_ptr<icalrecur_iterator, decltype(&icalrecur_iterator_free)>;
    Iterator iterator(icalrecur_iterator_new(iterationRule, start), icalrecur_iterator_free);
    if (!iterator) { *error = "无法展开工作日重复规则。"; return false; }
    int candidates = 0, kept = 0;
    for (;;) {
        const auto candidate = icalrecur_iterator_next(iterator.get());
        if (icaltime_is_null_time(candidate)) break;
        const auto day = localTime(root, event, candidate, ICAL_DTSTART_PROPERTY).date();
        const auto working = WorkdayCalendar::isWorkday(day, error);
        if (!working.has_value()) return false;
        if (++candidates > 9999) { *error = "工作日重复范围过大，请缩短日期范围或减少次数。"; return false; }
        if (*working) ++kept;
        else {
            auto *exdate = icalproperty_new_exdate(candidate);
            recurrenceParameters(event, exdate);
            auto *parameter = icalparameter_new_x("CN");
            icalparameter_set_xname(parameter, WorkdayParameter);
            icalproperty_add_parameter(exdate, parameter);
            icalcomponent_add_property(event, exdate);
        }
        if (mode == "count" && kept == requestedCount) break;
    }
    if (!kept || (mode == "count" && kept != requestedCount)) {
        *error = "所选日期和重复规则内没有足够的工作日，请调整范围或重复规则。"; return false;
    }
    if (mode == "count") {
        recurrence.count = candidates;
        icalproperty_set_rrule(property, recurrence);
        setExtension(event, WorkdayCountProperty, QString::number(requestedCount));
    }
    setExtension(event, WorkdaysProperty, "CN");
    return true;
}
icaltimetype recurrenceTime(icalcomponent *root, icalcomponent *master, const QString &id)
{
    auto value = icaltime_from_string(id.toUtf8().constData());
    if (icaltime_is_null_time(value) || !icaltime_is_valid_time(value)) return icaltime_null_time();
    const auto start = icalcomponent_get_dtstart(master);
    if (value.is_date != start.is_date || icaltime_is_utc(value) != icaltime_is_utc(start)) return icaltime_null_time();
    icaltime_set_timezone(&value, startZone(root, master));
    return value;
}
icalcomponent *findOverride(icalcomponent *root, const QString &id)
{
    for (auto *event = icalcomponent_get_first_component(root, ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root, ICAL_VEVENT_COMPONENT)) {
        const auto rid = icalcomponent_get_recurrenceid(event);
        if (!icaltime_is_null_time(rid) && QString::fromUtf8(icaltime_as_ical_string(rid)) == id) return event;
    }
    return nullptr;
}
bool containsOccurrence(icalcomponent *root, icalcomponent *master, const QString &id)
{
    if (auto *event = findOverride(root, id)) return icalcomponent_get_status(event) != ICAL_STATUS_CANCELLED;
    const auto value = recurrenceTime(root, master, id);
    if (icaltime_is_null_time(value) || (!icalcomponent_get_first_property(master, ICAL_RRULE_PROPERTY) &&
        !icalcomponent_get_first_property(master, ICAL_RDATE_PROPERTY))) return false;
    struct Match { time_t start; bool found = false; } match {icaltime_as_timet_with_zone(value, startZone(root, master))};
    const auto from = icaltime_from_timet_with_zone(match.start - 86400, 0, icaltimezone_get_utc_timezone());
    const auto until = icaltime_from_timet_with_zone(match.start + 86400, 0, icaltimezone_get_utc_timezone());
    icalcomponent_foreach_recurrence(master, from, until, [](icalcomponent *, icaltime_span *span, void *context) {
        auto *m = static_cast<Match *>(context); if (span->start == m->start) m->found = true;
    }, &match);
    return match.found;
}
void recurrenceParameters(icalcomponent *master, icalproperty *property)
{
    auto *start = icalcomponent_get_first_property(master, ICAL_DTSTART_PROPERTY);
    if (auto *tz = icalproperty_get_first_parameter(start, ICAL_TZID_PARAMETER))
        icalproperty_add_parameter(property, icalparameter_new_clone(tz));
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
            if (master) { if (error) *error = "此日程包含多个主事件，暂不支持显示。"; return {}; }
            master = event;
        }
        for (auto *p = icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY); p;
             p = icalcomponent_get_next_property(event, ICAL_RRULE_PROPERTY)) {
            const auto rule = icalproperty_get_rrule(p);
            if (rule.freq == ICAL_SECONDLY_RECURRENCE || rule.freq == ICAL_MINUTELY_RECURRENCE || rule.freq == ICAL_HOURLY_RECURRENCE) {
                if (error) *error = "暂不支持显示按小时、分钟或秒重复的日程。";
                return {};
            }
        }
        for (auto *p = icalcomponent_get_first_property(event, ICAL_RDATE_PROPERTY); p;
             p = icalcomponent_get_next_property(event, ICAL_RDATE_PROPERTY)) {
            if (!icaltime_is_null_time(icalproperty_get_rdate(p).period.start)) {
                if (error) *error = "暂不支持显示包含独立时间段的重复日程。";
                return {};
            }
        }
    }
    const bool writable = master && editable(root.get(), master);
    const bool recurring = master && (icalcomponent_get_first_property(master, ICAL_RRULE_PROPERTY) ||
        icalcomponent_get_first_property(master, ICAL_RDATE_PROPERTY));
    QVariantMap series = master ? eventFields(root.get(), master) : QVariantMap{};
    if (master) series["recurrence"] = recurrenceFields(root.get(), master);
    series["hasExceptions"] = master && hasUserExceptions(root.get(), master);
    for (auto *event = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT)) {
        auto rid = icalcomponent_get_recurrenceid(event);
        if (!icaltime_is_null_time(rid)) {
            auto *p = icalcomponent_get_first_property(event, ICAL_RECURRENCEID_PROPERTY);
            if (icalproperty_get_first_parameter(p, ICAL_RANGE_PARAMETER)) {
                if (error) *error = "暂不支持显示包含“此后所有日程”修改的重复日程。";
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
        const bool readOnly = !writable;
        QVariantMap base {{"title", property(event, ICAL_SUMMARY_PROPERTY)}, {"notes", property(event, ICAL_DESCRIPTION_PROPERTY)},
            {"location", property(event, ICAL_LOCATION_PROPERTY)}, {"uid", property(event, ICAL_UID_PROPERTY)},
            {"recurring", recurring}, {"recurrence", series.value("recurrence")}, {"series", series},
            {"readOnly", readOnly}, {"readOnlyReason", readOnly ? "此日程包含会议邀请或暂不支持的复杂重复规则，只能查看。" : ""}};
        const auto rid = icalcomponent_get_recurrenceid(event);
        base["recurrenceId"] = icaltime_is_null_time(rid) ? QString("") : QString::fromUtf8(icaltime_as_ical_string(rid));
        if (!icalcomponent_get_first_property(event, ICAL_RRULE_PROPERTY) &&
            !icalcomponent_get_first_property(event, ICAL_RDATE_PROPERTY)) {
            auto recurrenceStart = start;
            auto candidate = start;
            if (icalproperty_recurrence_is_excluded(event, &recurrenceStart, &candidate)) continue;
            const auto begin = localTime(root.get(), event, start, ICAL_DTSTART_PROPERTY);
            const auto end = eventEnd(event);
            const auto finish = localTime(root.get(), event, end,
                icalcomponent_get_first_property(event, ICAL_DTEND_PROPERTY) ? ICAL_DTEND_PROPERTY : ICAL_DTSTART_PROPERTY);
            if (begin.date() >= until || finish.date() < from) continue;
            base["date"] = begin.date().toString(Qt::ISODate);
            base["endDate"] = (start.is_date ? finish.date().addDays(-1) : finish.date()).toString(Qt::ISODate);
            base["allDay"] = bool(start.is_date);
            base["start"] = start.is_date ? "" : begin.time().toString("HH:mm");
            base["end"] = start.is_date ? "" : finish.time().toString("HH:mm");
            result.append(base); continue;
        }
        auto *zone = startZone(root.get(), event);
        Expansion expanded {{}, base, from, until, bool(start.is_date), zone,
            !icaltime_is_utc(start) && !zone, recurring, overrides, {}};
        const auto spanDays = qMax<qint64>(1, date(start).daysTo(date(eventEnd(event))) + 1);
        const auto begin = icaltime_from_timet_with_zone(QDateTime(from.addDays(-spanDays), QTime(0,0), Qt::UTC).toSecsSinceEpoch(), 0, icaltimezone_get_utc_timezone());
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
    auto *event = masterEvent(root.get());
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
        (allDay && !last.addDays(1).isValid()) ||
        (!allDay && (!start.isValid() || !end.isValid() || !QDateTime(day, start).isValid() || !QDateTime(last, end).isValid() || QDateTime(last, end) <= QDateTime(day, start)))) {
        *error = "请填写标题和有效的日期、时间范围。"; return {};
    }
    auto *master = event;
    const bool recurring = icalcomponent_get_first_property(master, ICAL_RRULE_PROPERTY);
    const QString scope = fields.value("scope", "series").toString();
    if (scope != "series" && scope != "occurrence") { *error = "请选择本次日程或整个系列。"; return {}; }
    const auto previous = raw.isEmpty() ? QVariantMap{} : eventFields(root.get(), master);
    const auto previousRule = recurrenceFields(root.get(), master);
    const auto desiredRule = requestedRecurrence(fields);
    if (desiredRule.value("workdaysOnly").toBool() && day != last) {
        *error = "仅工作日日程的每次安排需要在同一天内结束；若要按日期范围逐日安排，请先把重复设为“从不”。"; return {};
    }
    bool timeChanged = false;
    for (const auto &key : {"date", "endDate", "allDay", "start", "end"}) {
        if (allDay && (QByteArray(key) == "start" || QByteArray(key) == "end")) continue;
        if (previous.value(key) != fields.value(key)) timeChanged = true;
    }
    const bool startChanged = previous.value("date").toString() != day.toString(Qt::ISODate) ||
        previous.value("allDay").toBool() != allDay || (!allDay && previous.value("start").toString() != fields.value("start").toString());
    if (scope == "occurrence") {
        if (desiredRule != previousRule) {
            *error = "重复规则和仅工作日设置需要应用到整个系列。"; return {};
        }
        if (desiredRule.value("workdaysOnly").toBool()) {
            const auto working = WorkdayCalendar::isWorkday(day, error);
            if (!working.has_value()) return {};
            if (!*working) { *error = "仅工作日日程不能移到休息日，请选择工作日或调休补班日。"; return {}; }
        }
        const auto id = fields.value("recurrenceId").toString();
        const auto rid = recurrenceTime(root.get(), master, id);
        if (!recurring || icaltime_is_null_time(rid) || !containsOccurrence(root.get(), master, id)) {
            *error = "这一次重复日程已发生变化，请关闭后重新打开。"; return {};
        }
        event = findOverride(root.get(), id);
        if (!event) {
            event = icalcomponent_new_clone(master);
            for (auto kind : {ICAL_RRULE_PROPERTY, ICAL_RDATE_PROPERTY, ICAL_EXDATE_PROPERTY, ICAL_EXRULE_PROPERTY})
                removeProperties(event, kind);
            clearWorkdayExclusions(event);
            auto *property = icalproperty_new_recurrenceid(rid);
            recurrenceParameters(master, property);
            replace(event, ICAL_RECURRENCEID_PROPERTY, property);
            icalcomponent_add_component(root.get(), event);
        }
    } else if (recurring && hasUserExceptions(root.get(), master) &&
               (timeChanged || desiredRule != previousRule)) {
        *error = "此系列已有单次修改或删除；可调整本次时间，整个系列暂只支持修改标题、地点和备注。"; return {};
    }
    if (!allDay && (raw.isEmpty() || icalcomponent_get_dtstart(event).is_date) && !ensureLocalZone(root.get(), error)) return {};
    // Preserve floating, UTC and TZID semantics, including untouched parameters.
    auto *startProperty = timeProperty(root.get(), event, ICAL_DTSTART_PROPERTY, day, start, allDay);
    auto *endProperty = timeProperty(root.get(), event, ICAL_DTEND_PROPERTY, allDay ? last.addDays(1) : last, end, allDay);
    replace(event, ICAL_DTSTART_PROPERTY, startProperty);
    replace(event, ICAL_DTEND_PROPERTY, endProperty);
    removeProperties(event, ICAL_DURATION_PROPERTY);
    if (scope == "series" && (desiredRule != previousRule || startChanged)) {
        auto rule = desiredRule; rule["startDate"] = day.toString(Qt::ISODate);
        if (!setRecurrence(event, rule, error) || !setWorkdayExclusions(root.get(), event, rule, error)) return {};
    }
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

QByteArray ICalResource::exclude(const QByteArray &raw, const QString &recurrenceId, QString *error)
{
    auto root = parse(raw);
    auto *master = root ? masterEvent(root.get()) : nullptr;
    if (!master || !editable(root.get(), master) || !containsOccurrence(root.get(), master, recurrenceId)) {
        *error = "此日程已发生变化或不支持单次删除，请重新打开。"; return {};
    }
    const auto rid = recurrenceTime(root.get(), master, recurrenceId);
    if (icaltime_is_null_time(rid)) { *error = "重复日程标识无效。"; return {}; }
    if (auto *override = findOverride(root.get(), recurrenceId)) {
        icalcomponent_remove_component(root.get(), override); icalcomponent_free(override);
    }
    auto *exdate = icalproperty_new_exdate(rid);
    recurrenceParameters(master, exdate);
    icalcomponent_add_property(master, exdate);
    const auto now = icaltime_from_timet_with_zone(QDateTime::currentSecsSinceEpoch(), 0, icaltimezone_get_utc_timezone());
    replace(master, ICAL_DTSTAMP_PROPERTY, icalproperty_new_dtstamp(now));
    replace(master, ICAL_LASTMODIFIED_PROPERTY, icalproperty_new_lastmodified(now));
    auto *sequence = icalcomponent_get_first_property(master, ICAL_SEQUENCE_PROPERTY);
    replace(master, ICAL_SEQUENCE_PROPERTY, icalproperty_new_sequence(sequence ? icalproperty_get_sequence(sequence) + 1 : 0));
    return serialize(root.get());
}

QVariantMap ICalResource::fields(const QByteArray &raw)
{
    auto root = parse(raw);
    auto *master = root ? masterEvent(root.get()) : nullptr;
    if (!master) return {};
    auto result = eventFields(root.get(), master);
    result["recurrence"] = recurrenceFields(root.get(), master);
    return result;
}

QVariantMap ICalResource::details(const QByteArray &raw, const QString &recurrenceId, QString *error)
{
    auto root = parse(raw);
    auto *master = root ? masterEvent(root.get()) : nullptr;
    if (!master || icalcomponent_count_errors(root.get()) || !prepareZones(root.get())) {
        *error = "日程原始内容或时区无效，无法读取。"; return {};
    }
    auto series = eventFields(root.get(), master);
    series["recurrence"] = recurrenceFields(root.get(), master);
    series["hasExceptions"] = hasUserExceptions(root.get(), master);
    auto result = series;
    if (!recurrenceId.isEmpty()) {
        if (!containsOccurrence(root.get(), master, recurrenceId)) {
            *error = "这一次重复日程不存在或已删除，请重新查询。"; return {};
        }
        if (auto *item = findOverride(root.get(), recurrenceId)) result = eventFields(root.get(), item);
        else {
            const auto rid = recurrenceTime(root.get(), master, recurrenceId);
            const auto finish = icaltime_add(rid, icaltime_subtract(eventEnd(master), icalcomponent_get_dtstart(master)));
            const auto begin = localTime(root.get(), master, rid, ICAL_DTSTART_PROPERTY);
            const auto end = localTime(root.get(), master, finish, ICAL_DTSTART_PROPERTY);
            result["date"] = begin.date().toString(Qt::ISODate);
            result["endDate"] = (rid.is_date ? end.date().addDays(-1) : end.date()).toString(Qt::ISODate);
            result["start"] = rid.is_date ? "" : begin.time().toString("HH:mm");
            result["end"] = rid.is_date ? "" : end.time().toString("HH:mm");
        }
    }
    result["series"] = series;
    result["recurrence"] = series.value("recurrence");
    result["recurrenceId"] = recurrenceId;
    result["recurring"] = bool(icalcomponent_get_first_property(master, ICAL_RRULE_PROPERTY) ||
        icalcomponent_get_first_property(master, ICAL_RDATE_PROPERTY));
    result["readOnly"] = !editable(root.get(), master);
    result["readOnlyReason"] = result.value("readOnly").toBool() ? "此日程包含暂不支持的结构，只能查看。" : "";
    return result;
}

bool ICalResource::canEdit(const QByteArray &raw)
{
    auto root = parse(raw);
    return root && editable(root.get(), masterEvent(root.get()));
}

QByteArray ICalResource::copy(const QByteArray &raw, const QString &uid, QString *error)
{
    auto root = parse(raw);
    auto *event = root ? masterEvent(root.get()) : nullptr;
    if (uid.isEmpty() || !event || !editable(root.get(), event)) {
        if (error) *error = "暂不支持将此日程另存为副本。";
        return {};
    }
    for (event = icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT); event;
         event = icalcomponent_get_next_component(root.get(), ICAL_VEVENT_COMPONENT))
        replace(event, ICAL_UID_PROPERTY, icalproperty_new_uid(uid.toUtf8().constData()));
    return serialize(root.get());
}

QString ICalResource::title(const QByteArray &raw)
{
    auto root = parse(raw);
    auto *event = root ? masterEvent(root.get()) : nullptr;
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
