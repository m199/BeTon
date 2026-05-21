#include "DLNAService.h"
#include "Debug.h"
#include "Messages.h"

#include <UrlProtocolRoster.h>
#include <private/netservices/HttpRequest.h>
#include <private/netservices/HttpResult.h>
#include <DataIO.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <OS.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cerrno>
#include <deque>
#include <memory>
#include <map>
#include <set>
#include <string>

using namespace BPrivate::Network;

static const char* kSsdpAddress = "239.255.255.250";
static const uint16 kSsdpPort = 1900;

static BString
_NormalizedDlnaDedupPart(const BString& value)
{
    BString normalized(value);
    normalized.Trim();
    normalized.ToLower();
    return normalized;
}

static BString
_DlnaDedupKey(const DLNABrowseItem& item)
{
    if (!item.title.IsEmpty() &&
        (!item.duration.IsEmpty() || !item.artist.IsEmpty() ||
         !item.album.IsEmpty() || !item.resourceSize.IsEmpty())) {
        BString key("meta:");
        key << _NormalizedDlnaDedupPart(item.title) << "\n"
            << _NormalizedDlnaDedupPart(item.artist) << "\n"
            << _NormalizedDlnaDedupPart(item.album) << "\n"
            << _NormalizedDlnaDedupPart(item.duration) << "\n"
            << _NormalizedDlnaDedupPart(item.resourceSize);
        return key;
    }

    if (!item.refId.IsEmpty()) {
        BString key("ref:");
        key << item.refId;
        return key;
    }

    if (!item.resourceUrl.IsEmpty()) {
        BString key("url:");
        key << item.resourceUrl;
        return key;
    }

    BString key("id:");
    key << item.id;
    return key;
}

DLNAService::DLNAService(BMessenger target)
    : fTarget(target),
      fHasActiveRenderer(false),
      fDiscoveryMode(Manual),
      fDiscoveryThread(-1),
      fDiscoveryWakeSem(create_sem(0, "dlna_discovery_wake")),
      fRunning(false)
{
}

DLNAService::~DLNAService()
{
    StopDiscovery();
    if (fDiscoveryWakeSem >= 0)
        delete_sem(fDiscoveryWakeSem);
}

/**
 * @brief Starts the SSDP discovery thread.
 */
void DLNAService::StartDiscovery()
{
    if (fDiscoveryMode == Off)
        return;

    if (fRunning) {
        if (fDiscoveryWakeSem >= 0)
            release_sem(fDiscoveryWakeSem);
        return;
    }

    fRunning = true;
    fDiscoveryThread = spawn_thread(_DiscoveryThreadEntry, "dlna_discovery",
                                    B_LOW_PRIORITY, this);
    if (fDiscoveryThread >= 0)
        resume_thread(fDiscoveryThread);
    else
        fRunning = false;
}

/**
 * @brief Stops the discovery thread and waits for it to finish.
 */
void DLNAService::StopDiscovery()
{
    fRunning = false;
    if (fDiscoveryWakeSem >= 0)
        release_sem(fDiscoveryWakeSem);
    if (fDiscoveryThread >= 0) {
        status_t exit;
        wait_for_thread(fDiscoveryThread, &exit);
        fDiscoveryThread = -1;
    }
}

int32 DLNAService::_DiscoveryThreadEntry(void* arg)
{
    static_cast<DLNAService*>(arg)->_DiscoveryLoop();
    return 0;
}

/**
 * @brief Main discovery loop: sends M-SEARCH, receives responses, fetches descriptions.
 */
void DLNAService::_DiscoveryLoop()
{
    DEBUG_PRINT("Discovery loop started\n");

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        DEBUG_PRINT("Failed to create UDP socket: %s\n", strerror(errno));
        return;
    }

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int ttl = 4;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(kSsdpAddress);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        DEBUG_PRINT("Failed to join multicast group: %s\n", strerror(errno));
    }

    int loop = 0;
    setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    struct sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(0);
    if (bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0) {
        DEBUG_PRINT("bind() failed: %s\n", strerror(errno));
    } else {
        struct sockaddr_in bound;
        socklen_t boundLen = sizeof(bound);
        getsockname(sock, (struct sockaddr*)&bound, &boundLen);
        DEBUG_PRINT("Socket bound to port %d\n", ntohs(bound.sin_port));
    }

    while (fRunning) {
        _SendMSearch(sock);

        bigtime_t listenUntil = system_time() + 6000000;
        char buf[4096];
        int received = 0;
        while (fRunning && system_time() < listenUntil) {
            struct sockaddr_in from;
            socklen_t fromLen = sizeof(from);
            ssize_t len = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                                   (struct sockaddr*)&from, &fromLen);
            if (len > 0) {
                buf[len] = '\0';
                received++;
                _ProcessSsdpResponse(buf, len, from);
            } else if (len < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ETIMEDOUT && errno != B_TIMED_OUT) {
                    DEBUG_PRINT("recvfrom error: %s (%d)\n", strerror(errno), errno);
                }
            }
        }

        _PurgeStaleDevices();

        if (fRunning) {
            if (fDiscoveryWakeSem >= 0) {
                acquire_sem_etc(fDiscoveryWakeSem, 1, B_RELATIVE_TIMEOUT,
                                kDiscoveryInterval);
            } else {
                snooze(kDiscoveryInterval);
            }
        }
    }

    close(sock);
    DEBUG_PRINT("Discovery loop ended\n");
}

/**
 * @brief Sends an SSDP M-SEARCH multicast request.
 */
void DLNAService::_SendMSearch(int sock)
{
    const char* targets[] = {
        "upnp:rootdevice",
        "ssdp:all",
        "urn:schemas-upnp-org:device:MediaServer:1"
    };

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kSsdpPort);

    /// Try both multicast and broadcast addresses, as some devices only respond to one or the other. 
    const char* addrs[] = { kSsdpAddress, "255.255.255.255" };

    for (int a = 0; a < 2; a++) {
        dest.sin_addr.s_addr = inet_addr(addrs[a]);
        if (a == 1) {
            int broadcast = 1;
            setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
        }

        for (int i = 0; i < 3; i++) {
            BString msearch;
            msearch << "M-SEARCH * HTTP/1.1\r\n"
                    << "HOST: 239.255.255.250:1900\r\n"
                    << "MAN: \"ssdp:discover\"\r\n"
                    << "MX: 3\r\n"
                    << "ST: " << targets[i] << "\r\n"
                    << "USER-AGENT: Haiku/BeTon DLNA\r\n"
                    << "\r\n";

            sendto(sock, msearch.String(), msearch.Length(), 0,
                   (struct sockaddr*)&dest, sizeof(dest));
        }
    }

    DEBUG_PRINT("M-SEARCH sent (Multicast & Broadcast)\n");
}

/**
 * @brief Parses an SSDP response and fetches device description if new.
 */
void DLNAService::_ProcessSsdpResponse(const char* data, size_t len, const struct sockaddr_in& from)
{
    BString response(data, len);
    BString fromAddr = inet_ntoa(from.sin_addr);

    if (response.StartsWith("M-SEARCH"))
        return;

    bool isServer = response.IFindFirst("MediaServer") >= 0;
    bool isRenderer = response.IFindFirst("MediaRenderer") >= 0;
    if (!isServer && !isRenderer)
        return;

    /* DEBUG_PRINT("SSDP response from %s (%zd bytes) - candidate found\n", 
                fromAddr.String(), len); */

    BString location;
    int32 locPos = response.IFindFirst("LOCATION:");
    if (locPos < 0)
        return;
    int32 start = locPos + 9;
    while (start < response.Length() && response[start] == ' ')
        start++;
    int32 end = response.FindFirst("\r\n", start);
    if (end < 0)
        end = response.FindFirst("\n", start);
    if (end < 0)
        end = response.Length();
    response.CopyInto(location, start, end - start);
    location.Trim();

    if (location.IsEmpty())
        return;

    for (auto& dev : fDevices) {
        if (dev.location == location) {
            dev.lastSeen = system_time();
            return;
        }
    }

    DLNADevice dev;
    dev.type = isServer ? DLNADevice::MediaServer : DLNADevice::MediaRenderer;
    dev.location = location;
    dev.lastSeen = system_time();

    if (_FetchDeviceDescription(location, dev) == B_OK
        && !dev.friendlyName.IsEmpty()) {
        fDevices.push_back(dev);
        DEBUG_PRINT("Found %s: '%s' (%s)\n",
                    isServer ? "Server" : "Renderer",
                    dev.friendlyName.String(), dev.uuid.String());

        if (fTarget.IsValid()) {
            BMessage msg(MSG_DLNA_DEVICE_FOUND);
            msg.AddString("name", dev.friendlyName);
            msg.AddString("uuid", dev.uuid);
            msg.AddInt32("type", (int32)dev.type);
            fTarget.SendMessage(&msg);
        }
    }
}

/**
 * @brief Fetches and parses a UPnP device description XML.
 */
status_t DLNAService::_FetchDeviceDescription(const BString& location,
                                               DLNADevice& dev)
{
    BMallocIO sink;
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
    BUrl burl(location.String());
#else
    BUrl burl(location.String(), false);
#endif
    std::unique_ptr<BUrlRequest> req(
        BUrlProtocolRoster::MakeRequest(burl, &sink, nullptr, &fUrlContext));
    if (!req)
        return B_ERROR;

    thread_id tid = req->Run();
    status_t exit;
    if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 10000000, &exit) != B_OK) {
        req->Stop();
        wait_for_thread(tid, &exit);
        return B_TIMED_OUT;
    }

    BString xml((const char*)sink.Buffer(), sink.BufferLength());
    if (xml.IsEmpty())
        return B_ERROR;

    _ParseDeviceXml(xml, dev);
    return B_OK;
}

/**
 * @brief Extracts device info from the UPnP description XML using string search.
 */
void DLNAService::_ParseDeviceXml(const BString& xml, DLNADevice& dev)
{
    dev.friendlyName = _ExtractXmlTag(xml, "friendlyName");
    dev.uuid = _ExtractXmlTag(xml, "UDN");

    if (dev.uuid.StartsWith("uuid:")) {
        BString tmp;
        dev.uuid.CopyInto(tmp, 5, dev.uuid.Length() - 5);
        dev.uuid = tmp;
    }

    BString baseUrl;
    int32 slashSlash = dev.location.FindFirst("//");
    if (slashSlash >= 0) {
        int32 nextSlash = dev.location.FindFirst('/', slashSlash + 2);
        if (nextSlash >= 0)
            dev.location.CopyInto(baseUrl, 0, nextSlash);
        else
            baseUrl = dev.location;
    }

    BString devType = _ExtractXmlTag(xml, "deviceType");
    if (devType.FindFirst("MediaServer") >= 0)
        dev.type = DLNADevice::MediaServer;
    else if (devType.FindFirst("MediaRenderer") >= 0)
        dev.type = DLNADevice::MediaRenderer;

    int32 searchStart = 0;
    while (searchStart < xml.Length()) {
        int32 svcStart = xml.FindFirst("<service>", searchStart);
        if (svcStart < 0)
            svcStart = xml.FindFirst("<service ", searchStart);
        if (svcStart < 0)
            break;

        int32 svcEnd = xml.FindFirst("</service>", svcStart);
        if (svcEnd < 0)
            break;
        svcEnd += 10;

        BString svcBlock;
        xml.CopyInto(svcBlock, svcStart, svcEnd - svcStart);

        BString svcType = _ExtractXmlTag(svcBlock, "serviceType");
        BString ctrlUrl = _ExtractXmlTag(svcBlock, "controlURL");

        if (!ctrlUrl.IsEmpty() && !ctrlUrl.StartsWith("http")) {
            if (!ctrlUrl.StartsWith("/"))
                ctrlUrl.Prepend("/");
            ctrlUrl.Prepend(baseUrl);
        }

        if (svcType.FindFirst("ContentDirectory") >= 0)
            dev.contentDirUrl = ctrlUrl;
        else if (svcType.FindFirst("AVTransport") >= 0)
            dev.avTransportUrl = ctrlUrl;
        else if (svcType.FindFirst("RenderingControl") >= 0)
            dev.renderingCtlUrl = ctrlUrl;

        searchStart = svcEnd;
    }

    if (dev.type == DLNADevice::MediaServer && dev.contentDirUrl.IsEmpty()) {
        DEBUG_PRINT("Warning: MediaServer '%s' found but ContentDirectory URL is missing!\n",
                    dev.friendlyName.String());
    }
}

/**
 * @brief Removes devices not seen within the timeout period.
 */
void DLNAService::_PurgeStaleDevices()
{
    bigtime_t now = system_time();
    auto it = fDevices.begin();
    while (it != fDevices.end()) {
        if (now - it->lastSeen > kDeviceTimeout) {
            DEBUG_PRINT("Device timed out: '%s'\n",
                        it->friendlyName.String());
            if (fHasActiveRenderer && fActiveRenderer.uuid == it->uuid)
                fHasActiveRenderer = false;

            if (fTarget.IsValid()) {
                BMessage msg(MSG_DLNA_DEVICE_LOST);
                msg.AddString("uuid", it->uuid);
                fTarget.SendMessage(&msg);
            }
            it = fDevices.erase(it);
        } else {
            ++it;
        }
    }
}

void DLNAService::SetActiveRenderer(const BString& uuid)
{
    _StopPositionPolling();
    fCurrentPosition = 0;
    fCurrentDuration = 0;
    
    if (uuid.IsEmpty()) {
        fHasActiveRenderer = false;
        return;
    }
    for (auto& d : fDevices) {
        if (d.type == DLNADevice::MediaRenderer && d.uuid == uuid) {
            fActiveRenderer = d;
            fHasActiveRenderer = true;
            DEBUG_PRINT("Active renderer: '%s'\n",
                        d.friendlyName.String());
            return;
        }
    }
    fHasActiveRenderer = false;
}

std::vector<DLNADevice> DLNAService::Servers() const
{
    std::vector<DLNADevice> result;
    for (const auto& d : fDevices) {
        if (d.type == DLNADevice::MediaServer)
            result.push_back(d);
    }
    return result;
}

std::vector<DLNADevice> DLNAService::Renderers() const
{
    std::vector<DLNADevice> result;
    for (const auto& d : fDevices) {
        if (d.type == DLNADevice::MediaRenderer)
            result.push_back(d);
    }
    return result;
}

/**
 * @brief Sends a SOAP action to a UPnP service control URL.
 */
status_t DLNAService::_SendSoapAction(const BString& controlUrl,
                                       const BString& serviceUrn,
                                       const BString& action,
                                       const BString& bodyArgs,
                                       BString& response)
{
    BString soapBody;
    soapBody << "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
             << "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
             << " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
             << "<s:Body>"
             << "<u:" << action << " xmlns:u=\"" << serviceUrn << "\">"
             << bodyArgs
             << "</u:" << action << ">"
             << "</s:Body></s:Envelope>";

    BMallocIO sink;
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
    BUrl burl(controlUrl.String());
#else
    BUrl burl(controlUrl.String(), false);
#endif
    std::unique_ptr<BUrlRequest> req(
        BUrlProtocolRoster::MakeRequest(burl, &sink, nullptr, nullptr));
    if (!req)
        return B_ERROR;

    auto http = dynamic_cast<BHttpRequest*>(req.get());
    if (http) {
        http->SetMethod(B_HTTP_POST);
        BHttpHeaders headers;
        headers.AddHeader("Content-Type", "text/xml; charset=\"utf-8\"");

        BString soapAction;
        soapAction << "\"" << serviceUrn << "#" << action << "\"";
        headers.AddHeader("SOAPAction", soapAction.String());
        http->SetHeaders(headers);

        BMemoryIO* bodyIO = new BMemoryIO(soapBody.String(), soapBody.Length());
        http->AdoptInputData(bodyIO, soapBody.Length());
    }

    thread_id tid = req->Run();
    status_t exit;
    if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 30000000, &exit) != B_OK) {
        req->Stop();
        wait_for_thread(tid, &exit);
        return B_TIMED_OUT;
    }

    response.SetTo((const char*)sink.Buffer(), sink.BufferLength());
    return B_OK;
}

status_t DLNAService::SetAVTransportURI(const BString& uri, const BString& title)
{
    fCurrentPosition = 0;
    fCurrentDuration = 0;
    
    if (!fHasActiveRenderer || fActiveRenderer.avTransportUrl.IsEmpty())
        return B_NOT_ALLOWED;

    BString args;
    args << "<InstanceID>0</InstanceID>"
         << "<CurrentURI>" << uri << "</CurrentURI>"
         << "<CurrentURIMetaData></CurrentURIMetaData>";

    BString response;
    return _SendSoapAction(fActiveRenderer.avTransportUrl,
                           "urn:schemas-upnp-org:service:AVTransport:1",
                           "SetAVTransportURI", args, response);
}

status_t DLNAService::RendererPlay()
{
    if (!fHasActiveRenderer || fActiveRenderer.avTransportUrl.IsEmpty())
        return B_NOT_ALLOWED;

    BString args("<InstanceID>0</InstanceID><Speed>1</Speed>");
    BString response;
    status_t err = _SendSoapAction(fActiveRenderer.avTransportUrl,
                                   "urn:schemas-upnp-org:service:AVTransport:1",
                                   "Play", args, response);
    if (err == B_OK) {
        _StartPositionPolling();
    }
    return err;
}

status_t DLNAService::RendererStop()
{
    if (!fHasActiveRenderer || fActiveRenderer.avTransportUrl.IsEmpty())
        return B_NOT_ALLOWED;

    BString args("<InstanceID>0</InstanceID>");
    BString response;
    status_t err = _SendSoapAction(fActiveRenderer.avTransportUrl,
                                   "urn:schemas-upnp-org:service:AVTransport:1",
                                   "Stop", args, response);
    if (err == B_OK) {
        _StopPositionPolling();
    }
    return err;
}

status_t DLNAService::RendererPause()
{
    if (!fHasActiveRenderer || fActiveRenderer.avTransportUrl.IsEmpty())
        return B_NOT_ALLOWED;

    BString args("<InstanceID>0</InstanceID>");
    BString response;
    status_t err = _SendSoapAction(fActiveRenderer.avTransportUrl,
                                   "urn:schemas-upnp-org:service:AVTransport:1",
                                   "Pause", args, response);
    /// Note: Deliberately not stopping polling here so we can still track state/volume while paused.
    return err;
}

status_t DLNAService::RendererSeek(bigtime_t position)
{
    if (!fHasActiveRenderer || fActiveRenderer.avTransportUrl.IsEmpty())
        return B_NOT_ALLOWED;

    int h = position / 3600000000LL;
    int m = (position / 60000000LL) % 60;
    int s = (position / 1000000LL) % 60;
    
    char targetBuf[32];
    snprintf(targetBuf, sizeof(targetBuf), "%02d:%02d:%02d", h, m, s);

    BString args;
    args << "<InstanceID>0</InstanceID><Unit>REL_TIME</Unit><Target>" << targetBuf << "</Target>";
    BString response;
    status_t err = _SendSoapAction(fActiveRenderer.avTransportUrl,
                                   "urn:schemas-upnp-org:service:AVTransport:1",
                                   "Seek", args, response);
    if (err == B_OK)
        fCurrentPosition = position;
    return err;
}

bigtime_t DLNAService::_ParseUpnpTime(const BString& timeStr) const
{
    int h = 0, m = 0, s = 0;
    if (sscanf(timeStr.String(), "%d:%d:%d", &h, &m, &s) == 3) {
        return (h * 3600LL + m * 60LL + s) * 1000000LL;
    }
    return 0;
}

void DLNAService::_StartPositionPolling()
{
    if (fPositionPolling) return;
    fPositionPolling = true;
    fPositionPollThread = spawn_thread(_PositionPollThreadEntry, "DLNAPoll", B_NORMAL_PRIORITY, this);
    if (fPositionPollThread >= 0)
        resume_thread(fPositionPollThread);
}

void DLNAService::_StopPositionPolling()
{
    if (!fPositionPolling) return;
    fPositionPolling = false;
    if (fPositionPollThread >= 0) {
        status_t ret;
        wait_for_thread(fPositionPollThread, &ret);
        fPositionPollThread = -1;
    }
}

int32 DLNAService::_PositionPollThreadEntry(void* arg)
{
    static_cast<DLNAService*>(arg)->_PositionPollLoop();
    return 0;
}

void DLNAService::_PositionPollLoop()
{
    while (fPositionPolling) {
        snooze(1000000); ///< 1 second

        if (!fHasActiveRenderer) continue;

        /// Poll Volume
        int32 vol = -1;
        status_t volErr = GetRendererVolume(vol);
        if (volErr == B_OK && vol >= 0 && vol != fCurrentVolume.load()) {
            fCurrentVolume = vol;
            BMessage volMsg(MSG_DLNA_VOLUME_UPDATE);
            volMsg.AddInt32("volume", vol);
            fTarget.SendMessage(&volMsg);
        }

        BString args("<InstanceID>0</InstanceID>");
        BString response;
        
        /// Poll Transport State
        status_t stateErr = _SendSoapAction(fActiveRenderer.avTransportUrl,
                                       "urn:schemas-upnp-org:service:AVTransport:1",
                                       "GetTransportInfo", args, response);
        if (stateErr == B_OK) {
            BString state = _ExtractXmlTag(response, "CurrentTransportState");
            if (state == "STOPPED") {
                if (fCurrentDuration > 0 && fCurrentPosition > 0 && fCurrentPosition + 2000000LL >= fCurrentDuration) {
                    fCurrentPosition = fCurrentDuration.load();
                    fTarget.SendMessage(MSG_TRACK_ENDED);
                }
            }
        } else {
            DEBUG_PRINT("GetTransportInfo failed: %ld\n", (long)stateErr);
        }

        /// Poll Position
        status_t posErr = _SendSoapAction(fActiveRenderer.avTransportUrl,
                              "urn:schemas-upnp-org:service:AVTransport:1",
                              "GetPositionInfo", args, response);
        if (posErr == B_OK) {
            BString relTime = _ExtractXmlTag(response, "RelTime");
            BString trackDuration = _ExtractXmlTag(response, "TrackDuration");
            
            if (!relTime.IsEmpty()) {
                fCurrentPosition = _ParseUpnpTime(relTime);
            } else {
                DEBUG_PRINT("RelTime is empty! Raw response: %s\n", response.String());
            }
            
            if (!trackDuration.IsEmpty()) {
                fCurrentDuration = _ParseUpnpTime(trackDuration);
            }
            
            DEBUG_PRINT("Poll OK: pos=%lld dur=%lld\n", (long long)fCurrentPosition.load(), (long long)fCurrentDuration.load());
        } else {
            DEBUG_PRINT("GetPositionInfo failed: %ld\n", (long)posErr);
        }
    }
}

status_t DLNAService::SetRendererVolume(int32 percent)
{
    if (!fHasActiveRenderer || fActiveRenderer.renderingCtlUrl.IsEmpty())
        return B_NOT_ALLOWED;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    BString args;
    args << "<InstanceID>0</InstanceID>"
         << "<Channel>Master</Channel>"
         << "<DesiredVolume>" << percent << "</DesiredVolume>";

    BString response;
    return _SendSoapAction(fActiveRenderer.renderingCtlUrl,
                           "urn:schemas-upnp-org:service:RenderingControl:1",
                           "SetVolume", args, response);
}

status_t DLNAService::GetRendererVolume(int32& percent)
{
    if (!fHasActiveRenderer || fActiveRenderer.renderingCtlUrl.IsEmpty())
        return B_NOT_ALLOWED;

    BString args;
    args << "<InstanceID>0</InstanceID>"
         << "<Channel>Master</Channel>";

    BString response;
    status_t err = _SendSoapAction(fActiveRenderer.renderingCtlUrl,
                                   "urn:schemas-upnp-org:service:RenderingControl:1",
                                   "GetVolume", args, response);
    if (err != B_OK)
        return err;

    BString vol = _ExtractXmlTag(response, "CurrentVolume");
    if (vol.IsEmpty())
        return B_ERROR;

    percent = atoi(vol.String());
    return B_OK;
}

/**
 * @brief Browses a container on a DLNA MediaServer.
 */
status_t DLNAService::Browse(const DLNADevice& server, const BString& objectId,
                              std::vector<DLNABrowseItem>& results)
{
    DEBUG_PRINT("Browse: server='%s', url='%s'\n",
                server.friendlyName.String(), server.contentDirUrl.String());

    if (server.contentDirUrl.IsEmpty())
        return B_NOT_ALLOWED;

    BString args;
    args << "<ObjectID>" << objectId << "</ObjectID>"
         << "<BrowseFlag>BrowseDirectChildren</BrowseFlag>"
         << "<Filter>*</Filter>"
         << "<StartingIndex>0</StartingIndex>"
         << "<RequestedCount>200</RequestedCount>"
         << "<SortCriteria></SortCriteria>";

    BString response;
    status_t err = _SendSoapAction(server.contentDirUrl,
                                   "urn:schemas-upnp-org:service:ContentDirectory:1",
                                   "Browse", args, response);
    if (err != B_OK)
        return err;

    BString didl = _ExtractXmlTag(response, "Result");
    if (didl.IsEmpty())
        return B_ERROR;

    didl = _UnescapeXml(didl);
    _ParseDIDLLite(didl, results);

    DEBUG_PRINT("Browse '%s': %zu items\n",
                objectId.String(), results.size());
    return B_OK;
}

/**
 * @brief Parses DIDL-Lite XML into browse items.
 */
void DLNAService::_ParseDIDLLite(const BString& didl,
                                  std::vector<DLNABrowseItem>& items)
{
    int32 pos = 0;
    while (pos < didl.Length()) {
        bool isContainer = false;
        int32 itemStart = didl.FindFirst("<item ", pos);
        int32 contStart = didl.FindFirst("<container ", pos);

        int32 start = -1;
        const char* endTag = nullptr;

        if (itemStart >= 0 && (contStart < 0 || itemStart < contStart)) {
            start = itemStart;
            endTag = "</item>";
            isContainer = false;
        } else if (contStart >= 0) {
            start = contStart;
            endTag = "</container>";
            isContainer = true;
        } else {
            break;
        }

        int32 end = didl.FindFirst(endTag, start);
        if (end < 0)
            break;
        end += strlen(endTag);

        BString block;
        didl.CopyInto(block, start, end - start);

        DLNABrowseItem item;
        item.isContainer = isContainer;

        int32 idPos = block.FindFirst("id=\"");
        if (idPos >= 0) {
            int32 idStart = idPos + 4;
            int32 idEnd = block.FindFirst('"', idStart);
            if (idEnd > idStart)
                block.CopyInto(item.id, idStart, idEnd - idStart);
        }

        int32 refPos = block.FindFirst("refID=\"");
        if (refPos >= 0) {
            int32 refStart = refPos + 7;
            int32 refEnd = block.FindFirst('"', refStart);
            if (refEnd > refStart)
                block.CopyInto(item.refId, refStart, refEnd - refStart);
        }

        int32 pidPos = block.FindFirst("parentID=\"");
        if (pidPos >= 0) {
            int32 pidStart = pidPos + 10;
            int32 pidEnd = block.FindFirst('"', pidStart);
            if (pidEnd > pidStart)
                block.CopyInto(item.parentId, pidStart, pidEnd - pidStart);
        }

        item.title = _UnescapeXml(_ExtractXmlTag(block, "dc:title"));
        item.artist = _UnescapeXml(_ExtractXmlTag(block, "dc:creator"));
        if (item.artist.IsEmpty())
            item.artist = _UnescapeXml(_ExtractXmlTag(block, "upnp:artist"));
        item.album = _UnescapeXml(_ExtractXmlTag(block, "upnp:album"));
        item.genre = _UnescapeXml(_ExtractXmlTag(block, "upnp:genre"));
        item.upnpClass = _UnescapeXml(_ExtractXmlTag(block, "upnp:class"));
        item.albumArtUrl = _UnescapeXml(_ExtractXmlTag(block, "upnp:albumArtURI"));

        if (!isContainer) {
            int32 resStart = block.FindFirst("<res ");
            if (resStart >= 0) {
                int32 resClose = block.FindFirst('>', resStart);
                if (resClose >= 0) {
                    int32 resEnd = block.FindFirst("</res>", resClose);
                    if (resEnd > resClose + 1) {
                        BString rawUrl;
                        block.CopyInto(rawUrl, resClose + 1,
                                       resEnd - resClose - 1);
                        item.resourceUrl = _UnescapeXml(rawUrl);
                        item.resourceUrl.Trim();
                    }

                    BString resAttrs;
                    block.CopyInto(resAttrs, resStart, resClose - resStart);
                    int32 piPos = resAttrs.FindFirst("protocolInfo=\"");
                    if (piPos >= 0) {
                        int32 piStart = piPos + 14;
                        int32 piEnd = resAttrs.FindFirst('"', piStart);
                        if (piEnd > piStart) {
                            BString pi;
                            resAttrs.CopyInto(pi, piStart, piEnd - piStart);
                            int32 c1 = pi.FindFirst(':');
                            if (c1 >= 0) {
                                int32 c2 = pi.FindFirst(':', c1 + 1);
                                if (c2 >= 0) {
                                    int32 c3 = pi.FindFirst(':', c2 + 1);
                                    if (c3 > c2 + 1)
                                        pi.CopyInto(item.mimeType, c2 + 1,
                                                     c3 - c2 - 1);
                                }
                            }
                        }
                    }

                    int32 durPos = resAttrs.FindFirst("duration=\"");
                    if (durPos >= 0) {
                        int32 durStart = durPos + 10;
                        int32 durEnd = resAttrs.FindFirst('"', durStart);
                        if (durEnd > durStart)
                            resAttrs.CopyInto(item.duration, durStart,
                                              durEnd - durStart);
                    }

                    int32 sizePos = resAttrs.FindFirst("size=\"");
                    if (sizePos >= 0) {
                        int32 sizeStart = sizePos + 6;
                        int32 sizeEnd = resAttrs.FindFirst('"', sizeStart);
                        if (sizeEnd > sizeStart)
                            resAttrs.CopyInto(item.resourceSize, sizeStart,
                                              sizeEnd - sizeStart);
                    }
                }
            }
        }

        items.push_back(item);
        pos = end;
    }
}

/**
 * @brief Extracts the text content of a simple XML tag.
 */
BString DLNAService::_ExtractXmlTag(const BString& xml, const char* tagName) const
{
    int32 start = -1;
    BString p1; p1 << "<" << tagName;
    int32 i1 = xml.FindFirst(p1);
    if (i1 >= 0 && (xml[i1 + p1.Length()] == '>' || xml[i1 + p1.Length()] == ' ')) {
        start = i1 + p1.Length();
    }

    if (start < 0) {
        BString p2; p2 << ":" << tagName;
        int32 i2 = xml.FindFirst(p2);
        if (i2 >= 0 && (xml[i2 + p2.Length()] == '>' || xml[i2 + p2.Length()] == ' ')) {
            start = i2 + p2.Length();
        }
    }

    if (start < 0) return "";

    if (xml[start] == ' ') {
        int32 gt = xml.FindFirst('>', start);
        if (gt < 0) return "";
        start = gt + 1;
    } else if (xml[start] == '>') {
        start++;
    }

    int32 end = xml.FindFirst("</", start);
    while (end >= 0) {
        int32 nextGt = xml.FindFirst('>', end);
        if (nextGt < 0) break;

        BString closing;
        xml.CopyInto(closing, end + 2, nextGt - (end + 2));
        if (closing == tagName || closing.EndsWith(BString(":") << tagName)) {
            BString result;
            xml.CopyInto(result, start, end - start);
            return result;
        }
        end = xml.FindFirst("</", end + 2);
    }

    return "";
}

/**
 * @brief Unescapes XML entities in a string.
 */
BString DLNAService::_UnescapeXml(const BString& escaped) const
{
    BString result(escaped);
    result.ReplaceAll("&lt;", "<");
    result.ReplaceAll("&gt;", ">");
    result.ReplaceAll("&amp;", "&");
    result.ReplaceAll("&quot;", "\"");
    result.ReplaceAll("&apos;", "'");
    return result;
}

/**
 * @brief Uses the ContentDirectory Search action to fetch all audio items paginated.
 */
status_t DLNAService::_SearchAudio(const DLNADevice& server,
                                   std::vector<DLNABrowseItem>& allItems,
                                   BMessenger progressTarget)
{
    if (server.contentDirUrl.IsEmpty())
        return B_NOT_ALLOWED;

    uint32 startIndex = 0;
    uint32 requestedCount = 500;
    uint32 totalMatches = 1;

    int32 lastReported = 0;
    std::set<BString> seenItems;

    while (startIndex < totalMatches) {
        BString args;
        args << "<ContainerID>0</ContainerID>"
             << "<SearchCriteria>upnp:class derivedfrom &quot;object.item.audioItem&quot;</SearchCriteria>"
             << "<Filter>*</Filter>"
             << "<StartingIndex>" << startIndex << "</StartingIndex>"
             << "<RequestedCount>" << requestedCount << "</RequestedCount>"
             << "<SortCriteria></SortCriteria>";

        BString response;
        status_t err = _SendSoapAction(server.contentDirUrl,
                                       "urn:schemas-upnp-org:service:ContentDirectory:1",
                                       "Search", args, response);
        if (err != B_OK) {
            /// Some servers return an error (HTTP 500) if StartingIndex is exactly at the end
            /// or if a timeout occurs. If we already collected items, accept them!
            if (!allItems.empty()) {
                DEBUG_PRINT("_SearchAudio partial success: error at index %lu, keeping %zu items.\n",
                            (unsigned long)startIndex, allItems.size());
                break;
            }
            return err;
        }

        BString didl = _ExtractXmlTag(response, "Result");
        if (didl.IsEmpty()) {
            if (!allItems.empty())
                break;
            return B_ERROR;
        }

        BString totalStr = _ExtractXmlTag(response, "TotalMatches");
        if (!totalStr.IsEmpty()) {
            totalMatches = (uint32)atoi(totalStr.String());
        } else {
            totalMatches = startIndex + requestedCount;
        }

        didl = _UnescapeXml(didl);
        std::vector<DLNABrowseItem> pageResults;
        _ParseDIDLLite(didl, pageResults);

        for (const auto& item : pageResults) {
            if (!item.isContainer && item.upnpClass.StartsWith("object.item.audioItem")) {
                BString dedupKey = _DlnaDedupKey(item);
                if (seenItems.find(dedupKey) == seenItems.end()) {
                    seenItems.insert(dedupKey);
                    allItems.push_back(item);
                }
            }
        }

        BString returnedStr = _ExtractXmlTag(response, "NumberReturned");
        uint32 numberReturned = 0;
        if (!returnedStr.IsEmpty()) {
            numberReturned = (uint32)atoi(returnedStr.String());
        } else {
            numberReturned = pageResults.size();
        }

        if (numberReturned == 0) break; ///< Reached the end

        startIndex += numberReturned;

        if ((int32)allItems.size() - lastReported >= 200) {
            lastReported = (int32)allItems.size();
            if (progressTarget.IsValid()) {
                BMessage progress(MSG_DLNA_CRAWL_PROGRESS);
                progress.AddInt32("count", lastReported);
                progress.AddString("phase", "audio");
                progress.AddString("server", server.friendlyName);
                progressTarget.SendMessage(&progress);
            }
        }

        if (pageResults.empty()) break;
        if (allItems.size() >= kMaxCrawlItems) break;
    }

    if (progressTarget.IsValid()) {
        BMessage progress(MSG_DLNA_CRAWL_PROGRESS);
        progress.AddInt32("count", (int32)allItems.size());
        progress.AddString("phase", "audio_done");
        progress.AddString("server", server.friendlyName);
        progressTarget.SendMessage(&progress);
    }

    bool hasMissingAlbumArt = false;
    for (const auto& item : allItems) {
        if (item.albumArtUrl.IsEmpty()) {
            hasMissingAlbumArt = true;
            break;
        }
    }

    if (hasMissingAlbumArt) {
        std::vector<DLNABrowseItem> albumContainers;
        if (progressTarget.IsValid()) {
            BMessage progress(MSG_DLNA_CRAWL_PROGRESS);
            progress.AddInt32("count", (int32)allItems.size());
            progress.AddString("phase", "cover_start");
            progress.AddString("server", server.friendlyName);
            progressTarget.SendMessage(&progress);
        }

        if (_SearchAlbumContainers(server, albumContainers, progressTarget) == B_OK &&
            !albumContainers.empty()) {
            std::map<std::string, BString> artByContainerId;
            std::map<std::string, BString> artByAlbumKey;

            for (const auto& album : albumContainers) {
                if (album.albumArtUrl.IsEmpty())
                    continue;

                if (!album.id.IsEmpty())
                    artByContainerId[album.id.String()] = album.albumArtUrl;

                if (!album.title.IsEmpty()) {
                    std::string key = album.artist.String();
                    key += "\n";
                    key += album.title.String();
                    artByAlbumKey[key] = album.albumArtUrl;
                }
            }

            for (auto& item : allItems) {
                if (!item.albumArtUrl.IsEmpty())
                    continue;

                if (!item.parentId.IsEmpty()) {
                    auto byParent = artByContainerId.find(item.parentId.String());
                    if (byParent != artByContainerId.end()) {
                        item.albumArtUrl = byParent->second;
                        continue;
                    }
                }

                if (!item.album.IsEmpty()) {
                    std::string key = item.artist.String();
                    key += "\n";
                    key += item.album.String();
                    auto byAlbum = artByAlbumKey.find(key);
                    if (byAlbum != artByAlbumKey.end())
                        item.albumArtUrl = byAlbum->second;
                }
            }
        }
    }

    if (progressTarget.IsValid()) {
        BMessage progress(MSG_DLNA_CRAWL_PROGRESS);
        progress.AddInt32("count", (int32)allItems.size());
        progress.AddString("phase", "finalizing");
        progress.AddString("server", server.friendlyName);
        progressTarget.SendMessage(&progress);
    }

    return B_OK;
}

/**
 * @brief Uses Search to fetch album containers so their cover art can be
 * inherited by tracks that do not expose upnp:albumArtURI themselves.
 */
status_t DLNAService::_SearchAlbumContainers(
    const DLNADevice& server, std::vector<DLNABrowseItem>& albumContainers,
    BMessenger progressTarget)
{
    if (server.contentDirUrl.IsEmpty())
        return B_NOT_ALLOWED;

    uint32 startIndex = 0;
    uint32 requestedCount = 500;
    uint32 totalMatches = 1;

    while (startIndex < totalMatches) {
        BString args;
        args << "<ContainerID>0</ContainerID>"
             << "<SearchCriteria>upnp:class derivedfrom &quot;object.container.album.musicAlbum&quot;</SearchCriteria>"
             << "<Filter>*</Filter>"
             << "<StartingIndex>" << startIndex << "</StartingIndex>"
             << "<RequestedCount>" << requestedCount << "</RequestedCount>"
             << "<SortCriteria></SortCriteria>";

        BString response;
        status_t err = _SendSoapAction(
            server.contentDirUrl,
            "urn:schemas-upnp-org:service:ContentDirectory:1", "Search",
            args, response);
        if (err != B_OK)
            return albumContainers.empty() ? err : B_OK;

        BString didl = _ExtractXmlTag(response, "Result");
        if (didl.IsEmpty())
            return albumContainers.empty() ? B_ERROR : B_OK;

        BString totalStr = _ExtractXmlTag(response, "TotalMatches");
        if (!totalStr.IsEmpty())
            totalMatches = (uint32)atoi(totalStr.String());
        else
            totalMatches = startIndex + requestedCount;

        didl = _UnescapeXml(didl);
        std::vector<DLNABrowseItem> pageResults;
        _ParseDIDLLite(didl, pageResults);

        for (const auto& item : pageResults) {
            if (item.isContainer &&
                item.upnpClass.StartsWith("object.container.album.musicAlbum")) {
                albumContainers.push_back(item);
            }
        }

        BString returnedStr = _ExtractXmlTag(response, "NumberReturned");
        uint32 numberReturned = returnedStr.IsEmpty()
                                    ? (uint32)pageResults.size()
                                    : (uint32)atoi(returnedStr.String());
        if (numberReturned == 0)
            break;

        startIndex += numberReturned;

        if (progressTarget.IsValid()) {
            BMessage progress(MSG_DLNA_CRAWL_PROGRESS);
            progress.AddInt32("count", (int32)albumContainers.size());
            progress.AddInt32("total", (int32)totalMatches);
            progress.AddString("phase", "cover");
            progress.AddString("server", server.friendlyName);
            progressTarget.SendMessage(&progress);
        }

        if (pageResults.empty())
            break;
    }

    return B_OK;
}

/**
 * @brief Recursively browses all containers on a MediaServer using BFS.
 *
 * Traverses the server's ContentDirectory starting from the root ("0"),
 * collecting all playable audio items. Containers are expanded breadth-first.
 * Progress is reported via MSG_DLNA_CRAWL_PROGRESS every 50 items.
 */
status_t DLNAService::BrowseAll(const DLNADevice& server,
                                 std::vector<DLNABrowseItem>& allItems,
                                 BMessenger progressTarget)
{
    /// 1. Try the fast Search method first
    status_t err = _SearchAudio(server, allItems, progressTarget);
    if (err == B_OK && !allItems.empty()) {
        DEBUG_PRINT("BrowseAll: SearchAudio succeeded, found %zu items\n", allItems.size());
        return B_OK;
    }

    DEBUG_PRINT("BrowseAll: SearchAudio failed or empty (%s). Falling back to BFS crawl...\n", strerror(err));
    allItems.clear();

    if (progressTarget.IsValid()) {
        BMessage progress(MSG_DLNA_CRAWL_PROGRESS);
        progress.AddInt32("count", 0);
        progress.AddString("phase", "browse");
        progress.AddString("server", server.friendlyName);
        progressTarget.SendMessage(&progress);
    }

    struct QueueEntry {
        BString objectId;
        int32 depth;
    };

    std::deque<QueueEntry> queue;
    queue.push_back({"0", 0});

    std::set<BString> seenItems;
    std::map<std::string, BString> artByContainerId;
    int32 lastReported = 0;

    while (!queue.empty()) {
        QueueEntry entry = queue.front();
        queue.pop_front();

        if (entry.depth > kMaxCrawlDepth)
            continue;

        std::vector<DLNABrowseItem> results;
        err = Browse(server, entry.objectId, results);
        if (err != B_OK) {
            DEBUG_PRINT("BrowseAll: failed at '%s': %s\n",
                        entry.objectId.String(), strerror(err));
            continue;
        }

        for (const auto& item : results) {
            if (item.isContainer) {

                if (item.upnpClass.StartsWith("object.container.video") ||
                    item.upnpClass.StartsWith("object.container.image")) {
                    continue;
                }
                if (!item.id.IsEmpty() && !item.albumArtUrl.IsEmpty())
                    artByContainerId[item.id.String()] = item.albumArtUrl;
                queue.push_back({item.id, entry.depth + 1});
            } else {

                if (!item.upnpClass.StartsWith("object.item.audioItem")) {
                    continue;
                }
                DLNABrowseItem resolvedItem = item;
                if (resolvedItem.albumArtUrl.IsEmpty() &&
                    !resolvedItem.parentId.IsEmpty()) {
                    auto byParent =
                        artByContainerId.find(resolvedItem.parentId.String());
                    if (byParent != artByContainerId.end())
                        resolvedItem.albumArtUrl = byParent->second;
                }
                BString dedupKey = _DlnaDedupKey(resolvedItem);
                if (seenItems.find(dedupKey) == seenItems.end()) {
                    seenItems.insert(dedupKey);
                    allItems.push_back(resolvedItem);
                }
            }
        }

        if ((int32)allItems.size() - lastReported >= 50) {
            lastReported = (int32)allItems.size();
            if (progressTarget.IsValid()) {
                BMessage progress(MSG_DLNA_CRAWL_PROGRESS);
                progress.AddInt32("count", lastReported);
                progress.AddString("phase", "browse");
                progress.AddString("server", server.friendlyName);
                progressTarget.SendMessage(&progress);
            }
        }

        if ((int32)allItems.size() >= kMaxCrawlItems) {
            DEBUG_PRINT("BrowseAll: item limit reached (%ld)\n",
                        (long)allItems.size());
            break;
        }
    }

    DEBUG_PRINT("BrowseAll: finished with %zu items\n", allItems.size());
    return B_OK;
}

static const uint32 kCacheMagic = 'DLCA';
static const uint32 kCacheVersion = 1;

/**
 * @brief Derives the cache file path for a given server UUID.
 */
BPath DLNAService::_CachePath(const BString& uuid) const
{
    BPath path;
    find_directory(B_USER_SETTINGS_DIRECTORY, &path);
    path.Append("BeTon");

    BDirectory dir(path.Path());
    if (dir.InitCheck() != B_OK)
        create_directory(path.Path(), 0755);

    BString filename("dlna_");
    filename << uuid << ".cache";
    path.Append(filename.String());
    return path;
}

/**
 * @brief Helper to write a length-prefixed string to a file.
 */
static status_t _WriteString(BFile& file, const BString& str)
{
    uint16 len = (uint16)str.Length();
    if (file.Write(&len, sizeof(len)) != sizeof(len))
        return B_IO_ERROR;
    if (len > 0 && file.Write(str.String(), len) != len)
        return B_IO_ERROR;
    return B_OK;
}

/**
 * @brief Helper to read a length-prefixed string from a file.
 */
static status_t _ReadString(BFile& file, BString& str)
{
    uint16 len = 0;
    if (file.Read(&len, sizeof(len)) != sizeof(len))
        return B_IO_ERROR;
    if (len == 0) {
        str = "";
        return B_OK;
    }
    char* buf = str.LockBuffer(len + 1);
    if (!buf)
        return B_NO_MEMORY;
    ssize_t read = file.Read(buf, len);
    buf[len] = '\0';
    str.UnlockBuffer(len);
    if (read != len)
        return B_IO_ERROR;
    return B_OK;
}

/**
 * @brief Saves the browse results for a server to a binary cache file.
 */
status_t DLNAService::SaveServerCache(const BString& uuid,
                                       const std::vector<DLNABrowseItem>& items)
{
    BPath cachePath = _CachePath(uuid);
    BFile file(cachePath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() != B_OK) {
        DEBUG_PRINT("Cache: cannot create '%s'\n", cachePath.Path());
        return file.InitCheck();
    }

    uint32 magic = kCacheMagic;
    uint32 version = kCacheVersion;
    uint32 count = (uint32)items.size();
    int64 timestamp = system_time();

    file.Write(&magic, sizeof(magic));
    file.Write(&version, sizeof(version));
    file.Write(&count, sizeof(count));
    file.Write(&timestamp, sizeof(timestamp));

    for (const auto& item : items) {
        _WriteString(file, item.title);
        _WriteString(file, item.artist);
        _WriteString(file, item.album);
        _WriteString(file, item.genre);
        _WriteString(file, item.resourceUrl);
        _WriteString(file, item.mimeType);
        _WriteString(file, item.duration);
        _WriteString(file, item.albumArtUrl);
    }

    DEBUG_PRINT("Cache: saved %lu items to '%s'\n",
                (unsigned long)items.size(), cachePath.Path());
    return B_OK;
}

/**
 * @brief Loads the browse results for a server from its binary cache file.
 */
status_t DLNAService::LoadServerCache(const BString& uuid,
                                       std::vector<DLNABrowseItem>& items)
{
    BPath cachePath = _CachePath(uuid);
    BFile file(cachePath.Path(), B_READ_ONLY);
    if (file.InitCheck() != B_OK)
        return B_ENTRY_NOT_FOUND;

    uint32 magic = 0, version = 0, count = 0;
    int64 timestamp = 0;

    if (file.Read(&magic, sizeof(magic)) != sizeof(magic) || magic != kCacheMagic)
        return B_BAD_DATA;
    if (file.Read(&version, sizeof(version)) != sizeof(version) || version != kCacheVersion)
        return B_BAD_DATA;
    if (file.Read(&count, sizeof(count)) != sizeof(count))
        return B_BAD_DATA;
    if (file.Read(&timestamp, sizeof(timestamp)) != sizeof(timestamp))
        return B_BAD_DATA;

    items.clear();
    items.reserve(count);

    for (uint32 i = 0; i < count; i++) {
        DLNABrowseItem item;
        if (_ReadString(file, item.title) != B_OK) break;
        if (_ReadString(file, item.artist) != B_OK) break;
        if (_ReadString(file, item.album) != B_OK) break;
        if (_ReadString(file, item.genre) != B_OK) break;
        if (_ReadString(file, item.resourceUrl) != B_OK) break;
        if (_ReadString(file, item.mimeType) != B_OK) break;
        if (_ReadString(file, item.duration) != B_OK) break;
        if (_ReadString(file, item.albumArtUrl) != B_OK) break;
        item.isContainer = false;
        items.push_back(item);
    }

    DEBUG_PRINT("Cache: loaded %zu items from '%s'\n",
                items.size(), cachePath.Path());
    return B_OK;
}

/**
 * @brief Deletes the cache file for a specific server.
 */
status_t DLNAService::DeleteServerCache(const BString& uuid) {
    BPath path = _CachePath(uuid);
    BEntry entry(path.Path());
    if (!entry.Exists())
        return B_ENTRY_NOT_FOUND;
    return entry.Remove();
}
