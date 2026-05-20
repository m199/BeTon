#include "MetadataSyncController.h"

#include "MainWindow.h"
#include "MetadataService.h"
#include "LibraryBrowserController.h"
#include "MediaTableView.h"
#include "MetadataTagIO.h"
#include "MetadataSyncConflictDialog.h"
#include "Messages.h"
#include "Debug.h"

#include <Catalog.h>
#include <Path.h>
#include <Message.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MetadataSyncController"

MetadataSyncController::MetadataSyncController(MainWindow* window)
    : fWindow(window) {
}

MetadataSyncController::~MetadataSyncController() {
    delete fActiveMetadataConflictDialog;
}

void MetadataSyncController::StartSmartSync() {
  std::vector<BString> files;

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  BRow *row = nullptr;
  while ((row = cv->CurrentSelection(row)) != nullptr) {
    int32 idx = cv->IndexOf(row);
    const MediaItem *mi = cv->ItemAt(idx);
    if (mi && !mi->path.IsEmpty())
      files.push_back(mi->path);
  }

  if (!files.empty() && fWindow->fMetadataService) {
    fWindow->UpdateStatus(B_TRANSLATE("Syncing metadata..."), true);

    BMessenger target(fWindow);
    MetadataService *handler = fWindow->fMetadataService;
    fWindow->LaunchThread("sync_metadata",
                 [files, target, handler]() { handler->SyncMetadata(files); });
  } else if (files.empty()) {
    fWindow->UpdateStatus(B_TRANSLATE("No files selected"), false);
  }
}

void MetadataSyncController::UpdateSyncProgress(BMessage *msg) {
  int32 current, total;
  if (msg->FindInt32("current", &current) == B_OK &&
      msg->FindInt32("total", &total) == B_OK) {
    BString status;
    status.SetToFormat(B_TRANSLATE("Syncing... %ld/%ld"), (long)current,
                       (long)total);
    fWindow->UpdateStatus(status, true);
  }
}

void MetadataSyncController::QueueSyncConflict(BMessage *msg) {
  DEBUG_PRINT("MSG_SYNC_CONFLICT received\n");

  fPendingConflicts.push_back(*msg);

  if (!fConflictDialogOpen) {
    ShowNextConflictDialog();
  } else if (fActiveMetadataConflictDialog) {
    int32 currentTotal =
        fConflictsProcessed + static_cast<int32>(fPendingConflicts.size());
    fActiveMetadataConflictDialog->UpdateTotal(currentTotal);
  }
}

void MetadataSyncController::ResolveSyncConflict(BMessage *msg) {
  BString path;
  bool useTags;
  if (msg->FindString("path", &path) == B_OK &&
      msg->FindBool("useTags", &useTags) == B_OK) {

    BPath filePath(path.String());
    TagData tags, bfs;
    MetadataTagIO::ReadTags(filePath, tags);
    MetadataTagIO::ReadBfsAttributes(filePath, bfs);

    bool directionTowardsBfs = useTags;
    TagData &source = useTags ? tags : bfs;

    MetadataTagIO::ApplySync(filePath, source, directionTowardsBfs);
    DEBUG_PRINT("Conflict resolved: wrote %s -> %s for %s\n",
                useTags ? "Tags" : "BFS", directionTowardsBfs ? "BFS" : "Tags",
                path.String());

    if (!directionTowardsBfs) {
      BMessage update(MSG_MEDIA_ITEM_FOUND);
      update.AddString("path", path);
      update.AddString("title", source.title);
      update.AddString("artist", source.artist);
      update.AddString("album", source.album);
      update.AddString("genre", source.genre);
      update.AddInt32("year", source.year);
      update.AddInt32("track", source.track);
      fWindow->PostMessage(&update);
    }
  }
  fConflictDialogOpen = false;
  ShowNextConflictDialog();
}

void MetadataSyncController::ResolveAllSyncConflicts(BMessage *msg) {
  BString currentPath;
  bool useTags;
  if (msg->FindString("path", &currentPath) == B_OK &&
      msg->FindBool("useTags", &useTags) == B_OK) {

    BPath currentFilePath(currentPath.String());
    TagData currentTags, currentBfs;
    MetadataTagIO::ReadTags(currentFilePath, currentTags);
    MetadataTagIO::ReadBfsAttributes(currentFilePath, currentBfs);

    bool directionTowardsBfs = useTags;
    TagData &currentSource = useTags ? currentTags : currentBfs;

    MetadataTagIO::ApplySync(currentFilePath, currentSource, directionTowardsBfs);
    DEBUG_PRINT("Apply All (current): wrote %s -> %s for %s\n",
                useTags ? "Tags" : "BFS", directionTowardsBfs ? "BFS" : "Tags",
                currentPath.String());

    for (auto &pending : fPendingConflicts) {
      BString path;
      if (pending.FindString("path", &path) == B_OK) {
        BPath filePath(path.String());
        TagData tags, bfs;
        MetadataTagIO::ReadTags(filePath, tags);
        MetadataTagIO::ReadBfsAttributes(filePath, bfs);
        TagData &source = useTags ? tags : bfs;

        MetadataTagIO::ApplySync(filePath, source, directionTowardsBfs);
        DEBUG_PRINT("Apply All: wrote %s -> %s for %s\n",
                    useTags ? "Tags" : "BFS",
                    directionTowardsBfs ? "BFS" : "Tags", path.String());
      }
    }
    fPendingConflicts.clear();
    fWindow->UpdateStatus(B_TRANSLATE("Sync complete"), false);
  }
  fConflictDialogOpen = false;
  fConflictsProcessed = 0;
  fActiveMetadataConflictDialog = nullptr;
}

void MetadataSyncController::CancelSyncConflicts() {
  fPendingConflicts.clear();
  fConflictDialogOpen = false;
  fConflictsProcessed = 0;
  fActiveMetadataConflictDialog = nullptr;
  fWindow->UpdateStatus(B_TRANSLATE("Sync cancelled"), false);
}

void MetadataSyncController::ShowNextConflictDialog() {
  if (fPendingConflicts.empty()) {
    fConflictsProcessed = 0;
    fWindow->UpdateStatus(B_TRANSLATE("Sync complete"), false);
    return;
  }

  BMessage pending = fPendingConflicts.front();
  fPendingConflicts.erase(fPendingConflicts.begin());

  BString path;
  if (pending.FindString("path", &path) == B_OK) {

    BPath filePath(path.String());
    TagData tags, bfs;
    MetadataTagIO::ReadTags(filePath, tags);
    MetadataTagIO::ReadBfsAttributes(filePath, bfs);

    DEBUG_PRINT("Conflict detected for %s:\n", path.String());
    tags.LogDifferences(bfs);

    fConflictsProcessed++;
    int32 currentTotal =
        fConflictsProcessed + static_cast<int32>(fPendingConflicts.size());

    fConflictDialogOpen = true;
    MetadataSyncConflictDialog *dialog =
        new MetadataSyncConflictDialog(BMessenger(fWindow), path, tags, bfs,
                               fConflictsProcessed - 1, currentTotal);
    fActiveMetadataConflictDialog = dialog;
    dialog->Show();
    if (path.Length() > 0)
      DEBUG_PRINT("ShowNextConflictDialog: %s (%d of %d)\n",
                  path.String(), (int)fConflictsProcessed, (int)currentTotal);
  }
}
