#pragma once

#include "mcptools.h"
#include <QObject>
#include <QSet>
#include <QSettings>
#include <QTcpServer>

class CalendarStore;
class McpHttpConnection;

class McpServer final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString url READ url NOTIFY changed)
public:
    explicit McpServer(CalendarStore &store, QObject *parent = nullptr);
    bool enabled() const { return m_settings.value("mcp/enabled",false).toBool(); }
    int port() const { return m_settings.value("mcp/port",18742).toInt(); }
    bool running() const { return m_server.isListening(); }
    QString error() const { return m_error; }
    QString url() const;
    void setEnabled(bool enabled);
    void setPort(int port);
    Q_INVOKABLE void restart();
    Q_INVOKABLE void copyUrl() const;
    Q_INVOKABLE void copyConfiguration() const;
signals:
    void changed();
private:
    void stop();
    void acceptConnections();
    void handle(McpHttpConnection *connection, const QByteArray &method,
                const QMap<QByteArray,QByteArray> &headers, const QByteArray &body);
    QTcpServer m_server;
    QSettings m_settings;
    McpTools m_tools;
    QSet<McpHttpConnection *> m_connections;
    QString m_error;
};
