#include "PlaylistEditController.h"

#include "MainWindow.h"
#include "LibraryController.h"
#include "UndoManager.h"
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
#include <Directory.h>
#include <algorithm>
#include <stack>
#include <random>
#include "Config.h"
#include "PlaylistSelectionController.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaylistEditController"

static bool IsSupportedAudioFile(const BString &path) {
  BString lower(path);
  lower.ToLower();

  static const char *exts[] = {".mp3", ".wav", ".flac", ".ogg",
                               ".opus", ".m4a", ".aac",  ".wma"
#if ENABLE_MIDI_PLAYBACK
                               ,
                               ".mid", ".midi"
#endif
  };

  for (auto ext : exts) {
    if (lower.EndsWith(ext))
      return true;
  }
  return false;
}

void PlaylistEditController::ResolveRefRecursively(const entry_ref &ref, std::vector<BString> &outPaths) {
  BEntry entry(&ref, true);
  if (!entry.Exists())
    return;

  BPath path;
  if (entry.GetPath(&path) != B_OK)
    return;

  if (entry.IsDirectory()) {
    std::stack<BString> stack;
    stack.push(path.Path());

    while (!stack.empty()) {
      BString currentPath = stack.top();
      stack.pop();

      BDirectory dir(currentPath.String());
      if (dir.InitCheck() != B_OK)
        continue;

      BEntry childEntry;
      dir.Rewind();
      while (dir.GetNextEntry(&childEntry, true) == B_OK) {
        BPath childPath;
        if (childEntry.GetPath(&childPath) != B_OK)
          continue;

        BString leaf(childPath.Leaf());
        if (leaf.Length() > 0 && leaf.ByteAt(0) == '.')
          continue;

        if (childEntry.IsDirectory()) {
          stack.push(childPath.Path());
        } else {
          BString filePath(childPath.Path());
          if (IsSupportedAudioFile(filePath)) {
            if (std::find(outPaths.begin(), outPaths.end(), filePath) == outPaths.end()) {
              outPaths.push_back(filePath);
            }
          }
        }
      }
    }
  } else {
    BString filePath(path.Path());
    if (IsSupportedAudioFile(filePath)) {
      if (std::find(outPaths.begin(), outPaths.end(), filePath) == outPaths.end()) {
        outPaths.push_back(filePath);
      }
    }
  }
}

PlaylistEditController::PlaylistEditController(MainWindow* window)
    : fWindow(window)  {
}

PlaylistEditController::~PlaylistEditController() {
    delete fWindow->fFilePanel;
}

void PlaylistEditController::PromptNewPlaylist(BMessage *msg) {
  fPendingPlaylistFiles.MakeEmpty();

  BMessage filesMsg;
  BString initialName;
  if (msg->FindMessage("files", &filesMsg) == B_OK) {
    fPendingPlaylistFiles = filesMsg;
    DEBUG_PRINT("%ld Files for new playlist buffered\n",
                (long)filesMsg.CountNames(B_REF_TYPE));
    
    entry_ref ref;
    if (filesMsg.FindRef("refs", 0, &ref) == B_OK) {
      BEntry entry(&ref, true);
      if (entry.Exists() && entry.IsDirectory()) {
        initialName = ref.name;
      }
    }
  }

  PlaylistNameDialog *prompt = new PlaylistNameDialog(BMessenger(fWindow));
  if (!initialName.IsEmpty()) {
    prompt->SetInitialName(initialName);
  }
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

  std::vector<BString> resolvedPaths;
  entry_ref ref;
  int32 i = 0;
  while (fPendingPlaylistFiles.FindRef("refs", i++, &ref) == B_OK) {
    ResolveRefRecursively(ref, resolvedPaths);
  }

  for (const auto &path : resolvedPaths) {
    fWindow->fPlaylistLibrary->AddItemToPlaylist(name, path);
    DEBUG_PRINT("File '%s' added to new playlist '%s'\n",
                path.String(), name.String());
  }
  fPendingPlaylistFiles.MakeEmpty();

  if (name == fWindow->fCurrentPlaylistName) {
    int32 selected = fWindow->fPlaylistLibrary->View()->CurrentSelection();
    if (selected >= 0) {
      BMessage selMsg(MSG_PLAYLIST_SELECTION);
      selMsg.AddInt32("index", selected);
      selMsg.AddString("name", name);
      if (fWindow->Lock()) {
        fWindow->fPlaylistSelectionController->HandlePlaylistSelection(&selMsg);
        fWindow->Unlock();
      }
    }
  }

  if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
    BMessage u(MSG_DELETE_PLAYLIST_BY_NAME);
    u.AddString("playlist", name);

    BMessage r(MSG_CREATE_PLAYLIST_WITH_PATHS);
    r.AddString("playlist", name);
    for (const auto &p : resolvedPaths) {
      r.AddString("paths", p);
    }

    fWindow->fUndoManager->RecordAction({u}, {r});
  }
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

  std::vector<BString> beforePaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlistName);

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

  std::vector<BString> afterPaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlistName);
  if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
    BMessage u(MSG_RESTORE_PLAYLIST_PATHS);
    u.AddString("playlist", playlistName);
    for (const auto &p : beforePaths) u.AddString("paths", p);

    BMessage r(MSG_RESTORE_PLAYLIST_PATHS);
    r.AddString("playlist", playlistName);
    for (const auto &p : afterPaths) r.AddString("paths", p);

    fWindow->fUndoManager->RecordAction({u}, {r});
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

  std::vector<BString> beforePaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlistName);

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

  std::vector<BString> afterPaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlistName);
  if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
    BMessage u(MSG_RESTORE_PLAYLIST_PATHS);
    u.AddString("playlist", playlistName);
    for (const auto &p : beforePaths) u.AddString("paths", p);

    BMessage r(MSG_RESTORE_PLAYLIST_PATHS);
    r.AddString("playlist", playlistName);
    for (const auto &p : afterPaths) r.AddString("paths", p);

    fWindow->fUndoManager->RecordAction({u}, {r});
  }
}

void PlaylistEditController::HandlePlaylistDrop(BMessage *msg) {
  DEBUG_PRINT("B_SIMPLE_DATA received!\n");

  BString destPlaylist;
  BMessage filesMsg;
  if (msg->FindString("playlist", &destPlaylist) == B_OK &&
      msg->FindMessage("files", &filesMsg) == B_OK) {
    
    if (destPlaylist.IsEmpty() || !fWindow->fPlaylistLibrary->IsPlaylistWritable(destPlaylist)) {
      return;
    }

    std::vector<BString> beforePaths = fWindow->fPlaylistLibrary->LoadPlaylist(destPlaylist);

    std::vector<BString> resolvedPaths;
    entry_ref ref;
    int32 i = 0;
    while (filesMsg.FindRef("refs", i++, &ref) == B_OK) {
      ResolveRefRecursively(ref, resolvedPaths);
    }

    if (resolvedPaths.empty())
      return;

    for (const auto &path : resolvedPaths) {
      fWindow->fPlaylistLibrary->AddItemToPlaylist(destPlaylist, path);
    }

    if (destPlaylist == fWindow->fCurrentPlaylistName) {
      int32 selected = fWindow->fPlaylistLibrary->View()->CurrentSelection();
      if (selected >= 0) {
        BMessage selMsg(MSG_PLAYLIST_SELECTION);
        selMsg.AddInt32("index", selected);
        selMsg.AddString("name", destPlaylist);
        if (fWindow->Lock()) {
          fWindow->fPlaylistSelectionController->HandlePlaylistSelection(&selMsg);
          fWindow->Unlock();
        }
      }
    }

    std::vector<BString> afterPaths = fWindow->fPlaylistLibrary->LoadPlaylist(destPlaylist);
    if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
      BMessage u(MSG_RESTORE_PLAYLIST_PATHS);
      u.AddString("playlist", destPlaylist);
      for (const auto &p : beforePaths) u.AddString("paths", p);

      BMessage r(MSG_RESTORE_PLAYLIST_PATHS);
      r.AddString("playlist", destPlaylist);
      for (const auto &p : afterPaths) r.AddString("paths", p);

      fWindow->fUndoManager->RecordAction({u}, {r});
    }
    return;
  }

  if (msg->HasRef("refs")) {
    if (fWindow->fCurrentPlaylistName.IsEmpty() ||
        !fWindow->fPlaylistLibrary->IsPlaylistWritable(fWindow->fCurrentPlaylistName)) {
      DEBUG_PRINT("Drop ignored: active playlist '%s' is not writable or empty\n",
                  fWindow->fCurrentPlaylistName.String());
      return;
    }

    std::vector<BString> beforePaths = fWindow->fPlaylistLibrary->LoadPlaylist(fWindow->fCurrentPlaylistName);

    std::vector<BString> resolvedPaths;
    entry_ref ref;
    int32 i = 0;
    while (msg->FindRef("refs", i++, &ref) == B_OK) {
      ResolveRefRecursively(ref, resolvedPaths);
    }

    if (resolvedPaths.empty())
      return;

    for (const auto &path : resolvedPaths) {
      fWindow->fPlaylistLibrary->AddItemToPlaylist(fWindow->fCurrentPlaylistName, path);
    }

    int32 selected = fWindow->fPlaylistLibrary->View()->CurrentSelection();
    if (selected >= 0) {
      BMessage selMsg(MSG_PLAYLIST_SELECTION);
      selMsg.AddInt32("index", selected);
      selMsg.AddString("name", fWindow->fCurrentPlaylistName);
      if (fWindow->Lock()) {
        fWindow->fPlaylistSelectionController->HandlePlaylistSelection(&selMsg);
        fWindow->Unlock();
      }
    }

    std::vector<BString> afterPaths = fWindow->fPlaylistLibrary->LoadPlaylist(fWindow->fCurrentPlaylistName);
    if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
      BMessage u(MSG_RESTORE_PLAYLIST_PATHS);
      u.AddString("playlist", fWindow->fCurrentPlaylistName);
      for (const auto &p : beforePaths) u.AddString("paths", p);

      BMessage r(MSG_RESTORE_PLAYLIST_PATHS);
      r.AddString("playlist", fWindow->fCurrentPlaylistName);
      for (const auto &p : afterPaths) r.AddString("paths", p);

      fWindow->fUndoManager->RecordAction({u}, {r});
    }
    return;
  }

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

  std::vector<BString> beforePaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlistName);

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

  std::vector<BString> afterPaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlistName);
  if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
    BMessage u(MSG_RESTORE_PLAYLIST_PATHS);
    u.AddString("playlist", playlistName);
    for (const auto &p : beforePaths) u.AddString("paths", p);

    BMessage r(MSG_RESTORE_PLAYLIST_PATHS);
    r.AddString("playlist", playlistName);
    for (const auto &p : afterPaths) r.AddString("paths", p);

    fWindow->fUndoManager->RecordAction({u}, {r});
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

  std::vector<BString> beforePaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlist);

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

  std::vector<BString> afterPaths = fWindow->fPlaylistLibrary->LoadPlaylist(playlist);
  if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
    BMessage u(MSG_RESTORE_PLAYLIST_PATHS);
    u.AddString("playlist", playlist);
    for (const auto &p : beforePaths) u.AddString("paths", p);

    BMessage r(MSG_RESTORE_PLAYLIST_PATHS);
    r.AddString("playlist", playlist);
    for (const auto &p : afterPaths) r.AddString("paths", p);

    fWindow->fUndoManager->RecordAction({u}, {r});
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
    std::vector<BString> beforePaths;
    for (int32 i = 0; i < cv->CountRows(); i++) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi) {
        beforePaths.push_back(mi->path);
      }
    }

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

    if (fWindow->fUndoManager) {
      BMessage u(MSG_RESTORE_PLAYLIST_PATHS);
      u.AddString("playlist", fWindow->fCurrentPlaylistName);
      for (const auto &p : beforePaths)
        u.AddString("paths", p);

      BMessage r(MSG_RESTORE_PLAYLIST_PATHS);
      r.AddString("playlist", fWindow->fCurrentPlaylistName);
      for (const auto &p : remainingPaths)
        r.AddString("paths", p);

      fWindow->fUndoManager->RecordAction({u}, {r});
    }
  }
}

void PlaylistEditController::MoveSelectedItemsToTrash() {
  if (fWindow->fCurrentPlaylistName.IsEmpty() ||
      !fWindow->fPlaylistLibrary->IsPlaylistWritable(fWindow->fCurrentPlaylistName)) {
    return;
  }

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  std::vector<BString> selectedPaths;
  BRow *row = nullptr;
  while ((row = cv->CurrentSelection(row)) != nullptr) {
    const MediaItem *mi = cv->ItemAt(cv->IndexOf(row));
    if (mi) {
      selectedPaths.push_back(mi->path);
    }
  }

  if (selectedPaths.empty())
    return;

  BPath trashPath;
  if (find_directory(B_TRASH_DIRECTORY, &trashPath) != B_OK) {
    fWindow->UpdateStatus(B_TRANSLATE("Failed to find Trash directory"));
    return;
  }

  std::vector<BMessage> undoMsgs;
  std::vector<BMessage> redoMsgs;

  // 1. Save before paths of the playlist
  std::vector<BString> beforePaths = fWindow->fPlaylistLibrary->LoadPlaylist(fWindow->fCurrentPlaylistName);

  // 2. Move files to trash
  for (const auto &path : selectedPaths) {
    BEntry entry(path.String(), true);
    if (!entry.Exists())
      continue;

    char leafName[B_FILE_NAME_LENGTH];
    entry.GetName(leafName);

    BPath targetTrashPath(trashPath.Path(), leafName);
    BEntry testEntry(targetTrashPath.Path());
    if (testEntry.Exists()) {
      BString nameStr(leafName);
      int32 extIdx = nameStr.FindLast('.');
      BString base = (extIdx != B_ERROR) ? nameStr.Left(extIdx) : nameStr;
      BString ext = (extIdx != B_ERROR) ? nameStr.Substring(extIdx) : "";
      int32 counter = 1;
      do {
        BString newLeafName;
        newLeafName << base << " " << counter << ext;
        targetTrashPath.SetTo(trashPath.Path(), newLeafName.String());
        testEntry.SetTo(targetTrashPath.Path());
        counter++;
      } while (testEntry.Exists());
    }

    BString uniqueTrashPath = targetTrashPath.Path();

    // Build the file move message
    BMessage moveMsg(MSG_FILE_MOVE);
    moveMsg.AddString("from", path);
    moveMsg.AddString("to", uniqueTrashPath);
    moveMsg.AddBool("undo_replay", true); // prevent individual undo recording

    // Execute the file move synchronously
    fWindow->fLibraryController->HandleFileMove(&moveMsg);

    // Record for Undo/Redo
    BMessage u(MSG_FILE_MOVE);
    u.AddString("from", uniqueTrashPath);
    u.AddString("to", path);
    undoMsgs.push_back(u);

    BMessage r(MSG_FILE_MOVE);
    r.AddString("from", path);
    r.AddString("to", uniqueTrashPath);
    redoMsgs.push_back(r);
  }

  // 3. Compute after paths of the playlist (remove selected ones)
  std::vector<BString> afterPaths;
  for (const auto &p : beforePaths) {
    if (std::find(selectedPaths.begin(), selectedPaths.end(), p) == selectedPaths.end()) {
      afterPaths.push_back(p);
    }
  }

  // Save the new playlist on disk
  fWindow->fPlaylistLibrary->SavePlaylist(fWindow->fCurrentPlaylistName, afterPaths);
  
  // Refresh the active ColumnView
  int32 selected = fWindow->fPlaylistLibrary->View()->CurrentSelection();
  if (selected >= 0) {
    BMessage selMsg(MSG_PLAYLIST_SELECTION);
    selMsg.AddInt32("index", selected);
    selMsg.AddString("name", fWindow->fCurrentPlaylistName);
    fWindow->fPlaylistSelectionController->HandlePlaylistSelection(&selMsg);
  }

  // 4. Record playlist changes
  BMessage uPlaylist(MSG_RESTORE_PLAYLIST_PATHS);
  uPlaylist.AddString("playlist", fWindow->fCurrentPlaylistName);
  for (const auto &p : beforePaths) {
    uPlaylist.AddString("paths", p);
  }
  undoMsgs.push_back(uPlaylist);

  BMessage rPlaylist(MSG_RESTORE_PLAYLIST_PATHS);
  rPlaylist.AddString("playlist", fWindow->fCurrentPlaylistName);
  for (const auto &p : afterPaths) {
    rPlaylist.AddString("paths", p);
  }
  redoMsgs.push_back(rPlaylist);

  // 5. Register with UndoManager
  if (fWindow->fUndoManager) {
    fWindow->fUndoManager->RecordAction(undoMsgs, redoMsgs);
  }

  BString statusMsg;
  statusMsg.SetToFormat(B_TRANSLATE("Moved %zu items to Trash."), selectedPaths.size());
  fWindow->UpdateStatus(statusMsg);
}

void PlaylistEditController::RestorePlaylistPaths(BMessage *msg) {
  BString playlistName;
  if (msg->FindString("playlist", &playlistName) != B_OK || playlistName.IsEmpty())
    return;

  std::vector<BString> paths;
  BString path;
  for (int32 i = 0; msg->FindString("paths", i, &path) == B_OK; ++i) {
    paths.push_back(path);
  }

  fWindow->fPlaylistLibrary->SavePlaylist(playlistName, paths);

  if (playlistName == fWindow->fCurrentPlaylistName) {
    int32 selected = fWindow->fPlaylistLibrary->View()->FindIndexByName(playlistName);
    if (selected >= 0) {
      BMessage selMsg(MSG_PLAYLIST_SELECTION);
      selMsg.AddInt32("index", selected);
      selMsg.AddString("name", playlistName);
      if (fWindow->Lock()) {
        fWindow->fPlaylistSelectionController->HandlePlaylistSelection(&selMsg);
        fWindow->Unlock();
      }
    }
  }
}

void PlaylistEditController::CreatePlaylistWithPaths(BMessage *msg) {
  BString playlistName;
  if (msg->FindString("playlist", &playlistName) != B_OK || playlistName.IsEmpty())
    return;

  std::vector<BString> paths;
  BString path;
  for (int32 i = 0; msg->FindString("paths", i, &path) == B_OK; ++i) {
    paths.push_back(path);
  }

  fWindow->fPlaylistLibrary->CreateNewPlaylist(playlistName);
  fWindow->fPlaylistLibrary->SavePlaylist(playlistName, paths);
  fWindow->fPlaylistLibrary->View()->SelectByName(playlistName);
}

void PlaylistEditController::DeletePlaylistByName(BMessage *msg) {
  BString playlistName;
  if (msg->FindString("playlist", &playlistName) != B_OK || playlistName.IsEmpty())
    return;

  fWindow->fPlaylistLibrary->DeletePlaylist(playlistName);
  int32 index = fWindow->fPlaylistLibrary->View()->FindIndexByName(playlistName);
  if (index >= 0) {
    fWindow->fPlaylistLibrary->View()->RemovePlaylistAt(index);
  }

  if (playlistName == fWindow->fCurrentPlaylistName) {
    if (fWindow->fPlaylistLibrary->View()->CountItems() > 0) {
      fWindow->fPlaylistLibrary->View()->Select(0);
      fWindow->fPlaylistLibrary->View()->SelectionChanged(0);
    }
  }
}

void PlaylistEditController::SelectPlaylistFolder() {
  delete fWindow->fFilePanel;
  fWindow->fFilePanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(fWindow), nullptr,
                              B_DIRECTORY_NODE, false,
                              new BMessage(MSG_PLAYLIST_FOLDER_SELECTED));
  fWindow->fFilePanel->Show();
}



