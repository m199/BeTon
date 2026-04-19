#ifndef MEDIA_PLAYBACK_CONTROLLER_H
#define MEDIA_PLAYBACK_CONTROLLER_H

#include "Messages.h"

#include <MediaFile.h>
#include <MediaTrack.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <SoundPlayer.h>
#include <atomic>
#include <string>
#include <vector>

/**
 * @class MediaPlaybackController
 * @brief Manages audio playback, queue management, and playback state.
 *
 * Handles loading media files, decoding audio frames via BMediaTrack,
 * and playing them using BSoundPlayer. Manages a playback queue and
 * supports basic controls (play, pause, next, prev, seek, volume).
 *
 * Uses atomic flags to coordinate between the UI thread and the real-time
 * audio callback thread.
 */
class MediaPlaybackController {
public:
  MediaPlaybackController();
  ~MediaPlaybackController();

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
  void SetVolume(float percent);    ///< Sets volume (0.0 - 1.0).
  void Play(size_t trackIndex = 0); ///< Plays track at specified queue index.
  void Pause();                     ///< Pauses playback.
  void Resume();                    ///< Resumes playback.
  void Stop();                      ///< Stops playback and resets state.
  void PlayNext();                  ///< Advances to next track in queue.
  void PlayPrev();                  ///< Returns to previous track.
  void SeekTo(bigtime_t pos);       ///< Seeks to position in microseconds.
  ///@}

  /** @name State Queries */
  ///@{
  bool IsPlaying() const;     ///< True if playing and not paused.
  bool IsPaused() const;      ///< True if paused.
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
  bigtime_t Duration() const; ///< Duration of current track in microseconds.
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

  /** @name Media Kit Objects */
  ///@{
  BSoundPlayer *fPlayer = nullptr;
  BMediaFile *fMediaFile = nullptr;
  BMediaTrack *fTrack = nullptr;
  ///@}

  /** @name Playback Position, Volume and Index */
  ///@{
  bigtime_t fCurrentPos = 0;
  bigtime_t fDuration = 0;
  float fVolume = 1.0f;
  size_t fCurrentIdx = 0;
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
  ///@}

  /** @name Notification */
  ///@{
  BMessageRunner *fUpdateRunner = nullptr;
  BMessenger fTarget;
  ///@}
};

#endif // MEDIA_PLAYBACK_CONTROLLER_H
