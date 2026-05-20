#include "PlaylistSelectionController.h"

#include "MediaTableView.h"
#include "AudioPlaybackEngine.h"
#include "DLNAViewController.h"
#include "Debug.h"
#include "NowPlayingInfoPanel.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "Messages.h"
#include "PlaylistLibrary.h"
#include "RadioStationController.h"
#include "SingleColumnListView.h"

#include <GroupView.h>
#include <MenuItem.h>
#include <Message.h>
#include <String.h>
#include <TextControl.h>
#include <vector>

PlaylistSelectionController::PlaylistSelectionController(MainWindow *window)
    : fWindow(window) {}

void PlaylistSelectionController::HandlePlaylistSelection(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  if (msg->what == MSG_INIT_LIBRARY)
    RestoreInitialPlaylistSelection();

  int32 selected = fWindow->fPlaylistLibrary->View()->CurrentSelection();
  DEBUG_PRINT("Current selection index: %ld\n", (long)selected);
  if (selected < 0)
    return;

  BString name = fWindow->fPlaylistLibrary->View()->ItemAt(selected);
  if (name.IsEmpty())
    return;

  fWindow->fCurrentPlaylistName = name;

  PlaylistItemKind kind = PlaylistKindFromSelection(msg, name);
  fWindow->fIsLibraryMode = (kind == PlaylistItemKind::Library);
  fWindow->fIsRadioMode = (kind == PlaylistItemKind::Radio);
  fWindow->fIsDlnaMode = (kind == PlaylistItemKind::DLNA);

  if (fWindow->fSearchField && fWindow->fSearchField->Text()[0] != '\0')
    fWindow->fSearchField->SetText("");

  ApplyPlaylistFilterVisibility();
  ShowSelectedPlaylistSource(name);
  fWindow->UpdateFileInfo();
}

void PlaylistSelectionController::RestoreInitialPlaylistSelection() {
  DEBUG_PRINT("MSG_INIT_LIBRARY: InitialViewMode='%s', "
              "InitialPlaylist='%s'\n",
              fWindow->fInitialViewMode.String(),
              fWindow->fInitialPlaylistName.String());

  int32 res = -1;
  if (!fWindow->fInitialViewMode.IsEmpty()) {
    if (fWindow->fInitialViewMode == "Playlist" &&
        !fWindow->fInitialPlaylistName.IsEmpty()) {
      res = fWindow->fPlaylistLibrary->View()->SelectByName(
          fWindow->fInitialPlaylistName);
    } else {
      res = fWindow->fPlaylistLibrary->View()->SelectByName(
          fWindow->fInitialViewMode);
    }
  }

  if (res < 0) {
    DEBUG_PRINT("Selection failed, defaulting to Library\n");
    fWindow->fPlaylistLibrary->View()->SelectByName("Library");
  }

  fWindow->fInitialViewMode = "";
  fWindow->fInitialPlaylistName = "";
}

PlaylistItemKind PlaylistSelectionController::PlaylistKindFromSelection(
    BMessage *msg, const BString &name) const {
  int32 kindInt = 0;
  if (msg->what == MSG_PLAYLIST_SELECTION &&
      msg->FindInt32("kind", &kindInt) == B_OK) {
    return (PlaylistItemKind)kindInt;
  }

  if (name == "Library")
    return PlaylistItemKind::Library;
  if (name == "Radio")
    return PlaylistItemKind::Radio;
  if (name == "DLNA")
    return PlaylistItemKind::DLNA;
  return PlaylistItemKind::Playlist;
}

void PlaylistSelectionController::ApplyPlaylistFilterVisibility() {
  if (!fWindow->fFilterGroup)
    return;

  bool showFilters = false;
  if (fWindow->fIsLibraryMode)
    showFilters = fWindow->fShowFiltersLibrary;
  else if (fWindow->fIsRadioMode)
    showFilters = fWindow->fShowFiltersRadio;
  else if (fWindow->fIsDlnaMode)
    showFilters = fWindow->fShowFiltersDlna;
  else
    showFilters = fWindow->fShowFiltersPlaylist;

  if (fWindow->fViewFiltersItem)
    fWindow->fViewFiltersItem->SetMarked(showFilters);

  if (showFilters) {
    if (!fWindow->fIsFilterGroupVisible) {
      fWindow->fIsFilterGroupVisible = true;
      fWindow->fFilterGroup->Show();
    }
    return;
  }

  if (fWindow->fLibraryManager) {
    if (fWindow->fLibraryManager->GenreView())
      fWindow->fLibraryManager->GenreView()->DeselectAll();
    if (fWindow->fLibraryManager->ArtistView())
      fWindow->fLibraryManager->ArtistView()->DeselectAll();
    if (fWindow->fLibraryManager->AlbumView())
      fWindow->fLibraryManager->AlbumView()->DeselectAll();
  }
  if (fWindow->fIsFilterGroupVisible) {
    fWindow->fIsFilterGroupVisible = false;
    fWindow->fFilterGroup->Hide();
  }
}

void PlaylistSelectionController::ShowSelectedPlaylistSource(
    const BString &name) {
  if (fWindow->fIsRadioMode) {
    ShowRadioPlaylistSource();
  } else if (fWindow->fIsDlnaMode) {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->ShowPlaylistSource();
  } else if (fWindow->fIsLibraryMode) {
    ShowLibraryPlaylistSource();
  } else {
    ShowRegularPlaylistSource(name);
  }
}

void PlaylistSelectionController::ShowRadioPlaylistSource() {
  if (fWindow->fDlnaController)
    fWindow->fDlnaController->SetServerFieldVisible(false);
  fWindow->fLibraryManager->ContentView()->SetRadioMode(true);
  fWindow->fLibraryManager->SetActivePaths({});
  if (fWindow->fRadioStationController)
    fWindow->fRadioStationController->ShowStations();
  bool radioIsPlaying =
      fWindow->fPlaybackEngine &&
      (fWindow->fPlaybackEngine->IsPlaying() || fWindow->fPlaybackEngine->IsPaused()) &&
      fWindow->fRadioStationController &&
      fWindow->fRadioStationController->HasActiveStation();
  if (radioIsPlaying && fWindow->fNowPlayingInfoPanel &&
      fWindow->fShowCoverArt && fWindow->fRadioStationController->ActiveCover()) {
    fWindow->fNowPlayingInfoPanel->SetCover(
        fWindow->fRadioStationController->ActiveCover());
  }
}

void PlaylistSelectionController::ShowLibraryPlaylistSource() {
  if (fWindow->fDlnaController)
    fWindow->fDlnaController->SetServerFieldVisible(false);
  fWindow->fLibraryManager->ContentView()->SetRadioMode(false);
  fWindow->fLibraryManager->SetRadioFilterMode(false);
  fWindow->fLibraryManager->SetActivePaths({});
  fWindow->UpdateFilteredViews();
}

void PlaylistSelectionController::ShowRegularPlaylistSource(const BString &name) {
  if (fWindow->fDlnaController)
    fWindow->fDlnaController->SetServerFieldVisible(false);
  fWindow->fLibraryManager->ContentView()->SetRadioMode(false);
  fWindow->fLibraryManager->SetRadioFilterMode(false);
  std::vector<BString> paths = fWindow->fPlaylistLibrary->LoadPlaylist(name);
  fWindow->fLibraryManager->SetActivePaths(paths);
  fWindow->UpdateFilteredViews();
}

void PlaylistSelectionController::ResetAndHideFilters() {
  if (fWindow->fLibraryManager) {
    if (fWindow->fLibraryManager->GenreView())
      fWindow->fLibraryManager->GenreView()->DeselectAll();
    if (fWindow->fLibraryManager->ArtistView())
      fWindow->fLibraryManager->ArtistView()->DeselectAll();
    if (fWindow->fLibraryManager->AlbumView())
      fWindow->fLibraryManager->AlbumView()->DeselectAll();
  }
  fWindow->UpdateFilteredViews();
  if (fWindow->fFilterGroup && fWindow->fIsFilterGroupVisible) {
    fWindow->fIsFilterGroupVisible = false;
    fWindow->fFilterGroup->Hide();
  }
}
