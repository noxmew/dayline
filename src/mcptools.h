#pragma once

#include <QJsonArray>
#include <QJsonObject>

class CalendarStore;

class McpTools final
{
public:
    explicit McpTools(CalendarStore &store);
    QJsonArray definitions() const { return m_definitions; }
    bool contains(const QString &name) const;
    QJsonObject call(const QString &name, const QJsonObject &arguments);

private:
    QJsonObject execute(const QString &name, const QJsonObject &arguments);
    CalendarStore &m_store;
    QJsonArray m_definitions;
};
