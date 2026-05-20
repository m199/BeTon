#include "PropertiesController.h"

#include "MainWindow.h"
#include "MetadataService.h"
#include "MetadataPropertiesWindow.h"
#include "LibraryBrowserController.h"
#include "MediaTableView.h"

#include "Messages.h"
#include "Debug.h"

#include <Catalog.h>
#include <Path.h>
#include <MessageRunner.h>
#include <File.h>
#include <unistd.h>
#include <Entry.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PropertiesController"

PropertiesController::PropertiesController(MainWindow* window)
    : fWindow(window) {
}

PropertiesController::~PropertiesController() {
}

/**
 * @brief Saves property edits asynchronously via `MetadataService`.
 * @param msg Message containing file list and edited metadata fields.
 */
void PropertiesController::SavePropertyTags(BMessage *msg) {
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
      DEBUG_PRINT("Set rating %ld for %s\n", (long)rating,
                  path.Path());
    }

    MediaTableView *cv =
        fWindow->fLibraryManager ? fWindow->fLibraryManager->ContentView() : nullptr;
    if (cv) {
      auto mapIt = fWindow->fPathIndex.find(path.Path());
      if (mapIt != fWindow->fPathIndex.end()) {
        fWindow->fAllItems[mapIt->second].rating = rating;
        cv->UpdateItem(fWindow->fAllItems[mapIt->second]);
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
