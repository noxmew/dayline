#include "updatechecker.h"
#include "updateconfig.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <memory>

namespace {
constexpr qint64 MaximumPackageSize = 64 * 1024 * 1024;
constexpr qint64 MaximumMetadataSize = 2 * 1024 * 1024;
const QString Repository = QStringLiteral("https://github.com/noxmew/dayline");

QUrl proxyBase()
{
    QString value = QString::fromUtf8(QByteArray::fromHex(DAYLINE_UPDATE_PROXY_HEX)).trimmed();
    if (!value.endsWith('/')) value += '/';
    const QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) return {};
    return url;
}

QString architecture()
{
#if defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("amd64");
#elif defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("arm64");
#else
    return {};
#endif
}
}

class UpdateWorker final : public QObject
{
    Q_OBJECT
public:
    explicit UpdateWorker(QObject *parent = nullptr)
        : QObject(parent), m_network(this), m_timeout(this), m_proxy(proxyBase()),
          m_currentVersion(QCoreApplication::applicationVersion())
    {
        m_timeout.setSingleShot(true);
        connect(&m_timeout, &QTimer::timeout, this, [this] {
            fail(m_downloading ? "下载超时，请稍后重试。" : "检查更新超时，请稍后重试。");
        });
    }

public slots:
    void check()
    {
        if (m_reply) return;
        clearPackage();
        if (m_proxy.isEmpty()) { fail("此构建未配置更新服务。请使用发布页面提供的安装包。"); return; }
#if !defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
        fail("目前仅提供 Linux 桌面版更新包。"); return;
#endif
        if (architecture().isEmpty()) {
            fail("目前仅提供 amd64 和 arm64 架构的更新包。"); return;
        }
        emit snapshot("checking", "正在检查新版本…", {}, {}, {}, {}, 0);
        m_downloading = false;
        m_timeout.start(45000);
        start(QUrl("https://api.github.com/repos/noxmew/dayline/releases/latest"));
    }

    void download()
    {
        if (m_reply || m_assetUrl.isEmpty() || m_digest.size() != 32) return;
        const QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (directory.isEmpty() || !QDir().mkpath(directory)) { fail("无法访问下载文件夹，请检查目录权限。"); return; }
        m_destination = QDir(directory).filePath(m_name);
        // Keep an existing download intact; every retry gets its own complete file.
        for (int index = 1; QFileInfo::exists(m_destination); ++index) {
            if (index > 9999) { fail("下载文件夹中已有过多同名文件，请先整理后重试。"); return; }
            m_destination = QDir(directory).filePath(QFileInfo(m_name).completeBaseName() + " (" + QString::number(index) + ").deb");
        }
        m_file = std::make_unique<QSaveFile>(m_destination);
        m_file->setDirectWriteFallback(false);
        if (!m_file->open(QIODevice::WriteOnly)) { fail("无法创建下载文件，请检查目录权限和磁盘空间。"); return; }
        m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
        m_downloading = true;
        m_written = 0;
        emit snapshot("downloading", "正在下载安装包…", m_version, m_notes, m_name, {}, m_size);
        emit progress(0, m_size);
        m_timeout.start(15 * 60 * 1000);
        start(m_assetUrl);
    }

    void cancel()
    {
        const bool downloading = m_downloading;
        stop();
        emit snapshot(downloading && !m_assetUrl.isEmpty() ? "available" : "idle", "已取消。", m_version, m_notes, m_name, {}, m_size);
        emit progress(0, m_size);
    }

    void shutdown()
    {
        stop();
        QThread::currentThread()->quit();
    }

signals:
    void snapshot(const QString &state, const QString &message, const QString &version,
                  const QString &notes, const QString &name, const QString &path, qint64 size);
    void progress(qint64 received, qint64 total);

private:
    void clearPackage()
    {
        m_assetUrl.clear(); m_version.clear(); m_notes.clear(); m_name.clear(); m_digest.clear(); m_size = 0;
    }

    void stop()
    {
        m_timeout.stop();
        if (m_reply) {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        if (m_file) { m_file->cancelWriting(); m_file.reset(); }
        m_hash.reset();
        m_downloading = false;
        m_body.clear();
    }

    void fail(const QString &message)
    {
        stop();
        emit snapshot("error", message, m_version, m_notes, m_name, {}, m_size);
    }

    bool inProxy(const QUrl &url) const
    {
        return url.isValid() && url.scheme() == "https" && url.host() == m_proxy.host() &&
               url.port(443) == m_proxy.port(443) && url.userInfo().isEmpty() &&
               url.toString(QUrl::FullyEncoded).startsWith(m_proxy.toString(QUrl::FullyEncoded));
    }

    void start(const QUrl &upstream)
    {
        m_body.clear();
        m_redirects = 0;
        send(QUrl(m_proxy.toString(QUrl::FullyEncoded) + upstream.toString(QUrl::FullyEncoded), QUrl::StrictMode));
    }

    void send(const QUrl &url)
    {
        if (!inProxy(url)) { fail("更新服务返回了无效地址，已停止请求。"); return; }
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(30000);
        request.setRawHeader("User-Agent", "Dayline/" + m_currentVersion.toLatin1());
        request.setRawHeader("Accept", m_downloading ? "application/octet-stream" : "application/vnd.github+json");
        if (!m_downloading) {
            request.setRawHeader("Cache-Control", "no-cache");
            request.setRawHeader("Pragma", "no-cache");
        }
        m_reply = m_network.get(request);
        m_reply->setReadBufferSize(256 * 1024);
        connect(m_reply, &QNetworkReply::readyRead, this, &UpdateWorker::consume);
        connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
            if (m_downloading) emit progress(received, m_size);
        });
        connect(m_reply, &QNetworkReply::finished, this, &UpdateWorker::finished);
    }

    void consume()
    {
        if (!m_reply) return;
        if (m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            m_reply->readAll();
            return;
        }
        while (m_reply && m_reply->bytesAvailable()) {
            const QByteArray chunk = m_reply->read(64 * 1024);
            if (m_downloading) {
                m_written += chunk.size();
                if (m_written > m_size || m_written > MaximumPackageSize) { fail("安装包大小与发布信息不符，下载已丢弃。"); return; }
                if (m_file->write(chunk) != chunk.size()) { fail("保存安装包失败，请检查磁盘空间和下载目录权限。"); return; }
                m_hash->addData(chunk);
            } else {
                m_body += chunk;
                if (m_body.size() > MaximumMetadataSize) { fail("更新服务返回的数据过大，已停止检查。"); return; }
            }
        }
    }

    void finished()
    {
        auto *reply = m_reply;
        if (!reply) return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400) {
            const QUrl redirect = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            reply->deleteLater(); m_reply = nullptr;
            if (++m_redirects > 5 || !inProxy(redirect)) { fail("更新服务发生了未经允许的重定向，已停止请求。"); return; }
            send(redirect);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            fail(status ? QString("更新请求未完成（HTTP %1），请稍后重试。").arg(status) : "无法连接更新服务，请检查网络后重试。");
            return;
        }
        consume();
        if (!m_reply) return;
        m_reply->deleteLater(); m_reply = nullptr;
        m_timeout.stop();
        if (m_downloading) {
            if (m_written != m_size || m_hash->result() != m_digest) { fail("安装包校验未通过，下载已丢弃。请重新检查更新后再下载。"); return; }
            if (!m_file->commit()) { fail("无法保存完整安装包，请检查磁盘空间和下载目录权限。"); return; }
            m_file.reset(); m_hash.reset(); m_downloading = false;
            emit progress(m_written, m_size);
            emit snapshot("downloaded", "安装包已下载并通过校验。", m_version, m_notes, m_name, m_destination, m_size);
        } else readRelease();
    }

    void readRelease()
    {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(m_body, &error);
        m_body.clear();
        if (error.error != QJsonParseError::NoError || !document.isObject()) { fail("更新服务返回的信息无法解析。"); return; }
        const auto release = document.object();
        const auto match = QRegularExpression("^v([0-9]+\\.[0-9]+\\.[0-9]+)$").match(release.value("tag_name").toString());
        if (!match.hasMatch() || release.value("draft").toBool(true) || release.value("prerelease").toBool(true)) {
            fail("更新服务未返回有效的正式版本。"); return;
        }
        m_version = match.captured(1);
        m_notes = release.value("body").toString().section("<!--", 0, 0).trimmed().left(16000);
        qsizetype latestEnd = 0, currentEnd = 0;
        const auto latest = QVersionNumber::fromString(m_version, &latestEnd);
        const auto current = QVersionNumber::fromString(m_currentVersion, &currentEnd);
        if (latest.segmentCount() != 3 || current.segmentCount() != 3 || latestEnd != m_version.size() || currentEnd != m_currentVersion.size()) {
            fail("无法识别应用版本，请使用正式发布的安装包。"); return;
        }
        if (QVersionNumber::compare(latest, current) <= 0) {
            emit snapshot("current", "当前已是最新版本。", m_version, {}, {}, {}, 0); return;
        }
        m_name = "dayline_" + m_version + '_' + architecture() + ".deb";
        QJsonObject asset;
        int matches = 0;
        for (const auto &value : release.value("assets").toArray()) {
            if (value.toObject().value("name").toString() == m_name) { asset = value.toObject(); ++matches; }
        }
        const auto digest = QRegularExpression("^sha256:([0-9a-fA-F]{64})$").match(asset.value("digest").toString());
        const QString expectedUrl = Repository + "/releases/download/v" + m_version + '/' + m_name;
        m_size = asset.value("size").toVariant().toLongLong();
        if (matches != 1 || asset.value("state").toString() != "uploaded" || !digest.hasMatch() ||
            m_size <= 0 || m_size > MaximumPackageSize || asset.value("browser_download_url").toString() != expectedUrl) {
            clearPackage(); fail("当前设备的安装包信息不完整，请稍后重试。"); return;
        }
        m_digest = QByteArray::fromHex(digest.captured(1).toLatin1());
        m_assetUrl = QUrl(expectedUrl);
        emit snapshot("available", "发现新版本。", m_version, m_notes, m_name, {}, m_size);
    }

    QNetworkAccessManager m_network;
    QTimer m_timeout;
    QUrl m_proxy, m_assetUrl;
    QString m_currentVersion, m_version, m_notes, m_name, m_destination;
    QByteArray m_body, m_digest;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QSaveFile> m_file;
    std::unique_ptr<QCryptographicHash> m_hash;
    qint64 m_size = 0, m_written = 0;
    int m_redirects = 0;
    bool m_downloading = false;
};

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), m_worker(new UpdateWorker), m_thread(new QThread(this))
{
    m_configured = !proxyBase().isEmpty();
    m_message = m_configured ? "尚未检查更新。" : "此构建未配置更新服务。请使用发布页面提供的安装包。";
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &UpdateWorker::snapshot, this,
            [this](const QString &state, const QString &message, const QString &version, const QString &notes,
                   const QString &name, const QString &path, qint64 size) {
        m_state = state; m_message = message; m_version = version; m_notes = notes; m_name = name; m_path = path; m_size = size;
        emit changed();
    });
    connect(m_worker, &UpdateWorker::progress, this, [this](qint64 received, qint64 total) {
        m_received = received; m_total = total; emit progressChanged();
    });
    m_thread->start();
}

UpdateChecker::~UpdateChecker()
{
    // Only application teardown waits: the worker first aborts requests and
    // removes its temporary file, then exits its own event loop.
    QMetaObject::invokeMethod(m_worker, &UpdateWorker::shutdown, Qt::QueuedConnection);
    m_thread->wait();
}

void UpdateChecker::check()
{
    if (busy()) return;
    m_state = "checking"; m_message = "正在检查新版本…"; m_received = 0; m_total = 0;
    emit changed(); emit progressChanged();
    QMetaObject::invokeMethod(m_worker, &UpdateWorker::check, Qt::QueuedConnection);
}

void UpdateChecker::download()
{
    if (busy() || m_state != "available") return;
    m_state = "downloading"; m_message = "正在下载安装包…";
    emit changed();
    QMetaObject::invokeMethod(m_worker, &UpdateWorker::download, Qt::QueuedConnection);
}

void UpdateChecker::cancel()
{
    if (busy()) QMetaObject::invokeMethod(m_worker, &UpdateWorker::cancel, Qt::QueuedConnection);
}

void UpdateChecker::openDownloadFolder()
{
    if (m_path.isEmpty()) return;
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_path).absolutePath()))) {
        m_message = "无法打开下载文件夹，请在文件管理器中进入下载目录。";
        emit changed();
    }
}

#include "updatechecker.moc"
