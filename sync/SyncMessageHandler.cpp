#include "SyncMessageHandler.h"

#include "MainWindow.h"
#include "MetadataSyncController.h"
#include "Messages.h"

#include <Catalog.h>
#include <Message.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SyncMessageHandler"

SyncMessageHandler::SyncMessageHandler(MainWindow *window) : fWindow(window) {}

bool SyncMessageHandler::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_SYNC_SMART: {
    fWindow->fMetadataSyncController->StartSmartSync();
    break;
  }

  case MSG_SYNC_PROGRESS: {
    fWindow->fMetadataSyncController->UpdateSyncProgress(msg);
    break;
  }

  case MSG_SYNC_DONE:
    fWindow->UpdateStatus(B_TRANSLATE("Sync complete"), false);
    break;

  case MSG_SYNC_CONFLICT: {
    fWindow->fMetadataSyncController->QueueSyncConflict(msg);
    break;
  }

  case MSG_SYNC_CONFLICT_OK: {
    fWindow->fMetadataSyncController->ResolveSyncConflict(msg);
    break;
  }

  case MSG_SYNC_CONFLICT_ALL: {
    fWindow->fMetadataSyncController->ResolveAllSyncConflicts(msg);
    break;
  }

  case MSG_SYNC_CONFLICT_SKIP: {
    fWindow->fMetadataSyncController->CancelSyncConflicts();
    break;
  }

  default:
    return false;
  }

  return true;
}
