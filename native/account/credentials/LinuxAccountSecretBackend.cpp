#include "AccountSecretBackend.h"

// Qt's 'signals' macro conflicts with GLib structure members.
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {
const SecretSchema kSchema = {
    "org.brotherhood.Colosseum.AccountSecret.v1", SECRET_SCHEMA_NONE,
    {{"target", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
    0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

// The existing account contract is synchronous. Bound prompts/service failures
// instead of allowing a locked or absent desktop keyring to hang indefinitely.
class Operation {
public:
    Operation() : cancel(g_cancellable_new()), timer([this] {
        std::unique_lock<std::mutex> lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(5), [this] { return done; }))
            g_cancellable_cancel(cancel);
    }) {}
    ~Operation() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            done = true;
        }
        changed.notify_one();
        timer.join();
        if (error)
            g_error_free(error);
        g_object_unref(cancel);
    }
    GCancellable *cancel;
    GError *error = nullptr;
private:
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    std::thread timer;
};

class LinuxAccountSecretBackend final : public AccountSecretBackend {
public:
    bool isAvailable() const override {
        Operation op;
        SecretService *service = secret_service_get_sync(SECRET_SERVICE_OPEN_SESSION, op.cancel, &op.error);
        if (!service)
            return false;
        g_object_unref(service);
        return !op.error;
    }

    bool write(const QString &target, const QByteArray &blob) override {
        if (target.isEmpty() || blob.isEmpty())
            return false;
        const QByteArray name = target.toUtf8();
        // libsecret's password API is NUL-terminated; encode opaque binary blobs.
        const QByteArray encoded = blob.toBase64();
        Operation op;
        return secret_password_store_sync(&kSchema, SECRET_COLLECTION_DEFAULT,
            "Colosseum account", encoded.constData(), op.cancel, &op.error,
            "target", name.constData(), nullptr) && !op.error;
    }

    std::optional<QByteArray> read(const QString &target) const override {
        const QByteArray name = target.toUtf8();
        Operation op;
        gchar *password = secret_password_lookup_sync(&kSchema, op.cancel, &op.error,
            "target", name.constData(), nullptr);
        if (!password)
            return std::nullopt;
        const auto decoded = QByteArray::fromBase64Encoding(QByteArray(password),
            QByteArray::AbortOnBase64DecodingErrors);
        secret_password_free(password);
        if (op.error || !decoded)
            return std::nullopt;
        return decoded.decoded;
    }

    bool remove(const QString &target) override {
        const QByteArray name = target.toUtf8();
        Operation op;
        secret_password_clear_sync(&kSchema, op.cancel, &op.error,
            "target", name.constData(), nullptr);
        if (op.error)
            return false;
        // clear_sync only deletes unlocked matches. No error does not prove
        // that a locked credential was removed. Keep the controller's durable
        // sign-out tombstone until a search including locked items is empty.
        GList *remaining = secret_password_search_sync(&kSchema, SECRET_SEARCH_ALL,
            op.cancel, &op.error, "target", name.constData(), nullptr);
        const bool removed = !op.error && !remaining;
        g_list_free_full(remaining, g_object_unref);
        return removed;
    }

    QStringList targets(const QString &prefix) const override {
        Operation op;
        GList *items = secret_password_search_sync(&kSchema, SECRET_SEARCH_ALL,
            op.cancel, &op.error, nullptr);
        QStringList result;
        if (!op.error) {
            for (GList *node = items; node; node = node->next) {
                GHashTable *attributes = secret_retrievable_get_attributes(SECRET_RETRIEVABLE(node->data));
                const auto *name = static_cast<const gchar *>(g_hash_table_lookup(attributes, "target"));
                if (name) {
                    const QString target = QString::fromUtf8(name);
                    if (target.startsWith(prefix))
                        result.append(target);
                }
                g_hash_table_unref(attributes);
            }
        }
        g_list_free_full(items, g_object_unref);
        return result;
    }
};
}

std::shared_ptr<AccountSecretBackend> createAccountSecretBackend() {
    return std::make_shared<LinuxAccountSecretBackend>();
}
