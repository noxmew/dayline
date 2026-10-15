#include "secretstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QTimer>
#include <memory>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <unistd.h>

namespace {
constexpr int keySize = 32;
constexpr int nonceSize = 12;
constexpr int tagSize = 16;
constexpr int maximumSecretSize = 65536;
const QByteArray magic = QByteArrayLiteral("DLSE1");
const auto filePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
const auto directoryPermissions = filePermissions | QFileDevice::ExeOwner;
struct Result {
    QByteArray value;
    QString error;
};
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

bool privatePath(const QFileInfo &info, bool directory)
{
    const auto permissions = info.permissions();
    const auto publicPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
        QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    const auto required = directory ? directoryPermissions : filePermissions;
    return !info.isSymLink() && info.ownerId() == uint(geteuid()) &&
        (directory ? info.isDir() : info.isFile()) && !(permissions & publicPermissions) &&
        (permissions & required) == required && (directory || !(permissions & QFileDevice::ExeOwner));
}

bool save(const QString &path, const QByteArray &value)
{
    if (QFileInfo(path).isSymLink()) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (!file.setPermissions(filePermissions) || file.write(value) != value.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit() && privatePath(QFileInfo(path), false);
}

Result key(const QString &directory, bool create)
{
    const QString path = QDir(directory).filePath("key");
    const QFileInfo info(path);
    if (info.isSymLink()) return {{}, "本机凭据密钥文件异常，已停止访问。"};
    if (info.exists()) {
        if (!privatePath(info, false)) return {{}, "本机凭据密钥文件权限异常，已停止访问。"};
        QFile file(path);
        if (info.size() != keySize) return {{}, "本机凭据密钥已损坏，已停止访问。"};
        if (!file.open(QIODevice::ReadOnly)) return {{}, "无法读取本机凭据密钥，请检查文件权限。"};
        const QByteArray bytes = file.readAll();
        if (bytes.size() != keySize || file.error() != QFileDevice::NoError)
            return {{}, "无法完整读取本机凭据密钥，已停止访问。"};
        return {bytes, {}};
    }
    const auto credentials = QDir(directory).entryList({"*.secret"}, QDir::AllEntries | QDir::Hidden | QDir::System);
    if (!create || !credentials.isEmpty())
        return {{}, "本机凭据密钥缺失，无法解密专用密码。请检查本机凭据文件。"};
    QByteArray bytes(keySize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(bytes.data()), bytes.size()) != 1)
        return {{}, "无法生成本机加密密钥，专用密码尚未保存。"};
    if (!save(path, bytes)) return {{}, "无法安全保存本机加密密钥，请检查文件权限。"};
    return {bytes, {}};
}

Result encrypt(const QByteArray &key, const QByteArray &identity, const QByteArray &password)
{
    QByteArray nonce(nonceSize, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(nonce.data()), nonce.size()) != 1)
        return {{}, "无法生成本机加密随机数，专用密码尚未保存。"};
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) return {{}, "无法初始化本机凭据加密。"};
    const QByteArray associated = magic + identity;
    QByteArray ciphertext(password.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    int size = 0, finalSize = 0;
    QByteArray tag(tagSize, Qt::Uninitialized);
    if (EVP_EncryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1 ||
        EVP_EncryptInit_ex(context.get(), nullptr, nullptr,
            reinterpret_cast<const unsigned char *>(key.constData()), reinterpret_cast<const unsigned char *>(nonce.constData())) != 1 ||
        EVP_EncryptUpdate(context.get(), nullptr, &size,
            reinterpret_cast<const unsigned char *>(associated.constData()), associated.size()) != 1 ||
        EVP_EncryptUpdate(context.get(), reinterpret_cast<unsigned char *>(ciphertext.data()), &size,
            reinterpret_cast<const unsigned char *>(password.constData()), password.size()) != 1 ||
        EVP_EncryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(ciphertext.data()) + size, &finalSize) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_GET_TAG, tag.size(), tag.data()) != 1)
        return {{}, "本机凭据加密失败，专用密码尚未保存。"};
    ciphertext.resize(size + finalSize);
    return {magic + nonce + ciphertext + tag, {}};
}

Result decrypt(const QByteArray &key, const QByteArray &identity, const QByteArray &data)
{
    if (!data.startsWith(magic) || data.size() <= magic.size() + nonceSize + tagSize)
        return {{}, "本机凭据文件格式无效，请重新连接 iCloud。"};
    const QByteArray nonce = data.mid(magic.size(), nonceSize);
    QByteArray tag = data.right(tagSize);
    const QByteArray ciphertext = data.mid(magic.size() + nonceSize, data.size() - magic.size() - nonceSize - tagSize);
    const QByteArray associated = magic + identity;
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) return {{}, "无法初始化本机凭据解密。"};
    QByteArray plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, Qt::Uninitialized);
    int size = 0, finalSize = 0;
    if (EVP_DecryptInit_ex(context.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_IVLEN, nonce.size(), nullptr) != 1 ||
        EVP_DecryptInit_ex(context.get(), nullptr, nullptr,
            reinterpret_cast<const unsigned char *>(key.constData()), reinterpret_cast<const unsigned char *>(nonce.constData())) != 1 ||
        EVP_DecryptUpdate(context.get(), nullptr, &size,
            reinterpret_cast<const unsigned char *>(associated.constData()), associated.size()) != 1 ||
        EVP_DecryptUpdate(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()), &size,
            reinterpret_cast<const unsigned char *>(ciphertext.constData()), ciphertext.size()) != 1 ||
        EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_GCM_SET_TAG, tag.size(), tag.data()) != 1 ||
        EVP_DecryptFinal_ex(context.get(), reinterpret_cast<unsigned char *>(plaintext.data()) + size, &finalSize) != 1) {
        plaintext.fill(char(0));
        return {{}, "本机凭据校验失败，请重新连接 iCloud。"};
    }
    plaintext.resize(size + finalSize);
    return {plaintext, {}};
}

Result accessStorage(const QString &directory, const QString &account, const QByteArray &password, int operation)
{
    if (directory.isEmpty() || !QDir::isAbsolutePath(directory))
        return {{}, "本机凭据存储路径尚未配置。"};
    if (account.trimmed().isEmpty()) return {{}, "Apple 账户不能为空。"};
    if (operation == 1 && (password.isEmpty() || password.size() > maximumSecretSize))
        return {{}, "App 专用密码为空或过长，请重新填写。"};
    QFileInfo info(directory);
    if (info.isSymLink()) return {{}, "本机凭据目录异常，已停止访问。"};
    if (!info.exists()) {
        if (operation == 0) return {{}, "此设备尚未保存 iCloud 专用密码，请重新连接账户。"};
        if (operation == 2) return {};
        if (!QDir().mkdir(directory) || !QFile::setPermissions(directory, directoryPermissions))
            return {{}, "无法创建本机凭据目录，请检查文件权限。"};
        info.refresh();
    }
    if (!privatePath(info, true)) return {{}, "本机凭据目录权限异常，已停止访问。"};
    QLockFile lock(QDir(directory).filePath("credentials.lock"));
    if (!lock.tryLock(0)) return {{}, "本机凭据存储正被另一个日笺窗口使用，请稍后重试。"};
    const QByteArray identity = QCryptographicHash::hash(account.trimmed().toCaseFolded().toUtf8(), QCryptographicHash::Sha256);
    const QString path = QDir(directory).filePath(QString::fromLatin1(identity.toHex()) + ".secret");
    const QFileInfo credential(path);
    if (credential.isSymLink() || (credential.exists() && !privatePath(credential, false)))
        return {{}, "本机凭据文件权限异常，已停止访问。"};
    if (operation == 2) {
        if (!credential.exists() || QFile::remove(path)) return {};
        return {{}, "无法删除本机加密凭据，请检查文件权限。"};
    }
    if (operation == 0 && !credential.exists())
        return {{}, "此设备尚未保存 iCloud 专用密码，请重新连接账户。"};
    const Result encryptionKey = key(directory, operation == 1);
    if (!encryptionKey.error.isEmpty()) return encryptionKey;
    if (operation == 1) {
        const Result encrypted = encrypt(encryptionKey.value, identity, password);
        if (!encrypted.error.isEmpty()) return encrypted;
        if (!save(path, encrypted.value)) return {{}, "无法保存本机加密凭据，请检查文件权限。"};
        return {};
    }
    if (credential.size() > maximumSecretSize + magic.size() + nonceSize + tagSize)
        return {{}, "本机凭据文件大小异常，请重新连接 iCloud。"};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {{}, "无法读取本机加密凭据，请检查文件权限。"};
    const QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) return {{}, "无法完整读取本机加密凭据。"};
    return decrypt(encryptionKey.value, identity, data);
}
}

void SecretStore::setDirectory(const QString &directory) { m_directory = QDir::cleanPath(directory); }
void SecretStore::read(const QString &account, Completion done) { access(account, {}, 0, std::move(done)); }
void SecretStore::write(const QString &account, const QByteArray &password, Completion done) { access(account, password, 1, std::move(done)); }
void SecretStore::remove(const QString &account, Completion done) { access(account, {}, 2, std::move(done)); }

void SecretStore::access(const QString &account, const QByteArray &password, int operation, Completion done)
{
    QTimer::singleShot(0, this, [this, account, password, operation, done = std::move(done)] {
        const Result result = accessStorage(m_directory, account, password, operation);
        done(result.value, result.error);
    });
}
