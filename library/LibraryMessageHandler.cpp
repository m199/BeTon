#include "LibraryController.h"
#include "LibraryMessageHandler.h"

#include "MainWindow.h"
#include "PlaylistEditController.h"
#include "Messages.h"
#include "PlaylistSelectionController.h"

#include <Message.h>

LibraryMessageHandler::LibraryMessageHandler(MainWindow *window)
    : fWindow(window) {}

bool LibraryMessageHandler::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_CACHE_LOADED: {
    fWindow->fLibraryController->HandleCacheLoaded();
    break;
  }

  case MSG_DELETE_ITEM: {
    fWindow->fPlaylistEditController->DeleteSelectedPlaylistItems();
    break;
  }

  case MSG_RESCAN_FULL: {
    fWindow->fLibraryController->StartFullRescan();
    break;
  }

  case MSG_SCAN_PROGRESS: {
    fWindow->fLibraryController->UpdateScanProgress(msg);
    break;
  }

  case MSG_SCAN_DONE: {
    fWindow->fLibraryController->HandleScanDone(msg);
    break;
  }

  case MSG_BATCH_TIMER: {
    fWindow->fLibraryController->HandleBatchTimer();
    break;
  }

  case MSG_MEDIA_BATCH: {
    fWindow->fLibraryController->HandleMediaBatch(msg);
    break;
  }

  case MSG_VIEWS_REFRESH_PARTIAL: {
    fWindow->fLibraryController->RefreshPartialViews();
    break;
  }

  case MSG_MEDIA_ITEM_FOUND: {
    fWindow->fLibraryController->HandleMediaItemFound(msg);
    break;
  }

  case MSG_MEDIA_ITEM_REMOVED: {
    fWindow->fLibraryController->HandleMediaItemRemoved(msg);
    break;
  }

  case MSG_FILE_MOVE: {
    fWindow->fLibraryController->HandleFileMove(msg);
    break;
  }

  case MSG_PLAYLIST_SELECTION:
  case MSG_INIT_LIBRARY: {
    if (fWindow->fPlaylistSelectionController)
      fWindow->fPlaylistSelectionController->HandlePlaylistSelection(msg);
    break;
  }

  default:
    return false;
  }

  return true;
}

