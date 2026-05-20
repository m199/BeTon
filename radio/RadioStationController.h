#ifndef BETON_RADIO_STATION_CONTROLLER_H
#define BETON_RADIO_STATION_CONTROLLER_H

#include "MediaItem.h"

#include <Locker.h>
#include <String.h>
#include <SupportDefs.h>
#include <atomic>

class BBitmap;
class BMessage;
class MainWindow;

/**
 * @class RadioStationController
 * @brief Coordinates radio station CRUD, playback handoff, and metadata UI sync.
 */
class RadioStationController {
public:
  explicit RadioStationController(MainWindow *window);
  ~RadioStationController();

  void ShowStations();
  void ShowAddStationDialog();
  void ShowEditStationDialog();
  void DeleteSelectedStation();
  void SaveStation(BMessage *msg);
  void ImportStations(BMessage *msg);
  void ToggleEnabled();
  void HandleMetadata(BMessage *msg);
  void ShowUnsupportedAlert();
  void ShowConnectionFailedAlert(BMessage *msg);
  void PlayStation(const MediaItem &station);
  void SetActiveStation(const MediaItem &station);
  void ClearActiveStation();
  void ClearActiveCover();
  bool HasActiveStation() const;
  const BString &ActiveStationName() const;
  const BString &ActiveStationUrl() const;
  const BString &ActiveStationGenre() const;
  const BString &ActiveStreamTitle() const;
  const BString &ActiveStreamArtist() const;
  const BString &ActiveStreamTrackTitle() const;
  const BString &ActiveStreamAlbum() const;
  const BString &ActiveStreamCoverUrl() const;
  bool IsCurrentCoverDownloadThread(thread_id thread) const;
  void MarkCoverDownloadThreadDone();
  void StoreActiveCover(const BBitmap *bitmap);
  BBitmap *ActiveCover() const;
  void QueuePlay(const BString &stationUrl, const BString &stationName);
  void CancelQueuedPlay();
  void WaitForPlayThread();
  void PlayLoop();

private:
  int32 SelectedStationIndex();
  int32 FindStationIndex(const BString &stationUrl) const;
  void DownloadCover(const BString &coverUrl);

  MainWindow *fWindow;
  BString fActiveStationName;
  BString fActiveStationUrl;
  BString fActiveStationGenre;
  BString fActiveStreamTitle;
  BString fActiveStreamArtist;
  BString fActiveStreamTrackTitle;
  BString fActiveStreamAlbum;
  BBitmap *fActiveCover = nullptr;
  BString fActiveStreamCoverUrl;
  thread_id fCoverDownloadThread = -1;

  BLocker fPlayLock{"radio play"};
  thread_id fPlayThread = -1;
  std::atomic<int32> fPlayGeneration{0};
  BString fPendingUrl;
  BString fPendingName;
  bool fPendingPlay = false;
};

#endif // BETON_RADIO_STATION_CONTROLLER_H
