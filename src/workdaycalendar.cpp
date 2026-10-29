#include "workdaycalendar.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>

namespace {
struct CalendarData {
    QSet<int> years;
    QMap<QDate, bool> days;
    QString coverage;
    QString error;
};
const CalendarData &calendar()
{
    static const CalendarData data = [] {
        CalendarData result;
        auto invalid = [&] {
            result.error = "节假日数据不完整或无效，无法按工作日安排。";
            return result;
        };
        QFile file(":/dayline/assets/holidays/china.json");
        if (!file.open(QIODevice::ReadOnly)) return invalid();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return invalid();
        const auto years = document.object().value("years").toArray();
        if (years.isEmpty()) return invalid();
        int previousYear = 0;
        for (const auto &entry : years) {
            const auto year = entry.toObject();
            const int number = year.value("year").toInt();
            if (number < 1 || (previousYear && number != previousYear + 1) ||
                year.value("papers").toArray().isEmpty() || year.value("days").toArray().isEmpty()) return invalid();
            previousYear = number;
            result.years.insert(number);
            for (const auto &value : year.value("days").toArray()) {
                const auto item = value.toObject();
                const auto day = QDate::fromString(item.value("date").toString(), Qt::ISODate);
                if (!day.isValid() || qAbs(day.year() - number) > 1 || !item.value("isOffDay").isBool()) return invalid();
                // The following year's announcement can also arrange days in
                // December; process annual announcements in chronological order.
                result.days[day] = !item.value("isOffDay").toBool();
            }
        }
        const int first = years.first().toObject().value("year").toInt();
        result.coverage = first == previousYear ? QString::number(first) + " 年"
            : QString::number(first) + "–" + QString::number(previousYear) + " 年";
        return result;
    }();
    return data;
}
}

QString WorkdayCalendar::coverage() { return calendar().coverage; }

bool WorkdayCalendar::validateRange(const QDate &first, const QDate &last, QString *error)
{
    const auto &data = calendar();
    if (!data.error.isEmpty()) { *error = data.error; return false; }
    if (!first.isValid() || !last.isValid() || last < first) { *error = "请选择有效的工作日日期范围。"; return false; }
    for (int year = first.year(); year <= last.year(); ++year) {
        if (!data.years.contains(year)) {
            *error = QString("%1 年的节假日安排尚未收录，无法按工作日安排。当前数据：%2。").arg(year).arg(data.coverage);
            return false;
        }
    }
    return true;
}

std::optional<bool> WorkdayCalendar::isWorkday(const QDate &day, QString *error)
{
    if (!validateRange(day, day, error)) return std::nullopt;
    const auto &days = calendar().days;
    const auto override = days.constFind(day);
    return override != days.cend() ? override.value() : day.dayOfWeek() <= 5;
}
