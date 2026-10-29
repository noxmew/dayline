#pragma once

#include <QDate>
#include <QString>
#include <optional>

namespace WorkdayCalendar {
QString coverage();
bool validateRange(const QDate &first, const QDate &last, QString *error);
std::optional<bool> isWorkday(const QDate &day, QString *error);
}
