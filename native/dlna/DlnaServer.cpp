#include "DlnaServer.h"

#include "DlnaContentDirectory.h"
#include "DlnaMediaFiles.h"

#include <QDateTime>
#include <QFile>
#include <QSettings>
#include <QSysInfo>
#include <QUrl>
#include <QUuid>

#include "upnp.h"
#include "upnptools.h"
#include "UpnpActionRequest.h"
#include "UpnpExtraHeaders.h"
#include "UpnpFileInfo.h"
#include "UpnpStateVarRequest.h"
#include "UpnpSubscriptionRequest.h"
#include "ixml.h"

#include <chrono>

#ifndef COLOSSEUM_VERSION
#define COLOSSEUM_VERSION "0"
#endif

namespace {

const char* const kCdType = "urn:schemas-upnp-org:service:ContentDirectory:1";
const char* const kCdId = "urn:upnp-org:serviceId:ContentDirectory";
const char* const kCmType = "urn:schemas-upnp-org:service:ConnectionManager:1";
const char* const kCmId = "urn:upnp-org:serviceId:ConnectionManager";

// ContentDirectory:1 SCPD — exactly the actions implemented, with the standard evented
// state variables (ContainerUpdateIDs/TransferIDs included so the GENA contract is honest).
const char* const kCdScpd =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action><name>Browse</name>\n"
    "      <argumentList>\n"
    "        <argument><name>ObjectID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument>\n"
    "        <argument><name>BrowseFlag</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable></argument>\n"
    "        <argument><name>Filter</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument>\n"
    "        <argument><name>StartingIndex</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument>\n"
    "        <argument><name>RequestedCount</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
    "        <argument><name>SortCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument>\n"
    "        <argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument>\n"
    "        <argument><name>NumberReturned</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
    "        <argument><name>TotalMatches</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument>\n"
    "        <argument><name>UpdateID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action><name>GetSystemUpdateID</name>\n"
    "      <argumentList>\n"
    "        <argument><name>Id</name><direction>out</direction><relatedStateVariable>SystemUpdateID</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action><name>GetSearchCapabilities</name>\n"
    "      <argumentList>\n"
    "        <argument><name>SearchCaps</name><direction>out</direction><relatedStateVariable>SearchCapabilities</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action><name>GetSortCapabilities</name>\n"
    "      <argumentList>\n"
    "        <argument><name>SortCaps</name><direction>out</direction><relatedStateVariable>SortCapabilities</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"yes\">\n"
    "      <name>SystemUpdateID</name><dataType>ui4</dataType>\n"
    "    </stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\">\n"
    "      <name>ContainerUpdateIDs</name><dataType>string</dataType>\n"
    "    </stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>TransferIDs</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>SearchCapabilities</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>SortCapabilities</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ObjectID</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_BrowseFlag</name><dataType>string</dataType>\n"
    "      <allowedValues><allowedValue>BrowseMetadata</allowedValue><allowedValue>BrowseDirectChildren</allowedValue></allowedValues>\n"
    "    </stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Filter</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SortCriteria</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_UpdateID</name><dataType>ui4</dataType></stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

// ConnectionManager:1 SCPD.
const char* const kCmScpd =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\n"
    "  <actionList>\n"
    "    <action><name>GetProtocolInfo</name>\n"
    "      <argumentList>\n"
    "        <argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>\n"
    "        <argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action><name>GetCurrentConnectionIDs</name>\n"
    "      <argumentList>\n"
    "        <argument><name>ConnectionIDs</name><direction>out</direction><relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "    <action><name>GetCurrentConnectionInfo</name>\n"
    "      <argumentList>\n"
    "        <argument><name>ConnectionID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\n"
    "        <argument><name>RCSID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument>\n"
    "        <argument><name>AVTransportID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument>\n"
    "        <argument><name>ProtocolInfo</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument>\n"
    "        <argument><name>PeerConnectionManager</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument>\n"
    "        <argument><name>PeerConnectionID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\n"
    "        <argument><name>Direction</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument>\n"
    "        <argument><name>Status</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument>\n"
    "      </argumentList>\n"
    "    </action>\n"
    "  </actionList>\n"
    "  <serviceStateTable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"yes\"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType>\n"
    "      <allowedValues><allowedValue>OK</allowedValue><allowedValue>ContentFormatMismatch</allowedValue><allowedValue>InsufficientBandwidth</allowedValue><allowedValue>UnreliableChannel</allowedValue><allowedValue>Unknown</allowedValue></allowedValues>\n"
    "    </stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_AVTransportID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RcsID</name><dataType>ui4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>\n"
    "    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType>\n"
    "      <allowedValues><allowedValue>Input</allowedValue><allowedValue>Output</allowedValue></allowedValues>\n"
    "    </stateVariable>\n"
    "  </serviceStateTable>\n"
    "</scpd>\n";

// Pull <name>value</name> out of an action request document (the spike's proven walker).
std::string actionArg(IXML_Document* doc, const char* name)
{
    IXML_NodeList* list = ixmlDocument_getElementsByTagName(doc, name);
    if (!list)
        return {};
    IXML_Node* node = ixmlNodeList_item(list, 0);
    ixmlNodeList_free(list);
    if (!node)
        return {};
    IXML_Node* text = ixmlNode_getFirstChild(node);
    if (!text)
        return {};
    const char* v = ixmlNode_getNodeValue(text);
    return v ? std::string(v) : std::string();
}

void actionError(UpnpActionRequest* ar, int code, const char* text)
{
    UpnpActionRequest_set_ErrCode(ar, code);
    UpnpActionRequest_set_ActionResult(ar, nullptr);
    UpnpActionRequest_strcpy_ErrStr(ar, text);
}

QString xmlEscape(const QString& s)
{
    return DlnaContentDirectory::escape(s);
}

} // namespace

// ── pupnp callbacks (worker threads; Core only) ─────────────────────────────────────────

void handleAction(DlnaServerCore* core, UpnpActionRequest* request);
void handleGetVar(DlnaServerCore* core, UpnpStateVarRequest* request);
void handleSubscribe(DlnaServerCore* core, UpnpSubscriptionRequest* request);

namespace {

int deviceCallback(Upnp_EventType type, void* event, void* cookie)
{
    auto* core = static_cast<DlnaServerCore*>(cookie);
    switch (type) {
    case UPNP_CONTROL_ACTION_REQUEST:
        handleAction(core, static_cast<UpnpActionRequest*>(event));
        break;
    case UPNP_CONTROL_GET_VAR_REQUEST:
        handleGetVar(core, static_cast<UpnpStateVarRequest*>(event));
        break;
    case UPNP_EVENT_SUBSCRIPTION_REQUEST:
        handleSubscribe(core, static_cast<UpnpSubscriptionRequest*>(event));
        break;
    default:
        break;
    }
    return UPNP_E_SUCCESS;
}

} // namespace

// ── construction / wiring ─────────────────────────────────────────────────────────────────

DlnaServer::DlnaServer(QObject* parent) : QObject(parent)
{
    m_core = std::make_shared<DlnaServerCore>();
    m_core->friendlyName = QStringLiteral("Colosseum on %1").arg([]() {
        QString host = QSysInfo::machineHostName();
        if (host.isEmpty())
            host = qEnvironmentVariable("COMPUTERNAME");
        return host.isEmpty() ? QStringLiteral("PC") : host;
    }());
    m_core->modelNumber = QString::fromLatin1(COLOSSEUM_VERSION);
    m_core->cdScpd = QByteArray(kCdScpd);
    m_core->cmScpd = QByteArray(kCmScpd);
    loadIconAndScpdBytes();
    m_ownSettings = std::make_unique<QSettings>();
    m_settings = m_ownSettings.get();

    m_pollTimer.setInterval(10000);
    connect(&m_pollTimer, &QTimer::timeout, this, &DlnaServer::pollInterface);
    m_notifyTimer.setSingleShot(true);
    connect(&m_notifyTimer, &QTimer::timeout, this, &DlnaServer::notifySystemUpdateId);
}

// Stable device identity, created once in the ACTIVE settings store (tests inject their own;
// Kodi persists the same way).
void DlnaServer::ensureUdn()
{
    QString udn = m_settings->value(QStringLiteral("dlnaSharing/uuid")).toString();
    if (!udn.startsWith(QLatin1String("uuid:"))) {
        udn = QStringLiteral("uuid:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_settings->setValue(QStringLiteral("dlnaSharing/uuid"), udn);
        m_settings->sync();
    }
    m_core->udn = udn.toLatin1();
}

DlnaServer::~DlnaServer()
{
    if (m_running || m_busy)
        stopForQuit();
    if (m_worker.joinable())
        m_worker.join();
}

void DlnaServer::loadIconAndScpdBytes()
{
    QFile icon48(QStringLiteral(":/dlna/icons/colosseum-48.png"));
    if (icon48.open(QIODevice::ReadOnly))
        m_core->icon48 = icon48.readAll();
    QFile icon120(QStringLiteral(":/dlna/icons/colosseum-120.png"));
    if (icon120.open(QIODevice::ReadOnly))
        m_core->icon120 = icon120.readAll();
}

void DlnaServer::setSnapshotProvider(DlnaServerCore::SnapshotProvider provider)
{
    m_core->provider = std::move(provider);
}

void DlnaServer::setSettings(QSettings* settings)
{
    m_settings = settings;
    m_ownSettings.reset();
}

void DlnaServer::setForcedInterface(const DlnaNetAdapter& adapter)
{
    m_forcedInterface = true;
    m_boundIfName = adapter.ifName;
}

QString DlnaServer::friendlyName() const
{
    return m_core->friendlyName;
}

// ── start / stop state machine ───────────────────────────────────────────────────────────

void DlnaServer::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    m_settings->setValue(QStringLiteral("dlnaSharing/enabled"), on);
    m_settings->sync();
    emit enabledChanged();
    if (on) {
        if (m_busy)
            m_restartAfterStop = true; // a stop is winding down; come back after it
        else
            beginStart();
    } else if (m_running || m_busy) {
        beginStop();
    }
}

void DlnaServer::maybeStartFromSettings()
{
    if (m_settings->value(QStringLiteral("dlnaSharing/enabled"), false).toBool()) {
        m_enabled = true;
        emit enabledChanged();
        beginStart();
    }
}

void DlnaServer::beginStart()
{
    if (m_busy)
        return; // a stop is winding down; onStopped() restarts when enabled
    if (!m_core->provider)
        return;
    m_restartAfterStop = false;
    m_busy = true;
    m_error.clear();
    emit busyChanged();
    emit errorChanged();
    ensureUdn();

    DlnaNetAdapter adapter;
    if (m_forcedInterface) {
        // The test lane binds the forced adapter by name; no home-network guesswork.
        const QList<DlnaNetAdapter> all = DlnaNetwork::enumerateAdapters();
        for (const DlnaNetAdapter& a : all) {
            if (a.ifName == m_boundIfName) {
                adapter = a;
                break;
            }
        }
        if (adapter.ifName.isEmpty()) {
            onStartFailed(QStringLiteral("Forced interface not present"));
            return;
        }
    } else {
        const auto home = DlnaNetwork::pickHomeAdapter();
        if (!home) {
            onStartFailed(QStringLiteral("No home network found"));
            return;
        }
        adapter = *home;
    }
    const quint16 savedPort =
        quint16(m_settings->value(QStringLiteral("dlnaSharing/port"), 0).toInt());
    if (m_worker.joinable())
        m_worker.join();
    m_worker = std::thread(&DlnaServer::startWorker, this, adapter, savedPort);
}

void DlnaServer::startWorker(const DlnaNetAdapter& adapter, quint16 tryPort)
{
    const std::string ifNameAnsi = adapter.ifName.toLocal8Bit().toStdString();
    int rc = UpnpInit2(ifNameAnsi.c_str(), tryPort);
    if (rc != UPNP_E_SUCCESS && tryPort != 0)
        rc = UpnpInit2(ifNameAnsi.c_str(), 0); // Kodi's fallback: saved port taken -> random
    if (rc != UPNP_E_SUCCESS) {
        const int code = rc;
        QMetaObject::invokeMethod(
            this, [this, code] { onStartFailed(QString::fromLatin1(UpnpGetErrorMessage(code))); },
            Qt::QueuedConnection);
        return;
    }

    m_core->baseUrl = QStringLiteral("http://%1:%2")
                          .arg(QString::fromLatin1(UpnpGetServerIpAddress()),
                               QString::number(UpnpGetServerPort()));
    DlnaMediaFiles::install(m_core.get());
    m_core->deviceHandle = -1;
    const QByteArray desc = buildDescription(*m_core);
    UpnpDevice_Handle handle = -1;
    rc = UpnpRegisterRootDevice2(UPNPREG_BUF_DESC, desc.constData(), size_t(desc.size()), 1,
                                 deviceCallback, m_core.get(), &handle);
    if (rc != UPNP_E_SUCCESS) {
        UpnpFinish();
        QMetaObject::invokeMethod(
            this, [this, rc] { onStartFailed(QString::fromLatin1(UpnpGetErrorMessage(rc))); },
            Qt::QueuedConnection);
        return;
    }
    m_core->deviceHandle.store(handle);
    rc = UpnpSendAdvertisement(handle, 1800);
    if (rc != UPNP_E_SUCCESS) {
        UpnpUnRegisterRootDevice(handle);
        m_core->deviceHandle = -1;
        UpnpFinish();
        QMetaObject::invokeMethod(
            this, [this, rc] { onStartFailed(QString::fromLatin1(UpnpGetErrorMessage(rc))); },
            Qt::QueuedConnection);
        return;
    }
    const quint16 port = UpnpGetServerPort();
    const QString ifName = adapter.ifName;
    const QString label = adapter.label;
    QMetaObject::invokeMethod(this, [this, port, ifName, label] { onStarted(port, ifName, label); },
                              Qt::QueuedConnection);
}

void DlnaServer::beginStop(const QString& errorAfterStop)
{
    if (m_busy)
        return;
    m_busy = true;
    emit busyChanged();
    if (m_worker.joinable())
        m_worker.join();
    m_worker = std::thread([this, errorAfterStop] {
        stopWorker();
        QMetaObject::invokeMethod(this, [this, errorAfterStop] { onStopped(errorAfterStop); },
                                  Qt::QueuedConnection);
    });
}

void DlnaServer::stopWorker()
{
    m_core->revokeEpoch.fetch_add(1); // in-flight media reads abort on the next call
    DlnaMediaFiles::uninstall();
    const int handle = m_core->deviceHandle.exchange(-1);
    if (handle >= 0)
        UpnpUnRegisterRootDevice(handle); // sends the SSDP byebye
    UpnpFinish();
}

void DlnaServer::onStarted(quint16 port, const QString& ifName, const QString& label)
{
    m_busy = false;
    emit busyChanged();
    m_running = true;
    emit runningChanged();
    m_boundIfName = ifName;
    m_networkLabel = label;
    emit networkLabelChanged();
    if (!m_forcedInterface)
        m_pollTimer.start();
    m_settings->setValue(QStringLiteral("dlnaSharing/port"), int(port));
    m_settings->sync();
    if (!m_enabled)
        beginStop(); // switched off while starting
}

void DlnaServer::onStartFailed(const QString& message)
{
    if (m_worker.joinable())
        m_worker.join();
    m_busy = false;
    emit busyChanged();
    m_error = message;
    emit errorChanged();
    if (!m_forcedInterface)
        m_pollTimer.stop();
}

void DlnaServer::onStopped(const QString& errorAfterStop)
{
    if (m_worker.joinable())
        m_worker.join();
    m_busy = false;
    m_running = false;
    emit busyChanged();
    emit runningChanged();
    m_pollTimer.stop();
    if (m_settings)
        m_settings->setValue(QStringLiteral("dlnaSharing/systemUpdateId"),
                             int(m_core->provider ? m_core->provider()->systemUpdateId : 0));
    if (!errorAfterStop.isEmpty()) {
        m_error = errorAfterStop;
        emit errorChanged();
    }
    if (m_restartAfterStop && m_enabled && !m_stopForQuit) {
        m_restartAfterStop = false;
        beginStart();
    }
}

void DlnaServer::pollInterface()
{
    if (!m_running || m_forcedInterface)
        return;
    const auto home = DlnaNetwork::pickHomeAdapter();
    if (!home) {
        m_restartAfterStop = false;
        beginStop(QStringLiteral("No home network found"));
        return;
    }
    if (home->ifName != m_boundIfName) {
        // Adapter changed (Wi-Fi dropped, moved to Ethernet): byebye, then re-advertise on
        // the new interface.
        m_restartAfterStop = true;
        beginStop();
    } else if (!m_networkLabel.endsWith(home->ip)) {
        m_networkLabel = home->label;
        emit networkLabelChanged();
    }
}

void DlnaServer::stopForQuit()
{
    m_stopForQuit = true;
    m_restartAfterStop = false;
    m_enabled = false;
    m_pollTimer.stop();
    m_notifyTimer.stop();
    if (m_worker.joinable())
        m_worker.join();
    if (m_running || m_core->deviceHandle.load() >= 0) {
        // Synchronous teardown: aboutToQuit must complete before owners die.
        std::thread([this] { stopWorker(); }).join();
        m_running = false;
        m_busy = false;
        emit runningChanged();
        emit busyChanged();
    }
    if (m_settings && m_core->provider)
        m_settings->setValue(QStringLiteral("dlnaSharing/systemUpdateId"),
                             int(m_core->provider()->systemUpdateId));
}

// ── snapshot changes + GENA ──────────────────────────────────────────────────────────────

void DlnaServer::handleSnapshotChanged()
{
    if (!m_core->provider)
        return;
    const auto snap = m_core->provider();
    if (snap->itemCount != m_itemCount) {
        m_itemCount = snap->itemCount;
        emit itemCountChanged();
    }
    if (snap->typeOrder != m_shareTypes) {
        m_shareTypes = snap->typeOrder;
        emit shareTypesChanged();
    }
    m_settings->setValue(QStringLiteral("dlnaSharing/systemUpdateId"), int(snap->systemUpdateId));
    if (m_running) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastNotifyMs >= 2000)
            notifySystemUpdateId();
        else if (!m_notifyTimer.isActive())
            m_notifyTimer.start(int(2000 - (now - m_lastNotifyMs) + 50));
    }
}

void DlnaServer::notifySystemUpdateId()
{
    if (!m_running || !m_core->provider)
        return;
    const auto snap = m_core->provider();
    m_containerUpdateCounter += 1;
    const QString containerIds =
        QStringLiteral("0,%1").arg(m_containerUpdateCounter);
    const char* names[2] = {"SystemUpdateID", "ContainerUpdateIDs"};
    const QByteArray id = QByteArray::number(snap->systemUpdateId);
    const QByteArray cids = containerIds.toLatin1();
    const char* values[2] = {id.constData(), cids.constData()};
    UpnpNotify(m_core->deviceHandle.load(), m_core->udn.constData(), kCdId, names, values, 2);
    m_lastNotifyMs = QDateTime::currentMSecsSinceEpoch();
}

// ── device description ───────────────────────────────────────────────────────────────────

QByteArray DlnaServer::buildDescription(const DlnaServerCore& core)
{
    // Kodi-like identity: MediaServer:1 with DMS-1.50 X_DLNADOC, dlna namespace on the root,
    // PNG icons from the vdir, no presentationURL. Relative service URLs — pupnp rewrites the
    // URLBase from the bound address (BUF_DESC + config_baseURL=1).
    static const char* tmpl =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<root xmlns=\"urn:schemas-upnp-org:device-1-0\" "
        "xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">\n"
        "  <specVersion><major>1</major><minor>1</minor></specVersion>\n"
        "  <device>\n"
        "    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>\n"
        "    <friendlyName>%1</friendlyName>\n"
        "    <manufacturer>Colosseum</manufacturer>\n"
        "    <manufacturerURL>https://getcolosseum.world</manufacturerURL>\n"
        "    <modelDescription>Colosseum - DLNA Media Server</modelDescription>\n"
        "    <modelName>Colosseum</modelName>\n"
        "    <modelNumber>%2</modelNumber>\n"
        "    <UDN>%3</UDN>\n"
        "    <dlna:X_DLNADOC>DMS-1.50</dlna:X_DLNADOC>\n"
        "    <iconList>\n"
        "      <icon><mimetype>image/png</mimetype><width>48</width><height>48</height>"
        "<depth>32</depth><url>/icons/colosseum-48.png</url></icon>\n"
        "      <icon><mimetype>image/png</mimetype><width>120</width><height>120</height>"
        "<depth>32</depth><url>/icons/colosseum-120.png</url></icon>\n"
        "    </iconList>\n"
        "    <serviceList>\n"
        "      <service>\n"
        "        <serviceType>%4</serviceType>\n"
        "        <serviceId>%5</serviceId>\n"
        "        <SCPDURL>/scpd/cd.xml</SCPDURL>\n"
        "        <controlURL>/control/cd</controlURL>\n"
        "        <eventSubURL>/event/cd</eventSubURL>\n"
        "      </service>\n"
        "      <service>\n"
        "        <serviceType>%6</serviceType>\n"
        "        <serviceId>%7</serviceId>\n"
        "        <SCPDURL>/scpd/cm.xml</SCPDURL>\n"
        "        <controlURL>/control/cm</controlURL>\n"
        "        <eventSubURL>/event/cm</eventSubURL>\n"
        "      </service>\n"
        "    </serviceList>\n"
        "  </device>\n"
        "</root>\n";
    return QString(tmpl)
        .arg(xmlEscape(core.friendlyName), xmlEscape(core.modelNumber),
             QString::fromLatin1(core.udn), QLatin1String(kCdType), QLatin1String(kCdId),
             QLatin1String(kCmType), QLatin1String(kCmId))
        .toUtf8();
}


void handleAction(DlnaServerCore* core, UpnpActionRequest* ar)
{
    const char* service = UpnpActionRequest_get_ServiceID_cstr(ar);
    const char* action = UpnpActionRequest_get_ActionName_cstr(ar);
    IXML_Document* req = UpnpActionRequest_get_ActionRequest(ar);
    if (!service || !action || !req) {
        actionError(ar, 402, "Invalid args");
        return;
    }
    const auto snap = core->provider();

    if (strcmp(service, kCdId) == 0) {
        if (strcmp(action, "Browse") == 0) {
            const std::string flag = actionArg(req, "BrowseFlag");
            const std::string sort = actionArg(req, "SortCriteria");
            DlnaContentDirectory::BrowseRequest br;
            br.objectId = QString::fromStdString(actionArg(req, "ObjectID"));
            br.metadata = flag == "BrowseMetadata";
            br.filter = QString::fromStdString(actionArg(req, "Filter"));
            const QString startS = QString::fromStdString(actionArg(req, "StartingIndex"));
            const QString countS = QString::fromStdString(actionArg(req, "RequestedCount"));
            bool okStart = false, okCount = false;
            const quint64 start = startS.toULongLong(&okStart);
            const quint64 count = countS.toULongLong(&okCount);
            if (flag != "BrowseMetadata" && flag != "BrowseDirectChildren") {
                actionError(ar, 402, "Invalid args");
                return;
            }
            if ((!okStart && !startS.isEmpty()) || (!okCount && !countS.isEmpty())
                || start > 0xFFFFFFFFull || count > 0xFFFFFFFFull) {
                actionError(ar, 402, "Invalid args");
                return;
            }
            br.startingIndex = quint32(start);
            br.requestedCount = quint32(count);
            // Kodi validates sort syntax; we only APPLY dc:title (the one capability we
            // advertise). Well-formed unknown criteria are accepted and ignored, exactly
            // Kodi's behaviour.
            const QString sortQ = QString::fromStdString(sort).trimmed();
            if (!sortQ.isEmpty()) {
                bool wellFormed = true;
                const QStringList terms =
                    sortQ.split(QLatin1Char(','), Qt::KeepEmptyParts);
                for (const QString& term : terms) {
                    if (term.size() < 2
                        || (term.at(0) != QLatin1Char('+') && term.at(0) != QLatin1Char('-'))) {
                        wellFormed = false;
                        break;
                    }
                }
                if (!wellFormed) {
                    actionError(ar, 709, "Unsupported or invalid sort criteria");
                    return;
                }
                br.sortDescending = sortQ.startsWith(QLatin1Char('-'));
            }
            const DlnaContentDirectory::BrowseResponse out =
                DlnaContentDirectory::browse(*snap, core->baseUrl, br);
            if (!out.ok) {
                actionError(ar, out.errorCode,
                            out.errorCode == 701 ? "No such object" : "Invalid args");
                return;
            }
            IXML_Document* resp = nullptr;
            UpnpAddToActionResponse(&resp, "Browse", kCdType, "Result",
                                    out.didl.toUtf8().constData());
            UpnpAddToActionResponse(&resp, "Browse", kCdType, "NumberReturned",
                                    QByteArray::number(out.numberReturned).constData());
            UpnpAddToActionResponse(&resp, "Browse", kCdType, "TotalMatches",
                                    QByteArray::number(out.totalMatches).constData());
            UpnpAddToActionResponse(&resp, "Browse", kCdType, "UpdateID",
                                    QByteArray::number(snap->systemUpdateId).constData());
            UpnpActionRequest_set_ActionResult(ar, resp);
            UpnpActionRequest_set_ErrCode(ar, UPNP_E_SUCCESS);
        } else if (strcmp(action, "GetSystemUpdateID") == 0) {
            IXML_Document* resp = nullptr;
            UpnpAddToActionResponse(&resp, "GetSystemUpdateID", kCdType, "Id",
                                    QByteArray::number(snap->systemUpdateId).constData());
            UpnpActionRequest_set_ActionResult(ar, resp);
            UpnpActionRequest_set_ErrCode(ar, UPNP_E_SUCCESS);
        } else if (strcmp(action, "GetSearchCapabilities") == 0) {
            IXML_Document* resp = nullptr;
            UpnpAddToActionResponse(&resp, "GetSearchCapabilities", kCdType, "SearchCaps", "");
            UpnpActionRequest_set_ActionResult(ar, resp);
            UpnpActionRequest_set_ErrCode(ar, UPNP_E_SUCCESS);
        } else if (strcmp(action, "GetSortCapabilities") == 0) {
            // The one criterion every listing already honours (DlnaLibrary sorts by title).
            IXML_Document* resp = nullptr;
            UpnpAddToActionResponse(&resp, "GetSortCapabilities", kCdType, "SortCaps",
                                    "dc:title");
            UpnpActionRequest_set_ActionResult(ar, resp);
            UpnpActionRequest_set_ErrCode(ar, UPNP_E_SUCCESS);
        } else {
            actionError(ar, 401, "Invalid Action");
        }
        return;
    }

    if (strcmp(service, kCmId) == 0) {
        if (strcmp(action, "GetProtocolInfo") == 0) {
            const QString source =
                DlnaContentDirectory::sourceProtocolInfos(*snap).join(QLatin1Char(','));
            const QByteArray src = source.toLatin1();
            IXML_Document* resp = nullptr;
            UpnpAddToActionResponse(&resp, "GetProtocolInfo", kCmType, "Source",
                                    src.constData());
            UpnpAddToActionResponse(&resp, "GetProtocolInfo", kCmType, "Sink", "");
            UpnpActionRequest_set_ActionResult(ar, resp);
            UpnpActionRequest_set_ErrCode(ar, UPNP_E_SUCCESS);
        } else if (strcmp(action, "GetCurrentConnectionIDs") == 0) {
            IXML_Document* resp = nullptr;
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionIDs", kCmType, "ConnectionIDs",
                                    "0");
            UpnpActionRequest_set_ActionResult(ar, resp);
            UpnpActionRequest_set_ErrCode(ar, UPNP_E_SUCCESS);
        } else if (strcmp(action, "GetCurrentConnectionInfo") == 0) {
            if (actionArg(req, "ConnectionID") != "0") {
                actionError(ar, 706, "No such connection");
                return;
            }
            // Kodi's fixed values: no RCS, no AVTransport, wildcard protocol, output, unknown.
            IXML_Document* resp = nullptr;
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionInfo", kCmType, "RCSID", "-1");
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionInfo", kCmType, "AVTransportID",
                                    "-1");
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionInfo", kCmType, "ProtocolInfo",
                                    "http-get:*:*:*");
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionInfo", kCmType,
                                    "PeerConnectionManager", "");
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionInfo", kCmType,
                                    "PeerConnectionID", "-1");
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionInfo", kCmType, "Direction",
                                    "Output");
            UpnpAddToActionResponse(&resp, "GetCurrentConnectionInfo", kCmType, "Status",
                                    "Unknown");
            UpnpActionRequest_set_ActionResult(ar, resp);
            UpnpActionRequest_set_ErrCode(ar, UPNP_E_SUCCESS);
        } else {
            actionError(ar, 401, "Invalid Action");
        }
        return;
    }

    actionError(ar, 401, "Invalid Action");
}

void handleGetVar(DlnaServerCore* core, UpnpStateVarRequest* sv)
{
    const char* var = UpnpStateVarRequest_get_StateVarName_cstr(sv);
    if (!var) {
        UpnpStateVarRequest_set_ErrCode(sv, 404);
        return;
    }
    const auto snap = core->provider();
    QByteArray value;
    if (strcmp(var, "SystemUpdateID") == 0)
        value = QByteArray::number(snap->systemUpdateId);
    else if (strcmp(var, "ContainerUpdateIDs") == 0)
        value = QByteArray("");
    else if (strcmp(var, "SourceProtocolInfo") == 0)
        value = DlnaContentDirectory::sourceProtocolInfos(*snap).join(QLatin1Char(','))
                    .toLatin1();
    else if (strcmp(var, "SinkProtocolInfo") == 0)
        value = "";
    else if (strcmp(var, "CurrentConnectionIDs") == 0)
        value = "0";
    else {
        UpnpStateVarRequest_set_ErrCode(sv, 404);
        return;
    }
    static thread_local QByteArray stableValue;
    stableValue = value;
    UpnpStateVarRequest_set_CurrentVal(sv, stableValue.data());
    UpnpStateVarRequest_set_ErrCode(sv, UPNP_E_SUCCESS);
}

void handleSubscribe(DlnaServerCore* core, UpnpSubscriptionRequest* sr)
{
    const char* serviceId = UpnpSubscriptionRequest_get_ServiceId_cstr(sr);
    const char* udn = UpnpSubscriptionRequest_get_UDN_cstr(sr);
    const char* sid = UpnpSubscriptionRequest_get_SID_cstr(sr);
    if (!serviceId || !udn || !sid)
        return; // the lib already answered; nothing sane to accept into
    const auto snap = core->provider();
    if (strcmp(serviceId, kCdId) == 0) {
        const QByteArray id = QByteArray::number(snap->systemUpdateId);
        const char* names[2] = {"SystemUpdateID", "ContainerUpdateIDs"};
        const char* values[2] = {id.constData(), ""};
        UpnpAcceptSubscription(core->deviceHandle.load(), udn, serviceId, names, values, 2,
                               sid);
    } else if (strcmp(serviceId, kCmId) == 0) {
        const QByteArray source =
            DlnaContentDirectory::sourceProtocolInfos(*snap).join(QLatin1Char(',')).toLatin1();
        const char* names[3] = {"SourceProtocolInfo", "SinkProtocolInfo",
                                "CurrentConnectionIDs"};
        const char* values[3] = {source.constData(), "", "0"};
        UpnpAcceptSubscription(core->deviceHandle.load(), udn, serviceId, names, values, 3,
                               sid);
    }
}
