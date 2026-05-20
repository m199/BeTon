#ifndef BETON_AUDIO_PLAYBACK_ENGINE_H
#define BETON_AUDIO_PLAYBACK_ENGINE_H

#include "Config.h"
#include "Messages.h"

#include <Autolock.h>
#include <Locker.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <SoundPlayer.h>
#include <Url.h>
#include <UrlContext.h>
#include <atomic>
#include <string>
#include <vector>

/**
 * @class AudioPlaybackEngine
 * @brief Manages audio playback, queue management, and playback state.
 *
 * Handles loading media files, decoding audio frames via BMediaTrack,
 * and playing them using BSoundPlayer. Manages a playback queue and
 * supports basic controls (play, pause, next, prev, seek, volume).
 *
 * Uses atomic flags to coordinate between the UI thread and the real-time
 * audio callback thread.
 */
class DLNAService;
class LocalFileHttpServer;

class AudioPlaybackEngine {
public:
  AudioPlaybackEngine();
  ~AudioPlaybackEngine();

#if ENABLE_DLNA_OUTPUT
  void SetRemoteOutputManagers(DLNAService *dlna, LocalFileHttpServer *localServer);
#endif

  /**
   * @brief Sets the target messenger for playback events.
   * @param target Receiver for MSG_PLAYBACK_PREV, MSG_TRACK_ENDED, etc.
   */
  void SetTarget(BMessenger target);

  /**
   * @brief Safely shuts down the controller and playback engine.
   */
  void Shutdown();

  /** @name Playback Controls */
  ///@{
  void SetVolume(float percent);     ///< Sets volume (0.0 - 1.0).
  void Play(size_t trackIndex = 0);  ///< Plays track at specified queue index.
  void Pause();                      ///< Pauses playback.
  void Resume();                     ///< Resumes playback.
  void Stop(bool switching = false); ///< Stops playback and resets state.
  void PlayNext();                   ///< Advances to next track in queue.
  void PlayPrev();                   ///< Returns to previous track.
  void SeekTo(bigtime_t pos);        ///< Seeks to position in microseconds.
  void PlayUrl(const BUrl &url, const char *title = nullptr,
               int32 durationSeconds = 0,
               BPrivate::Network::BUrlContext *context = nullptr);
  ///@}

  /** @name State Queries */
  ///@{
  bool IsPlaying() const; ///< True if playing and not paused.
  bool IsPaused() const;  ///< True if paused.
  bool IsStreaming() const {
    return fIsStreaming.load(std::memory_order_relaxed);
  }
  int32 CurrentIndex() const; ///< Index of currently playing track.
  ///@}

  /** @name Queue Management */
  ///@{
  void SetQueue(const std::vector<std::string> &queue);
  int32 QueueSize() const { return static_cast<int32>(fQueue.size()); }
  ///@}

  /** @name Time Info */
  ///@{
  bigtime_t
  CurrentPosition() const;    ///< Current playback position in microseconds.
  bigtime_t Duration() const;
    int32 CurrentBitrate() const { return fCurrentBitrate.load(); }
    int32 CurrentSampleRate() const { return fCurrentSampleRate.load(); }
    int32 CurrentChannels() const { return fCurrentChannels.load(); }
 ///< Duration of current track in microseconds.
  ///@}

private:
  /**
   * @brief Audio callback function for BSoundPlayer.
   *
   * Reads decoded frames from the media track and fills the audio buffer.
   */
  static void _PlayBuffer(void *cookie, void *buffer, size_t size,
                          const media_raw_audio_format &format);

  void _StartTimeUpdates();
  void _StopTimeUpdates();
  void _CleanupMedia();
  void _StopLocked(bool switching);
  status_t _StartMidiAt(int32 position);
  void _StopMidi(bool unload);
  void _SilenceMidi();
  /**
   * @brief Handles BMidiSynthFile end-of-file notifications.
   */
  static void _MidiFileHook(int32 arg);

  /** @name Media Kit Objects */
  ///@{
  BSoundPlayer *fPlayer = nullptr;
  BMediaFile *fMediaFile = nullptr;
  BMediaTrack *fTrack = nullptr;
  class BMidiSynthFile *fMidiSynth = nullptr;
  std::atomic<bool> fIsMidiPlaying{false};
  std::atomic<bool> fMidiRunning{false};
  std::atomic<bool> fSuppressMidiHook{false};
  std::atomic<int32> fMidiPosition{0};
  std::atomic<int32> fMidiTickDuration{0};
  std::atomic<int32> fMidiSeekSerial{0};
  std::atomic<bigtime_t> fMidiBasePos{0};
  std::atomic<bigtime_t> fMidiStartTime{0};
  BLocker fMidiLock;
  ///@}

  /** @name Playback Position, Volume and Index */
  ///@{
  std::atomic<bigtime_t> fCurrentPos{0};
  bigtime_t fDuration = 0;
  float fVolume = 1.0f;
  std::atomic<int32> fCurrentBitrate{0};
  std::atomic<int32> fCurrentSampleRate{0};
  std::atomic<int32> fCurrentChannels{0};

  std::atomic<size_t> fCurrentIdx{0};
  ///@}

  /** @name Queue, Thread Safety and Playback*/
  ///@{
  std::vector<std::string> fQueue;
  std::atomic<bool> fPlaying{false};
  std::atomic<bool> fPaused{false};
  std::atomic<bool> fAtEnd{false};
  std::atomic<bool> fShuttingDown{false};
  std::atomic<bool> fInCallback{false};
  std::atomic<bool> fStopping{false};
  std::atomic<bool> fIsStreaming{false}; ///< True when playing a URL stream.
  class NetworkAudioStreamIO *fNetworkStream = nullptr;
  ///@}

  /** @name Notification */
  ///@{
  BMessageRunner *fUpdateRunner = nullptr;
  BMessenger fTarget;
  ///@}

  BPrivate::Network::BUrlContext fUrlContext;
  BLocker fPlayLock;

#if ENABLE_DLNA_OUTPUT
  DLNAService *fDlnaManager = nullptr;
  LocalFileHttpServer *fLocalFileHttpServer = nullptr;
  std::atomic<bool> fIsRemotePlaying{false};
#endif
};

#endif // BETON_AUDIO_PLAYBACK_ENGINE_H
