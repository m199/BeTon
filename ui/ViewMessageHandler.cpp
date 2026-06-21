#include "ViewStateController.h"
#include "ViewMessageHandler.h"

#include "MainWindow.h"
#include "Messages.h"
#include "ArtworkController.h"

#include <Message.h>

ViewMessageHandler::ViewMessageHandler(MainWindow *window)
    : fWindow(window) {}

bool ViewMessageHandler::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_ARTWORK_ON:
    fWindow->fViewStateController->ToggleArtworkVisible();
    return true;

  case MSG_FILEINFO_ON:
    fWindow->fViewStateController->ToggleFileInfoVisible();
    return true;

  case MSG_TOGGLE_FILTERS: {
    fWindow->fViewStateController->ToggleFilters();
    return true;
  }

  case MSG_ARTWORK_OFF:
  case MSG_FILEINFO_OFF:
    /**
     * @brief Deprecated legacy messages retained as no-op for compatibility.
     */
    return true;

  case MSG_VIEW_INFO:
    fWindow->fViewStateController->ShowFileInfo();
    return true;

  case MSG_VIEW_COVER:
    fWindow->fViewStateController->ShowCoverArt();
    return true;

  case MSG_SEEKBAR_COLOR_DROPPED: {
    fWindow->fViewStateController->ApplyDroppedSeekBarColor(msg);
    return true;
  }

  case MSG_SELECTION_COLOR_SYSTEM:
    fWindow->fViewStateController->UseSystemSelectionColor();
    return true;

  case MSG_SELECTION_COLOR_MATCH:
    fWindow->fViewStateController->UseSeekBarSelectionColor();
    return true;

  case MSG_TOOLTIPS_ON:
    fWindow->fViewStateController->SetTooltipsEnabled(true);
    return true;

  case MSG_TOOLTIPS_OFF:
    fWindow->fViewStateController->SetTooltipsEnabled(false);
    return true;

  case MSG_TOGGLE_FAST_EDIT:
    fWindow->fViewStateController->SetFastEditEnabled(
        !fWindow->fFastEditEnabled);
    return true;

  case B_COLORS_UPDATED:
    fWindow->fViewStateController->HandleColorsUpdated();
    return true;

  case MSG_SELECTION_CHANGED_CONTENT: {
    fWindow->fViewStateController->HandleContentSelectionChanged();
    break;
  }

  case MSG_COVER_BITMAP_READY: {
    if (fWindow->fArtworkController)
      fWindow->fArtworkController->HandleCoverBitmapReady(msg);
    break;
  }

  default:
    return false;
  }

  return true;
}

