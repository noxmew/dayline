#include "mcptools.h"
#include "calendarstore.h"
#include "workdaycalendar.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>
#include <cmath>

namespace {
QJsonObject textSchema(const QString &description = {}, int maxLength = 4096)
{
    return {{"type","string"}, {"description",description}, {"maxLength",maxLength}};
}
QJsonObject choice(const QStringList &values, const QString &description = {})
{
    auto schema = textSchema(description);
    schema["enum"] = QJsonArray::fromStringList(values);
    return schema;
}
QJsonObject integer(int minimum, int maximum)
{
    return {{"type","integer"}, {"minimum",minimum}, {"maximum",maximum}};
}
QJsonObject object(const QJsonObject &properties, const QStringList &required = {})
{
    return {{"type","object"}, {"properties",properties}, {"required",QJsonArray::fromStringList(required)}, {"additionalProperties",false}};
}
QJsonObject dateSchema(const QString &description)
{
    auto schema = textSchema(description, 10);
    schema["pattern"] = "^[0-9]{4}-[0-9]{2}-[0-9]{2}$";
    return schema;
}
QJsonObject recurrenceSchema()
{
    return object({
        {"frequency",choice({"none","daily","weekly","monthly","yearly"})},
        {"interval",integer(1,999)},
        {"weekdays",QJsonObject{{"type","array"}, {"items",integer(1,7)}, {"maxItems",7}, {"uniqueItems",true},
            {"description","每周重复的星期，1=周一，7=周日，须包含开始日期所在星期；新建时省略则使用开始日。"}}},
        {"end_mode",choice({"never","until","count"})},
        {"until",dateSchema("最后一次允许发生的日期，包含该日。")},
        {"count",integer(1,9999)},
        {"week_start",choice({"MO","TU","WE","TH","FR","SA","SU"})},
        {"workdays_only",QJsonObject{{"type","boolean"}, {"description","按中国大陆工作日排除周末、法定假期，保留调休补班。重复须有结束日期或次数；frequency=none 时按起止日期逐日安排。"}}}
    });
}
QJsonObject eventProperties()
{
    auto time = textSchema("本机时区的时间，HH:mm。全天日程可省略。", 5);
    time["pattern"] = "^([01][0-9]|2[0-3]):[0-5][0-9]$";
    return {{"calendar_id",textSchema("来自 list_calendars 的日历标识。")},
        {"title",textSchema("日程标题。",1000)},
        {"start_date",dateSchema("开始日期；编辑整个系列时使用系列首日。")},
        {"end_date",dateSchema("结束日期；全天日程包含此日，定时日程在此日的 end_time 结束。新建时默认等于 start_date。")},
        {"all_day",QJsonObject{{"type","boolean"}, {"description","默认 false。"}}},
        {"start_time",time}, {"end_time",time},
        {"location",textSchema("地点；空字符串清除。",4096)},
        {"notes",textSchema("备注；空字符串清除。",65536)},
        {"recurrence",recurrenceSchema()}};
}
QJsonObject success(const QJsonObject &value)
{
    return {{"content",QJsonArray{QJsonObject{{"type","text"}, {"text",QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact))}}}},
        {"structuredContent",value}, {"isError",false}};
}
QJsonObject failure(const QString &code, const QString &message)
{
    const QJsonObject error{{"error",QJsonObject{{"code",code}, {"message",message}}}};
    auto result = success(error);
    result["isError"] = true;
    return result;
}
QString hash(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString validate(const QJsonValue &value, const QJsonObject &schema, const QString &path)
{
    const auto type = schema.value("type").toString();
    if ((type == "object" && !value.isObject()) || (type == "string" && !value.isString()) ||
        (type == "boolean" && !value.isBool()) || (type == "array" && !value.isArray()) ||
        (type == "integer" && (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble()))) return path + " 的类型无效。";
    if (schema.contains("enum") && !schema.value("enum").toArray().contains(value)) return path + " 不在允许的值中。";
    if (value.isObject()) {
        const auto fields = value.toObject(), properties = schema.value("properties").toObject();
        for (const auto &key : schema.value("required").toArray()) if (!fields.contains(key.toString())) return path + "." + key.toString() + " 是必填项。";
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            if (!properties.contains(it.key())) return path + "." + it.key() + " 是未知字段。";
            const auto error = validate(it.value(), properties.value(it.key()).toObject(), path + "." + it.key());
            if (!error.isEmpty()) return error;
        }
    } else if (value.isArray()) {
        const auto items = value.toArray();
        if (schema.contains("maxItems") && items.size() > schema.value("maxItems").toInt()) return path + " 项目过多。";
        for (qsizetype i = 0; i < items.size(); ++i) {
            const auto error = validate(items[i], schema.value("items").toObject(), path + "[" + QString::number(i) + "]");
            if (!error.isEmpty()) return error;
            if (schema.value("uniqueItems").toBool()) for (qsizetype j = 0; j < i; ++j) if (items[j] == items[i]) return path + " 包含重复项。";
        }
    } else if (value.isString()) {
        const auto text = value.toString();
        if (schema.contains("maxLength") && text.size() > schema.value("maxLength").toInt()) return path + " 过长。";
        if (schema.contains("pattern") && !QRegularExpression(schema.value("pattern").toString()).match(text).hasMatch()) return path + " 格式无效。";
    } else if (type == "integer") {
        if (value.toDouble() < schema.value("minimum").toDouble() || value.toDouble() > schema.value("maximum").toDouble()) return path + " 超出允许范围。";
    }
    return {};
}
QJsonObject publicRecurrence(const QVariantMap &rule)
{
    QJsonObject result;
    const QMap<QString,QString> keys{{"frequency","frequency"}, {"interval","interval"}, {"weekdays","weekdays"},
        {"endMode","end_mode"}, {"until","until"}, {"count","count"}, {"weekStart","week_start"}, {"workdaysOnly","workdays_only"}};
    for (auto it = keys.begin(); it != keys.end(); ++it) if (rule.contains(it.key())) result[it.value()] = QJsonValue::fromVariant(rule.value(it.key()));
    return result;
}
QJsonObject publicEvent(const QVariantMap &event, bool details = false)
{
    const QMap<QString,QString> keys{{"id","event_id"}, {"calendar","calendar_id"}, {"calendarName","calendar_name"},
        {"title","title"}, {"date","start_date"}, {"endDate","end_date"}, {"allDay","all_day"},
        {"start","start_time"}, {"end","end_time"}, {"location","location"}, {"notes","notes"},
        {"recurrenceId","recurrence_id"}, {"recurring","recurring"}, {"revision","revision"},
        {"readOnly","read_only"}, {"readOnlyReason","read_only_reason"}, {"hasExceptions","has_exceptions"}};
    QJsonObject result;
    for (auto it = keys.begin(); it != keys.end(); ++it) if (event.contains(it.key())) result[it.value()] = QJsonValue::fromVariant(event.value(it.key()));
    result["recurrence"] = publicRecurrence(event.value("recurrence").toMap());
    if (details && event.contains("series")) result["series"] = publicEvent(event.value("series").toMap());
    return result;
}
QVariantMap internalFields(const QJsonObject &value)
{
    const QMap<QString,QString> keys{{"calendar_id","calendar"}, {"title","title"}, {"start_date","date"},
        {"end_date","endDate"}, {"all_day","allDay"}, {"start_time","start"}, {"end_time","end"}, {"location","location"}, {"notes","notes"}};
    QVariantMap result;
    for (auto it = keys.begin(); it != keys.end(); ++it) if (value.contains(it.key())) result[it.value()] = value.value(it.key()).toVariant();
    return result;
}
QVariantMap internalRecurrence(const QJsonObject &value, QVariantMap result)
{
    const QMap<QString,QString> keys{{"frequency","frequency"}, {"interval","interval"}, {"weekdays","weekdays"},
        {"end_mode","endMode"}, {"until","until"}, {"count","count"}, {"week_start","weekStart"}, {"workdays_only","workdaysOnly"}};
    for (auto it = keys.begin(); it != keys.end(); ++it) if (value.contains(it.key())) result[it.value()] = value.value(it.key()).toVariant();
    return result;
}
}

McpTools::McpTools(CalendarStore &store) : m_store(store)
{
    auto add = [this](const QString &name, const QString &description, const QJsonObject &schema, bool readOnly, bool destructive = false) {
        m_definitions.append(QJsonObject{{"name",name}, {"description",description}, {"inputSchema",schema},
            {"annotations",QJsonObject{{"readOnlyHint",readOnly}, {"destructiveHint",destructive}, {"idempotentHint",readOnly}, {"openWorldHint",false}}}});
    };
    add("list_calendars", "列出日笺管理的全部日历及读写权限，不受界面显示开关影响。", object({}), true);
    const QJsonObject range{{"from",dateSchema("范围起始日，包含该日。")}, {"until",dateSchema("范围结束日，不包含该日。单次范围最多 366 天。")}};
    auto list = range;
    list["calendar_ids"] = QJsonObject{{"type","array"}, {"items",textSchema()}, {"maxItems",100}, {"uniqueItems",true}};
    list["query"] = textSchema("在标题、地点、备注中搜索。",200);
    list["limit"] = integer(1,200);
    list["cursor"] = textSchema("使用上次返回的 next_cursor；其余筛选条件保持一致。",1024);
    add("list_events", "按日期范围查询并展开重复日程，包含与范围重叠的跨天日程。不受界面月份、搜索或隐藏设置影响。", object(list,{"from","until"}), true);
    QJsonObject ref{{"calendar_id",textSchema()}, {"event_id",textSchema()}, {"recurrence_id",textSchema("单次重复日程的标识，原样使用查询结果中的 recurrence_id。省略时读取系列。",128)}};
    add("get_event", "读取日程或指定的重复实例；获取 revision 后再修改或删除。所有日期和时间按本机时区解释。", object(ref,{"calendar_id","event_id"}), true);
    add("create_event", "创建普通、全天、跨天或重复日程。默认非全天，定时日程须明确提供起止时间。返回已保存的日程，不表示远端已完成更新。", object(eventProperties(),{"calendar_id","title","start_date"}), false);
    ref["scope"] = choice({"occurrence","series"},"occurrence 仅本次，必须提供 recurrence_id；series 整个系列，不传 recurrence_id。不支持此后所有日程。");
    ref["expected_revision"] = textSchema("必须与 get_event 或 list_events 返回的 revision 一致，防止覆盖中途修改。",64);
    auto update = ref;
    update["changes"] = object(eventProperties());
    add("update_event", "部分修改日程；省略的字段保持原值，recurrence 也按字段合并。scope=series 时日期针对系列首日，先 get_event 读取系列。", object(update,{"calendar_id","event_id","scope","expected_revision","changes"}), false, true);
    add("delete_event", "删除一项日程、单次重复实例或整个系列，必须明确 scope 并校验 revision。", object(ref,{"calendar_id","event_id","scope","expected_revision"}), false, true);
    add("get_memo", "读取日笺唯一的本机备忘录及 revision。备忘录显示开关不影响读取。", object({}), true);
    add("update_memo", "追加或替换备忘录，必须提供当前 revision。append 原样拼接，换行须在 content 中明确提供；不会开启备忘录显示。",
        object({{"mode",choice({"append","replace"})}, {"content",textSchema({},262144)}, {"expected_revision",textSchema({},64)}}, {"mode","content","expected_revision"}), false, true);
    add("list_workdays", "查询中国大陆工作日与休息日，包含调休规则。未知年份明确报错。", object(range,{"from","until"}), true);
}

bool McpTools::contains(const QString &name) const
{
    for (const auto &entry : m_definitions) if (entry.toObject().value("name").toString() == name) return true;
    return false;
}

QJsonObject McpTools::call(const QString &name, const QJsonObject &arguments)
{
    for (const auto &entry : m_definitions) if (entry.toObject().value("name").toString() == name) {
        const auto error = validate(arguments, entry.toObject().value("inputSchema").toObject(), "arguments");
        if (!error.isEmpty()) return failure("invalid_arguments",error);
        return execute(name,arguments);
    }
    return failure("unknown_tool","未知工具。");
}

QJsonObject McpTools::execute(const QString &name, const QJsonObject &args)
{
    const QString timeZone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    if (name == "list_calendars") {
        QJsonArray result;
        for (const auto &value : m_store.allCalendars()) {
            const auto item = value.toMap();
            result.append(QJsonObject{{"calendar_id",item.value("id").toString()}, {"name",item.value("name").toString()},
                {"color",item.value("color").toString()}, {"writable",item.value("writable").toBool()}});
        }
        return success({{"calendars",result}});
    }
    if (name == "get_memo" || name == "update_memo") {
        const auto current = m_store.memo();
        const auto revision = hash(current.toUtf8());
        if (name == "update_memo") {
            if (args.value("expected_revision").toString() != revision) return failure("revision_conflict","备忘录已修改，请重新读取后再更新。");
            const auto content = args.value("content").toString();
            const auto next = args.value("mode") == "append" ? current + content : content;
            if (next.size() > 262144) return failure("invalid_arguments","备忘录最多支持 262144 个字符。");
            m_store.setMemo(next);
            if (!m_store.saveMemo()) return failure("save_failed",m_store.memoError());
        }
        return success({{"content",m_store.memo()}, {"revision",hash(m_store.memo().toUtf8())}});
    }
    if (name == "list_workdays" || name == "list_events") {
        const auto from = QDate::fromString(args.value("from").toString(),Qt::ISODate);
        const auto until = QDate::fromString(args.value("until").toString(),Qt::ISODate);
        if (!from.isValid() || !until.isValid() || until <= from || from.daysTo(until) > 366) return failure("invalid_arguments","请输入有效的日期范围：until 不包含当日，范围为 1 到 366 天。");
        QString error;
        if (name == "list_workdays") {
            if (!WorkdayCalendar::validateRange(from,until.addDays(-1),&error)) return failure("unknown_workdays",error);
            QJsonArray working, resting;
            for (auto day = from; day < until; day = day.addDays(1)) {
                const auto workday = WorkdayCalendar::isWorkday(day,&error);
                if (!workday.has_value()) return failure("unknown_workdays",error);
                (*workday ? working : resting).append(day.toString(Qt::ISODate));
            }
            return success({{"workdays",working}, {"non_workdays",resting}, {"coverage",m_store.workdayCoverage()}});
        }
        QStringList ids;
        for (const auto &value : args.value("calendar_ids").toArray()) {
            const auto id = value.toString();
            bool found = false;
            for (const auto &calendar : m_store.allCalendars()) if (calendar.toMap().value("id").toString() == id) found = true;
            if (!found) return failure("calendar_not_found","指定的日历不存在：" + id);
            ids.append(id);
        }
        const auto events = m_store.queryEvents(from,until,ids,&error);
        if (!error.isEmpty()) return failure("query_failed",error);
        const auto query = args.value("query").toString().trimmed();
        QJsonArray all;
        for (const auto &value : events) {
            const auto item = value.toMap();
            if (!query.isEmpty() && !item.value("title").toString().contains(query,Qt::CaseInsensitive) &&
                !item.value("location").toString().contains(query,Qt::CaseInsensitive) && !item.value("notes").toString().contains(query,Qt::CaseInsensitive)) continue;
            all.append(publicEvent(item));
        }
        auto fingerprintArgs = args;
        fingerprintArgs.remove("cursor"); fingerprintArgs.remove("limit");
        const auto fingerprint = hash(QJsonDocument(all).toJson(QJsonDocument::Compact) + QJsonDocument(fingerprintArgs).toJson(QJsonDocument::Compact));
        int offset = 0;
        if (args.contains("cursor")) {
            const auto decoded = QByteArray::fromBase64Encoding(args.value("cursor").toString().toLatin1(),QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
            const auto cursor = QJsonDocument::fromJson(decoded.decoded).object();
            const double number = cursor.value("offset").toDouble(-1);
            if (!decoded || cursor.value("fingerprint").toString() != fingerprint || number < 0 || std::floor(number) != number || number >= all.size()) return failure("invalid_cursor","分页条件或日程已改变，请从第一页重新查询。");
            offset = int(number);
        }
        const int end = qMin(offset + args.value("limit").toInt(100),int(all.size()));
        QJsonArray page;
        for (int i = offset; i < end; ++i) page.append(all[i]);
        QJsonValue next = QJsonValue::Null;
        if (end < all.size()) next = QString::fromLatin1(QJsonDocument(QJsonObject{{"offset",end}, {"fingerprint",fingerprint}}).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
        return success({{"events",page}, {"next_cursor",next}, {"total",all.size()}, {"time_zone",timeZone}});
    }
    QVariantMap fields;
    QString error;
    const bool creating = name == "create_event";
    if (creating) {
        fields = internalFields(args);
        fields["allDay"] = args.value("all_day").toBool(false);
        fields["endDate"] = args.value("end_date").toString(args.value("start_date").toString());
        fields["notes"] = args.value("notes").toString(QStringLiteral(""));
        fields["location"] = args.value("location").toString(QStringLiteral(""));
    } else {
        const auto id = args.value("event_id").toString(), recurrenceId = args.value("recurrence_id").toString();
        fields = m_store.event(id,recurrenceId,&error);
        if (!error.isEmpty() || fields.isEmpty()) return failure("event_not_found",error);
        if (fields.value("calendar").toString() != args.value("calendar_id").toString()) return failure("event_not_found","日程不属于指定的日历。");
        if (name == "get_event") return success({{"event",publicEvent(fields,true)}, {"time_zone",timeZone}});
        const auto scope = args.value("scope").toString();
        if ((scope == "occurrence" && recurrenceId.isEmpty()) || (scope == "series" && !recurrenceId.isEmpty())) return failure("invalid_scope","仅本次必须提供 recurrence_id；整个系列请省略 recurrence_id。");
        if (fields.value("readOnly").toBool()) return failure("read_only",fields.value("readOnlyReason").toString());
        if (args.value("expected_revision").toString() != fields.value("revision").toString()) return failure("revision_conflict","日程已修改，请重新读取后再操作。");
        if (name == "delete_event") {
            if (!m_store.deleteEvent(id,recurrenceId)) return failure("delete_failed",m_store.error());
            return success({{"deleted",true}, {"calendar_id",args.value("calendar_id")}, {"event_id",id}, {"scope",scope}});
        }
        const auto changes = args.value("changes").toObject();
        if (changes.isEmpty()) return failure("invalid_arguments","changes 至少需要一个修改字段。");
        const auto edits = internalFields(changes);
        for (auto it = edits.begin(); it != edits.end(); ++it) fields[it.key()] = it.value();
        fields["scope"] = scope;
    }
    const auto incoming = creating ? args : args.value("changes").toObject();
    QVariantMap rule = fields.value("recurrence").toMap();
    if (rule.isEmpty() || rule.size() == 1) {
        const auto frequency = rule.value("frequency","none");
        rule = {{"frequency",frequency}, {"interval",1}, {"weekdays",QVariantList{}}, {"endMode","never"},
            {"until",""}, {"count",1}, {"weekStart","MO"}, {"workdaysOnly",false}};
    }
    const auto recurrence = incoming.value("recurrence").toObject();
    rule = internalRecurrence(recurrence,rule);
    if (rule.value("frequency").toString() == "weekly" && !recurrence.contains("weekdays") && rule.value("weekdays").toList().isEmpty())
        rule["weekdays"] = QVariantList{QDate::fromString(fields.value("date").toString(),Qt::ISODate).dayOfWeek()};
    fields["recurrence"] = rule;
    bool writable = false;
    for (const auto &value : m_store.allCalendars()) {
        const auto calendar = value.toMap();
        if (calendar.value("id") == fields.value("calendar")) writable = calendar.value("writable").toBool();
    }
    if (!writable) return failure("read_only","目标日历不存在或不可写。");
    QString savedId;
    if (!m_store.writeEvent(fields,&savedId)) return failure("save_failed",m_store.error());
    auto saved = m_store.event(savedId,creating || args.value("scope") == "series" ? QString() : args.value("recurrence_id").toString(),&error);
    if (!error.isEmpty()) return failure("read_after_write_failed","日程已保存，但无法读取结果：" + error);
    return success({{"event",publicEvent(saved,true)}, {"time_zone",timeZone}});
}
