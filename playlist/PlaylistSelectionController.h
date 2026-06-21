#ifndef BETON_PLAYLIST_SELECTION_CONTROLLER_H
#define BETON_PLAYLIST_SELECTION_CONTROLLER_H

#include "PlaylistSidebarView.h"

class BMessage;
class BString;
class MainWindow;

class PlaylistSelectionController {
public:
  explicit PlaylistSelectionController(MainWindow *window);

  void HandlePlaylistSelection(BMessage *msg);
  void ResetAndHideFilters();

private:
  void RestoreInitialPlaylistSelection();
  PlaylistItemKind PlaylistKindFromSelection(BMessage *msg,
                                             const BString &name) const;
  void ApplyPlaylistFilterVisibility();
  void ShowSelectedPlaylistSource(const BString &name);
  void ShowRadioPlaylistSource();
  void ShowLibraryPlaylistSource();
  void ShowRegularPlaylistSource(const BString &name);
  void ShowFolderPlaylistSource(const BString &name);

  MainWindow *fWindow;
};

#endif // BETON_PLAYLIST_SELECTION_CONTROLLER_H
