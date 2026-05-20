#ifndef BETON_DLNA_VIEW_CONTROLLER_H
#define BETON_DLNA_VIEW_CONTROLLER_H

#include "Config.h"
#include "DLNAService.h"
#include "MediaItem.h"

#include <vector>

class BMessage;
class MediaTableView;
class MainWindow;

/**
 * @class DLNAViewController
 * @brief Coordinates DLNA source UI, crawling workflow, and DLNA playback queue.
 *
 * Bridges `MainWindow` UI state with `DLNAService` discovery/browse results and
 * provides queue/playback helpers for DLNA-selected media.
 */
class DLNAViewController {
public:
  /**
   * @brief Constructs the controller for a main window context.
   * @param window Owning main window.
   */
  explicit DLNAViewController(MainWindow *window);

  /** @brief Shows discovered DLNA servers in the content view. */
  void ShowServers();

  /** @brief Activates DLNA playlist source mode and prepares server selection. */
  void ShowPlaylistSource();

  /** @brief Opens the renderer popup menu (DLNA output builds). */
  void ShowRendererMenu();

  /** @brief Rebuilds the DLNA server selection menu. */
  void RebuildServerMenu();

  /** @brief Rebuilds renderer menus in toolbar/settings output UI. */
  void RebuildRendererMenu();

  /** @brief Shows or hides the DLNA server field. */
  /** @param visible Desired visibility state. */
  void SetServerFieldVisible(bool visible);

  /** @brief Handles a newly discovered DLNA device event. */
  void HandleDeviceFound();

  /** @brief Handles a lost/expired DLNA device event. */
  void HandleDeviceLost();

  /** @brief Triggers manual DLNA discovery refresh. */
  void RefreshDevices();

  /** @brief Clears cache for a server and reloads if active. */
  /** @param msg Message containing server `uuid`. */
  void RefreshServerCache(BMessage *msg);

  /** @brief Selects a server from message payload. */
  /** @param msg Message containing server `uuid`. */
  void SelectServerFromMessage(BMessage *msg);

  /** @brief Toggles DLNA feature availability in UI/settings. */
  void ToggleEnabled();

  /** @brief Updates status text based on crawl progress phase/count. */
  /** @param msg Message containing crawl progress fields. */
  void HandleCrawlProgress(BMessage *msg);

  /** @brief Finalizes crawl and loads cache for the completed server. */
  /** @param msg Message containing completed crawl server `uuid`. */
  void HandleCrawlDone(BMessage *msg);

  /** @brief Selects active DLNA server and starts cache/crawl pipeline. */
  /** @param uuid Server UUID. */
  void SelectServer(const BString &uuid);

  /** @brief Converts browse items into view/playable `MediaItem` entries. */
  /** @param browseItems Raw browse results from cache/crawl. */
  void PopulateItems(const std::vector<DLNABrowseItem> &browseItems);

  /** @brief Builds queue from a view and starts DLNA playback at row index. */
  /** @param view Source media table view. */
  /** @param rowIndex Selected row index. */
  void PlaySelection(MediaTableView *view, int32 rowIndex);

  /** @brief Starts playback for a queued DLNA item. */
  /** @param index Queue index. */
  void PlayIndex(int32 index);

  /** @brief Clears DLNA play queue and resets active play index. */
  void ClearPlayQueue();

  /** @brief Returns true if DLNA play queue has items. */
  bool HasPlayQueue() const;

  /** @brief Returns current DLNA queue play index, or -1. */
  int32 CurrentPlayIndex() const;

  /** @brief Returns number of queued DLNA items. */
  int32 PlayQueueSize() const;

  /** @brief Returns currently active queued item, or nullptr if none. */
  const MediaItem *CurrentPlayItem() const;

  /** @brief Applies DLNA volume updates to the local volume slider. */
  /** @param msg Message containing volume payload. */
  void ApplyVolumeUpdate(BMessage *msg);

  /** @brief Shows an alert when a DLNA resource cannot be played. */
  /** @param msg Message containing optional `title` and `path` fields. */
  void ShowResourceUnavailableAlert(BMessage *msg);

#if ENABLE_DLNA_OUTPUT
  void ToggleRendererButton();
  /** @brief Selects local/remote renderer and triggers volume sync. */
  /** @param msg Message containing renderer `uuid`. */
  void SelectRenderer(BMessage *msg);
#endif

private:
  /** Main window context used for UI updates and controller access. */
  MainWindow *fWindow;
  /** Current DLNA playback queue (view-ordered snapshot). */
  std::vector<MediaItem> fPlayQueue;
  /** Active playback index in `fPlayQueue`; -1 when idle. */
  int32 fPlayIndex = -1;
};

#endif // BETON_DLNA_VIEW_CONTROLLER_H
