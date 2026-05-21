#include "RadioStationController.h"

#include "MediaTableView.h"
#include "Debug.h"
#include "DLNAViewController.h"
#include "NowPlayingInfoPanel.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "AudioPlaybackEngine.h"
#include "Messages.h"
#include "PlaybackQueueManager.h"
#include "PlaylistSidebarView.h"
#include "PlaylistLibrary.h"
#include "RadioStationLibrary.h"
#include "RadioStationEditorDialog.h"

#include <Alert.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <DataIO.h>
#include <FilePanel.h>
#include <HttpRequest.h>
#include <HttpResult.h>
#include <MenuItem.h>
#include <Path.h>
#include <TranslationUtils.h>
#include <Url.h>
#include <UrlProtocolRoster.h>

#include <atomic>
#include <memory>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RadioStationController"

namespace {

bool IsLikelyCoverUrl(BString url) {
  url.Trim();
  if (!url.IStartsWith("http://") && !url.IStartsWith("https://"))
    return false;

  url.ToLower();
  int32 query = url.FindFirst('?');
  if (query >= 0)
    url.Truncate(query);

  if (url.EndsWith(".jpg") || url.EndsWith(".jpeg") || url.EndsWith(".png") ||
      url.EndsWith(".webp") || url.EndsWith(".gif")) {
    return true;
  }

  if (url.FindFirst("/cover") >= 0 || url.FindFirst("/covers/") >= 0 ||
      url.FindFirst("/artwork") >= 0 || url.FindFirst("/image") >= 0 ||
      url.FindFirst("/images/") >= 0) {
    return true;
  }

  if (url.EndsWith(".mp3") || url.EndsWith(".aac") || url.EndsWith(".ogg") ||
      url.EndsWith(".opus") || url.EndsWith(".flac") ||
      url.EndsWith(".m3u") || url.EndsWith(".m3u8") ||
      url.EndsWith(".pls")) {
    return false;
  }

  return false;
}

} // namespace

RadioStationController::RadioStationController(MainWindow *window) : fWindow(window) {}

RadioStationController::~RadioStationController() {
  delete fActiveCover;
}

void RadioStationController::ShowStations() {
  if (!fWindow || !fWindow->fRadioStationLibrary || !fWindow->fLibraryManager)
    return;

  const auto &stations = fWindow->fRadioStationLibrary->AllStations();
  fWindow->fRadioItems.clear();
  fWindow->fRadioItems.reserve(stations.size());

  for (const auto &rs : stations) {
    MediaItem mi;
    mi.title = rs.name;
    mi.genre = rs.genre;
    mi.artist = rs.country;
    mi.album = rs.language;
    mi.path = rs.url;
    fWindow->fRadioItems.push_back(mi);
  }

  fWindow->fLibraryManager->SetRadioFilterMode(true);
  fWindow->fLibraryManager->UpdateFilteredViews(
      fWindow->fRadioItems, true, "Radio", fWindow->fSearchField->Text());

  BString status;
  status.SetToFormat("%ld stations", (long)stations.size());
  fWindow->UpdateStatus(status, false);

  DEBUG_PRINT("_ShowRadioStations: %zu stations\n",
              stations.size());
}

void RadioStationController::ShowAddStationDialog() {
  if (!fWindow)
    return;

  RadioStationEditorDialog *dlg = new RadioStationEditorDialog(BMessenger(fWindow));
  dlg->Show();
}

int32 RadioStationController::SelectedStationIndex() {
  if (!fWindow || !fWindow->fIsRadioMode || !fWindow->fLibraryManager ||
      !fWindow->fLibraryManager->ContentView()) {
    return -1;
  }

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  BRow *row = cv->CurrentSelection();
  if (!row)
    return -1;

  int32 rowIdx = cv->IndexOf(row);
  const MediaItem *mi = cv->ItemAt(rowIdx);
  if (!mi)
    return -1;

  return FindStationIndex(mi->path);
}

void RadioStationController::ShowEditStationDialog() {
  if (!fWindow)
    return;

  int32 idx = SelectedStationIndex();
  if (idx < 0)
    return;

  const RadioStation *rs = fWindow->fRadioStationLibrary->StationAt(idx);
  if (!rs)
    return;

  RadioStationEditorDialog *dlg = new RadioStationEditorDialog(
      BMessenger(fWindow), idx, rs->name.String(), rs->url.String(),
      rs->genre.String(), rs->country.String(), rs->language.String());
  dlg->Show();
}

void RadioStationController::DeleteSelectedStation() {
  if (!fWindow || !fWindow->fRadioStationLibrary)
    return;

  int32 idx = SelectedStationIndex();
  if (idx >= 0 && fWindow->fRadioStationLibrary->RemoveStation(idx))
    ShowStations();
}

void RadioStationController::SaveStation(BMessage *msg) {
  if (!fWindow || !msg || !fWindow->fRadioStationLibrary)
    return;

  BString name, url, genre, country, language;
  int32 editIndex = -1;
  msg->FindString("name", &name);
  msg->FindString("url", &url);
  msg->FindString("genre", &genre);
  msg->FindString("country", &country);
  msg->FindString("language", &language);
  msg->FindInt32("edit_index", &editIndex);

  if (name.IsEmpty())
    name = url;

  RadioStation rs(name, url, genre, country, language);
  if (editIndex >= 0)
    fWindow->fRadioStationLibrary->EditStation(editIndex, rs);
  else
    fWindow->fRadioStationLibrary->AddStation(rs);

  if (fWindow->fIsRadioMode)
    ShowStations();
}

void RadioStationController::ImportStations(BMessage *msg) {
  if (!fWindow || !msg || !fWindow->fRadioStationLibrary)
    return;

  entry_ref ref;
  if (msg->FindRef("refs", &ref) != B_OK) {
    delete fWindow->fFilePanel;
    fWindow->fFilePanel =
        new BFilePanel(B_OPEN_PANEL, new BMessenger(fWindow), nullptr,
                       B_FILE_NODE, false, new BMessage(MSG_RADIO_IMPORT));
    fWindow->fFilePanel->SetPanelDirectory("/boot/home");
    fWindow->fFilePanel->Show();
    return;
  }

  BPath path(&ref);
  if (path.InitCheck() != B_OK)
    return;

  BString p = path.Path();
  int32 n = 0;
  if (p.IEndsWith(".m3u") || p.IEndsWith(".m3u8"))
    n = fWindow->fRadioStationLibrary->ImportM3U(p.String());
  else if (p.IEndsWith(".pls"))
    n = fWindow->fRadioStationLibrary->ImportPLS(p.String());

  if (n <= 0)
    return;

  BString status;
  status.SetToFormat("%ld stations imported", (long)n);
  fWindow->UpdateStatus(status, false);
  if (fWindow->fIsRadioMode)
    ShowStations();
}

void RadioStationController::ToggleEnabled() {
  if (!fWindow)
    return;

  fWindow->fRadioEnabled = !fWindow->fRadioEnabled;
  fWindow->fToggleRadioItem->SetMarked(fWindow->fRadioEnabled);

  if (!fWindow->fRadioEnabled) {
    int32 idx = fWindow->fPlaylistLibrary->View()->FindIndexByName("Radio");
    if (idx >= 0)
      fWindow->fPlaylistLibrary->View()->RemovePlaylistAt(idx);
  } else {
    if (fWindow->fPlaylistLibrary->View()->FindIndexByName("Radio") < 0) {
      fWindow->fPlaylistLibrary->View()->AddItem("Radio", false,
                                                 PlaylistItemKind::Radio);
      fWindow->fPlaylistLibrary->View()->SetPlaylistOrder(
          fWindow->fPlaylistLibrary->View()->GetPlaylistOrder());
    }
    if (fWindow->fRadioStationLibrary &&
        fWindow->fRadioStationLibrary->AllStations().empty()) {
      fWindow->fRadioStationLibrary->LoadStations();
    }
  }

  fWindow->SaveSettings();
}

int32 RadioStationController::FindStationIndex(const BString &stationUrl) const {
  if (!fWindow || !fWindow->fRadioStationLibrary)
    return -1;

  const auto &stations = fWindow->fRadioStationLibrary->AllStations();
  for (size_t i = 0; i < stations.size(); ++i) {
    if (stations[i].url == stationUrl)
      return static_cast<int32>(i);
  }
  return -1;
}

void RadioStationController::HandleMetadata(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  bool metadataChanged = false;
  BString streamArtist;
  BString streamTrackTitle;
  BString streamAlbum;
  if (msg->FindString("stream_artist", &streamArtist) == B_OK) {
    fActiveStreamArtist = streamArtist;
    metadataChanged = true;
  }
  if (msg->FindString("stream_track_title", &streamTrackTitle) == B_OK) {
    fActiveStreamTrackTitle = streamTrackTitle;
    metadataChanged = true;
  }
  if (msg->FindString("stream_album", &streamAlbum) == B_OK) {
    fActiveStreamAlbum = streamAlbum;
    metadataChanged = true;
  }

  BString streamTitle;
  if (msg->FindString("stream_title", &streamTitle) == B_OK) {
    BString label = streamTitle;
    if (fWindow->fTitleView)
      fWindow->fTitleView->SetText(label.String());

    fActiveStreamTitle = streamTitle;
    metadataChanged = true;
  }

  if (metadataChanged)
    fWindow->UpdateFileInfo();

  BString coverUrl;
  if (msg->FindString("stream_url", &coverUrl) == B_OK &&
      !coverUrl.IsEmpty()) {
    if (!IsLikelyCoverUrl(coverUrl)) {
      DEBUG_PRINT("Ignoring non-cover stream URL: %s\n",
                  coverUrl.String());
      return;
    }

    if (fActiveStreamCoverUrl == coverUrl)
      return;

    fActiveStreamCoverUrl = coverUrl;
    DownloadCover(coverUrl);
  }
}

void RadioStationController::DownloadCover(const BString &coverUrl) {
  if (!fWindow)
    return;

  DEBUG_PRINT("Radio metadata has new cover URL: %s\n",
              coverUrl.String());

  MainWindow *window = fWindow;
  RadioStationController *radio = this;
  fCoverDownloadThread =
      fWindow->LaunchThread("radio_cover_dl", [coverUrl, window, radio]() {
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
            BPrivate::Network::BHttpHeaders headers;
            headers.AddHeader("User-Agent", "BeTon/1.0 (Haiku OS)");
            http->SetHeaders(headers);
          }
          thread_id tid = req->Run();
          status_t exit;
          if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 10000000, &exit) ==
              B_OK) {
            if (auto http =
                    dynamic_cast<BPrivate::Network::BHttpRequest *>(req.get())) {
              const BPrivate::Network::BHttpResult &result =
                  dynamic_cast<const BPrivate::Network::BHttpResult &>(
                      http->Result());
              DEBUG_PRINT("Radio cover HTTP status: %ld, bytes=%zu\n",
                          (long)result.StatusCode(), sink.BufferLength());
            }
            sink.Seek(0, SEEK_SET);
            BBitmap *bitmap = BTranslationUtils::GetBitmap(&sink);
            if (bitmap) {
              BMessage update(MSG_COVER_BITMAP_READY);
              update.AddString("path", coverUrl);
              update.AddPointer("bitmap", bitmap);
              if (window->PostMessage(&update) != B_OK)
                delete bitmap;
            } else {
              DEBUG_PRINT("Radio cover decode failed: %s (%zu bytes)\n",
                          coverUrl.String(), sink.BufferLength());
            }
          } else {
            req->Stop();
            DEBUG_PRINT("Radio cover download timeout: %s\n",
                        coverUrl.String());
          }
        }
        radio->MarkCoverDownloadThreadDone();
      });
}

void RadioStationController::ShowUnsupportedAlert() {
  if (!fWindow)
    return;

  BAlert *alert = new BAlert(
      B_TRANSLATE("Format not supported"),
      B_TRANSLATE("This radio station uses a format that is "
                  "currently not supported by BeTon or the "
                  "Haiku Media Kit."),
      B_TRANSLATE("OK"), nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
  alert->Go();

  if (fWindow->fTitleView)
    fWindow->fTitleView->SetText(B_TRANSLATE("Ready"));
  fWindow->UpdateStatus(B_TRANSLATE("Unsupported stream format"));
}

void RadioStationController::ShowConnectionFailedAlert(BMessage *msg) {
  if (!fWindow)
    return;

  BString stationUrl;
  BString stationName;
  if (msg) {
    msg->FindString("url", &stationUrl);
    msg->FindString("name", &stationName);
  }
  if (stationUrl.IsEmpty())
    stationUrl = fActiveStationUrl;
  if (stationName.IsEmpty())
    stationName = fActiveStationName;

  BAlert *alert = new BAlert(
      B_TRANSLATE("Connection failed"),
      B_TRANSLATE("Could not connect to the radio station. The server might "
                  "be down or unreachable."),
      B_TRANSLATE("Retry"), B_TRANSLATE("Cancel"), nullptr, B_WIDTH_AS_USUAL,
      B_STOP_ALERT);
  int32 choice = alert->Go();

  if (choice == 0 && !stationUrl.IsEmpty()) {
    if (stationName.IsEmpty())
      stationName = stationUrl;
    if (fWindow->fTitleView) {
      BString label;
      label.SetToFormat("Connecting: %s...", stationName.String());
      fWindow->fTitleView->SetText(label.String());
    }
    fWindow->UpdateStatus(B_TRANSLATE("Connecting to radio station..."), true);
    QueuePlay(stationUrl, stationName);
    return;
  }

  if (fWindow->fTitleView)
    fWindow->fTitleView->SetText(B_TRANSLATE("Ready"));
  fWindow->UpdateStatus(B_TRANSLATE("Connection to radio station failed"));
}

void RadioStationController::PlayStation(const MediaItem &station) {
  if (!fWindow)
    return;

  DEBUG_PRINT("MSG_PLAY (radio): station '%s' url=%s\n",
              station.title.String(), station.path.String());

  SetActiveStation(station);

  if (fWindow->fDlnaController)
    fWindow->fDlnaController->ClearPlayQueue();
  if (fWindow->fPlaybackQueueManager)
    fWindow->fPlaybackQueueManager->SetActiveSource(
        PlaybackQueueManager::SourceRadio);

  fWindow->UpdateFileInfo();

  BString label;
  label.SetToFormat("Connecting: %s...", station.title.String());
  if (fWindow->fTitleView)
    fWindow->fTitleView->SetText(label.String());
  fWindow->UpdateStatus(B_TRANSLATE("Connecting to radio station..."), true);

  QueuePlay(station.path, station.title);
  if (fWindow->fIconPause) {
    fWindow->fBtnPlayPause->SetIcon(fWindow->fIconPause, 0);
    fWindow->fBtnPlayPause->SetLabel("");
  }
}

void RadioStationController::SetActiveStation(const MediaItem &station) {
  if (!fWindow)
    return;

  fActiveStationName = station.title;
  fActiveStationUrl = station.path;
  fActiveStationGenre = station.genre;
  fActiveStreamTitle = "(Fetching metadata...)";
  fActiveStreamArtist = "";
  fActiveStreamTrackTitle = "";
  fActiveStreamAlbum = "";
  ClearActiveCover();
}

void RadioStationController::ClearActiveStation() {
  if (!fWindow)
    return;

  fActiveStationUrl = "";
  fActiveStationName = "";
  fActiveStationGenre = "";
  fActiveStreamTitle = "";
  fActiveStreamArtist = "";
  fActiveStreamTrackTitle = "";
  fActiveStreamAlbum = "";
}

void RadioStationController::ClearActiveCover() {
  if (!fWindow)
    return;

  delete fActiveCover;
  fActiveCover = nullptr;
  fActiveStreamCoverUrl = "";
  if (fCoverDownloadThread >= 0)
    fCoverDownloadThread = -1;
}

bool RadioStationController::HasActiveStation() const {
  return !fActiveStationUrl.IsEmpty();
}

const BString &RadioStationController::ActiveStationName() const {
  return fActiveStationName;
}

const BString &RadioStationController::ActiveStationUrl() const {
  return fActiveStationUrl;
}

const BString &RadioStationController::ActiveStationGenre() const {
  return fActiveStationGenre;
}

const BString &RadioStationController::ActiveStreamTitle() const {
  return fActiveStreamTitle;
}

const BString &RadioStationController::ActiveStreamArtist() const {
  return fActiveStreamArtist;
}

const BString &RadioStationController::ActiveStreamTrackTitle() const {
  return fActiveStreamTrackTitle;
}

const BString &RadioStationController::ActiveStreamAlbum() const {
  return fActiveStreamAlbum;
}

const BString &RadioStationController::ActiveStreamCoverUrl() const {
  return fActiveStreamCoverUrl;
}

bool RadioStationController::IsCurrentCoverDownloadThread(thread_id thread) const {
  return fCoverDownloadThread == thread;
}

void RadioStationController::MarkCoverDownloadThreadDone() {
  fCoverDownloadThread = -1;
}

void RadioStationController::StoreActiveCover(const BBitmap *bitmap) {
  delete fActiveCover;
  fActiveCover = bitmap ? new BBitmap(bitmap) : nullptr;
}

BBitmap *RadioStationController::ActiveCover() const {
  return fActiveCover;
}

void RadioStationController::QueuePlay(const BString &stationUrl,
                                const BString &stationName) {
  if (!fWindow)
    return;

  fPlayGeneration.fetch_add(1, std::memory_order_relaxed);

  BAutolock lock(&fPlayLock);
  fPendingUrl = stationUrl;
  fPendingName = stationName;
  fPendingPlay = true;

  if (fPlayThread >= 0)
    return;

  fPlayThread = fWindow->LaunchThread("radio_play", [this]() { PlayLoop(); });
  if (fPlayThread < 0)
    fPlayThread = -1;
}

void RadioStationController::CancelQueuedPlay() {
  if (!fWindow)
    return;

  fPlayGeneration.fetch_add(1, std::memory_order_relaxed);

  BAutolock lock(&fPlayLock);
  fPendingPlay = false;
  fPendingUrl = "";
  fPendingName = "";
}

void RadioStationController::WaitForPlayThread() {
  thread_id radioThread = -1;
  {
    BAutolock lock(&fPlayLock);
    radioThread = fPlayThread;
  }
  if (radioThread >= 0) {
    status_t exit;
    wait_for_thread(radioThread, &exit);
  }
}

void RadioStationController::PlayLoop() {
  if (!fWindow)
    return;

  while (!fWindow->fShuttingDown.load(std::memory_order_relaxed)) {
    BString stationUrl;
    BString stationName;
    int32 generation = fPlayGeneration.load(std::memory_order_relaxed);

    {
      BAutolock lock(&fPlayLock);
      if (!fPendingPlay) {
        fPlayThread = -1;
        return;
      }

      stationUrl = fPendingUrl;
      stationName = fPendingName;
      fPendingPlay = false;
    }

    BMessage statusMsg(MSG_STATUS_UPDATE);
    statusMsg.AddString("text", B_TRANSLATE("Resolving stream URL..."));
    statusMsg.AddBool("isPermanent", true);
    fWindow->PostMessage(&statusMsg);

    BString resolved = fWindow->fRadioStationLibrary->ResolveStreamUrl(stationUrl);
    if (generation != fPlayGeneration.load(std::memory_order_relaxed) ||
        fWindow->fShuttingDown.load(std::memory_order_relaxed)) {
      continue;
    }

    if (resolved == "ERR:UNSUPPORTED") {
      fWindow->PostMessage(MSG_RADIO_FORMAT_UNSUPPORTED);
      continue;
    }
    if (resolved == "ERR:CONNECTION") {
      BMessage failed(MSG_RADIO_CONNECTION_FAILED);
      failed.AddString("url", stationUrl);
      failed.AddString("name", stationName);
      fWindow->PostMessage(&failed);
      continue;
    }
    if (resolved.IsEmpty())
      continue;

    DEBUG_PRINT("resolved: %s\n", resolved.String());

    statusMsg.MakeEmpty();
    statusMsg.what = MSG_STATUS_UPDATE;
    statusMsg.AddString("text", B_TRANSLATE("Buffering audio stream..."));
    statusMsg.AddBool("isPermanent", true);
    fWindow->PostMessage(&statusMsg);

#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
    BUrl url(resolved.String());
#else
    BUrl url(resolved.String(), false);
#endif
    fWindow->fPlaybackEngine->PlayUrl(url, stationName.String());

    if (generation != fPlayGeneration.load(std::memory_order_relaxed) ||
        fWindow->fShuttingDown.load(std::memory_order_relaxed)) {
      continue;
    }

    BString nowPlaying;
    nowPlaying.SetToFormat(B_TRANSLATE("Now playing: %s"),
                           stationName.String());
    statusMsg.MakeEmpty();
    statusMsg.what = MSG_STATUS_UPDATE;
    statusMsg.AddString("text", nowPlaying.String());
    fWindow->PostMessage(&statusMsg);
  }

  BAutolock lock(&fPlayLock);
  fPlayThread = -1;
}
