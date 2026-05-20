#ifndef BETON_DLNA_SERVICE_H
#define BETON_DLNA_SERVICE_H

#include "DLNADeviceInfo.h"

#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <String.h>
#include <UrlContext.h>
#include <atomic>
#include <vector>

/**
 * @struct DLNABrowseItem
 * @brief Represents a single item or container returned by ContentDirectory Browse.
 */
struct DLNABrowseItem {
    BString id;           ///< ObjectID for further browsing.
    BString refId;        ///< Canonical ObjectID if this item is a reference.
    BString parentId;     ///< Parent container ObjectID.
    BString title;        ///< dc:title
    BString artist;       ///< dc:creator or upnp:artist
    BString album;        ///< upnp:album
    BString genre;        ///< upnp:genre
    BString upnpClass;    ///< upnp:class (e.g. object.item.audioItem)
    BString albumArtUrl;  ///< upnp:albumArtURI
    BString duration;     ///< Duration string (H:MM:SS).
    BString resourceSize; ///< Byte size from the res@size attribute.
    BString resourceUrl;  ///< HTTP URL of the media resource (empty for containers).
    BString mimeType;     ///< MIME type from protocolInfo.
    bool    isContainer;  ///< True if this is a folder/container, false if playable item.

    DLNABrowseItem() : isContainer(false) {}
};

/**
 * @class DLNAService
 * @brief Manages UPnP/DLNA device discovery, server browsing, and renderer control.
 *
 * Provides two main functions:
 * - **Input:** Discover MediaServers and browse their ContentDirectory to find
 *   playable audio items. The returned URLs can be passed to PlayUrl().
 * - **Output:** Discover MediaRenderers and control them via AVTransport and
 *   RenderingControl SOAP actions (Play, Stop, Pause, Volume).
 *
 * Discovery uses SSDP multicast (239.255.255.250:1900) via POSIX UDP sockets.
 * SOAP actions use BUrlRequest for HTTP transport.
 */
class DLNAService {
public:
    /**
     * @brief Discovery mode for SSDP.
     */
    enum DiscoveryMode {
        Auto,    ///< Discovery starts automatically on app launch.
        Manual,  ///< Discovery only when user clicks "Refresh".
        Off      ///< No network activity.
    };

    /**
     * @brief Constructs the DLNAService.
     * @param target Messenger for UI notifications (MSG_DLNA_DEVICE_FOUND, etc.)
     */
    DLNAService(BMessenger target);
    ~DLNAService();

    /** @name Discovery */
    ///@{

    /**
     * @brief Starts the SSDP discovery thread.
     */
    void StartDiscovery();

    /**
     * @brief Stops the discovery thread and cleans up.
     */
    void StopDiscovery();

    /**
     * @brief Returns all currently known devices.
     */
    const std::vector<DLNADevice>& Devices() const { return fDevices; }

    ///@}

    /** @name Input: MediaServer Browsing */
    ///@{

    /**
     * @brief Returns only MediaServer devices.
     */
    std::vector<DLNADevice> Servers() const;

    /**
     * @brief Browses a ContentDirectory container on a MediaServer.
     * @param server The server to browse.
     * @param objectId The container ObjectID ("0" for root).
     * @param results Output vector of browse items.
     * @return B_OK on success, error code otherwise.
     */
    status_t Browse(const DLNADevice& server, const BString& objectId,
                    std::vector<DLNABrowseItem>& results);

    /**
     * @brief Recursively browses all containers on a MediaServer.
     *
     * Performs a breadth-first traversal starting from the root container,
     * collecting all playable audio items into a flat list. Sends periodic
     * MSG_DLNA_CRAWL_PROGRESS messages to the target.
     *
     * @param server The server to crawl.
     * @param allItems Output vector of all playable items found.
     * @param progressTarget Messenger to receive progress updates.
     * @return B_OK on success, error code otherwise.
     */
    status_t BrowseAll(const DLNADevice& server,
                       std::vector<DLNABrowseItem>& allItems,
                       BMessenger progressTarget);

    ///@}

    /** @name Server Cache */
    ///@{

    /**
     * @brief Saves a server's browse results to a binary disk cache.
     * @param uuid Server UUID used to derive the cache filename.
     * @param items The items to persist.
     * @return B_OK on success.
     */
    status_t SaveServerCache(const BString& uuid,
                             const std::vector<DLNABrowseItem>& items);

    /**
     * @brief Loads a server's browse results from the binary disk cache.
     * @param uuid Server UUID used to derive the cache filename.
     * @param items Output vector populated from cache.
     * @return B_OK on success, B_ENTRY_NOT_FOUND if no cache exists.
     */
    status_t LoadServerCache(const BString& uuid,
                             std::vector<DLNABrowseItem>& items);

    /**
     * @brief Deletes the cache file for a specific server.
     * @param uuid Server UUID.
     * @return B_OK on success, B_ENTRY_NOT_FOUND if no cache exists.
     */
    status_t DeleteServerCache(const BString& uuid);

    ///@}

    /** @name Output: MediaRenderer Control */
    ///@{

    /**
     * @brief Returns only MediaRenderer devices.
     */
    std::vector<DLNADevice> Renderers() const;

    /**
     * @brief Returns the currently active renderer, or nullptr for local output.
     */
    const DLNADevice* ActiveRenderer() const { return fHasActiveRenderer ? &fActiveRenderer : nullptr; }

    /**
     * @brief Sets the active renderer by UUID. Empty string selects local output.
     * @param uuid UUID of the renderer, or empty for local.
     */
    void SetActiveRenderer(const BString& uuid);

    /**
     * @brief Returns true if output is directed to a DLNA renderer.
     */
    bool IsRemoteOutput() const { return fHasActiveRenderer; }

    /**
     * @brief Sets the media URI on the active renderer.
     * @param uri HTTP URL of the media to play.
     * @param title Display title for the renderer.
     * @return B_OK on success.
     */
    status_t SetAVTransportURI(const BString& uri, const BString& title);

    /**
     * @brief Sends Play command to the active renderer.
     */
    status_t RendererPlay();

    /**
     * @brief Sends Stop command to the active renderer.
     */
    status_t RendererStop();

    /**
     * @brief Sends Pause command to the active renderer.
     */
    status_t RendererPause();

    /**
     * @brief Seeks the active renderer to the given position.
     * @param position Time in microseconds.
     */
    status_t RendererSeek(bigtime_t position);

    bigtime_t GetCurrentPosition() const { return fCurrentPosition; }
    bigtime_t GetCurrentDuration() const { return fCurrentDuration; }

    /**
     * @brief Sets the volume on the active renderer.
     * @param percent Volume level 0-100.
     */
    status_t SetRendererVolume(int32 percent);

    /**
     * @brief Gets the current volume from the active renderer.
     * @param percent Output: volume level 0-100.
     */
    status_t GetRendererVolume(int32& percent);

    ///@}

    /** @name Settings */
    ///@{
    void SetDiscoveryMode(DiscoveryMode mode) { fDiscoveryMode = mode; }
    DiscoveryMode GetDiscoveryMode() const { return fDiscoveryMode; }
    BPrivate::Network::BUrlContext* GetUrlContext() { return &fUrlContext; }
    ///@}

private:
    /** @name Discovery internals */
    ///@{
    static int32 _DiscoveryThreadEntry(void* arg);
    void _DiscoveryLoop();
    void _SendMSearch(int sock);
    void _ProcessSsdpResponse(const char* data, size_t len, const struct sockaddr_in& from);
    status_t _FetchDeviceDescription(const BString& location, DLNADevice& dev);
    void _ParseDeviceXml(const BString& xml, DLNADevice& dev);
    void _PurgeStaleDevices();
    ///@}

    /** @name Polling internals */
    ///@{
    static int32 _PositionPollThreadEntry(void* arg);
    void _PositionPollLoop();
    void _StartPositionPolling();
    void _StopPositionPolling();
    bigtime_t _ParseUpnpTime(const BString& timeStr) const;
    ///@}

    /** @name SOAP communication */
    ///@{
    status_t _SendSoapAction(const BString& controlUrl,
                             const BString& serviceUrn,
                             const BString& action,
                             const BString& bodyArgs,
                             BString& response);
    ///@}

    /** @name ContentDirectory parsing */
    ///@{
    status_t _SearchAudio(const DLNADevice& server, std::vector<DLNABrowseItem>& allItems, BMessenger progressTarget);
    status_t _SearchAlbumContainers(const DLNADevice& server,
                                    std::vector<DLNABrowseItem>& albumContainers,
                                    BMessenger progressTarget);
    void _ParseDIDLLite(const BString& didl, std::vector<DLNABrowseItem>& items);
    BString _ExtractXmlTag(const BString& xml, const char* tagName) const;
    BString _ExtractXmlTagFull(const BString& xml, const char* openTag,
                               const char* closeTag) const;
    BString _UnescapeXml(const BString& escaped) const;
    BString _ResolveRelativeUrl(const BString& base, const BString& relative) const;
    ///@}

    /** @name Cache internals */
    ///@{
    BPath _CachePath(const BString& uuid) const;
    ///@}

    /** @name State */
    ///@{
    BMessenger fTarget;
    std::vector<DLNADevice> fDevices;
    DLNADevice fActiveRenderer;
    bool fHasActiveRenderer = false;
    DiscoveryMode fDiscoveryMode;
    thread_id fDiscoveryThread;
    sem_id fDiscoveryWakeSem;
    std::atomic<bool> fRunning;
    BPrivate::Network::BUrlContext fUrlContext;

    thread_id fPositionPollThread = -1;
    std::atomic<bool> fPositionPolling{false};
    std::atomic<bigtime_t> fCurrentPosition{0};
    std::atomic<bigtime_t> fCurrentDuration{0};
    std::atomic<int32> fCurrentVolume{-1};
    ///@}

    static const bigtime_t kDeviceTimeout = 120000000LL; ///< 120 seconds
    static const bigtime_t kDiscoveryInterval = 300000000LL; ///< 5 minutes
    static const int32 kMaxCrawlItems = 100000; ///< Safety limit for recursive crawl
    static const int32 kMaxCrawlDepth = 10; ///< Maximum directory nesting depth
};

#endif // BETON_DLNA_SERVICE_H
