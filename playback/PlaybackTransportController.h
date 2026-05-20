#ifndef BETON_PLAYBACK_TRANSPORT_CONTROLLER_H
#define BETON_PLAYBACK_TRANSPORT_CONTROLLER_H

#include "MediaItem.h"

#include <String.h>

class BMessage;
class MainWindow;

/**
 * @class PlaybackTransportController
 * @brief Handles transport controls and now-playing state synchronization.
 */
class PlaybackTransportController {
public:
  /**
   * @brief Creates transport controller bound to main window state.
   * @param window Owning main window context.
   */
  explicit PlaybackTransportController(MainWindow *window);

  /** @brief Toggles play/pause according to current engine state. */
  void TogglePlayPause();
  
  /** @brief Toggles pause/resume while preserving current track state. */
  void TogglePause();
  
  /** @brief Stops playback and resets transport-related UI state. */
  void StopPlayback();
  
  /** @brief Applies a seek request from seek-bar UI message. */
  void HandleSeekRequest(BMessage *msg);
  
  /** @brief Updates seek-bar time values from current engine position. */
  void UpdatePlaybackTime();
  
  /** @brief Toggles mute state and updates volume UI controls. */
  void ToggleMute();
  
  /** @brief Applies current volume slider value to playback output. */
  void ApplyVolumeChange();
  
  /** @brief Applies remote DLNA volume updates to local UI state. */
  void ApplyDlnaVolumeUpdate(BMessage *msg);
  
  /** @brief Updates now-playing metadata context from playback notification. */
  void HandleNowPlaying(BMessage *msg);
  
  /** @brief Returns whether cached now-playing item data is valid. */
  bool NowPlayingIsValid() const;
  
  /** @brief Returns cached now-playing item if available, otherwise `nullptr`. */
  const MediaItem *NowPlayingItem() const;
  
  /** @brief Returns currently tracked now-playing path or stream URL. */
  const BString &NowPlayingPath() const;

private:
  /** @brief Normalizes and applies now-playing label/item state. */
  void _UpdateNowPlayingState(const BString &path, bool isStream,
                              BMessage *msg);
                              
  /** @brief Sets transport play icon state on play/pause button. */
  void _SetPlayIcon();
  
  /** @brief Sets transport pause icon state on play/pause button. */
  void _SetPauseIcon();

  /** @brief Owning main window and shared playback/UI state access. */
  MainWindow *fWindow;
  
  /** @brief Path/URL currently tracked as now playing. */
  BString fNowPlayingPath;
  
  /** @brief Cached media item metadata for now-playing entry. */
  MediaItem fNowPlayingItem;
  
  /** @brief Indicates whether `fNowPlayingItem` contains valid data. */
  bool fNowPlayingIsValid = false;
};

#endif // BETON_PLAYBACK_TRANSPORT_CONTROLLER_H
