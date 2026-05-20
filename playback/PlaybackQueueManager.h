#ifndef BETON_PLAYBACK_QUEUE_MANAGER_H
#define BETON_PLAYBACK_QUEUE_MANAGER_H

#include <SupportDefs.h>
#include <random>
#include <vector>

class MediaTableView;
class MainWindow;

/**
 * @class PlaybackQueueManager
 * @brief Controls queue navigation, repeat/shuffle, and source-aware next/prev.
 */
class PlaybackQueueManager {
public:
  /** @brief Repeat policy for queue progression. */
  enum RepeatMode { RepeatOff, RepeatAll, RepeatOne };
  
  /** @brief Logical playback source currently driving queue behavior. */
  enum PlaybackSource {
    SourceNone,
    SourceLibrary,
    SourcePlaylist,
    SourceRadio,
    SourceDLNA
  };

  /**
   * @brief Creates queue manager bound to main window state.
   * @param window Owning main window context.
   */
  explicit PlaybackQueueManager(MainWindow *window);

  /** @brief Builds queue from current selection and starts playback. */
  void PlaySelectedQueue();
  
  /** @brief Plays next item according to source, repeat, and shuffle rules. */
  void PlayNext();
  
  /** @brief Plays previous item according to source and shuffle history. */
  void PlayPrevious();
  
  /** @brief Handles end-of-track progression. */
  void HandleTrackEnded();
  
  /** @brief Toggles shuffle mode and updates shuffle icon state. */
  void ToggleShuffle();
  
  /** @brief Cycles repeat mode and updates repeat icon state. */
  void ToggleRepeat();
  
  /** @brief Returns whether shuffle mode is enabled. */
  bool ShuffleEnabled() const;
  
  /** @brief Returns repeat mode as persisted integer value. */
  int32 RepeatModeValue() const;
  
  /** @brief Sets shuffle mode from persisted/apply state. */
  void SetShuffleEnabled(bool enabled);
  
  /** @brief Sets repeat mode from persisted/apply state. */
  void SetRepeatModeValue(int32 mode);
  
  /** @brief Sets active playback source for navigation behavior. */
  void SetActiveSource(PlaybackSource source);
  
  /** @brief Plays current selection depending on active app mode. */
  void PlayCurrentSelection();
  
  /** @brief Plays a concrete local row selection from content view. */
  void PlayLocalSelection(MediaTableView *view, int32 rowIndex);

private:
  /** @brief Returns random queue index in range `[0, count)`. */
  int32 _RandomIndex(int32 count);
  /** @brief Updates shuffle button icon to current shuffle state. */
  void _UpdateShuffleIcon();
  /** @brief Updates repeat button icon to current repeat state. */
  void _UpdateRepeatIcon();
  /** @brief Ensures play/pause button shows pause while playing. */
  void _SetPlayPauseIcon();
  /** @brief Clears active DLNA play queue context. */
  void _ClearDlnaQueue();
  /** @brief Plays adjacent DLNA item; returns `true` if handled. */
  bool _PlayAdjacentDlna(int32 direction);
  /** @brief Plays adjacent radio station; returns `true` if handled. */
  bool _PlayAdjacentRadio(int32 direction);

  /** @brief Owning main window and shared playback/UI state access. */
  MainWindow *fWindow;
  
  /** @brief Current source context for queue-navigation decisions. */
  PlaybackSource fActiveSource = SourceNone;
  
  /** @brief Shuffle mode flag. */
  bool fShuffleEnabled = false;
  
  /** @brief Active repeat mode. */
  RepeatMode fRepeatMode = RepeatOff;
  
  /** @brief Random generator used for shuffle picks. */
  std::mt19937 fRng{std::random_device{}()};
  
  /** @brief Stack-like history for shuffle previous navigation. */
  std::vector<int32> fShuffleHistory;
};

#endif // BETON_PLAYBACK_QUEUE_MANAGER_H
