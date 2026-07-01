#include "PropertiesController.h"

#include "MainWindow.h"
#include "MetadataService.h"
#include "MetadataPropertiesWindow.h"
#include "LibraryBrowserController.h"
#include "MediaTableView.h"

#include "Messages.h"
#include "Debug.h"
#include "UndoManager.h"
#include "MetadataTagIO.h"

#include <Catalog.h>
#include <Path.h>
#include <MessageRunner.h>
#include <File.h>
#include <unistd.h>
#include <Entry.h>
#include <vector>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PropertiesController"

PropertiesController::PropertiesController(MainWindow* window)
    : fWindow(window) {
}

PropertiesController::~PropertiesController() {
}

/**
 * @brief Captures pre-save field values as an undo action.
 * @param msg The pending MSG_PROP_SAVE message.
 *
 * For every file in the message, the current library values of the
 * fields being changed are packed into a replay MSG_PROP_SAVE that
 * restores them. Skipped entirely when the save is itself a replay.
 */
void PropertiesController::_RecordUndoForPropertySave(BMessage *msg) {
  if (msg->HasBool("undo_replay") || !fWindow->fUndoManager)
    return;

  std::vector<BMessage> undoMsgs;
  BString file;
  for (int32 i = 0; msg->FindString("file", i, &file) == B_OK; ++i) {
    auto it = fWindow->fPathIndex.find(file);
    if (it == fWindow->fPathIndex.end())
      continue;
    const MediaItem &old = fWindow->fAllItems[it->second];

    BMessage u(MSG_PROP_SAVE);
    u.AddString("file", file);
    bool any = false;

    auto addOld = [&](const char *field, const BString &val) {
      if (msg->HasString(field)) {
        u.AddString(field, val);
        any = true;
      }
    };
    addOld("title", old.title);
    addOld("artist", old.artist);
    addOld("album", old.album);
    addOld("albumArtist", old.albumArtist);
    addOld("composer", old.composer);
    addOld("genre", old.genre);
    addOld("comment", old.comment);
    addOld("mbAlbumID", old.mbAlbumId);
    addOld("mbArtistID", old.mbArtistId);
    addOld("mbTrackID", old.mbTrackId);

    auto addOldInt = [&](const char *field, int32 val) {
      if (msg->HasString(field)) {
        BString s;
        s << val;
        u.AddString(field, s);
        any = true;
      }
    };
    addOldInt("year", old.year);
    addOldInt("track", old.track);
    addOldInt("trackTotal", old.trackTotal);
    addOldInt("disc", old.disc);
    addOldInt("discTotal", old.discTotal);

    if (msg->HasInt32("rating")) {
      u.AddInt32("rating", old.rating);
      any = true;
    }

    if (any)
      undoMsgs.push_back(u);
  }

  if (!undoMsgs.empty())
    fWindow->fUndoManager->RecordAction(std::move(undoMsgs),
                                        {BMessage(*msg)});
}

/**
 * @brief Saves property edits asynchronously via `MetadataService`.
 * @param msg Message containing file list and edited metadata fields.
 */
void PropertiesController::SavePropertyTags(BMessage *msg) {
  _RecordUndoForPropertySave(msg);

  BString tmp;
  if (msg->FindString("mbAlbumID", &tmp) == B_OK)
    DEBUG_PRINT("PROP_SAVE: mbAlbumID='%s'\n", tmp.String());
  if (msg->FindString("mbTrackID", &tmp) == B_OK)
    DEBUG_PRINT("PROP_SAVE: mbTrackID='%s'\n", tmp.String());
  if (msg->FindString("disc", &tmp) == B_OK)
    DEBUG_PRINT("PROP_SAVE: disc='%s'\n", tmp.String());

  auto *copy = new BMessage(*msg);
  MetadataService *metadataHandler = fWindow->fMetadataService;
  thread_id thread = fWindow->LaunchThread("save property tags",
      [metadataHandler, copy]() {
        if (metadataHandler)
          metadataHandler->SaveTags(copy);
        delete copy;
      });

  if (thread < 0) {
    if (metadataHandler)
      metadataHandler->SaveTags(msg);
    delete copy;
  } else {
    fWindow->UpdateStatus(B_TRANSLATE("Saving metadata..."));
  }
}

static void CollectPathsFromMessage(const BMessage *msg,
                                    std::vector<BPath> &out) {
  out.clear();
  if (!msg)
    return;

  BString s;
  int32 i = 0;
  while (msg->FindString("file", i++, &s) == B_OK)
    if (!s.IsEmpty())
      out.emplace_back(s.String());
  if (!out.empty())
    return;

  entry_ref ref;
  int32 r = 0;
  while (msg->FindRef("refs", r++, &ref) == B_OK) {
    BEntry e(&ref, true);
    if (e.InitCheck() == B_OK && e.Exists()) {
      BPath p;
      if (e.GetPath(&p) == B_OK)
        out.push_back(p);
    }
  }
}

/**
 * @brief Opens properties window in single, multi, or context mode.
 * @param msg Message containing explicit file paths or refs.
 */
void PropertiesController::OpenMetadataPropertiesWindow(BMessage *msg) {
  std::vector<BPath> files;
  CollectPathsFromMessage(msg, files);

  if (files.empty()) {
    BRow *row = nullptr;
    MediaTableView *cv = fWindow->fLibraryManager->ContentView();
    while ((row = cv->CurrentSelection(row)) != nullptr) {
      int32 idx = cv->IndexOf(row);
      BString path = fWindow->GetPathForContentItem(idx);
      if (!path.IsEmpty())
        files.emplace_back(path.String());
    }
  }

  if (files.empty()) {
    DEBUG_PRINT(
        "No paths in MSG_PROPERTIES (file/refs + selection "
        "empty)\n");
    return;
  }

  if (files.size() == 1) {
    std::vector<BPath> contextFiles;
    int32 selectionIndex = 0;

    MediaTableView *cv = fWindow->fLibraryManager->ContentView();
    int32 count = cv->CountRows();

    contextFiles.reserve(count);

    BString targetPath = files[0].Path();

    for (int32 i = 0; i < count; ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi) {
        contextFiles.emplace_back(mi->path.String());
        if (mi->path == targetPath)
          selectionIndex = (int32)contextFiles.size() - 1;
      }
    }

    fWindow->fMetadataPropertiesWindow =
        new MetadataPropertiesWindow(contextFiles, selectionIndex, BMessenger(fWindow));
  } else {
    std::vector<MediaItem> preloadedItems;
    preloadedItems.reserve(files.size());
    bool haveAllPreloaded = true;
    for (const auto &path : files) {
      auto it = fWindow->fPathIndex.find(path.Path());
      if (it == fWindow->fPathIndex.end() ||
          it->second >= fWindow->fAllItems.size()) {
        haveAllPreloaded = false;
        break;
      }
      preloadedItems.push_back(fWindow->fAllItems[it->second]);
    }

    if (haveAllPreloaded)
      fWindow->fMetadataPropertiesWindow =
          new MetadataPropertiesWindow(files, preloadedItems, BMessenger(fWindow));
    else
      fWindow->fMetadataPropertiesWindow =
          new MetadataPropertiesWindow(files, BMessenger(fWindow));
  }
  fWindow->fMetadataPropertiesWindow->Show();
}

/**
 * @brief Writes BFS rating attribute for selected files and updates view.
 * @param msg Message with rating and selected refs.
 */
void PropertiesController::SetRating(BMessage *msg) {
  int32 rating = 0;
  if (msg->FindInt32("rating", &rating) != B_OK)
    return;

  BMessage files;
  if (msg->FindMessage("files", &files) != B_OK)
    return;

  // Record per-file old ratings as an undo action (skip replays).
  if (!msg->HasBool("undo_replay") && fWindow->fUndoManager) {
    std::vector<BMessage> undoMsgs;
    entry_ref undoRef;
    for (int32 i = 0; files.FindRef("refs", i, &undoRef) == B_OK; i++) {
      BPath p(&undoRef);
      if (p.InitCheck() != B_OK)
        continue;
      auto it = fWindow->fPathIndex.find(p.Path());
      if (it == fWindow->fPathIndex.end())
        continue;
      BMessage u(MSG_SET_RATING);
      u.AddInt32("rating", fWindow->fAllItems[it->second].rating);
      BMessage uf;
      uf.AddRef("refs", &undoRef);
      u.AddMessage("files", &uf);
      undoMsgs.push_back(u);
    }
    if (!undoMsgs.empty())
      fWindow->fUndoManager->RecordAction(std::move(undoMsgs),
                                          {BMessage(*msg)});
  }

  int32 skipped = 0;
  entry_ref ref;
  for (int32 i = 0; files.FindRef("refs", i, &ref) == B_OK; i++) {
    BPath path(&ref);
    if (path.InitCheck() != B_OK)
      continue;

    if (access(path.Path(), W_OK) != 0) {
      skipped++;
      continue;
    }

    BFile file(path.Path(), B_READ_WRITE);
    if (file.InitCheck() == B_OK) {
      file.WriteAttr("Media:Rating", B_INT32_TYPE, 0, &rating, sizeof(rating));
      DEBUG_PRINT("Set rating %ld for %s\n", (long)rating, path.Path());

      if (!MetadataTagIO::IsBeFsVolume(path)) {
        TagData td;
        MetadataTagIO::ReadTags(path, td);
        td.rating = (uint32)rating;
        MetadataTagIO::WriteTags(path, td);
        DEBUG_PRINT("Wrote rating %ld to embedded tags for %s\n",
                    (long)rating, path.Path());

        // On non-BFS volumes the live query never fires — update in-memory
        // state and views directly, mirroring what the BFS query path does
        // but without going through HandleMediaItemFound (which normalizes
        // the path and can create a duplicate item triggering a full rebuild).
        BString pathStr(path.Path());
        auto mapIt = fWindow->fPathIndex.find(pathStr);
        if (mapIt != fWindow->fPathIndex.end()) {
          MediaItem &mi = fWindow->fAllItems[mapIt->second];
          mi.rating = rating;

          // Replicate exactly what MetadataService::SaveTags does after a
          // successful write: send MSG_MEDIA_ITEM_FOUND with all fields to
          // the window via BMessenger. This lets the window's message loop
          // handle the view update cleanly, just like the Apply button path.
          BMessage update(MSG_MEDIA_ITEM_FOUND);
          update.AddString("path", pathStr);
          update.AddString("title", mi.title);
          update.AddString("artist", mi.artist);
          update.AddString("album", mi.album);
          update.AddString("genre", mi.genre);
          update.AddString("comment", mi.comment);
          update.AddString("albumArtist", mi.albumArtist);
          update.AddString("composer", mi.composer);
          update.AddInt32("year", mi.year);
          update.AddInt32("track", mi.track);
          update.AddInt32("trackTotal", mi.trackTotal);
          update.AddInt32("disc", mi.disc);
          update.AddInt32("discTotal", mi.discTotal);
          update.AddInt32("rating", rating);
          update.AddInt32("duration", mi.duration);
          update.AddInt32("bitrate", mi.bitrate);
          BMessenger(fWindow).SendMessage(&update);
        } else {
          // Folder Mode on non-BFS: item not in library cache — use the
          // TagData already read above to send a full update so the view
          // row is not left with empty fields.
          BMessage update(MSG_MEDIA_ITEM_FOUND);
          update.AddString("path", pathStr);
          update.AddString("title", td.title);
          update.AddString("artist", td.artist);
          update.AddString("album", td.album);
          update.AddString("genre", td.genre);
          update.AddString("comment", td.comment);
          update.AddString("albumArtist", td.albumArtist);
          update.AddString("composer", td.composer);
          update.AddInt32("year",       (int32)td.year);
          update.AddInt32("track",      (int32)td.track);
          update.AddInt32("trackTotal", (int32)td.trackTotal);
          update.AddInt32("disc",       (int32)td.disc);
          update.AddInt32("discTotal",  (int32)td.discTotal);
          update.AddInt32("rating",     rating);
          update.AddInt32("duration",   (int32)td.lengthSec);
          update.AddInt32("bitrate",    (int32)td.bitrate);
          BMessenger(fWindow).SendMessage(&update);
        }
      }
    }
  }

  if (skipped > 0)
    fWindow->UpdateStatus(B_TRANSLATE("Rating: some files are read-only."));
  else
    fWindow->UpdateStatus(B_TRANSLATE("Rating updated."));

  BMessage resetMsg(MSG_RESET_STATUS);
  delete fWindow->fStatusRunner;
  fWindow->fStatusRunner = new BMessageRunner(BMessenger(fWindow), &resetMsg, 2000000, 1);
}
