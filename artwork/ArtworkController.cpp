#include "ArtworkController.h"

#include "Config.h"
#include "MediaTableView.h"
#include "NowPlayingInfoPanel.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "MediaItem.h"
#include "Messages.h"
#include "MetadataService.h"
#include "PlaybackTransportController.h"
#include "RadioStationController.h"
#include "MetadataTagIO.h"

#include <Bitmap.h>
#include <ColumnListView.h>
#include <DataIO.h>
#include <HttpRequest.h>
#include <MenuItem.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <Url.h>
#include <UrlProtocolRoster.h>

#include <memory>

/**
 * @brief Constructs the artwork controller.
 * @param window Owning main window context.
 */
ArtworkController::ArtworkController(MainWindow *window) : fWindow(window) {}

/**
 * @brief Toggles artwork visibility and updates persisted settings.
 */
void ArtworkController::ToggleArtworkVisible() {
  if (!fWindow)
    return;

  fWindow->fShowCoverArt = !fWindow->fShowCoverArt;
  if (fWindow->fViewCoverItem)
    fWindow->fViewCoverItem->SetMarked(fWindow->fShowCoverArt);
  if (fWindow->fNowPlayingInfoPanel)
    fWindow->fNowPlayingInfoPanel->SetCoverVisible(fWindow->fShowCoverArt);
  fWindow->SaveSettings();
}

/**
 * @brief Enables artwork visibility and updates persisted settings.
 */
void ArtworkController::ShowCoverArt() {
  if (!fWindow)
    return;

  fWindow->fShowCoverArt = true;
  if (fWindow->fViewCoverItem)
    fWindow->fViewCoverItem->SetMarked(true);
  if (fWindow->fNowPlayingInfoPanel)
    fWindow->fNowPlayingInfoPanel->SetCoverVisible(true);
  fWindow->SaveSettings();
}

/**
 * @brief Handles asynchronous cover-bitmap responses.
 *
 * Applies the bitmap only when it matches current UI/playback context.
 */
void ArtworkController::HandleCoverBitmapReady(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  BBitmap *bmp = nullptr;
  msg->FindPointer("bitmap", (void **)&bmp);

  BString path;
  if (msg->FindString("path", &path) != B_OK) {
    delete bmp;
    return;
  }

  if (fWindow->fRadioStationController &&
      fWindow->fRadioStationController->IsCurrentCoverDownloadThread(
          find_thread(nullptr))) {
    fWindow->fRadioStationController->MarkCoverDownloadThreadDone();
  }

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  BRow *row = cv->CurrentSelection();
  bool match = false;
  if (row) {
    int32 idx = cv->IndexOf(row);
    const MediaItem *mi = cv->ItemAt(idx);
    if (mi && path == mi->path)
      match = true;
  }

  if (!match && path == cv->NowPlayingPath())
    match = true;

  if (!match && fWindow->fIsRadioMode && fWindow->fRadioStationController &&
      path == fWindow->fRadioStationController->ActiveStationUrl())
    match = true;

  if (!match && fWindow->fIsRadioMode && path.StartsWith("http") &&
      fWindow->fRadioStationController &&
      path == fWindow->fRadioStationController->ActiveStreamCoverUrl()) {
    match = true;
  }

  if (match && fWindow->fNowPlayingInfoPanel && fWindow->fShowCoverArt && bmp) {
    fWindow->fNowPlayingInfoPanel->SetCover(bmp);
    if (fWindow->fIsRadioMode && path.StartsWith("http")) {
      if (fWindow->fRadioStationController)
        fWindow->fRadioStationController->StoreActiveCover(bmp);
    }
  }

  delete bmp;
}

/**
 * @brief Starts cover lookup for the current now-playing item.
 *
 * Tries explicit cover URL first, then falls back to embedded artwork.
 */
void ArtworkController::FetchNowPlayingCover(const BString &path,
                                             bool isStream) {
  if (!fWindow->fShowCoverArt || !fWindow->fNowPlayingInfoPanel || path.IsEmpty())
    return;

  fWindow->fNowPlayingInfoPanel->ClearCover();

  BString urlToDownload;
  const MediaItem *nowPlaying =
      fWindow->fPlaybackTransportController
          ? fWindow->fPlaybackTransportController->NowPlayingItem()
          : nullptr;
  if (nowPlaying && !nowPlaying->coverUrl.IsEmpty()) {
    urlToDownload = nowPlaying->coverUrl;
  } else if (isStream) {
    for (const auto &mi : fWindow->fRadioItems) {
      if (mi.path == path && !mi.coverUrl.IsEmpty()) {
        urlToDownload = mi.coverUrl;
        break;
      }
    }
  }

  if (!urlToDownload.IsEmpty())
    DownloadCoverBitmap(path, urlToDownload);
  else
    FetchEmbeddedCoverBitmap(path);
}

/**
 * @brief Downloads artwork from a remote URL in a background thread.
 */
void ArtworkController::DownloadCoverBitmap(const BString &path,
                                            const BString &coverUrl) {
  BMessenger target(fWindow);
  fWindow->LaunchThread("dlna_cover_dl", [coverUrl, target, path]() {
    BMallocIO sink;
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
    BUrl burl(coverUrl.String());
#else
    BUrl burl(coverUrl.String(), true);
#endif
    std::unique_ptr<BPrivate::Network::BUrlRequest> req(
        BPrivate::Network::BUrlProtocolRoster::MakeRequest(burl, &sink));
    if (req) {
      if (auto http =
              dynamic_cast<BPrivate::Network::BHttpRequest *>(req.get())) {
        http->SetFollowLocation(true);
      }
      thread_id tid = req->Run();
      status_t exit;
      if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 5000000, &exit) ==
          B_OK) {
        sink.Seek(0, SEEK_SET);
        BBitmap *bitmap = BTranslationUtils::GetBitmap(&sink);
        if (bitmap) {
          BMessage update(MSG_COVER_BITMAP_READY);
          update.AddString("path", path);
          update.AddPointer("bitmap", bitmap);
          if (target.SendMessage(&update) != B_OK)
            delete bitmap;
        }
      } else {
        req->Stop();
      }
    }
  });
}

/**
 * @brief Extracts embedded artwork from a local media file asynchronously.
 */
void ArtworkController::FetchEmbeddedCoverBitmap(const BString &path) {
  BMessenger target(fWindow);
  BString pathStr = path;
  fWindow->LaunchThread("CoverFetch", [target, pathStr]() {
    BPath p(pathStr.String());
    CoverBlob cb;
    BBitmap *bmp = nullptr;

    if (MetadataTagIO::ExtractEmbeddedCover(p, cb) && cb.data() && cb.size() > 0) {
      BMemoryIO io(cb.data(), cb.size());
      bmp = BTranslationUtils::GetBitmap(&io);
    }

    if (target.IsValid()) {
      BMessage reply(MSG_COVER_BITMAP_READY);
      reply.AddString("path", pathStr);
      if (bmp)
        reply.AddPointer("bitmap", bmp);
      if (target.SendMessage(&reply) != B_OK)
        delete bmp;
    } else {
      delete bmp;
    }
  });
}

/**
 * @brief Applies dropped cover bytes to a single file.
 */
void ArtworkController::ApplyAlbumCover(BMessage *msg) {
  const void *data = nullptr;
  ssize_t size = 0;
  BString filePath;
  if (msg->FindString("file", &filePath) == B_OK &&
      msg->FindData("bytes", B_RAW_TYPE, &data, &size) == B_OK && size > 0) {
    fWindow->fMetadataService->ApplyAlbumCover(filePath, data, size);
    fWindow->UpdateFileInfo();
  }
}

/**
 * @brief Clears embedded artwork from a single file.
 */
void ArtworkController::ClearAlbumCover(BMessage *msg) {
  BString filePath;
  if (msg->FindString("file", &filePath) == B_OK) {
    fWindow->fMetadataService->ClearAlbumCover(filePath);
    fWindow->UpdateFileInfo();
  }
}

/**
 * @brief Applies cover updates to multiple files and refreshes visible item.
 */
void ArtworkController::ApplyDroppedCoverToAll(BMessage *msg) {
  fWindow->fMetadataService->ApplyCoverToAll(msg);

  MediaTableView *cv =
      fWindow->fLibraryManager ? fWindow->fLibraryManager->ContentView()
                               : nullptr;
  const MediaItem *selected = cv ? cv->SelectedItem() : nullptr;
  if (!selected || selected->path.IsEmpty())
    return;

  bool selectedAffected = false;
  BString path;
  int32 i = 0;
  while (msg->FindString("file", i++, &path) == B_OK) {
    if (path == selected->path) {
      selectedAffected = true;
      break;
    }
  }

  if (!selectedAffected)
    return;

  bool clearCover = false;
  msg->FindBool("clear_cover", &clearCover);
  if (clearCover) {
    if (fWindow->fNowPlayingInfoPanel)
      fWindow->fNowPlayingInfoPanel->ClearCover();
  } else {
    FetchEmbeddedCoverBitmap(selected->path);
  }
}

/**
 * @brief Returns embedded cover bytes for metadata properties UI.
 */
void ArtworkController::RequestEmbeddedCover(BMessage *msg) {
  BString file;
  if (msg->FindString("file", &file) != B_OK || file.IsEmpty())
    return;

  CoverBlob cover;
  if (MetadataTagIO::ExtractEmbeddedCover(BPath(file.String()), cover)) {
    BMessage reply(MSG_PROP_SET_COVER_DATA);
    reply.AddData("bytes", B_RAW_TYPE, cover.data(), (ssize_t)cover.size());

    BMessenger sender = msg->ReturnAddress();
    sender.SendMessage(&reply);
  }
}
