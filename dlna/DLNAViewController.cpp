#include "DLNAViewController.h"

#include "MediaTableView.h"
#include "Debug.h"
#include "NowPlayingInfoPanel.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "AudioPlaybackEngine.h"
#include "Messages.h"
#include "PlaybackQueueManager.h"
#include "PlaylistSidebarView.h"
#include "PlaylistLibrary.h"
#include "RadioStationController.h"

#include <Alert.h>
#include <Catalog.h>
#include <Button.h>
#include <Menu.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Slider.h>
#include <StringView.h>
#include <Url.h>
#include <algorithm>
#include <cstdio>
#include <set>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DLNAViewController"

static int32 ParseDurationString(const BString &dur) {
  int h = 0, m = 0, s = 0;
  if (sscanf(dur.String(), "%d:%2d:%2d", &h, &m, &s) >= 2)
    return (int32)(h * 3600 + m * 60 + s);
  return 0;
}

static BString NormalizedDlnaDedupPart(const BString &value) {
  BString normalized(value);
  normalized.Trim();
  normalized.ToLower();
  return normalized;
}

static BString DlnaDedupKey(const DLNABrowseItem &item) {
  if (!item.title.IsEmpty() &&
      (!item.duration.IsEmpty() || !item.artist.IsEmpty() ||
       !item.album.IsEmpty() || !item.resourceSize.IsEmpty())) {
    BString key("meta:");
    key << NormalizedDlnaDedupPart(item.title) << "\n"
        << NormalizedDlnaDedupPart(item.artist) << "\n"
        << NormalizedDlnaDedupPart(item.album) << "\n"
        << NormalizedDlnaDedupPart(item.duration) << "\n"
        << NormalizedDlnaDedupPart(item.resourceSize);
    return key;
  }

  if (!item.refId.IsEmpty()) {
    BString key("ref:");
    key << item.refId;
    return key;
  }

  if (!item.resourceUrl.IsEmpty()) {
    BString key("url:");
    key << item.resourceUrl;
    return key;
  }

  BString key("id:");
  key << item.id;
  return key;
}

DLNAViewController::DLNAViewController(MainWindow *window) : fWindow(window) {}

/**
 * @brief Applies remote renderer volume updates to local UI state.
 * @param msg Message containing `volume` and optional `initial_sync` flag.
 */
void DLNAViewController::ApplyVolumeUpdate(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  if (system_time() - fWindow->fLastUserVolumeChange < 1500000)
    return;

  int32 vol = 0;
  if (msg->FindInt32("volume", &vol) == B_OK && fWindow->fVolumeSlider) {
    if (fWindow->fVolumeSlider->Value() != vol)
      fWindow->fVolumeSlider->SetValue(vol);

    bool initialSync = false;
    if (msg->FindBool("initial_sync", &initialSync) == B_OK && initialSync) {
      float linear = vol / 100.0f;
      fWindow->fPlaybackEngine->SetVolume(linear * linear);
    }
  }
}

void DLNAViewController::ShowResourceUnavailableAlert(BMessage *msg) {
  if (!fWindow)
    return;

  BString title;
  if (msg)
    msg->FindString("title", &title);

  BString text;
  if (title.IsEmpty()) {
    text = B_TRANSLATE("This DLNA item is no longer available. The server may "
                       "be offline or the file may have been removed.");
  } else {
    text.SetToFormat(
        B_TRANSLATE("'%s' is no longer available. The server may be offline "
                    "or the file may have been removed."),
        title.String());
  }

  BAlert *alert =
      new BAlert(B_TRANSLATE("DLNA item unavailable"), text.String(),
                 B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL,
                 B_WARNING_ALERT);
  alert->Go();

  if (fWindow->fTitleView)
    fWindow->fTitleView->SetText(B_TRANSLATE("Ready"));
  fWindow->UpdateStatus(B_TRANSLATE("DLNA item unavailable"), false);
}

/**
 * @brief Builds a DLNA play queue from the current view and starts playback.
 * @param view Source table view.
 * @param rowIndex Selected row index to start from.
 */
void DLNAViewController::PlaySelection(MediaTableView *view, int32 rowIndex) {
  if (!fWindow || !view)
    return;

  if (fWindow->fRadioStationController)
    fWindow->fRadioStationController->CancelQueuedPlay();
  fPlayQueue.clear();

  int32 count = view->CountRows();
  for (int32 i = 0; i < count; i++) {
    if (const MediaItem *mi = view->ItemAt(i))
      fPlayQueue.push_back(*mi);
  }

  if (rowIndex >= 0 && rowIndex < (int32)fPlayQueue.size())
    PlayIndex(rowIndex);
}

/**
 * @brief Activates the DLNA source view and selects an initial server.
 */
void DLNAViewController::ShowPlaylistSource() {
  if (!fWindow || !fWindow->fDlnaManager || !fWindow->fLibraryManager)
    return;

  fWindow->fLibraryManager->ContentView()->SetRadioMode(false);
  fWindow->fLibraryManager->SetRadioFilterMode(false);
  fWindow->fLibraryManager->SetActivePaths({});
  fWindow->fDlnaManager->StartDiscovery();
  SetServerFieldVisible(true);
  RebuildServerMenu();

  auto servers = fWindow->fDlnaManager->Servers();
  if (!servers.empty()) {
    BString targetUuid = servers[0].uuid;
    if (!fWindow->fInitialDlnaUuid.IsEmpty()) {
      for (const auto &s : servers) {
        if (s.uuid == fWindow->fInitialDlnaUuid) {
          targetUuid = s.uuid;
          break;
        }
      }
    }
    SelectServer(targetUuid);
  } else {
    ShowServers();
    fWindow->UpdateStatus(B_TRANSLATE("Searching for DLNA devices..."), true);
  }
  fWindow->fInitialDlnaUuid = "";
}

#if ENABLE_DLNA_OUTPUT
/**
 * @brief Opens the renderer selection popup at the renderer button.
 */
void DLNAViewController::ShowRendererMenu() {
  if (!fWindow || !fWindow->fRendererMenu || !fWindow->fBtnRenderer)
    return;

  BPoint p = fWindow->fBtnRenderer->Bounds().LeftBottom();
  fWindow->fBtnRenderer->ConvertToScreen(&p);
  BMenuItem *item = fWindow->fRendererMenu->Go(p);
  if (item && item->Message()) {
    BMessage rendererMessage(*item->Message());
    fWindow->PostMessage(&rendererMessage);
  }
}
#else
/**
 * @brief No-op when DLNA output support is disabled at compile time.
 */
void DLNAViewController::ShowRendererMenu() {}
#endif

/**
 * @brief Handles device-found notifications and updates DLNA UI state.
 */
void DLNAViewController::HandleDeviceFound() {
  if (!fWindow)
    return;

#if ENABLE_DLNA_OUTPUT
  RebuildRendererMenu();
#endif
  if (!fWindow->fIsDlnaMode || !fWindow->fDlnaManager)
    return;

  RebuildServerMenu();
  if (!fWindow->fActiveDlnaServer.uuid.IsEmpty())
    return;

  auto servers = fWindow->fDlnaManager->Servers();
  if (servers.empty())
    return;

  BString status;
  status.SetToFormat(B_TRANSLATE("Found '%s'. Preparing DLNA scan..."),
                     servers[0].friendlyName.String());
  fWindow->UpdateStatus(status, true);
  SelectServer(servers[0].uuid);
}

/**
 * @brief Handles device-lost notifications and refreshes relevant menus.
 */
void DLNAViewController::HandleDeviceLost() {
  if (!fWindow)
    return;

#if ENABLE_DLNA_OUTPUT
  RebuildRendererMenu();
#endif
  if (fWindow->fIsDlnaMode)
    RebuildServerMenu();
}

/**
 * @brief Triggers (re)discovery of DLNA devices and updates status text.
 */
void DLNAViewController::RefreshDevices() {
  if (!fWindow)
    return;

  if (fWindow->fDlnaManager)
    fWindow->fDlnaManager->StartDiscovery();
  fWindow->UpdateStatus(B_TRANSLATE("Searching for DLNA devices..."), true);
}

/**
 * @brief Clears cached browse data for a server and reloads if currently active.
 * @param msg Message containing target server `uuid`.
 */
void DLNAViewController::RefreshServerCache(BMessage *msg) {
  if (!fWindow || !msg || !fWindow->fDlnaManager)
    return;

  BString uuid;
  if (msg->FindString("uuid", &uuid) != B_OK)
    return;

  fWindow->fDlnaManager->DeleteServerCache(uuid);
  if (fWindow->fActiveDlnaServer.uuid == uuid)
    SelectServer(uuid);
}

/**
 * @brief Selects a server from a message payload.
 * @param msg Message containing server `uuid`.
 */
void DLNAViewController::SelectServerFromMessage(BMessage *msg) {
  if (!fWindow || !msg || !fWindow->fDlnaManager)
    return;

  BString uuid;
  if (msg->FindString("uuid", &uuid) == B_OK)
    SelectServer(uuid);
}

/**
 * @brief Toggles global DLNA feature availability and related UI entries.
 */
void DLNAViewController::ToggleEnabled() {
  if (!fWindow)
    return;

  fWindow->fDlnaEnabled = !fWindow->fDlnaEnabled;
  fWindow->fToggleDlnaItem->SetMarked(fWindow->fDlnaEnabled);
#if ENABLE_DLNA_OUTPUT
  fWindow->_SetOutputMenuVisible(fWindow->fDlnaEnabled);
#endif

  if (!fWindow->fDlnaEnabled) {
    if (fWindow->fDlnaManager) {
      fWindow->fDlnaManager->SetActiveRenderer("");
      fWindow->fDlnaManager->StopDiscovery();
    }
    int32 idx = fWindow->fPlaylistLibrary->View()->FindIndexByName("DLNA");
    if (idx >= 0)
      fWindow->fPlaylistLibrary->View()->RemovePlaylistAt(idx);
    SetServerFieldVisible(false);
  } else {
    if (fWindow->fPlaylistLibrary->View()->FindIndexByName("DLNA") < 0) {
      fWindow->fPlaylistLibrary->View()->AddItem("DLNA", false,
                                                 PlaylistItemKind::DLNA);
      fWindow->fPlaylistLibrary->View()->SetPlaylistOrder(
          fWindow->fPlaylistLibrary->View()->GetPlaylistOrder());
    }
    if (fWindow->fDlnaManager)
      fWindow->fDlnaManager->StartDiscovery();
  }

  fWindow->SaveSettings();
}

#if ENABLE_DLNA_OUTPUT
/**
 * @brief Toggles visibility of the renderer toolbar button.
 */
void DLNAViewController::ToggleRendererButton() {
  if (!fWindow)
    return;

  fWindow->fShowRendererBtn = !fWindow->fShowRendererBtn;
  if (fWindow->fShowRendererBtnItem)
    fWindow->fShowRendererBtnItem->SetMarked(fWindow->fShowRendererBtn);

  fWindow->_SetOutputMenuVisible(fWindow->fDlnaEnabled);

  fWindow->SaveSettings();
}

/**
 * @brief Selects local output or a remote renderer and syncs renderer volume.
 * @param msg Message containing renderer `uuid` (empty = local output).
 */
void DLNAViewController::SelectRenderer(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  BString uuid;
  if (msg->FindString("uuid", &uuid) != B_OK || !fWindow->fDlnaManager)
    return;

  fWindow->fDlnaManager->SetActiveRenderer(uuid);
  DEBUG_PRINT("Selected Renderer: %s\n",
              uuid.IsEmpty() ? "Locale" : uuid.String());

  if (!uuid.IsEmpty()) {
    struct VolFetchData {
      DLNAService *mgr;
      BMessenger target;
    };
    VolFetchData *data =
        new VolFetchData{fWindow->fDlnaManager, BMessenger(fWindow)};
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          VolFetchData *d = (VolFetchData *)arg;
          int32 vol = -1;
          if (d->mgr->GetRendererVolume(vol) != B_OK || vol < 0) {
            vol = 20;
            d->mgr->SetRendererVolume(vol);
          }
          BMessage volMsg(MSG_DLNA_VOLUME_UPDATE);
          volMsg.AddInt32("volume", vol);
          d->target.SendMessage(&volMsg);

          BMessage syncMsg(MSG_VOLUME_CHANGED);
          d->target.SendMessage(&syncMsg);

          delete d;
          return 0;
        },
        "dlna_init_vol", B_NORMAL_PRIORITY, data);
    if (tid >= 0)
      resume_thread(tid);
    else
      delete data;
  }

  if (fWindow->fPlaybackEngine && fWindow->fPlaybackEngine->IsPlaying()) {
    if (fWindow->fIsRadioMode && fWindow->fRadioStationController &&
        fWindow->fRadioStationController->HasActiveStation()) {
      const char *stationUrl =
          fWindow->fRadioStationController->ActiveStationUrl().String();
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
      BUrl url(stationUrl);
#else
      BUrl url(stationUrl, true);
#endif
      fWindow->fPlaybackEngine->PlayUrl(
          url, fWindow->fRadioStationController->ActiveStationName().String(),
          0, nullptr);
    } else {
      int32 idx = fWindow->fPlaybackEngine->CurrentIndex();
      if (idx >= 0)
        fWindow->fPlaybackEngine->Play(idx);
    }
  }

  RebuildRendererMenu();
}
#endif

/**
 * @brief Consumes crawl progress updates and maps them to status messages.
 * @param msg Message containing `phase`, `count`, and `server`.
 */
void DLNAViewController::HandleCrawlProgress(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  int32 count = 0;
  BString server;
  BString phase;
  msg->FindInt32("count", &count);
  msg->FindString("server", &server);
  msg->FindString("phase", &phase);

  if (!fWindow->fIsDlnaMode || fWindow->fActiveDlnaServer.friendlyName != server)
    return;

  BString status;
  if (phase == "cover_start") {
    status.SetToFormat(B_TRANSLATE("Indexing '%s'... matching album covers"),
                       server.String());
  } else if (phase == "cover") {
    status.SetToFormat(B_TRANSLATE("Indexing '%s'... %ld album covers"),
                       server.String(), (long)count);
  } else if (phase == "finalizing") {
    status.SetToFormat(B_TRANSLATE("Indexing '%s'... finalizing %ld items"),
                       server.String(), (long)count);
  } else if (phase == "saving") {
    status.SetToFormat(B_TRANSLATE("Indexing '%s'... saving %ld items"),
                       server.String(), (long)count);
  } else if (phase == "audio") {
    status.SetToFormat(B_TRANSLATE("Indexing '%s'... %ld audio files found"),
                       server.String(), (long)count);
  } else if (phase == "audio_done") {
    status.SetToFormat(B_TRANSLATE("Indexing '%s'... %ld audio files found"),
                       server.String(), (long)count);
  } else {
    status.SetToFormat(B_TRANSLATE("Indexing '%s'... %ld items"),
                       server.String(), (long)count);
  }
  fWindow->UpdateStatus(status, true);
}

/**
 * @brief Handles crawl completion, then loads and shows cached browse results.
 * @param msg Message containing crawled server `uuid`.
 */
void DLNAViewController::HandleCrawlDone(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  fWindow->fDlnaCrawling = false;

  if (!fWindow->fDlnaManager)
    return;

  BString targetUuid;
  if (msg->FindString("uuid", &targetUuid) != B_OK)
    return;

  if (!fWindow->fIsDlnaMode || fWindow->fActiveDlnaServer.uuid != targetUuid) {
    DEBUG_PRINT("DLNA CRAWL_DONE ignored: Navigated away or "
                "UUID mismatch.\n");
    return;
  }

  std::vector<DLNABrowseItem> cached;
  bigtime_t tCacheStart = system_time();
  if (fWindow->fDlnaManager->LoadServerCache(targetUuid, cached) == B_OK &&
      !cached.empty()) {
    bigtime_t tCacheEnd = system_time();
    DEBUG_PRINT("DLNA CRAWL_DONE: cache load '%s': %zu "
                "items = %lld us\n",
                fWindow->fActiveDlnaServer.friendlyName.String(),
                cached.size(), (long long)(tCacheEnd - tCacheStart));

    PopulateItems(cached);

    BString status;
    status.SetToFormat("%ld items from '%s'", (long)fWindow->fRadioItems.size(),
                       fWindow->fActiveDlnaServer.friendlyName.String());
    fWindow->UpdateStatus(status, false);
  } else {
    fWindow->UpdateStatus(B_TRANSLATE("Failed to load DLNA cache."), false);
  }
}

/**
 * @brief Shows discovered DLNA servers in the content view as selectable items.
 */
void DLNAViewController::ShowServers() {
  if (!fWindow || !fWindow->fDlnaManager || !fWindow->fLibraryManager)
    return;

  auto servers = fWindow->fDlnaManager->Servers();
  fWindow->fRadioItems.clear();
  fWindow->fRadioItems.reserve(servers.size());

  for (const auto &dev : servers) {
    MediaItem mi;
    mi.title = dev.friendlyName;
    mi.artist = "DLNA Server";
    mi.album = "";
    mi.genre = "";
    mi.path = dev.uuid;
    fWindow->fRadioItems.push_back(mi);
  }

  fWindow->fLibraryManager->SetRadioFilterMode(false);
  fWindow->fLibraryManager->UpdateFilteredViews(
      fWindow->fRadioItems, true, "DLNA", fWindow->fSearchField->Text());

  if (servers.empty()) {
    fWindow->UpdateStatus(B_TRANSLATE("Searching for DLNA devices..."), true);
  } else {
    BString status;
    status.SetToFormat("%ld DLNA servers", (long)servers.size());
    fWindow->UpdateStatus(status, false);
  }
}

/**
 * @brief Rebuilds the DLNA server dropdown menu, including cache refresh items.
 */
void DLNAViewController::RebuildServerMenu() {
  if (!fWindow || !fWindow->fDlnaServerMenu || !fWindow->fDlnaManager)
    return;

  while (fWindow->fDlnaServerMenu->CountItems() > 0)
    delete fWindow->fDlnaServerMenu->RemoveItem((int32)0);

  auto servers = fWindow->fDlnaManager->Servers();
  for (const auto &dev : servers) {
    BMessage *msg = new BMessage(MSG_DLNA_SELECT_SERVER);
    msg->AddString("uuid", dev.uuid);
    BMenuItem *item = new BMenuItem(dev.friendlyName.String(), msg);
    if (dev.uuid == fWindow->fActiveDlnaServer.uuid)
      item->SetMarked(true);
    fWindow->fDlnaServerMenu->AddItem(item);
  }

  if (servers.size() > 0)
    fWindow->fDlnaServerMenu->AddSeparatorItem();

  for (const auto &dev : servers) {
    BString label;
    label.SetToFormat(B_TRANSLATE("Refresh '%s'"), dev.friendlyName.String());
    BMessage *msg = new BMessage(MSG_DLNA_REFRESH_CACHE);
    msg->AddString("uuid", dev.uuid);
    fWindow->fDlnaServerMenu->AddItem(new BMenuItem(label.String(), msg));
  }

  fWindow->fDlnaServerMenu->SetTargetForItems(fWindow);
}

#if ENABLE_DLNA_OUTPUT
/**
 * @brief Rebuilds renderer menus in toolbar popup and settings output submenu.
 */
void DLNAViewController::RebuildRendererMenu() {
  if (!fWindow || !fWindow->fRendererMenu)
    return;

  BString activeUuid;
  if (fWindow->fDlnaManager && fWindow->fDlnaManager->ActiveRenderer())
    activeUuid = fWindow->fDlnaManager->ActiveRenderer()->uuid;

  while (fWindow->fRendererMenu->CountItems() > 0)
    delete fWindow->fRendererMenu->RemoveItem((int32)0);

  if (fWindow->fOutputMenu) {
    while (fWindow->fOutputMenu->CountItems() > 2)
      delete fWindow->fOutputMenu->RemoveItem((int32)2);
  }

  BMessage *localeMsg = new BMessage(MainWindow::MSG_RENDERER_SELECTED);
  localeMsg->AddString("uuid", "");
  BMenuItem *localeItem = new BMenuItem(B_TRANSLATE("Locale"), localeMsg);
  if (activeUuid.IsEmpty())
    localeItem->SetMarked(true);
  fWindow->fRendererMenu->AddItem(localeItem);

  if (fWindow->fOutputMenu) {
    BMessage *locMsgOut = new BMessage(MainWindow::MSG_RENDERER_SELECTED);
    locMsgOut->AddString("uuid", "");
    BMenuItem *locItemOut = new BMenuItem(B_TRANSLATE("Locale"), locMsgOut);
    if (activeUuid.IsEmpty())
      locItemOut->SetMarked(true);
    fWindow->fOutputMenu->AddItem(locItemOut);
  }

  if (fWindow->fDlnaManager) {
    auto renderers = fWindow->fDlnaManager->Renderers();
    if (!renderers.empty()) {
      fWindow->fRendererMenu->AddSeparatorItem();
      if (fWindow->fOutputMenu)
        fWindow->fOutputMenu->AddSeparatorItem();
      for (const auto &dev : renderers) {
        BMessage *msg = new BMessage(MainWindow::MSG_RENDERER_SELECTED);
        msg->AddString("uuid", dev.uuid);
        BMenuItem *item = new BMenuItem(dev.friendlyName.String(), msg);
        if (dev.uuid == activeUuid)
          item->SetMarked(true);
        fWindow->fRendererMenu->AddItem(item);

        if (fWindow->fOutputMenu) {
          BMessage *msgOut = new BMessage(MainWindow::MSG_RENDERER_SELECTED);
          msgOut->AddString("uuid", dev.uuid);
          BMenuItem *itemOut = new BMenuItem(dev.friendlyName.String(), msgOut);
          if (dev.uuid == activeUuid)
            itemOut->SetMarked(true);
          fWindow->fOutputMenu->AddItem(itemOut);
        }
      }
    }
  }

  fWindow->fRendererMenu->SetTargetForItems(fWindow);
  if (fWindow->fOutputMenu)
    fWindow->fOutputMenu->SetTargetForItems(fWindow);
}
#else
/**
 * @brief No-op when DLNA output support is disabled at compile time.
 */
void DLNAViewController::RebuildRendererMenu() {}
#endif

/**
 * @brief Shows or hides the DLNA server field in the transport row.
 * @param visible Desired visibility state.
 */
void DLNAViewController::SetServerFieldVisible(bool visible) {
  if (!fWindow || !fWindow->fDlnaServerField)
    return;

  if (visible && fWindow->fDlnaServerField->IsHidden())
    fWindow->fDlnaServerField->Show();
  else if (!visible && !fWindow->fDlnaServerField->IsHidden())
    fWindow->fDlnaServerField->Hide();
}

/**
 * @brief Selects a media server and starts cache-first loading/crawling.
 * @param uuid UUID of the target media server.
 */
void DLNAViewController::SelectServer(const BString &uuid) {
  if (!fWindow || !fWindow->fDlnaManager)
    return;

  for (const auto &dev : fWindow->fDlnaManager->Devices()) {
    if (dev.uuid == uuid && dev.type == DLNADevice::MediaServer) {
      fWindow->fActiveDlnaServer = dev;
      break;
    }
  }

  if (fWindow->fActiveDlnaServer.uuid.IsEmpty()) {
    DEBUG_PRINT("_SelectDlnaServer: UUID not found\n");
    return;
  }

  BString preparingStatus;
  preparingStatus.SetToFormat(B_TRANSLATE("Preparing DLNA scan for '%s'..."),
                              fWindow->fActiveDlnaServer.friendlyName.String());
  fWindow->UpdateStatus(preparingStatus, true);

  RebuildServerMenu();

  std::vector<DLNABrowseItem> cached;
  bigtime_t tCacheStart = system_time();
  if (fWindow->fDlnaManager->LoadServerCache(uuid, cached) == B_OK &&
      !cached.empty()) {
    bigtime_t tCacheEnd = system_time();
    DEBUG_PRINT(
        "DLNA cache hit for '%s': %zu items, load=%lld us\n",
        fWindow->fActiveDlnaServer.friendlyName.String(), cached.size(),
        (long long)(tCacheEnd - tCacheStart));
    PopulateItems(cached);

    BString status;
    status.SetToFormat("%ld items from '%s'", (long)fWindow->fRadioItems.size(),
                       fWindow->fActiveDlnaServer.friendlyName.String());
    fWindow->UpdateStatus(status, false);
    return;
  }

  fWindow->fRadioItems.clear();
  fWindow->fLibraryManager->SetRadioFilterMode(false);
  fWindow->fLibraryManager->UpdateFilteredViews(
      fWindow->fRadioItems, true, "DLNA", fWindow->fSearchField->Text());

  if (fWindow->fDlnaCrawling) {
    fWindow->UpdateStatus(B_TRANSLATE("A DLNA scan is already in progress..."),
                          false);
    return;
  }

  fWindow->fDlnaCrawling = true;
  fWindow->UpdateStatus(B_TRANSLATE("Indexing DLNA server..."), true);

  DLNADevice serverCopy = fWindow->fActiveDlnaServer;
  DLNAService *mgr = fWindow->fDlnaManager;
  BMessenger target(fWindow);

  fWindow->LaunchThread("dlna_crawl", [mgr, serverCopy, target]() {
    std::vector<DLNABrowseItem> allItems;
    status_t err = mgr->BrowseAll(serverCopy, allItems, target);

    if (err == B_OK && !allItems.empty()) {
      BMessage saving(MSG_DLNA_CRAWL_PROGRESS);
      saving.AddString("server", serverCopy.friendlyName);
      saving.AddString("phase", "saving");
      saving.AddInt32("count", (int32)allItems.size());
      target.SendMessage(&saving);

      mgr->SaveServerCache(serverCopy.uuid, allItems);
    }

    BMessage done(MSG_DLNA_CRAWL_DONE);
    done.AddString("uuid", serverCopy.uuid);
    target.SendMessage(&done);
  });
}

/**
 * @brief Starts playback for a DLNA queue index and updates now-playing UI.
 * @param index Queue index to play.
 */
void DLNAViewController::PlayIndex(int32 index) {
  if (!fWindow || index < 0 || index >= (int32)fPlayQueue.size())
    return;

  fPlayIndex = index;
  if (fWindow->fPlaybackQueueManager)
    fWindow->fPlaybackQueueManager->SetActiveSource(
        PlaybackQueueManager::SourceDLNA);
  if (fWindow->fRadioStationController) {
    fWindow->fRadioStationController->ClearActiveStation();
    fWindow->fRadioStationController->ClearActiveCover();
  }
  const MediaItem &mi = fPlayQueue[index];
  if (mi.path.IsEmpty()) {
    BMessage unavailable(MSG_DLNA_RESOURCE_UNAVAILABLE);
    unavailable.AddString("title", mi.title);
    fWindow->PostMessage(&unavailable);
    return;
  }

  if (fWindow->fNowPlayingInfoPanel && fWindow->fShowCoverArt)
    fWindow->fNowPlayingInfoPanel->ClearCover();

  DEBUG_PRINT("_PlayDlnaIndex: playing URL '%s'\n", mi.path.String());
  AudioPlaybackEngine *ctrl = fWindow->fPlaybackEngine;
  BString title = mi.title;
  BString artist = mi.artist;
  BString displayLabel;
  if (!artist.IsEmpty())
    displayLabel << artist << " - ";
  displayLabel << title;

  fWindow->UpdateStatus(B_TRANSLATE("Playing from DLNA server..."), false);
  if (fWindow->fTitleView)
    fWindow->fTitleView->SetText(displayLabel.String());

  if (fWindow->fNowPlayingInfoPanel)
    fWindow->fNowPlayingInfoPanel->SetTags(mi.artist, mi.title, mi.album);

  int32 duration = mi.duration;
  BString itemPath = mi.path;
  BPrivate::Network::BUrlContext *ctx = fWindow->fDlnaManager->GetUrlContext();

  if (fWindow->fLibraryManager && fWindow->fLibraryManager->ContentView()) {
    fWindow->fLibraryManager->ContentView()->DeselectAll();
    int32 count = fWindow->fLibraryManager->ContentView()->CountRows();
    for (int32 i = 0; i < count; i++) {
      if (const MediaItem *viewItem =
              fWindow->fLibraryManager->ContentView()->ItemAt(i)) {
        if (viewItem->path == mi.path) {
          if (BRow *row = fWindow->fLibraryManager->ContentView()->RowAt(i)) {
            fWindow->fLibraryManager->ContentView()->AddToSelection(row);
            fWindow->fLibraryManager->ContentView()->ScrollTo(row);
            break;
          }
        }
      }
    }
  }

  fWindow->LaunchThread("dlna_play", [ctrl, itemPath, title, duration, ctx]() {
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
    BUrl url(itemPath.String());
#else
    BUrl url(itemPath.String(), false);
#endif
    ctrl->PlayUrl(url, title.String(), duration, ctx);
  });

  if (fWindow->fIconPause) {
    fWindow->fBtnPlayPause->SetIcon(fWindow->fIconPause, 0);
    fWindow->fBtnPlayPause->SetLabel("");
  }
}

/**
 * @brief Clears the current DLNA play queue and resets play index.
 */
void DLNAViewController::ClearPlayQueue() {
  fPlayQueue.clear();
  fPlayIndex = -1;
}

/**
 * @brief Returns whether a DLNA play queue is currently available.
 */
bool DLNAViewController::HasPlayQueue() const {
  return !fPlayQueue.empty();
}

/**
 * @brief Returns current DLNA play index, or -1 if none.
 */
int32 DLNAViewController::CurrentPlayIndex() const {
  return fPlayIndex;
}

/**
 * @brief Returns number of entries in the current DLNA play queue.
 */
int32 DLNAViewController::PlayQueueSize() const {
  return (int32)fPlayQueue.size();
}

/**
 * @brief Returns the currently active play item, if valid.
 */
const MediaItem *DLNAViewController::CurrentPlayItem() const {
  if (fPlayIndex < 0 || fPlayIndex >= (int32)fPlayQueue.size())
    return nullptr;
  return &fPlayQueue[fPlayIndex];
}

/**
 * @brief Converts browse items into `MediaItem`s and refreshes filtered views.
 * @param browseItems Raw browse items from cache or crawl.
 */
void DLNAViewController::PopulateItems(
    const std::vector<DLNABrowseItem> &browseItems) {
  if (!fWindow || !fWindow->fLibraryManager)
    return;

  bigtime_t t0 = system_time();
  fWindow->fRadioItems.clear();
  fWindow->fRadioItems.reserve(browseItems.size());

  std::set<BString> seenItems;
  for (const auto &bi : browseItems) {
    BString dedupKey = DlnaDedupKey(bi);
    if (seenItems.find(dedupKey) != seenItems.end())
      continue;
    seenItems.insert(dedupKey);

    MediaItem mi;
    mi.title = bi.title;
    mi.artist = bi.artist;
    mi.album = bi.album;
    mi.genre = bi.genre;
    mi.path = bi.resourceUrl;
    mi.coverUrl = bi.albumArtUrl;
    mi.duration = ParseDurationString(bi.duration);
    fWindow->fRadioItems.push_back(mi);
  }
  bigtime_t t1 = system_time();

  fWindow->fLibraryManager->SetRadioFilterMode(false);
  fWindow->fLibraryManager->UpdateFilteredViews(
      fWindow->fRadioItems, true, "DLNA", fWindow->fSearchField->Text());
  bigtime_t t2 = system_time();

  DEBUG_PRINT("_PopulateDlnaItems(%zu): convert=%lld us, "
              "filterViews=%lld us, total=%lld us\n",
              browseItems.size(), (long long)(t1 - t0),
              (long long)(t2 - t1), (long long)(t2 - t0));
}
