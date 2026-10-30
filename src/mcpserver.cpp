#include "mcpserver.h"
#include "calendarstore.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <functional>

namespace {
constexpr qsizetype MaxHeader = 16384;
constexpr qsizetype MaxBody = 1024 * 1024;
const QString ProtocolVersion = QStringLiteral("2025-11-25");
QJsonObject rpcError(const QJsonValue &id, int code, const QString &message)
{
    return {{"jsonrpc","2.0"}, {"id",id}, {"error",QJsonObject{{"code",code}, {"message",message}}}};
}
}

// One bounded HTTP request per connection. Connection: close also avoids keeping
// idle client sockets alive when the user turns the service off or changes port.
class McpHttpConnection final : public QObject
{
public:
    using Handler = std::function<void(McpHttpConnection *,const QByteArray &,const QMap<QByteArray,QByteArray> &,const QByteArray &)>;
    McpHttpConnection(QTcpSocket *socket, Handler handler, QObject *parent)
        : QObject(parent), m_socket(socket), m_handler(std::move(handler))
    {
        socket->setParent(this);
        socket->setReadBufferSize(MaxBody * 2 + MaxHeader);
        connect(socket,&QTcpSocket::disconnected,this,&QObject::deleteLater);
        connect(socket,&QTcpSocket::readyRead,this,[this] { read(); });
        m_timeout.setSingleShot(true);
        connect(&m_timeout,&QTimer::timeout,this,[this] {
            if (!m_done) respond(408,"Request timeout");
            else m_socket->abort();
        });
        m_timeout.start(15000);
        if (socket->bytesAvailable()) QTimer::singleShot(0,this,[this] { read(); });
    }
    void abort() { m_done = true; m_socket->abort(); deleteLater(); }
    void respond(int status, const QByteArray &body = {}, const QByteArray &contentType = "text/plain; charset=utf-8")
    {
        if (m_done) return;
        m_done = true;
        const QMap<int,QByteArray> reasons{{200,"OK"}, {202,"Accepted"}, {400,"Bad Request"}, {403,"Forbidden"},
            {404,"Not Found"}, {405,"Method Not Allowed"}, {406,"Not Acceptable"}, {408,"Request Timeout"},
            {411,"Length Required"}, {413,"Content Too Large"}, {415,"Unsupported Media Type"},
            {431,"Request Header Fields Too Large"}, {503,"Service Unavailable"}};
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + reasons.value(status) + "\r\n";
        response += "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
        if (status == 405) response += "Allow: POST\r\n";
        response += "Content-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        m_socket->write(response);
        m_socket->disconnectFromHost();
    }
    void json(int status, const QJsonObject &value)
    {
        respond(status,QJsonDocument(value).toJson(QJsonDocument::Compact),"application/json");
    }
private:
    void read()
    {
        if (m_done) return;
        m_buffer += m_socket->readAll();
        if (m_buffer.size() + m_body.size() > MaxBody * 2 + MaxHeader) { respond(413,"Request too large"); return; }
        if (!m_headersReady) {
            const auto separator = m_buffer.indexOf("\r\n\r\n");
            if (separator < 0) {
                if (m_buffer.size() > MaxHeader) respond(431,"Headers too large");
                return;
            }
            if (separator > MaxHeader) { respond(431,"Headers too large"); return; }
            const auto lines = m_buffer.left(separator).split('\n');
            const auto request = lines.first().trimmed().split(' ');
            if (request.size() != 3 || request[2] != "HTTP/1.1") { respond(400,"Invalid HTTP request"); return; }
            m_method = request[0];
            if (request[1] != "/mcp") { respond(404,"Not found"); return; }
            static const QRegularExpression headerName(QStringLiteral("^[!#$%&'*+.^_`|~0-9A-Za-z-]+$"));
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const auto line = lines[i];
                const auto colon = line.indexOf(':');
                if (colon <= 0 || !headerName.match(QString::fromLatin1(line.left(colon))).hasMatch()) { respond(400,"Invalid header"); return; }
                const auto key = line.left(colon).toLower(), value = line.mid(colon + 1).trimmed();
                if (m_headers.contains(key) || value.contains('\r') || value.contains('\n') || value.contains('\0')) { respond(400,"Duplicate or invalid header"); return; }
                m_headers.insert(key,value);
            }
            m_buffer.remove(0,separator + 4);
            m_headersReady = true;
            if (m_headers.contains("transfer-encoding")) {
                if (m_headers.value("transfer-encoding").toLower() != "chunked" || m_headers.contains("content-length")) { respond(400,"Invalid message framing"); return; }
                m_chunked = true;
            } else if (m_headers.contains("content-length")) {
                const auto length = m_headers.value("content-length");
                bool valid = false;
                m_length = length.toLongLong(&valid);
                if (!valid || length.isEmpty() || length.contains('+') || length.contains('-') || m_length < 0) { respond(400,"Invalid content length"); return; }
                if (m_length > MaxBody) { respond(413,"Request too large"); return; }
            } else if (m_method == "POST") { respond(411,"Content-Length or chunked encoding required"); return; }
            if (m_headers.value("expect").toLower() == "100-continue") m_socket->write("HTTP/1.1 100 Continue\r\n\r\n");
        }
        if (m_chunked) {
            while (true) {
                if (m_chunkSize < 0) {
                    const auto end = m_buffer.indexOf("\r\n");
                    if (end < 0) { if (m_buffer.size() > 1024) respond(400,"Invalid chunk header"); return; }
                    const auto sizeText = m_buffer.left(end).split(';').first();
                    bool valid = false;
                    m_chunkSize = sizeText.toLongLong(&valid,16);
                    if (!valid || sizeText.isEmpty() || sizeText.contains('-') || sizeText.contains('+') || m_chunkSize < 0) { respond(400,"Invalid chunk size"); return; }
                    if (m_chunkSize > MaxBody - m_body.size()) { respond(413,"Request too large"); return; }
                    m_buffer.remove(0,end + 2);
                }
                if (m_chunkSize == 0) {
                    if (m_buffer.startsWith("\r\n")) break;
                    if (m_buffer.indexOf("\r\n\r\n") >= 0) break;
                    if (m_buffer.size() > MaxHeader) respond(431,"Trailers too large");
                    return;
                }
                if (m_buffer.size() < m_chunkSize + 2) return;
                if (m_buffer.mid(m_chunkSize,2) != "\r\n") { respond(400,"Invalid chunk terminator"); return; }
                m_body += m_buffer.left(m_chunkSize);
                m_buffer.remove(0,m_chunkSize + 2);
                m_chunkSize = -1;
            }
        } else {
            if (m_buffer.size() < m_length) return;
            m_body = m_buffer.left(m_length);
        }
        m_handler(this,m_method,m_headers,m_body);
    }
    QTcpSocket *m_socket;
    QTimer m_timeout;
    Handler m_handler;
    QByteArray m_buffer, m_body, m_method;
    QMap<QByteArray,QByteArray> m_headers;
    qint64 m_length = 0, m_chunkSize = -1;
    bool m_headersReady = false, m_chunked = false, m_done = false;
};

McpServer::McpServer(CalendarStore &store, QObject *parent)
    : QObject(parent), m_settings("Dayline",store.demo() ? "preview" : "dayline"), m_tools(store)
{
    m_server.setMaxPendingConnections(32);
    connect(&m_server,&QTcpServer::newConnection,this,&McpServer::acceptConnections);
    connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,&McpServer::stop);
    restart();
}

QString McpServer::url() const { return QString("http://127.0.0.1:%1/mcp").arg(port()); }
void McpServer::copyUrl() const { QGuiApplication::clipboard()->setText(url()); }
void McpServer::copyConfiguration() const
{
    QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(QJsonObject{
        {"mcpServers",QJsonObject{{"dayline",QJsonObject{{"url",url()}}}}}}).toJson(QJsonDocument::Indented)));
}
void McpServer::stop()
{
    m_server.close();
    const auto connections = m_connections;
    for (auto *connection : connections) connection->abort();
    m_connections.clear();
}
void McpServer::restart()
{
    stop();
    m_error.clear();
    if (enabled()) {
        if (port() < 1 || port() > 65535) m_error = "端口必须在 1–65535 之间。";
        else if (!m_server.listen(QHostAddress::LocalHost,quint16(port())))
            m_error = m_server.serverError() == QAbstractSocket::AddressInUseError
                ? QString("端口 %1 已被占用，请更换端口或关闭占用程序。").arg(port())
                : QString("无法监听端口 %1：%2").arg(port()).arg(m_server.errorString());
    }
    emit changed();
}
void McpServer::setEnabled(bool value)
{
    if (value == enabled()) return;
    m_settings.setValue("mcp/enabled",value);
    restart();
}
void McpServer::setPort(int value)
{
    if (value < 1 || value > 65535) { m_error = "端口必须在 1–65535 之间。"; emit changed(); return; }
    if (value == port()) return;
    m_settings.setValue("mcp/port",value);
    restart();
}
void McpServer::acceptConnections()
{
    while (auto *socket = m_server.nextPendingConnection()) {
        if (m_connections.size() >= 32) { socket->abort(); socket->deleteLater(); continue; }
        auto *connection = new McpHttpConnection(socket,[this](auto *client, const auto &method, const auto &headers, const auto &body) {
            handle(client,method,headers,body);
        },this);
        m_connections.insert(connection);
        connect(connection,&QObject::destroyed,this,[this,connection] { m_connections.remove(connection); });
    }
}

void McpServer::handle(McpHttpConnection *connection, const QByteArray &method,
                       const QMap<QByteArray,QByteArray> &headers, const QByteArray &body)
{
    // Validate both Host and Origin against the actual listener to prevent DNS
    // rebinding and cross-origin browser requests to a local write-capable API.
    const auto host = headers.value("host").toLower();
    const auto suffix = ':' + QByteArray::number(port());
    if (host != "127.0.0.1" + suffix && host != "localhost" + suffix &&
        !(port() == 80 && (host == "127.0.0.1" || host == "localhost"))) { connection->respond(403,"Invalid Host"); return; }
    if (headers.contains("origin")) {
        const auto origin = QUrl::fromEncoded(headers.value("origin"));
        if (!origin.isValid() || origin.scheme() != "http" || !origin.userInfo().isEmpty() ||
            (origin.host() != "127.0.0.1" && origin.host() != "localhost") || origin.port(80) != port() ||
            (!origin.path().isEmpty() && origin.path() != "/") || origin.hasQuery() || origin.hasFragment()) {
            connection->respond(403,"Invalid Origin"); return;
        }
    }
    if (method != "POST") { connection->respond(405); return; }
    if (headers.value("content-type").split(';').first().trimmed().toLower() != "application/json") { connection->respond(415,"Expected application/json"); return; }
    bool acceptsJson = false;
    for (const auto &part : headers.value("accept").split(',')) {
        const auto type = part.split(';').first().trimmed().toLower();
        if (type == "application/json" || type == "application/*" || type == "*/*") acceptsJson = true;
    }
    if (!acceptsJson) { connection->respond(406,"Accept must include application/json"); return; }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(body,&parseError);
    if (parseError.error != QJsonParseError::NoError) { connection->json(400,rpcError(QJsonValue::Null,-32700,"Parse error")); return; }
    if (!document.isObject()) { connection->json(400,rpcError(QJsonValue::Null,-32600,"Expected one JSON-RPC message")); return; }
    const auto request = document.object();
    const auto id = request.value("id");
    const auto rpcMethod = request.value("method").toString();
    const bool hasId = request.contains("id");
    if (headers.contains("mcp-protocol-version") && headers.value("mcp-protocol-version") != ProtocolVersion.toLatin1()) {
        connection->respond(400,"Unsupported MCP-Protocol-Version"); return;
    }
    const bool response = !request.contains("method") && hasId && (id.isString() || id.isDouble()) &&
        (request.contains("result") != request.contains("error")) &&
        (!request.contains("error") || request.value("error").isObject());
    if (request.value("jsonrpc") == "2.0" && response) { connection->respond(202); return; }
    if (request.value("jsonrpc") != "2.0" || rpcMethod.isEmpty() ||
        (hasId && !id.isString() && !id.isDouble()) ||
        (request.contains("params") && !request.value("params").isObject())) {
        connection->json(400,rpcError(QJsonValue::Null,-32600,"Invalid Request")); return;
    }
    if (rpcMethod != "initialize" && headers.value("mcp-protocol-version") != ProtocolVersion.toLatin1()) {
        connection->respond(400,"Unsupported or missing MCP-Protocol-Version"); return;
    }
    // A stateless Streamable HTTP endpoint needs no session ID or GET/SSE stream.
    // Valid client notifications have no response body and never invoke tools.
    if (!hasId) { connection->respond(202); return; }
    const auto params = request.value("params").toObject();
    QJsonObject result;
    if (rpcMethod == "initialize") {
        if (!params.value("protocolVersion").isString() || !params.value("capabilities").isObject() || !params.value("clientInfo").isObject()) {
            connection->json(200,rpcError(id,-32602,"Invalid initialization parameters")); return;
        }
        result = {{"protocolVersion",ProtocolVersion}, {"capabilities",QJsonObject{{"tools",QJsonObject{}}}},
            {"serverInfo",QJsonObject{{"name","dayline"}, {"version",QCoreApplication::applicationVersion()}}},
            {"instructions","操作日笺中的日历、日程和本机备忘录。所有日期和时间按电脑本机时区解释；查询范围为 [from, until)，全天日程的 end_date 包含当日。修改或删除前先读取 revision。"}};
    } else if (rpcMethod == "ping") result = {};
    else if (rpcMethod == "tools/list") {
        if (params.contains("cursor")) { connection->json(200,rpcError(id,-32602,"Tool list has no additional pages")); return; }
        result = {{"tools",m_tools.definitions()}};
    } else if (rpcMethod == "tools/call") {
        const auto tool = params.value("name").toString();
        if (!m_tools.contains(tool) || (params.contains("arguments") && !params.value("arguments").isObject())) {
            connection->json(200,rpcError(id,-32602,"Unknown tool or invalid arguments")); return;
        }
        result = m_tools.call(tool,params.value("arguments").toObject());
    } else { connection->json(200,rpcError(id,-32601,"Method not found")); return; }
    connection->json(200,{{"jsonrpc","2.0"}, {"id",id}, {"result",result}});
}
