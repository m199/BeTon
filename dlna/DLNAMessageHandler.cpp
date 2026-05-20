#include "DLNAMessageHandler.h"

#include "Config.h"
#include "DLNAViewController.h"
#include "MainWindow.h"
#include "Messages.h"

#include <Message.h>

DLNAMessageHandler::DLNAMessageHandler(MainWindow *window) : fWindow(window) {}

bool DLNAMessageHandler::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
#if ENABLE_DLNA_OUTPUT
  case MainWindow::MSG_SHOW_RENDERER_MENU: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->ShowRendererMenu();
    break;
  }
#endif

  case MSG_DLNA_DEVICE_FOUND: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->HandleDeviceFound();
    break;
  }

  case MSG_DLNA_DEVICE_LOST: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->HandleDeviceLost();
    break;
  }

  case MSG_DLNA_REFRESH: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->RefreshDevices();
    break;
  }

  case MSG_DLNA_REFRESH_CACHE: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->RefreshServerCache(msg);
    break;
  }

  case MSG_DLNA_SELECT_SERVER: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->SelectServerFromMessage(msg);
    break;
  }

  case MainWindow::MSG_TOGGLE_DLNA: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->ToggleEnabled();
    break;
  }

#if ENABLE_DLNA_OUTPUT
  case MainWindow::MSG_TOGGLE_RENDERER_BTN: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->ToggleRendererButton();
    break;
  }

  case MainWindow::MSG_RENDERER_SELECTED: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->SelectRenderer(msg);
    break;
  }
#endif

  case MSG_DLNA_CRAWL_PROGRESS: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->HandleCrawlProgress(msg);
    break;
  }

  case MSG_DLNA_CRAWL_DONE: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->HandleCrawlDone(msg);
    break;
  }

  case MSG_DLNA_RESOURCE_UNAVAILABLE: {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->ShowResourceUnavailableAlert(msg);
    break;
  }

  default:
    return false;
  }

  return true;
}
