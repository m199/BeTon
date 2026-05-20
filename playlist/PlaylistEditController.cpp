#include "PlaylistEditController.h"

#include "MainWindow.h"
#include "PlaylistLibrary.h"
#include "LibraryBrowserController.h"
#include "MediaTableView.h"

#include "PlaylistSidebarView.h"
#include "PlaylistNameDialog.h"
#include "SmartPlaylistGeneratorWindow.h"
#include "Debug.h"
#include "Messages.h"

#include <Catalog.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Entry.h>
#include <random>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaylistEditController"

PlaylistEditController::PlaylistEditController(MainWindow* window)
    : fWindow(window)  {
}

PlaylistEditController::~PlaylistEditController() {
    delete fWindow->fFilePanel;
}

void PlaylistEditController::PromptNewPlaylist(BMessage *msg) {
  fPendingPlaylistFiles.MakeEmpty();

  BMessage filesMsg;
  if (msg->FindMessage("files", &filesMsg) == B_OK) {
    fPendingPlaylistFiles = filesMsg;
    DEBUG_PRINT("%ld"
                " Files for new playlist buffered\n",
                (long)filesMsg.CountNames(B_REF_TYPE));
  }

  PlaylistNameDialog *prompt = new PlaylistNameDialog(BMessenger(fWindow));
  prompt->Show();
}

void PlaylistEditController::SaveSelectedPlaylist() {
  int32 selected = fWindow->fPlaylistLibrary->View()->CurrentSelection();
  if (selected < 0)
    return;

  BString item = fWindow->fPlaylistLibrary->View()->ItemAt(selected);
  if (!item)
    return;

  BString name = item;
  std::vector<BString> paths;

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  for (int32 i = 0; i < cv->CountRows(); ++i) {
    const MediaItem *mi = cv->ItemAt(i);
    if (mi)
      paths.push_back(mi->path);
  }

  fWindow->fPlaylistLibrary->SavePlaylist(name, paths);
}

void PlaylistEditController::CreatePlaylistFromPrompt(BMessage *msg) {
  BString name;
  if (msg->FindString("name", &name) != B_OK || name.IsEmpty())
    return;


  fWindow->fPlaylistLibrary->CreateNewPlaylist(name);

  entry_ref ref;
  int32 i = 0;
  while (fPendingPlaylistFiles.FindRef("refs", i++, &ref) == B_OK) {
    BPath path(&ref);
    fWindow->fPlaylistLibrary->AddItemToPlaylist(name, path.Path());
    DEBUG_PRINT("Files '%s' added to new playlist '%s'\n",
                path.Path(), name.String());
  }
  fPendingPlaylistFiles.MakeEmpty();
}

void PlaylistEditController::RenamePlaylistFromPrompt(BMessage *msg) {
  BString oldName, newName;
  if (msg->FindString("old", &oldName) != B_OK ||
      msg->FindString("name", &newName) != B_OK || newName.IsEmpty()) {
    return;
  }

  BPath dirPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &dirPath) != B_OK)
    return;

  dirPath.Append("BeTon/Playlists");

  BString oldFile = oldName;
  oldFile << ".m3u";
  BString newFile = newName;
  newFile << ".m3u";

  BPath oldPath(dirPath.Path(), oldFile.String());
  BPath newPath(dirPath.Path(), newFile.String());

  BEntry entry(oldPath.Path());
  if (entry.Exists() && entry.Rename(newPath.Path()) == B_OK) {
    DEBUG_PRINT("Playlist '%s' -> '%s' renamed\n",
                oldName.String(), newName.String());

    fWindow->fPlaylistLibrary->RenamePlaylist(oldName, newName);
  }
}

void PlaylistEditController::ReplyWithPlaylistNames(BMessage *msg) {
  BMessage reply;
  fWindow->fPlaylistLibrary->GetPlaylistNames(reply, false);
  msg->SendReply(&reply);
}

void PlaylistEditController::HandlePlaylistFolderSelected(BMessage *msg) {
  entry_ref ref;
  if (msg->FindRef("refs", &ref) != B_OK)
    return;

  BEntry entry(&ref, true);
  BPath path;
  if (entry.GetPath(&path) != B_OK)
    return;

  fWindow->fPlaylistPath = path.Path();
  fWindow->fPlaylistLibrary->SetPlaylistFolderPath(fWindow->fPlaylistPath);
  fWindow->fPlaylistLibrary->LoadAvailablePlaylists();
  fWindow->SaveSettings();

  BString statusMsg;
  statusMsg.SetToFormat(B_TRANSLATE("Playlist-Folder set: %s"),
                        fWindow->fPlaylistPath.String());
  fWindow->UpdateStatus(statusMsg);
}

void PlaylistEditController::MovePlaylistItem(BMessage *msg) {
  int32 index = -1;
  if (msg->FindInt32("index", &index) != B_OK || index < 0)
    return;

  int32 playlistIdx = fWindow->fPlaylistLibrary->View()->CurrentSelection();
  if (playlistIdx <= 0)
    return;

  BString playlistName = fWindow->fPlaylistLibrary->View()->ItemAt(playlistIdx);
  if (playlistName.IsEmpty())
    return;

  int32 newIndex = (msg->what == MSG_MOVE_UP) ? index - 1 : index + 1;
  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  if (newIndex < 0 || newIndex >= cv->CountRows())
    return;

  fWindow->fPlaylistLibrary->ReorderPlaylistItem(playlistName, index, newIndex);

  std::vector<MediaItem> items;
  items.reserve(cv->CountRows());
  for (int32 i = 0; i < cv->CountRows(); ++i) {
    const MediaItem *mi = cv->ItemAt(i);
    if (mi)
      items.push_back(*mi);
  }

  if (index >= 0 && index < (int32)items.size() && newIndex >= 0 &&
      newIndex < (int32)items.size()) {
    MediaItem temp = items[index];
    items.erase(items.begin() + index);
    items.insert(items.begin() + newIndex, temp);
  }

  cv->ClearEntries();
  for (const auto &mi : items)
    cv->AddEntry(mi);

  if (BRow *row = cv->RowAt(newIndex)) {
    cv->DeselectAll();
    cv->AddToSelection(row);
    cv->ScrollTo(row);
  }
}

void PlaylistEditController::ReorderPlaylist(BMessage *msg) {
  int32 fromIndex = -1, toIndex = -1;
  if (msg->FindInt32("from_index", &fromIndex) != B_OK ||
      msg->FindInt32("to_index", &toIndex) != B_OK)
    return;

  if (fromIndex == toIndex || fromIndex < 0 || toIndex < 0)
    return;

  int32 playlistIdx = fWindow->fPlaylistLibrary->View()->CurrentSelection();
  if (playlistIdx <= 0)
    return;

  BString playlistName = fWindow->fPlaylistLibrary->View()->ItemAt(playlistIdx);
  if (playlistName.IsEmpty())
    return;

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  if (toIndex >= cv->CountRows())
    toIndex = cv->CountRows() - 1;

  fWindow->fPlaylistLibrary->ReorderPlaylistItem(playlistName, fromIndex, toIndex);

  std::vector<MediaItem> items;
  items.reserve(cv->CountRows());
  for (int32 i = 0; i < cv->CountRows(); ++i) {
    const MediaItem *mi = cv->ItemAt(i);
    if (mi)
      items.push_back(*mi);
  }

  if (fromIndex < (int32)items.size() && toIndex < (int32)items.size()) {
    MediaItem temp = items[fromIndex];
    items.erase(items.begin() + fromIndex);
    items.insert(items.begin() + toIndex, temp);
  }

  cv->DeselectAll();

  cv->ClearEntries();
  for (const auto &mi : items)
    cv->AddEntry(mi);

  cv->Invalidate();
  if (BView *scrollView = cv->ScrollView())
    scrollView->Invalidate();

  BRow *newRow = cv->RowAt(toIndex);
  if (newRow) {
    cv->SetFocusRow(newRow);
    cv->AddToSelection(newRow);
    cv->ScrollTo(newRow);
  }

  cv->Sync();
  fWindow->UpdateIfNeeded();
}

void PlaylistEditController::HandlePlaylistDrop(BMessage *msg) {
  DEBUG_PRINT("B_SIMPLE_DATA received!\n");

  int32 sourceIndex = -1;
  if (msg->FindInt32("source_index", &sourceIndex) != B_OK) {
    DEBUG_PRINT("No source_index\n");
    return;
  }
  DEBUG_PRINT("source_index=%ld\n", (long)sourceIndex);

  int32 playlistIdx = fWindow->fPlaylistLibrary->View()->CurrentSelection();
  if (playlistIdx <= 0)
    return;

  BString playlistName = fWindow->fPlaylistLibrary->View()->ItemAt(playlistIdx);
  if (playlistName.IsEmpty())
    return;

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  BPoint dropPoint;
  if (msg->FindPoint("_drop_point_", &dropPoint) == B_OK ||
      msg->FindPoint("be:view_where", &dropPoint) == B_OK) {
    cv->ConvertFromScreen(&dropPoint);
  } else {
    cv->GetMouse(&dropPoint, nullptr);
  }

  BRow *targetRow = cv->RowAt(dropPoint);
  int32 targetIndex = targetRow ? cv->IndexOf(targetRow) : cv->CountRows() - 1;

  if (sourceIndex == targetIndex || sourceIndex < 0 || targetIndex < 0)
    return;

  fWindow->fPlaylistLibrary->ReorderPlaylistItem(playlistName, sourceIndex, targetIndex);

  std::vector<MediaItem> items;
  items.reserve(cv->CountRows());
  for (int32 i = 0; i < cv->CountRows(); ++i) {
    const MediaItem *mi = cv->ItemAt(i);
    if (mi)
      items.push_back(*mi);
  }

  if (sourceIndex < (int32)items.size() && targetIndex < (int32)items.size()) {
    MediaItem temp = items[sourceIndex];
    items.erase(items.begin() + sourceIndex);
    items.insert(items.begin() + targetIndex, temp);
  }

  cv->ClearEntries();
  for (const auto &mi : items)
    cv->AddEntry(mi);

  if (BRow *row = cv->RowAt(targetIndex)) {
    cv->DeselectAll();
    cv->AddToSelection(row);
    cv->ScrollTo(row);
  }
}

void PlaylistEditController::AddSelectedItemsToPlaylist(BMessage *msg) {
  BString playlist;
  if (msg->FindString("playlist", &playlist) != B_OK)
    return;

  if (!fWindow->fPlaylistLibrary->IsPlaylistWritable(playlist)) {
    DEBUG_PRINT("addp rejected: Playlist '%s' is not "
                "writable\n",
                playlist.String());
    return;
  }

  int32 index;
  bool hadAny = false;
  for (int32 i = 0; msg->FindInt32("index", i, &index) == B_OK; ++i) {
    BString path = fWindow->GetPathForContentItem(index);
    if (path.IsEmpty())
      continue;

    DEBUG_PRINT("addp: Index=%ld"
                ", Playlist=%s, Pfad=%s\n",
                (long)index, playlist.String(), path.String());

    fWindow->fPlaylistLibrary->AddItemToPlaylist(playlist, path);
    hadAny = true;
  }

  if (!hadAny && msg->FindInt32("index", &index) == B_OK) {
    BString path = fWindow->GetPathForContentItem(index);
    if (path.IsEmpty())
      return;

    DEBUG_PRINT("addp(single): Index=%ld"
                ", Playlist=%s, Pfad=%s\n",
                (long)index, playlist.String(), path.String());

    fWindow->fPlaylistLibrary->AddItemToPlaylist(playlist, path);
  }
}

void PlaylistEditController::ShowSmartPlaylistGenerator() {
  std::set<BString> uniqueGenres;
  for (const auto &item : fWindow->fAllItems) {
    if (!item.genre.IsEmpty())
      uniqueGenres.insert(item.genre);
  }
  std::vector<BString> genres(uniqueGenres.begin(), uniqueGenres.end());

  SmartPlaylistGeneratorWindow *win =
      new SmartPlaylistGeneratorWindow(BMessenger(fWindow), genres);
  win->Show();
}

void PlaylistEditController::GenerateSmartPlaylist(BMessage *msg) {
  BString name;
  if (msg->FindString("name", &name) != B_OK || name.IsEmpty())
    name = B_TRANSLATE("Generated Playlist");

  bool shuffle = false;
  msg->FindBool("shuffle", &shuffle);

  std::vector<BMessage> rules;
  BMessage ruleMsg;
  int32 i = 0;
  while (msg->FindMessage("rule", i++, &ruleMsg) == B_OK)
    rules.push_back(ruleMsg);

  int32 limitMode = 0;
  msg->FindInt32("limit_mode", &limitMode);
  int32 limitValue = 0;
  msg->FindInt32("limit_value", &limitValue);

  std::vector<MediaItem> matches;
  matches.reserve(fWindow->fAllItems.size());

  for (const auto &item : fWindow->fAllItems) {
    bool allRulesMatch = true;

    for (const auto &r : rules) {
      int32 type = 0;
      r.FindInt32("type", &type);
      BString val1;
      r.FindString("val1", &val1);
      BString val2;
      r.FindString("val2", &val2);
      bool exclude = false;
      r.FindBool("exclude", &exclude);

      bool currentRuleMatch = false;

      if (type == 0) {
        if (!val1.IsEmpty())
          currentRuleMatch = (item.genre.ICompare(val1) == 0);
      } else if (type == 1) {
        if (!val1.IsEmpty())
          currentRuleMatch = (item.artist.IFindFirst(val1) >= 0);
      } else if (type == 2) {
        int32 y1 = atoi(val1.String());
        int32 y2 = atoi(val2.String());

        bool inRange = true;
        if (y1 > 0 && item.year < y1)
          inRange = false;
        if (y2 > 0 && item.year > y2)
          inRange = false;
        currentRuleMatch = inRange;
      }

      if (exclude) {
        if (currentRuleMatch) {
          allRulesMatch = false;
          break;
        }
      } else if (!currentRuleMatch) {
        allRulesMatch = false;
        break;
      }
    }

    if (allRulesMatch)
      matches.push_back(item);
  }

  if (shuffle) {
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(matches.begin(), matches.end(), g);
  }

  if (limitMode > 0 && !matches.empty()) {
    if (limitMode == 1) {
      if ((int32)matches.size() > limitValue)
        matches.resize(limitValue);
    } else if (limitMode == 2) {
      int64 maxSeconds = (int64)limitValue * 60;
      int64 currentSeconds = 0;
      size_t cutIndex = matches.size();
      for (size_t k = 0; k < matches.size(); ++k) {
        currentSeconds += matches[k].duration;
        if (currentSeconds > maxSeconds) {
          cutIndex = k;
          break;
        }
      }
      if (cutIndex < matches.size())
        matches.resize(cutIndex);
    }
  }

  std::vector<BString> paths;
  paths.reserve(matches.size());
  for (const auto &m : matches)
    paths.push_back(m.path);

  fWindow->fPlaylistLibrary->SavePlaylist(name, paths);

  BString statusMsg;
  statusMsg.SetToFormat(B_TRANSLATE("Playlist '%s' created"), name.String());
  if (shuffle)
    statusMsg << " " << B_TRANSLATE("(mixed)");
  if (limitMode > 0)
    statusMsg << " " << B_TRANSLATE("(limited)");

  BString countStr;
  countStr.SetToFormat(B_TRANSLATE(": %zu tracks."), matches.size());
  statusMsg << countStr;

  fWindow->UpdateStatus(statusMsg);
}

void PlaylistEditController::DeleteSelectedPlaylistItems() {
  std::vector<BString> removedPaths;

  BRow *row = nullptr;
  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  while ((row = cv->CurrentSelection(row)) != nullptr) {
    const MediaItem *mi = cv->ItemAt(cv->IndexOf(row));
    if (mi) {
      removedPaths.push_back(mi->path);
    }
  }

  if (!removedPaths.empty() && fWindow->fCurrentPlaylistName.Length() > 0) {
    for (const auto &path : removedPaths) {
      for (int32 i = 0; i < cv->CountRows(); i++) {
        const MediaItem *mi = cv->ItemAt(i);
        if (mi && mi->path == path) {
          BRow *r = cv->RowAt(i);
          cv->RemoveRow(r);
          delete r;
          break;
        }
      }
    }

    std::vector<BString> remainingPaths;
    for (int32 i = 0; i < cv->CountRows(); i++) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi) {
        remainingPaths.push_back(mi->path);
      }
    }
    fWindow->fPlaylistLibrary->SavePlaylist(fWindow->fCurrentPlaylistName, remainingPaths);
  }
}

void PlaylistEditController::SelectPlaylistFolder() {
  delete fWindow->fFilePanel;
  fWindow->fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(fWindow), nullptr,
                              B_DIRECTORY_NODE, false,
                              new BMessage(MSG_PLAYLIST_FOLDER_SELECTED));
  fWindow->fFilePanel->Show();
}



