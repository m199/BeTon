#ifndef BETON_DLNA_DEVICE_INFO_H
#define BETON_DLNA_DEVICE_INFO_H

#include <String.h>
#include <OS.h>

/**
 * @struct DLNADevice
 * @brief Represents a discovered UPnP device (MediaServer or MediaRenderer).
 *
 * Stores identity, service control URLs, and a last-seen timestamp
 * for automatic expiration of devices that go offline.
 */
struct DLNADevice {
    /**
     * @brief The type of UPnP device.
     */
    enum Type {
        MediaServer,   ///< Content provider (NAS, media server software)
        MediaRenderer  ///< Playback target (speaker, TV, receiver)
    };

    Type    type;            ///< Device type (Server or Renderer).
    BString friendlyName;    ///< Human-readable name (e.g. "Living Room NAS").
    BString uuid;            ///< Unique Device Name (UDN) from the device description.
    BString location;        ///< URL of the device description XML.

    /** @name MediaServer service URLs */
    ///@{
    BString contentDirUrl;   ///< ContentDirectory control URL (Server only).
    ///@}

    /** @name MediaRenderer service URLs */
    ///@{
    BString avTransportUrl;  ///< AVTransport control URL (Renderer only).
    BString renderingCtlUrl; ///< RenderingControl control URL (Renderer only).
    ///@}

    bigtime_t lastSeen;      ///< system_time() when last seen via SSDP.

    DLNADevice()
        : type(MediaServer),
          lastSeen(0)
    {
    }
};

#endif // BETON_DLNA_DEVICE_INFO_H
