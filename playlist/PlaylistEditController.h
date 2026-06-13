#ifndef BETON_PLAYLIST_EDIT_CONTROLLER_H
#define BETON_PLAYLIST_EDIT_CONTROLLER_H

#include <Message.h>
#include <String.h>
#include <vector>

class MainWindow;
class BFilePanel;

class PlaylistEditController {
public:
  PlaylistEditController(MainWindow *window);
  ~PlaylistEditController();

  void ShowSmartPlaylistGenerator();
  void GenerateSmartPlaylist(BMessage *msg);
  void MovePlaylistItem(BMessage *msg);
  void ReorderPlaylist(BMessage *msg);
  void HandlePlaylistDrop(BMessage *msg);
  void AddSelectedItemsToPlaylist(BMessage *msg);
  void PromptNewPlaylist(BMessage *msg);
  void SaveSelectedPlaylist();
  void SelectPlaylistFolder();
  void CreatePlaylistFromPrompt(BMessage *msg);
  void RenamePlaylistFromPrompt(BMessage *msg);
  void ReplyWithPlaylistNames(BMessage *msg);
  void HandlePlaylistFolderSelected(BMessage *msg);
  void DeleteSelectedPlaylistItems();
  void MoveSelectedItemsToTrash();
  void SelectMoveToFolder();
  void HandleMoveToFolderSelected(BMessage *msg);
  void MoveSelectedItemsTo(const entry_ref *targetDirRef);
  void RestorePlaylistPaths(BMessage *msg);
  void CreatePlaylistWithPaths(BMessage *msg);
  void DeletePlaylistByName(BMessage *msg);

private:
  MainWindow *fWindow;
  BMessage fPendingPlaylistFiles;

  void ResolveRefRecursively(const struct entry_ref &ref, std::vector<BString> &outPaths);
};

#endif // BETON_PLAYLIST_EDIT_CONTROLLER_H
