#pragma once

#include <QDate>
#include <QVariantList>
#include <QByteArray>

namespace ICalResource {
QVariantList occurrences(const QByteArray &raw, const QDate &from, const QDate &until,
                         QString *error = nullptr);
QByteArray edit(const QByteArray &raw, const QVariantMap &fields, const QString &uid, QString *error);
QByteArray exclude(const QByteArray &raw, const QString &recurrenceId, QString *error);
QVariantMap fields(const QByteArray &raw);
QVariantMap details(const QByteArray &raw, const QString &recurrenceId, QString *error);
bool canEdit(const QByteArray &raw);
QByteArray copy(const QByteArray &raw, const QString &uid, QString *error);
QString title(const QByteArray &raw);
QString uid(const QByteArray &raw);
}
