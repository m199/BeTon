#include "StatusBarController.h"

#include "MediaTableView.h"
#include "DLNAService.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "MediaItem.h"
#include "Messages.h"
#include "RadioStationLibrary.h"

#include <Catalog.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <StringView.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StatusBarController"

StatusBarController::StatusBarController(MainWindow *window) : fWindow(window) {}

bool StatusBarController::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_SEARCH_MODIFY:
    ScheduleSearchUpdate();
    break;

  case MSG_SEARCH_EXECUTE:
    fWindow->UpdateFilteredViews();
    break;

  case MSG_VIEWS_REFRESH:
    fWindow->UpdateFilteredViews(true);
    break;

  case MSG_SELECTION_CHANGED_GENRE:
  case MSG_SELECTION_CHANGED_ALBUM:
  case MSG_SELECTION_CHANGED_ARTIST:
    fWindow->UpdateFilteredViews();
    break;

  case MSG_RESET_STATUS:
    UpdateLibraryStatus();
    break;

  case MSG_STATUS_UPDATE:
    ApplyStatusUpdate(msg);
    break;

  case MSG_LIBRARY_PREVIEW:
    UpdateLibraryPreview(msg);
    break;

  case MSG_COUNT_UPDATED:
    UpdateLibraryStatus();
    break;

  default:
    return false;
  }

  return true;
}

void StatusBarController::ScheduleSearchUpdate() {
  delete fWindow->fSearchRunner;
  BMessage exec(MSG_SEARCH_EXECUTE);
  fWindow->fSearchRunner =
      new BMessageRunner(BMessenger(fWindow), &exec, 300000, 1);
}

void StatusBarController::ApplyStatusUpdate(BMessage *msg) {
  BString text;
  if (msg->FindString("text", &text) == B_OK) {
    bool permanent = msg->GetBool("isPermanent", false);
    UpdateStatus(text, permanent);
  }
}

void StatusBarController::UpdateLibraryPreview(BMessage *msg) {
  if (fWindow->fIsDlnaMode) {
    UpdateLibraryStatus();
    return;
  }

  int32 count = 0;
  int64 duration = 0;
  if (msg->FindInt32("count", &count) != B_OK)
    return;

  if (msg->FindInt64("duration", &duration) != B_OK)
    duration = 0;

  BString text;
  if (duration > 0) {
    int32 h = duration / 3600;
    int32 m = (duration % 3600) / 60;
    int32 s = duration % 60;
    if (h > 0)
      text.SetToFormat(
          B_TRANSLATE("%ld  tracks. Total duration %02d:%02d:%02d"),
          (long)count, (int)h, (int)m, (int)s);
    else
      text.SetToFormat(B_TRANSLATE("%ld  tracks. Total duration %02d:%02d"),
                       (long)count, (int)m, (int)s);
  } else {
    text.SetToFormat(B_TRANSLATE("%ld tracks"), (long)count);
  }

  if (fWindow->fStatusLabel)
    fWindow->fStatusLabel->SetText(text.String());
}

void StatusBarController::UpdateStatus(const BString &text, bool isPermanent) {
  if (fWindow->fStatusLabel)
    fWindow->fStatusLabel->SetText(text.String());

  delete fWindow->fStatusRunner;
  fWindow->fStatusRunner = nullptr;

  if (!isPermanent) {
    BMessage msg(MSG_RESET_STATUS);
    fWindow->fStatusRunner =
        new BMessageRunner(BMessenger(fWindow), &msg, 5000000, 1);
  }
}

void StatusBarController::UpdateLibraryStatus() {
  if (fWindow->fIsDlnaMode && fWindow->fDlnaManager) {
    if (fWindow->fDlnaCrawling) {
      BString status;
      if (!fWindow->fActiveDlnaServer.friendlyName.IsEmpty()) {
        status.SetToFormat(B_TRANSLATE("Indexing '%s'..."),
                           fWindow->fActiveDlnaServer.friendlyName.String());
      } else {
        status = B_TRANSLATE("Indexing DLNA server...");
      }
      UpdateStatus(status, true);
      return;
    }

    if (fWindow->fActiveDlnaServer.uuid.IsEmpty()) {
      auto servers = fWindow->fDlnaManager->Servers();
      if (servers.empty())
        UpdateStatus(B_TRANSLATE("Searching for DLNA devices..."), true);
      else
        UpdateStatus(B_TRANSLATE("Preparing DLNA scan..."), true);
      return;
    }

    int32 count = 0;
    if (fWindow->fLibraryManager && fWindow->fLibraryManager->ContentView())
      count = fWindow->fLibraryManager->ContentView()->CountRows();

    if (count == 0) {
      BString status;
      status.SetToFormat(B_TRANSLATE("Preparing DLNA scan for '%s'..."),
                         fWindow->fActiveDlnaServer.friendlyName.String());
      UpdateStatus(status, true);
    } else {
      BString status;
      status.SetToFormat("%ld items from '%s'", (long)count,
                         fWindow->fActiveDlnaServer.friendlyName.String());
      UpdateStatus(status, true);
    }
    return;
  }

  if (fWindow->fIsRadioMode && fWindow->fRadioStationLibrary) {
    BString status;
    status.SetToFormat("%ld stations",
                       (long)fWindow->fRadioStationLibrary->CountStations());
    UpdateStatus(status, true);
    return;
  }

  if (!fWindow->fCacheLoaded)
    return;

  int32 count = 0;
  int64 totalSeconds = 0;

  if (fWindow->fLibraryManager && fWindow->fLibraryManager->ContentView()) {
    MediaTableView *cv = fWindow->fLibraryManager->ContentView();
    count = cv->CountRows();
    for (int32 i = 0; i < count; ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi)
        totalSeconds += mi->duration;
    }
  } else {
    count = fWindow->fAllItems.size();
    for (const auto &mi : fWindow->fAllItems)
      totalSeconds += mi.duration;
  }

  if (count == 0) {
    UpdateStatus(B_TRANSLATE("No results found."), true);
    return;
  }

  int32 hours = totalSeconds / 3600;
  int32 mins = (totalSeconds % 3600) / 60;
  int32 secs = totalSeconds % 60;

  BString s;
  if (hours > 0)
    s.SetToFormat(B_TRANSLATE("%ld tracks. Total duration %02d:%02d:%02d"),
                  (long)count, (int)hours, (int)mins, (int)secs);
  else
    s.SetToFormat(B_TRANSLATE("%ld tracks. Total duration %02d:%02d"),
                  (long)count, (int)mins, (int)secs);

  UpdateStatus(s.String(), true);
}
