#ifndef BETON_NETWORK_AUDIO_STREAM_IO_H
#define BETON_NETWORK_AUDIO_STREAM_IO_H

#include <DataIO.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <Messenger.h>
#include <String.h>
#include <OS.h>
#include <atomic>
#include <vector>

#include <UrlContext.h>

struct AVDictionary;

/**
 * @class NetworkAudioStreamIO
 * @brief Handles network audio streaming through FFmpeg and a PCM ring buffer.
 *
 * FFmpeg opens HTTP/HTTPS, ICY/Icecast and HLS streams, decodes them to float
 * PCM, and writes the audio into the ring buffer. BSoundPlayer reads PCM
 * directly through ReadPcm().
 */
class NetworkAudioStreamIO : public BPositionIO {
public:
    /**
     * @brief Streaming backend mode.
     */
    enum Mode { MODE_ICY, MODE_HLS, MODE_DLNA };

    /**
     * @brief Constructs the streaming IO object.
     * @param target Messenger for metadata/status notifications.
     * @param context Optional URL context for network requests.
     */
    NetworkAudioStreamIO(BMessenger target, BPrivate::Network::BUrlContext* context = nullptr);

    /**
     * @brief Stops streaming and releases ring-buffer/semaphore resources.
     */
    virtual ~NetworkAudioStreamIO();

    /**
     * @brief Opens a remote stream and starts decode thread.
     * @param url Stream URL.
     * @param mode Stream mode (ICY/HLS/DLNA).
     * @param context Optional URL context override.
     * @return B_OK on success.
     */
    status_t Open(const BString& url, Mode mode, BPrivate::Network::BUrlContext* context = NULL);

    /**
     * @brief Stops active streaming session and joins worker thread.
     */
    void     Stop();

    /**
     * @brief Requests a seek to absolute media time.
     * @param position Target position in microseconds.
     * @return B_OK if seek request was accepted.
     */
    status_t SeekToTime(bigtime_t position);

    bool     IsRunning() const { return fRunning; }
    bool     IsRequestRunning() const { return fRequestRunning; }
    Mode     GetMode() const { return fMode; }

    /** Legacy compatibility; FFmpeg streams should use WaitForFormat(). */
    status_t WaitForData(size_t minBytes, bigtime_t timeoutUs) const;

    /**
     * @brief Waits for FFmpeg to determine the decoded audio format.
     * @param format Output: the decoded audio format.
     * @param timeoutUs Maximum wait time in microseconds.
     * @return B_OK if format was determined.
     */
    status_t WaitForFormat(media_raw_audio_format* format, bigtime_t timeoutUs) const;

    /**
     * @brief Reads decoded PCM audio from the ring buffer.
     * @param buffer Output buffer for PCM data.
     * @param size Number of bytes to read.
     * @return Number of bytes read, or B_ERROR.
     */
    ssize_t ReadPcm(void* buffer, size_t size);

    /** @name BPositionIO interface (legacy compatibility only) */
    ///@{
    virtual ssize_t  ReadAt(off_t position, void* buffer, size_t size) override;
    virtual ssize_t  WriteAt(off_t position, const void* buffer, size_t size) override;
    virtual off_t    Seek(off_t position, uint32 seekMode) override;
    virtual off_t    Position() const override;
    virtual status_t GetSize(off_t* size) const override;
    virtual ssize_t  Read(void* buffer, size_t size) override;
    virtual ssize_t  Write(const void* buffer, size_t size) override;
    ///@}

private:
    /** @name ICY Mode Logic */
    ///@{
    void _ParseIcyMeta(const BString& raw);
    void _NotifyMetadata(const BString& title, const BString& url);
    void _NotifyMetadata(const BString& artist, const BString& title,
                         const BString& album, const BString& url);
    ///@}

    /** @name FFmpeg Stream Logic */
    ///@{
    static int32 _FfmpegThreadEntry(void* arg);
    void _FfmpegLoop();
    void _ProcessFfmpegMetadata(AVDictionary* dict);
    void _ProcessHlsTimedMetadata(const uint8* data, size_t size);
    void _PollFfmpegIcyMetadata(void* formatContext);
    void _PollHlsPlaylistMetadata();
    void _QueueHlsSegmentMetadata(const uint8* data, size_t size);
    void _DispatchDueHlsMetadata();
    static int _FfmpegInterruptCallback(void* arg);
    ///@}

    /** @name Ring Buffer */
    ///@{
    static const size_t kBufferCapacity = 4 * 1024 * 1024; ///< 4 MB
    uint8*   fBuffer;
    std::atomic<off_t>   fWritePos;
    std::atomic<off_t>   fTotalWritten;
    std::atomic<off_t>   fReadPos;
    std::atomic<off_t>   fValidStart;
    sem_id   fDataReady;
    ///@}

    /** @name State */
    ///@{
    BMessenger                fTarget;
    Mode                      fMode;
    BPrivate::Network::BUrlContext* fContext;
    std::atomic<bool>         fRunning;
    std::atomic<bool>         fRequestRunning;
    off_t                     fExpectedSize;
    BString                   fUrl;
    ///@}

    /** @name ICY State */
    ///@{
    BString fLastStreamTitle;
    BString fLastStreamUrl;
    BString fLastIcyPacket;
    BString fLastHlsMetadataSegment;
    ///@}

    /** @name FFmpeg State */
    ///@{
    struct HlsTimedMetadataEvent {
        bigtime_t dueTime;
        BString artist;
        BString title;
        BString album;
        BString url;
    };

    thread_id               fFfmpegThread;
    media_raw_audio_format  fHlsFormat;       ///< Decoded audio format
    sem_id                  fHlsFormatReady;  ///< Signaled when format known
    bool                    fHlsFormatKnown;  ///< True after decoder setup
    bigtime_t               fLastHlsMetadataPoll;
    std::atomic<bigtime_t>  fLastPcmReadDebugLog;
    std::atomic<bigtime_t>  fFfmpegReadDeadline;
    std::atomic<bigtime_t>  fPendingSeekTime;
    std::vector<HlsTimedMetadataEvent> fPendingHlsMetadata;
    ///@}
};

#endif // BETON_NETWORK_AUDIO_STREAM_IO_H
