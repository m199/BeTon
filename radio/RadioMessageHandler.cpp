#include "RadioMessageHandler.h"

#include "MainWindow.h"
#include "Messages.h"
#include "RadioStationController.h"

#include <Message.h>

RadioMessageHandler::RadioMessageHandler(MainWindow *window)
    : fWindow(window) {}

bool RadioMessageHandler::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_RADIO_ADD: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->ShowAddStationDialog();
    break;
  }

  case MSG_RADIO_EDIT: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->ShowEditStationDialog();
    break;
  }

  case MSG_RADIO_DELETE: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->DeleteSelectedStation();
    break;
  }

  case MSG_RADIO_SAVE: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->SaveStation(msg);
    break;
  }

  case MSG_RADIO_IMPORT: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->ImportStations(msg);
    break;
  }

  case MainWindow::MSG_TOGGLE_RADIO: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->ToggleEnabled();
    break;
  }

  case MSG_STREAM_METADATA: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->HandleMetadata(msg);
    break;
  }

  case MSG_RADIO_FORMAT_UNSUPPORTED: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->ShowUnsupportedAlert();
    break;
  }

  case MSG_RADIO_CONNECTION_FAILED: {
    if (fWindow->fRadioStationController)
      fWindow->fRadioStationController->ShowConnectionFailedAlert(msg);
    break;
  }

  default:
    return false;
  }

  return true;
}
