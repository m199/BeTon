#include "SettingsController.h"

#include "Config.h"
#include "MediaTableView.h"
#include "DLNAViewController.h"
#include "DLNAService.h"
#include "NowPlayingInfoPanel.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "PlaybackQueueManager.h"
#include "PlaylistSidebarView.h"
#include "PlaylistLibrary.h"
#include "RadioStationLibrary.h"

#include <Button.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <Slider.h>
#include <vector>

SettingsController::SettingsController(MainWindow *window) : fWindow(window) {}

/**
 * @brief Persists current application settings to `~/config/settings/BeTon/settings`.
 */
void SettingsController::SaveSettings() {
  if (!fWindow || !fWindow->fLibraryManager ||
      !fWindow->fLibraryManager->ContentView()) {
    return;
  }

  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
    settingsPath.Append("BeTon/settings");
    BFile file(settingsPath.Path(),
               B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() == B_OK) {
      BMessage state;
      SaveSettingsToMessage(state);
      state.Flatten(&file);
    }
  }
}

/**
 * @brief Serializes runtime/UI settings into a persistable BMessage.
 */
void SettingsController::SaveSettingsToMessage(BMessage &state) {
  // Version tag for forward-compatible settings loading.
  state.AddInt32("settings_version", 2);

  fWindow->fLibraryManager->ContentView()->SaveState(&state);

  state.AddBool("show_cover_art", fWindow->fShowCoverArt);
  state.AddBool("show_file_info", fWindow->fShowFileInfo);
  state.AddBool("show_filters_library", fWindow->fShowFiltersLibrary);
  state.AddBool("show_filters_playlist", fWindow->fShowFiltersPlaylist);
  state.AddBool("show_filters_radio", fWindow->fShowFiltersRadio);
  state.AddBool("show_filters_dlna", fWindow->fShowFiltersDlna);
  if (!fWindow->fPlaylistPath.IsEmpty())
    state.AddString("playlist_path", fWindow->fPlaylistPath);

  state.AddBool("use_custom_seekbar_color", fWindow->fUseCustomSeekBarColor);
  state.AddBool("use_seekbar_color_for_selection",
                fWindow->fUseSeekBarColorForSelection);
  state.AddData("seekbar_color", B_RGB_COLOR_TYPE, &fWindow->fSeekBarColor,
                sizeof(rgb_color));
  state.AddData("selection_color", B_RGB_COLOR_TYPE,
                &fWindow->fSelectionColor, sizeof(rgb_color));

  if (fWindow->fPlaybackQueueManager) {
    state.AddBool("shuffle_enabled",
                  fWindow->fPlaybackQueueManager->ShuffleEnabled());
    state.AddInt32("repeat_mode",
                   fWindow->fPlaybackQueueManager->RepeatModeValue());
  }
  state.AddBool("show_tooltips", fWindow->fShowTooltips);

  if (fWindow->fIsMuted) {
    state.AddInt32("volume_level", (int32)fWindow->fPreMuteVolume);
  } else {
    state.AddInt32("volume_level",
                   fWindow->fVolumeSlider ? fWindow->fVolumeSlider->Value()
                                          : 100);
  }

  if (fWindow->fPlaylistLibrary && fWindow->fPlaylistLibrary->View()) {
    std::vector<BString> playlistOrder =
        fWindow->fPlaylistLibrary->View()->GetPlaylistOrder();
    for (const auto &name : playlistOrder)
      state.AddString("playlist_order", name);
    fWindow->fPlaylistLibrary->SaveFolderSources(state);
  }

  fWindow->fLibraryManager->SaveSettings(&state);

  state.AddBool("radio_enabled", fWindow->fRadioEnabled);
  state.AddBool("dlna_enabled", fWindow->fDlnaEnabled);
#if ENABLE_DLNA_OUTPUT
  state.AddBool("show_renderer_btn", fWindow->fShowRendererBtn);
#endif

  BString mode = "Library";
  if (fWindow->fIsRadioMode)
    mode = "Radio";
  else if (fWindow->fIsDlnaMode)
    mode = "DLNA";
  else if (fWindow->fIsFolderMode)
    mode = "Folder";
  else if (!fWindow->fIsLibraryMode)
    mode = "Playlist";

  state.AddString("active_view_mode", mode);
  state.AddString("active_playlist_name", fWindow->fCurrentPlaylistName);
  if (fWindow->fIsDlnaMode && !fWindow->fActiveDlnaServer.uuid.IsEmpty())
    state.AddString("active_dlna_uuid", fWindow->fActiveDlnaServer.uuid);
}

/**
 * @brief Loads persisted settings and applies them to active window state.
 */
void SettingsController::LoadSettings() {
  if (!fWindow || !fWindow->fLibraryManager ||
      !fWindow->fLibraryManager->ContentView()) {
    return;
  }

  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
    settingsPath.Append("BeTon/settings");
    BFile file(settingsPath.Path(), B_READ_ONLY);
    if (file.InitCheck() == B_OK) {
      BMessage state;

      if (state.Unflatten(&file) == B_OK)
        LoadSettingsFromMessage(state);
    }
  }

  ApplyLoadedSettings();
}

/**
 * @brief Reads saved values from the settings archive message.
 */
void SettingsController::LoadSettingsFromMessage(BMessage &state) {
  int32 version = 0;
  state.FindInt32("settings_version", &version);

  if (version >= 2)
    fWindow->fLibraryManager->ContentView()->LoadState(&state);

  fWindow->fLibraryManager->LoadSettings(&state);

  if (state.FindBool("show_cover_art", &fWindow->fShowCoverArt) == B_OK) {
    if (fWindow->fViewCoverItem)
      fWindow->fViewCoverItem->SetMarked(fWindow->fShowCoverArt);
  }
  if (state.FindBool("show_file_info", &fWindow->fShowFileInfo) == B_OK) {
    if (fWindow->fViewInfoItem)
      fWindow->fViewInfoItem->SetMarked(fWindow->fShowFileInfo);
  }

  state.FindBool("show_filters_library", &fWindow->fShowFiltersLibrary);
  state.FindBool("show_filters_playlist", &fWindow->fShowFiltersPlaylist);
  state.FindBool("show_filters_radio", &fWindow->fShowFiltersRadio);
  state.FindBool("show_filters_dlna", &fWindow->fShowFiltersDlna);

  if (fWindow->fNowPlayingInfoPanel) {
    fWindow->fNowPlayingInfoPanel->SetCoverVisible(fWindow->fShowCoverArt);
    fWindow->fNowPlayingInfoPanel->SetInfoVisible(fWindow->fShowFileInfo);
  }

  if (state.FindString("playlist_path", &fWindow->fPlaylistPath) != B_OK)
    fWindow->fPlaylistPath = "";
  if (fWindow->fPlaylistLibrary)
    fWindow->fPlaylistLibrary->LoadFolderSources(state);

  state.FindBool("use_custom_seekbar_color", &fWindow->fUseCustomSeekBarColor);
  state.FindBool("use_seekbar_color_for_selection",
                 &fWindow->fUseSeekBarColorForSelection);

  rgb_color *color;
  ssize_t size;
  if (state.FindData("seekbar_color", B_RGB_COLOR_TYPE,
                     (const void **)&color, &size) == B_OK &&
      size == sizeof(rgb_color)) {
    fWindow->fSeekBarColor = *color;
  } else {
    fWindow->fSeekBarColor = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
  }
  if (state.FindData("selection_color", B_RGB_COLOR_TYPE,
                     (const void **)&color, &size) == B_OK &&
      size == sizeof(rgb_color)) {
    fWindow->fSelectionColor = *color;
  } else {
    fWindow->fSelectionColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
  }

  if (fWindow->fSelColorSystemItem)
    fWindow->fSelColorSystemItem->SetMarked(
        !fWindow->fUseSeekBarColorForSelection);
  if (fWindow->fSelColorMatchItem)
    fWindow->fSelColorMatchItem->SetMarked(
        fWindow->fUseSeekBarColorForSelection);

  fWindow->ApplyColors();

  bool shuffleEnabled = false;
  if (state.FindBool("shuffle_enabled", &shuffleEnabled) == B_OK) {
    if (fWindow->fPlaybackQueueManager)
      fWindow->fPlaybackQueueManager->SetShuffleEnabled(shuffleEnabled);
  }

  int32 repeatMode = PlaybackQueueManager::RepeatOff;
  if (state.FindInt32("repeat_mode", &repeatMode) == B_OK) {
    if (fWindow->fPlaybackQueueManager)
      fWindow->fPlaybackQueueManager->SetRepeatModeValue(repeatMode);
  }

  if (state.FindBool("show_tooltips", &fWindow->fShowTooltips) == B_OK) {
    if (fWindow->fTooltipsOnItem)
      fWindow->fTooltipsOnItem->SetMarked(fWindow->fShowTooltips);
    if (fWindow->fTooltipsOffItem)
      fWindow->fTooltipsOffItem->SetMarked(!fWindow->fShowTooltips);

    if (fWindow->fShowTooltips) {
      if (fWindow->fBtnShuffle)
        fWindow->fBtnShuffle->SetToolTip("Shuffle");
      if (fWindow->fBtnRepeat)
        fWindow->fBtnRepeat->SetToolTip("Repeat");
    }
  }

  int32 volLevel;
  if (state.FindInt32("volume_level", &volLevel) == B_OK) {
    if (fWindow->fVolumeSlider)
      fWindow->fVolumeSlider->SetValue(volLevel);
    fWindow->PostMessage(MSG_VOLUME_CHANGED);
  }

  // Applied later after playlist sidebar items have been created!
  std::vector<BString> playlistOrder;
  BString name;
  for (int32 i = 0; state.FindString("playlist_order", i, &name) == B_OK; ++i)
    playlistOrder.push_back(name);

  fWindow->fPendingPlaylistOrder = playlistOrder;

  if (state.FindBool("radio_enabled", &fWindow->fRadioEnabled) == B_OK)
    fWindow->fToggleRadioItem->SetMarked(fWindow->fRadioEnabled);
  if (state.FindBool("dlna_enabled", &fWindow->fDlnaEnabled) == B_OK)
    fWindow->fToggleDlnaItem->SetMarked(fWindow->fDlnaEnabled);
#if ENABLE_DLNA_OUTPUT
  if (state.FindBool("show_renderer_btn", &fWindow->fShowRendererBtn) == B_OK)
    fWindow->fShowRendererBtnItem->SetMarked(fWindow->fShowRendererBtn);
#endif
  state.FindString("active_view_mode", &fWindow->fInitialViewMode);
  state.FindString("active_playlist_name", &fWindow->fInitialPlaylistName);
  state.FindString("active_dlna_uuid", &fWindow->fInitialDlnaUuid);
}

/**
 * @brief Applies post-load settings that depend on initialized UI/controllers.
 */
void SettingsController::ApplyLoadedSettings() {
#if ENABLE_DLNA_OUTPUT
  fWindow->_SetOutputMenuVisible(fWindow->fDlnaEnabled);
#endif

  if (fWindow->fPlaylistPath.IsEmpty()) {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
      path.Append("BeTon/Playlists");
      fWindow->fPlaylistPath = path.Path();
    }
  }

  if (fWindow->fPlaylistLibrary && !fWindow->fPlaylistPath.IsEmpty()) {
    fWindow->fPlaylistLibrary->SetPlaylistFolderPath(fWindow->fPlaylistPath);
    fWindow->fPlaylistLibrary->LoadAvailablePlaylists();

    // Apply saved ordering only !!after!! sidebar items exist.
    if (!fWindow->fPendingPlaylistOrder.empty()) {
      fWindow->fPlaylistLibrary->View()->SetPlaylistOrder(
          fWindow->fPendingPlaylistOrder);
      fWindow->fPendingPlaylistOrder.clear();
    }
  }

  if (!fWindow->fRadioEnabled) {
    int32 idx = fWindow->fPlaylistLibrary->View()->FindIndexByName("Radio");
    if (idx >= 0)
      fWindow->fPlaylistLibrary->View()->RemovePlaylistAt(idx);
  } else {
    if (fWindow->fRadioStationLibrary &&
        fWindow->fRadioStationLibrary->AllStations().empty()) {
      fWindow->fRadioStationLibrary->LoadStations();
    }
  }

  if (!fWindow->fDlnaEnabled) {
    if (fWindow->fDlnaManager)
      fWindow->fDlnaManager->StopDiscovery();
    int32 idx = fWindow->fPlaylistLibrary->View()->FindIndexByName("DLNA");
    if (idx >= 0)
      fWindow->fPlaylistLibrary->View()->RemovePlaylistAt(idx);
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->SetServerFieldVisible(false);
  } else {
    if (fWindow->fDlnaManager)
      fWindow->fDlnaManager->StartDiscovery();
  }
}
