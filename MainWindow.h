#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "CacheManager.h"
#include "LibraryViewManager.h"
#include "MediaItem.h"
#include "MediaPlaybackController.h"
#include "Messages.h"
#include "MetadataHandler.h"
#include "MusicBrainzClient.h"
#include "PlaylistManager.h"
#include "TagSync.h"

#include <Bitmap.h>
#include <Button.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MessageRunner.h>
#include <OS.h>
#include <Slider.h>
#include <StatusBar.h>
#include <String.h>
#include <StringView.h>
#include <TextControl.h>
#include <Window.h>
#include <functional>
#include <set>
#include <vector>

class SeekBarView;
class InfoPanel;
class PropertiesWindow;

/**
 * @class MainWindow
 * @brief The primary application window.
 *
 * Orchestrates the interaction between the UI components and the backend logic:
 * - **Managers:** Owns `LibraryViewManager` (browsing), `PlaylistManager`
 * (playlists), and `CacheManager` (data).
 * - **Playback:** Owns and communicates with `MediaPlaybackController`.
 * - **Metadata:** Coordinates `MetadataHandler` and `MusicBrainzClient` for
 * tagging.
 * - **UI:** Creates and lays out standard Haiku widgets (BButton, BSlider,
 * BListView, etc.).
 *
 * It handles the main BMessage loop for user actions and background worker
 * notifications.
 */
class MainWindow : public BWindow {
public:
  MainWindow();
  virtual ~MainWindow();

  void MessageReceived(BMessage *msg) override;

  /** @name Helpers used by child windows/components */
  ///@{
  BString GetPathForContentItem(int index);
  void GetPlaylistNames(BMessage &out, bool onlyWritable = true) const;
  void AddPlaylistEntry(const BString &playlistName, const BString &label,
                        const BString &fullPath);

  ///@}

  /** @name Core Update Logic */
  ///@{
  void UpdateFilteredViews();
  void UpdateFileInfo();
  void UpdateStatus(const BString &text, bool isPermanent = false);
  bool IsPlaylistSelected() const { return !fIsLibraryMode; }

  ///@}

  /** @name Settings */
  ///@{
  void LoadSettings();
  void SaveSettings();

  ///@}

  /** @name Async Logic */
  ///@{
  void RegisterWithCacheManager();
  thread_id LaunchThread(const char *name, std::function<void()> &&func);

  ///@}

private:
  static status_t _ThreadEntry(void *data);

  void _BuildUI();
  void _SelectPlaylistFolder();
  void _UpdateStatusLibrary();

  /** @name Data & State */
  ///@{
  std::vector<MediaItem> fAllItems; ///< Complete database cache
  bool fIsLibraryMode = true; ///< True = All tracks, False = Playlist view
  int32 fMbSearchGeneration =
      0; ///< Generation counter to invalidate old async searches

  ///@}

  /** @name MusicBrainz & Metadata Context */
  ///@{
  MBRelease fPendingRelease;
  CoverBlob fPendingCoverBlob;
  std::vector<BString> fPendingFiles;

  ///@}

  /** @name Cache Loading State */
  ///@{
  std::vector<MediaItem> fPendingItems;
  std::set<BString> fKnownPaths;
  int32 fCurrentIndex{0};
  int32 fNewFilesCount{0};
  bool fCacheLoaded = false;

  ///@}

  /** @name Playlist State */
  ///@{
  BString fPlaylistPath;
  BMessage fPendingPlaylistFiles;
  BString fCurrentPlaylistName;
  std::vector<BString> fPendingPlaylistOrder;

  ///@}

  /** @name Playback State */
  ///@{
  bool fShuffleEnabled = false;
  enum RepeatMode { RepeatOff, RepeatAll, RepeatOne };
  RepeatMode fRepeatMode = RepeatOff;
  bigtime_t fSongDuration{0};
  BString fLastSelectedPath; // To prevent redundant updates

  ///@}

  /** @name UI Components */
  ///@{
  BMenuBar *fMenuBar;
  BButton *fBtnPrev;
  BButton *fBtnPlayPause;
  BButton *fBtnStop;
  BButton *fBtnNext;
  BButton *fBtnShuffle;
  BButton *fBtnRepeat;
  BSlider *fVolumeSlider;
  BStatusBar *fVisualBar;
  BTextControl *fSearchField;

  BStringView *fStatusLabel;
  BStringView *fTitleView;
  SeekBarView *fSeekBar;

  bool fShowCoverArt = true;
  BMenuItem *fViewInfoItem = nullptr;
  BMenuItem *fViewCoverItem = nullptr;
  InfoPanel *fInfoPanel = nullptr;

  bool fShowTooltips = false;
  BMenuItem *fTooltipsOnItem = nullptr;
  BMenuItem *fTooltipsOffItem = nullptr;

  ///@}

  /** @name Player Icon Bitmaps */
  ///@{
  BBitmap *fIconPlay{nullptr};
  BBitmap *fIconPause{nullptr};
  BBitmap *fIconStop{nullptr};
  BBitmap *fIconNext{nullptr};
  BBitmap *fIconPrev{nullptr};
  BBitmap *fIconShuffleOff{nullptr};
  BBitmap *fIconShuffleOn{nullptr};
  BBitmap *fIconRepeatOff{nullptr};
  BBitmap *fIconRepeatAll{nullptr};
  BBitmap *fIconRepeatOne{nullptr};

  ///@}

  /** @name Color Customization */
  ///@{
  rgb_color fSeekBarColor;
  rgb_color fSelectionColor;
  bool fUseCustomSeekBarColor = false;
  bool fUseSeekBarColorForSelection = false;
  void ApplyColors();
  BMenuItem *fSelColorSystemItem = nullptr;
  BMenuItem *fSelColorMatchItem = nullptr;

  ///@}

  /** @name Child Windows */
  ///@{
  PropertiesWindow *fPropertiesWindow{nullptr};

  ///@}

  /** @name Managers & Controllers */
  ///@{
  LibraryViewManager *fLibraryManager;
  PlaylistManager *fPlaylistManager;
  MetadataHandler *fMetadataHandler;

  CacheManager *fCacheManager;
  MusicBrainzClient *fMbClient;
  MediaPlaybackController *fController;

  ///@}

  /** @name Message Runners (Timers) */
  ///@{
  BMessageRunner *fBatchRunner{nullptr};  ///< Slow-loading UI batch timer
  BMessageRunner *fUpdateRunner{nullptr}; ///< Playback progress update timer
  BMessageRunner *fStatusRunner{nullptr}; ///< Status bar clear timer
  BMessageRunner *fSearchRunner{nullptr}; ///< Search debounce timer
  ///@}
};

#endif // MAINWINDOW_H
