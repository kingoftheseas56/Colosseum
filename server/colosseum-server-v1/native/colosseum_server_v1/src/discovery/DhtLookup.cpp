#include "server1/discovery/DhtLookup.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkDatagram>
#include <QNetworkProxy>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace server1::discovery {
namespace {

constexpr std::size_t concurrency = 10;
constexpr std::size_t closestCount = 20;
constexpr int requestTickMs = 500;
constexpr qsizetype maximumPacketBytes = 65507;

// Small, bounded bencode reader. Network input must not allocate based on an
// untrusted length, recurse without bounds, or accept a partial dictionary.
struct Value {
    enum class Type { Bytes, Integer, List, Dictionary } type = Type::Bytes;
    QByteArray bytes;
    qint64 integer = 0;
    std::vector<Value> list;
    std::map<QByteArray, Value> dictionary;

    const Value *get(const QByteArray &key) const
    {
        if (type != Type::Dictionary) return nullptr;
        const auto it = dictionary.find(key);
        return it == dictionary.end() ? nullptr : &it->second;
    }
};

class BencodeReader {
public:
    explicit BencodeReader(const QByteArray &bytes) : bytes_(bytes) {}
    bool read(Value &result)
    {
        return bytes_.size() <= maximumPacketBytes && value(result, 0)
            && position_ == bytes_.size();
    }

private:
    bool bytes(QByteArray &out)
    {
        if (position_ >= bytes_.size() || bytes_[position_] < '0' || bytes_[position_] > '9') return false;
        qsizetype length = 0;
        while (position_ < bytes_.size() && bytes_[position_] >= '0' && bytes_[position_] <= '9') {
            const auto digit = bytes_[position_++] - '0';
            if (length > (maximumPacketBytes - digit) / 10) return false;
            length = length * 10 + digit;
        }
        if (position_ >= bytes_.size() || bytes_[position_++] != ':'
            || length > bytes_.size() - position_) return false;
        out = bytes_.mid(position_, length);
        position_ += length;
        return true;
    }

    bool value(Value &out, unsigned depth)
    {
        if (depth > 16 || ++values_ > 4096 || position_ >= bytes_.size()) return false;
        const char type = bytes_[position_];
        if (type >= '0' && type <= '9') return bytes(out.bytes);
        ++position_;
        if (type == 'i') {
            out.type = Value::Type::Integer;
            if (position_ < bytes_.size() && bytes_[position_] == '-') ++position_;
            const auto begin = position_;
            const bool negative = begin > 0 && bytes_[begin - 1] == '-';
            while (position_ < bytes_.size() && bytes_[position_] >= '0' && bytes_[position_] <= '9') ++position_;
            bool valid = false;
            out.integer = bytes_.mid(begin - (negative ? 1 : 0), position_ - begin + (negative ? 1 : 0)).toLongLong(&valid);
            return valid && position_ != begin && position_ - begin <= 20
                && position_ < bytes_.size() && bytes_[position_++] == 'e';
        }
        if (type != 'd' && type != 'l') return false;
        out.type = type == 'd' ? Value::Type::Dictionary : Value::Type::List;
        while (position_ < bytes_.size() && bytes_[position_] != 'e') {
            QByteArray key;
            if (type == 'd' && !bytes(key)) return false;
            Value child;
            if (!value(child, depth + 1)) return false;
            if (type == 'd') {
                if (!out.dictionary.emplace(std::move(key), std::move(child)).second) return false;
            } else out.list.push_back(std::move(child));
        }
        return position_ < bytes_.size() && bytes_[position_++] == 'e';
    }

    const QByteArray &bytes_;
    qsizetype position_ = 0;
    unsigned values_ = 0;
};

QByteArray encodedBytes(const QByteArray &value)
{
    return QByteArray::number(value.size()) + ':' + value;
}

bool isBytes(const Value *value)
{
    return value && value->type == Value::Type::Bytes;
}

QString endpointKey(const QHostAddress &host, quint16 port)
{
    return host.toString() + ':' + QString::number(port);
}

QHostAddress ipv4(const QByteArray &bytes, qsizetype offset)
{
    return QHostAddress((quint32(quint8(bytes[offset])) << 24)
        | (quint32(quint8(bytes[offset + 1])) << 16)
        | (quint32(quint8(bytes[offset + 2])) << 8) | quint8(bytes[offset + 3]));
}

quint16 portAt(const QByteArray &bytes, qsizetype offset)
{
    return quint16((quint16(quint8(bytes[offset])) << 8) | quint8(bytes[offset + 1]));
}

QByteArray encode(const Value &value)
{
    if (value.type == Value::Type::Bytes) return encodedBytes(value.bytes);
    if (value.type == Value::Type::Integer) return "i" + QByteArray::number(value.integer) + "e";
    QByteArray result = value.type == Value::Type::List ? "l" : "d";
    if (value.type == Value::Type::List) for (const auto &child : value.list) result += encode(child);
    else for (const auto &[key, child] : value.dictionary) result += encodedBytes(key) + encode(child);
    return result + 'e';
}

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, '\0');
    for (int i = 0; i < count; ++i) bytes[i] = char(QRandomGenerator::system()->generate() & 0xff);
    return bytes;
}

struct Node {
    QHostAddress address;
    quint16 port = 0;
    QByteArray id;
    QByteArray token;
};

class KBucket {
    struct Bucket {
        bool dontSplit = false;
        std::vector<Node> contacts;
        std::unique_ptr<Bucket> left, right;
    };
public:
    struct Eviction { std::vector<Node> oldest; Node candidate; };
    explicit KBucket(QByteArray local) : local_(std::move(local)) {}

    bool contains(const QByteArray &id) const
    {
        const auto *bucket = leaf(id);
        return std::any_of(bucket->contacts.begin(), bucket->contacts.end(), [&](const Node &node) { return node.id == id; });
    }

    std::unique_ptr<Eviction> add(Node node)
    {
        if (node.id.size() != 20) return {};
        auto *bucket = &root_;
        unsigned bit = 0;
        while (bucket->left) bucket = branch(*bucket, node.id, bit++);
        const auto found = std::find_if(bucket->contacts.begin(), bucket->contacts.end(), [&](const Node &old) { return old.id == node.id; });
        if (found != bucket->contacts.end()) bucket->contacts.erase(found);
        else if (bucket->contacts.size() >= closestCount) {
            if (bucket->dontSplit || bit >= 160) {
                auto eviction = std::make_unique<Eviction>();
                eviction->oldest.assign(bucket->contacts.begin(), bucket->contacts.begin() + std::min(concurrency, bucket->contacts.size()));
                eviction->candidate = std::move(node);
                return eviction;
            }
            bucket->left = std::make_unique<Bucket>();
            bucket->right = std::make_unique<Bucket>();
            for (auto &contact : bucket->contacts) branch(*bucket, contact.id, bit)->contacts.push_back(std::move(contact));
            bucket->contacts.clear();
            auto *localBranch = branch(*bucket, local_, bit);
            (localBranch == bucket->left.get() ? bucket->right : bucket->left)->dontSplit = true;
            return add(std::move(node));
        }
        bucket->contacts.push_back(std::move(node));
        return {};
    }

    void remove(const QByteArray &id)
    {
        auto *bucket = const_cast<Bucket *>(leaf(id));
        bucket->contacts.erase(std::remove_if(bucket->contacts.begin(), bucket->contacts.end(), [&](const Node &node) { return node.id == id; }), bucket->contacts.end());
    }

    std::vector<Node> closest(const QByteArray &target, std::size_t count = static_cast<std::size_t>(-1)) const
    {
        std::vector<Node> result;
        std::vector<const Bucket *> stack{&root_};
        unsigned bit = 0;
        while (!stack.empty() && result.size() < count) {
            const auto *bucket = stack.back(); stack.pop_back();
            if (!bucket->left) result.insert(result.end(), bucket->contacts.begin(), bucket->contacts.end());
            else {
                const auto *selected = branch(*bucket, target, bit++);
                stack.push_back(selected == bucket->left.get() ? bucket->right.get() : bucket->left.get());
                stack.push_back(selected);
            }
        }
        // k-bucket in the pinned JS computes distance as a Number, including
        // its stable ordering when two 160-bit distances round to one double.
        const auto distance = [&](const Node &node) {
            double value = 0;
            for (int i = 0; i < 20; ++i) value = 256 * value + (quint8(node.id[i]) ^ quint8(target[i]));
            return value;
        };
        std::stable_sort(result.begin(), result.end(), [&](const Node &a, const Node &b) { return distance(a) < distance(b); });
        if (result.size() > count) result.resize(count);
        return result;
    }

private:
    static bool right(const QByteArray &id, unsigned bit)
    {
        return bit < unsigned(id.size() * 8) && (quint8(id[bit / 8]) & (1u << (7 - bit % 8)));
    }
    static Bucket *branch(Bucket &bucket, const QByteArray &id, unsigned bit)
    { return right(id, bit) ? bucket.right.get() : bucket.left.get(); }
    static const Bucket *branch(const Bucket &bucket, const QByteArray &id, unsigned bit)
    { return right(id, bit) ? bucket.right.get() : bucket.left.get(); }
    const Bucket *leaf(const QByteArray &id) const
    {
        const auto *bucket = &root_; unsigned bit = 0;
        while (bucket->left) bucket = branch(*bucket, id, bit++);
        return bucket;
    }
    QByteArray local_;
    Bucket root_;
};

QByteArray compactNode(const Node &node)
{
    const quint32 address = node.address.toIPv4Address();
    QByteArray result = node.id;
    result.append(char(address >> 24)); result.append(char(address >> 16));
    result.append(char(address >> 8)); result.append(char(address));
    result.append(char(node.port >> 8)); result.append(char(node.port));
    return result;
}

} // namespace

struct DhtLookup::Impl {
    struct Lookup {
        Lookup(LookupId handle, QByteArray target, bool background)
            : id(handle), target(std::move(target)), table(this->target), tokens(this->target), background(background) {}
        LookupId id;
        QByteArray target;
        KBucket table;
        KBucket tokens;
        std::set<QString> visited;
        std::deque<Endpoint> seeds;
        std::size_t pending = 0;
        bool once = true, aborted = false, stop = false, background = false;
    };
    struct Request {
        Node node;
        LookupId lookup = 0;
        int ttl = 4;
        std::function<void(bool)> ping;
    };
    struct Resolution { LookupId lookup; Endpoint endpoint; };
    struct PeerGroup { std::map<QByteArray, quint64> peers; quint64 touched = 0; };
    struct StoredValue { Value value; quint64 touched = 0; };
    struct TokenTable { std::vector<Node> nodes; qint64 modified = 0; quint64 touched = 0; };
    struct Probe { std::vector<Node> old; Node candidate; bool retry = false; };

    explicit Impl(DhtLookup *owner, const QString &hash, std::vector<Endpoint> seeds)
        : owner(owner), nodeId(randomBytes(20)), bootstrap(std::move(seeds)), socket(owner),
          routing(nodeId), timeoutTick(owner), secretTick(owner)
    {
        socket.setProxy(QNetworkProxy::NoProxy);
        bool valid = hash.size() == 40;
        for (const auto c : hash) {
            if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
                  || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
                  || (c >= QLatin1Char('A') && c <= QLatin1Char('F')))) valid = false;
        }
        if (valid) infoHash = QByteArray::fromHex(hash.toLatin1());
        clock.start();
        secret = randomBytes(20); previousSecret = randomBytes(20);
        QObject::connect(&socket, &QUdpSocket::readyRead, owner, [this] { receive(); });
        timeoutTick.setTimerType(Qt::PreciseTimer);
        timeoutTick.setInterval(requestTickMs);
        QObject::connect(&timeoutTick, &QTimer::timeout, owner, [this] { expire(); });
        timeoutTick.start();
        secretTick.setTimerType(Qt::PreciseTimer);
        secretTick.setInterval(300000);
        QObject::connect(&secretTick, &QTimer::timeout, owner, [this] { previousSecret = secret; secret = randomBytes(20); });
        secretTick.start();
        QTimer::singleShot(0, owner, [this] {
            if (!closed && infoHash.size() == 20 && !bootstrap.empty()) createLookup(nodeId, true);
        });
    }

    bool ensureSocket()
    {
        return socket.state() == QAbstractSocket::BoundState || socket.bind(QHostAddress::AnyIPv4, 0);
    }

    void close()
    {
        if (closed) return;
        closed = true; ++generation;
        timeoutTick.stop(); secretTick.stop();
        for (const auto &[id, resolution] : resolutions) QHostInfo::abortHostLookup(id);
        resolutions.clear(); requests.clear(); lookups.clear(); probeQueue.clear();
        socket.close();
    }

    void cancel()
    {
        ++generation;
        std::set<LookupId> removed;
        for (auto it = lookups.begin(); it != lookups.end();) {
            if (!it->second->background) { removed.insert(it->first); it = lookups.erase(it); }
            else ++it;
        }
        for (auto it = requests.begin(); it != requests.end();) {
            if (removed.count(it->second.lookup)) it = requests.erase(it); else ++it;
        }
        for (auto it = resolutions.begin(); it != resolutions.end();) {
            if (removed.count(it->second.lookup)) { QHostInfo::abortHostLookup(it->first); it = resolutions.erase(it); }
            else ++it;
        }
        pump();
    }

    bool active() const
    {
        return std::any_of(lookups.begin(), lookups.end(), [](const auto &entry) { return !entry.second->background; });
    }

    LookupId createLookup(const QByteArray &target, bool background)
    {
        if (closed || infoHash.size() != 20 || !ensureSocket()) return 0;
        const auto handle = ++lookupSequence;
        lookups.emplace(handle, std::make_shared<Lookup>(handle, target, background));
        if (!background) {
            const auto currentGeneration = generation;
            QTimer::singleShot(0, owner, [this, currentGeneration, target] {
                if (closed || currentGeneration != generation) return;
                emitPeers(samplePeers(target));
            });
        }
        pump();
        return handle;
    }

    void abort(LookupId id)
    {
        const auto found = lookups.find(id);
        if (found != lookups.end()) found->second->aborted = true;
    }

    QByteArray nextTransaction()
    {
        QByteArray tx(2, '\0');
        do {
            if (++sequence == 0) ++sequence;
            tx[0] = char(sequence >> 8); tx[1] = char(sequence);
        } while (requests.count(tx));
        return tx;
    }

    void query(Node node, LookupId lookup, std::function<void(bool)> ping = {})
    {
        if (closed || !node.port || node.address.protocol() != QAbstractSocket::IPv4Protocol) return;
        const auto found = lookups.find(lookup);
        if (lookup && found == lookups.end()) return;
        if (lookup && !found->second->visited.insert(endpointKey(node.address, node.port)).second) return;
        const auto tx = nextTransaction();
        QByteArray args = "d2:id20:" + nodeId;
        QByteArray method = "ping";
        if (lookup) {
            method = found->second->background ? "find_node" : "get_peers";
            args += (found->second->background ? "6:target20:" : "9:info_hash20:") + found->second->target;
            ++found->second->pending;
        }
        const auto body = "d1:a" + args + "e1:q" + encodedBytes(method) + "1:t" + encodedBytes(tx) + "1:y1:qe";
        requests.emplace(tx, Request{node, lookup, 4, std::move(ping)});
        if (socket.writeDatagram(body, node.address, node.port) != body.size()) complete(tx, nullptr, false);
    }

    void resolve(const Endpoint &endpoint, LookupId lookup)
    {
        if (!endpoint.port || endpoint.host.isEmpty()) return;
        QHostAddress address;
        if (address.setAddress(endpoint.host)) { query({address, endpoint.port, {}, {}}, lookup); return; }
        const auto found = lookups.find(lookup);
        if (found == lookups.end()) return;
        ++found->second->pending;
        const int id = QHostInfo::lookupHost(endpoint.host, owner, [this](const QHostInfo &info) {
            if (closed) return;
            const auto resolution = resolutions.find(info.lookupId());
            if (resolution == resolutions.end()) return;
            const auto work = resolution->second; resolutions.erase(resolution);
            const auto lookup = lookups.find(work.lookup);
            if (lookup != lookups.end()) {
                --lookup->second->pending;
                if (info.error() == QHostInfo::NoError) for (const auto &address : info.addresses()) {
                    if (address.protocol() != QAbstractSocket::IPv4Protocol) continue;
                    query({address, work.endpoint.port, {}, {}}, work.lookup); break;
                }
            }
            pump();
        });
        resolutions.emplace(id, Resolution{lookup, endpoint});
    }

    void remember(Node node)
    {
        if (node.id.size() != 20 || node.id == nodeId || !node.port || routing.contains(node.id)) return;
        if (auto eviction = routing.add(std::move(node))) {
            auto probe = std::make_shared<Probe>();
            probe->old = std::move(eviction->oldest); probe->candidate = std::move(eviction->candidate);
            probeQueue.push_back(probe);
        }
    }

    void continueProbe(const std::shared_ptr<Probe> &probe, bool okay)
    {
        if (closed || probe->old.empty()) return;
        if (probe->retry) {
            // Preserve the pinned callback's outer-error capture: once the
            // first ping fails, its retry completion evicts even if it replies.
            routing.remove(probe->old.back().id);
            remember(probe->candidate);
            return;
        }
        if (okay) probe->old.pop_back(); else probe->retry = true;
        if (!probe->old.empty()) probeQueue.push_front(probe);
    }

    void pump()
    {
        if (closed || pumping) return;
        pumping = true;
        while (requests.size() < concurrency && !probeQueue.empty()) {
            const auto probe = probeQueue.front(); probeQueue.pop_front();
            if (probe->old.empty()) continue;
            query(probe->old.back(), 0, [this, probe](bool okay) { continueProbe(probe, okay); });
        }
        std::vector<std::shared_ptr<Lookup>> contexts;
        for (const auto &[id, lookup] : lookups) if (!lookup->background) contexts.push_back(lookup);
        for (const auto &[id, lookup] : lookups) if (lookup->background) contexts.push_back(lookup);
        for (const auto &lookup : contexts) {
            if (requests.size() + resolutions.size() >= concurrency) break;
            const auto other = requests.size() + resolutions.size() - std::min(lookup->pending, requests.size() + resolutions.size());
            if (lookup->background && requests.size() >= concurrency / 4 && other) continue;
            auto closest = lookup->table.closest(lookup->target, closestCount);
            if (closest.empty() || closest.size() < bootstrap.size()) closest = routing.closest(lookup->target, closestCount);
            if ((closest.empty() || closest.size() < bootstrap.size()) && lookup->once) {
                lookup->once = false; lookup->seeds.assign(bootstrap.begin(), bootstrap.end());
            }
            while (!lookup->seeds.empty() && requests.size() + resolutions.size() < concurrency) {
                const auto seed = lookup->seeds.front(); lookup->seeds.pop_front(); resolve(seed, lookup->id);
            }
            if (!lookup->stop) for (const auto &node : closest) {
                if (requests.size() + resolutions.size() >= concurrency) break;
                query(node, lookup->id);
            }
            if (!lookup->pending && lookup->seeds.empty()) {
                if (!lookup->background) {
                    tables[lookup->target] = {lookup->tokens.closest(lookup->target), clock.elapsed(), ++accessSequence};
                    trimLru(tables, 1000);
                }
                lookups.erase(lookup->id);
            }
        }
        pumping = false;
    }

    template<class Cache> void trimLru(Cache &cache, std::size_t limit)
    {
        while (cache.size() > limit) {
            const auto oldest = std::min_element(cache.begin(), cache.end(), [](const auto &a, const auto &b) { return a.second.touched < b.second.touched; });
            cache.erase(oldest);
        }
    }

    void expire()
    {
        std::vector<QByteArray> expired;
        for (auto &[tx, request] : requests) { if (request.ttl) --request.ttl; else expired.push_back(tx); }
        for (const auto &tx : expired) complete(tx, nullptr, false, true);
        for (auto it = tables.begin(); it != tables.end();) {
            if (clock.elapsed() - it->second.modified > 300000) it = tables.erase(it); else ++it;
        }
        pump();
    }

    bool emitPeers(const std::vector<QByteArray> &values)
    {
        const auto currentGeneration = generation;
        const QPointer<DhtLookup> guard(owner);
        for (const auto &peer : values) {
            if (peer.size() < 6) break;
            const auto port = portAt(peer, 4);
            if (!port) continue;
            const auto callback = owner->onPeer;
            if (callback) callback(endpointKey(ipv4(peer, 0), port).toStdString());
            if (!guard || closed || generation != currentGeneration) return false;
        }
        return true;
    }

    void complete(const QByteArray &tx, const Value *reply, bool success, bool removeFailed = false)
    {
        const auto found = requests.find(tx);
        if (found == requests.end()) return;
        const auto request = found->second; requests.erase(found);
        if (!success && removeFailed && request.lookup && !request.node.id.isEmpty()) routing.remove(request.node.id);
        if (request.ping) { request.ping(success); return; }
        const auto current = lookups.find(request.lookup);
        if (current == lookups.end()) return;
        const auto lookup = current->second;
        if (lookup->pending) --lookup->pending;
        if (!reply) return;
        const auto *id = reply->get("id");
        if (success && isBytes(id) && id->bytes.size() == 20 && id->bytes != nodeId) {
            Node node{request.node.address, request.node.port, id->bytes, {}};
            lookup->table.add(node);
            const auto *token = reply->get("token");
            if (isBytes(token)) { node.token = token->bytes; lookup->tokens.add(node); }
        }
        const auto *contacts = reply->get("nodes");
        if (isBytes(contacts)) for (qsizetype at = 0; at + 26 <= contacts->bytes.size(); at += 26) {
            Node node{ipv4(contacts->bytes, at + 20), portAt(contacts->bytes, at + 24), contacts->bytes.mid(at, 20), {}};
            if (node.port && node.id != nodeId) lookup->table.add(std::move(node));
        }
        if (!lookup->background) {
            if (lookup->aborted) { lookup->stop = true; return; }
            const auto *values = reply->get("values");
            if (values && values->type == Value::Type::List) {
                std::vector<QByteArray> peers;
                for (const auto &value : values->list) { if (!isBytes(&value)) break; peers.push_back(value.bytes); }
                emitPeers(peers);
            }
        }
    }

    QByteArray tokenFor(const QHostAddress &address, const QByteArray &key) const
    {
        return QCryptographicHash::hash(address.toString().toUtf8() + key, QCryptographicHash::Sha1);
    }

    bool validToken(const QHostAddress &address, const Value *token) const
    {
        return isBytes(token) && (token->bytes == tokenFor(address, secret) || token->bytes == tokenFor(address, previousSecret));
    }

    std::vector<QByteArray> samplePeers(const QByteArray &hash)
    {
        std::vector<QByteArray> result;
        const auto found = peers.find(hash);
        if (found == peers.end()) return result;
        found->second.touched = ++accessSequence;
        for (const auto &[peer, used] : found->second.peers) result.push_back(peer);
        const auto count = std::min<std::size_t>(result.size(), 100);
        for (std::size_t i = 0; i < count; ++i) {
            const auto next = i + QRandomGenerator::global()->bounded(quint32(result.size() - i));
            std::swap(result[i], result[next]);
        }
        result.resize(count);
        return result;
    }

    void addPeer(const QByteArray &hash, const QHostAddress &address, quint16 port)
    {
        auto &group = peers[hash];
        group.touched = ++accessSequence;
        const auto compact = compactNode({address, port, {}, {}});
        if (!group.peers.count(compact)) ++peerCount;
        group.peers[compact] = ++accessSequence;
        if (peerCount > 10000) {
            const auto oldest = std::min_element(peers.begin(), peers.end(), [](const auto &a, const auto &b) { return a.second.touched < b.second.touched; });
            auto &entries = oldest->second.peers;
            const auto peer = std::min_element(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.second < b.second; });
            entries.erase(peer); --peerCount;
            if (entries.empty()) peers.erase(oldest);
        }
    }

    QByteArray encodedNodes(const QByteArray &target) const
    {
        QByteArray nodes;
        for (const auto &node : routing.closest(target)) nodes += compactNode(node);
        return "5:nodes" + encodedBytes(nodes);
    }

    void sendResponse(const QNetworkDatagram &datagram, const QByteArray &tx, const QByteArray &fields)
    {
        const auto body = "d1:rd2:id20:" + nodeId + fields + "e1:t" + encodedBytes(tx) + "1:y1:re";
        socket.writeDatagram(body, datagram.senderAddress(), datagram.senderPort());
    }

    void sendError(const QNetworkDatagram &datagram, const QByteArray &tx, int code, const QByteArray &message)
    {
        const auto body = "d1:eli" + QByteArray::number(code) + "e" + encodedBytes(message)
            + "e1:t" + encodedBytes(tx) + "1:y1:ee";
        socket.writeDatagram(body, datagram.senderAddress(), datagram.senderPort());
    }

    void incoming(const QNetworkDatagram &datagram, const Value &message, const QByteArray &tx)
    {
        const auto *method = message.get("q"), *args = message.get("a");
        if (!isBytes(method) || !args || args->type != Value::Type::Dictionary) return;
        const auto *id = args->get("id");
        if (isBytes(id)) remember({datagram.senderAddress(), quint16(datagram.senderPort()), id->bytes, {}});
        if (method->bytes == "ping") { sendResponse(datagram, tx, {}); return; }
        const auto *target = args->get(method->bytes == "get_peers" || method->bytes == "announce_peer" ? "info_hash" : "target");
        if (method->bytes == "find_node") {
            if (!isBytes(target)) sendError(datagram, tx, 203, "`find_node` missing required `a.target` field");
            else if (target->bytes.size() == 20) sendResponse(datagram, tx, encodedNodes(target->bytes));
        } else if (method->bytes == "get_peers") {
            if (!isBytes(target)) { sendError(datagram, tx, 203, "`get_peers` missing required `a.info_hash` field"); return; }
            QByteArray fields;
            const auto values = samplePeers(target->bytes);
            if (values.empty()) { if (target->bytes.size() != 20) return; fields = encodedNodes(target->bytes); }
            fields += "5:token" + encodedBytes(tokenFor(datagram.senderAddress(), secret));
            if (!values.empty()) { fields += "6:valuesl"; for (const auto &peer : values) fields += encodedBytes(peer); fields += 'e'; }
            sendResponse(datagram, tx, fields);
        } else if (method->bytes == "announce_peer") {
            const auto *implied = args->get("implied_port"), *port = args->get("port"), *token = args->get("token");
            const bool useSender = implied && (implied->type != Value::Type::Integer || implied->integer != 0);
            const qint64 number = useSender ? datagram.senderPort() : port && port->type == Value::Type::Integer ? port->integer : 0;
            if (number <= 0 || number > 65535 || !isBytes(target) || !token) return;
            if (!validToken(datagram.senderAddress(), token)) { sendError(datagram, tx, 203, "cannot `announce_peer` with bad token"); return; }
            addPeer(target->bytes, datagram.senderAddress(), quint16(number)); sendResponse(datagram, tx, {});
        } else if (method->bytes == "get") {
            if (!isBytes(target)) return;
            QByteArray fields;
            const auto found = values.find(target->bytes);
            if (found == values.end()) { if (target->bytes.size() != 20) return; fields = encodedNodes(target->bytes); }
            fields += "5:token" + encodedBytes(tokenFor(datagram.senderAddress(), secret));
            if (found != values.end()) { found->second.touched = ++accessSequence; fields += "1:v" + encode(found->second.value); }
            sendResponse(datagram, tx, fields);
        } else if (method->bytes == "put") {
            const auto *value = args->get("v"), *token = args->get("token");
            if (!value || (value->type == Value::Type::Integer && value->integer == 0) || !id || !token) return;
            if (!validToken(datagram.senderAddress(), token)) { sendError(datagram, tx, 203, "cannot `put` with bad token"); return; }
            if ((isBytes(value) && value->bytes.size() > 1000) || (value->type == Value::Type::List && value->list.size() > 1000)) {
                sendError(datagram, tx, 205, "data payload too large"); return;
            }
            if (args->get("k") || args->get("sig")) { sendError(datagram, tx, 400, "verification not supported"); return; }
            const auto key = QCryptographicHash::hash(encode(*value), QCryptographicHash::Sha1);
            values[key] = {*value, ++accessSequence}; trimLru(values, 1000);
            sendResponse(datagram, tx, {});
        }
    }

    void receive()
    {
        while (!closed && socket.hasPendingDatagrams()) {
            const auto datagram = socket.receiveDatagram(maximumPacketBytes);
            const auto packet = datagram.data(); Value message;
            if (!BencodeReader(packet).read(message)) continue;
            const auto *tx = message.get("t"), *type = message.get("y");
            if (!isBytes(tx) || !isBytes(type)) continue;
            if (type->bytes == "q") { incoming(datagram, message, tx->bytes); pump(); continue; }
            if (tx->bytes.size() < 2 || (type->bytes != "r" && type->bytes != "e")) continue;
            const auto transaction = tx->bytes.left(2);
            const auto *reply = message.get("r"), *id = reply ? reply->get("id") : nullptr;
            const auto observe = [&] {
                if (isBytes(id)) remember({datagram.senderAddress(), quint16(datagram.senderPort()), id->bytes, {}});
            };
            const auto found = requests.find(transaction);
            if (found == requests.end()) { observe(); pump(); continue; }
            const auto from = found->second.node;
            // The pinned socket correlates by transaction and IP address only.
            // Its response observer still learns valid IDs from unmatched and
            // out-of-order replies without delivering their peer values.
            if (datagram.senderAddress() != from.address) { observe(); pump(); continue; }
            const bool idPresent = id && (id->type != Value::Type::Integer || id->integer != 0);
            const bool mismatch = type->bytes == "r" && !from.id.isEmpty() && idPresent
                && (!isBytes(id) || from.id != id->bytes);
            const bool success = type->bytes == "r" && !mismatch;
            found->second.node.port = quint16(datagram.senderPort());
            const auto context = lookups.find(found->second.lookup);
            if (context != lookups.end()) context->second->visited.insert(endpointKey(datagram.senderAddress(), quint16(datagram.senderPort())));
            const QPointer<DhtLookup> guard(owner);
            const auto currentGeneration = generation;
            complete(transaction, mismatch ? nullptr : reply, success, mismatch);
            if (!guard || closed || generation != currentGeneration) return;
            if (success) observe();
            pump();
        }
    }

    DhtLookup *owner;
    QByteArray infoHash, nodeId, secret, previousSecret;
    std::vector<Endpoint> bootstrap;
    QUdpSocket socket;
    KBucket routing;
    QTimer timeoutTick, secretTick;
    QElapsedTimer clock;
    std::map<LookupId, std::shared_ptr<Lookup>> lookups;
    std::map<QByteArray, Request> requests;
    std::map<int, Resolution> resolutions;
    std::deque<std::shared_ptr<Probe>> probeQueue;
    std::map<QByteArray, PeerGroup> peers;
    std::map<QByteArray, StoredValue> values;
    std::map<QByteArray, TokenTable> tables;
    std::size_t peerCount = 0;
    quint16 sequence = 0;
    LookupId lookupSequence = 0;
    quint64 generation = 0, accessSequence = 0;
    bool pumping = false, closed = false;
};

DhtLookup::DhtLookup(QString infoHash, QObject *parent)
    : DhtLookup(std::move(infoHash), defaultBootstrapNodes(), parent) {}
DhtLookup::DhtLookup(QString infoHash, std::vector<Endpoint> bootstrapNodes, QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this, infoHash, std::move(bootstrapNodes))) {}
DhtLookup::~DhtLookup() { impl_->close(); }
DhtLookup::LookupId DhtLookup::startLookup() { return impl_->createLookup(impl_->infoHash, false); }
void DhtLookup::abortLookup(LookupId id) { impl_->abort(id); }
void DhtLookup::close() { impl_->close(); }
void DhtLookup::start() { if (!active()) (void) startLookup(); }
void DhtLookup::cancel() { impl_->cancel(); }
bool DhtLookup::active() const noexcept { return impl_->active(); }
std::vector<DhtLookup::Endpoint> DhtLookup::defaultBootstrapNodes()
{
    return {{QStringLiteral("router.bittorrent.com"), 6881},
            {QStringLiteral("router.utorrent.com"), 6881},
            {QStringLiteral("dht.transmissionbt.com"), 6881}};
}
} // namespace server1::discovery
