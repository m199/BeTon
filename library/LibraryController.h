#ifndef BETON_LIBRARY_CONTROLLER_H
#define BETON_LIBRARY_CONTROLLER_H

#include <Message.h>

class MainWindow;

/**
 * @brief Coordinates library-scanning and cache update flow for the main window.
 *
 * This controller bridges asynchronous cache/scan messages to UI updates,
 * keeps the in-memory media index in sync, and triggers view refreshes.
 */
class LibraryController {
public:
  /**
   * @brief Creates a library controller bound to the given main window.
   * @param window Owning window context.
   */
  LibraryController(MainWindow* window);

  /**
   * @brief Destroys the controller.
   */
  ~LibraryController();

  /**
   * @brief Opens the music-source manager dialog.
   */
  void ShowDirectoryManager();

  /**
   * @brief Reveals one or more files in Tracker.
   * @param msg Message containing `refs` or a nested `files` message with refs.
   */
  void RevealInTracker(BMessage* msg);

  /**
   * @brief Handles initial cache-load completion and refreshes views.
   */
  void HandleCacheLoaded();

  /**
   * @brief Starts a full media rescan and clears visible list views.
   */
  void StartFullRescan();

  /**
   * @brief Updates scan progress text from a scan-progress message.
   * @param msg Progress message containing folder/file counters and optional time.
   */
  void UpdateScanProgress(BMessage* msg);

  /**
   * @brief Finalizes rescan state and reloads library entries from cache.
   * @param msg Scan-done message with elapsed time metadata.
   */
  void HandleScanDone(BMessage* msg);

  /**
   * @brief Processes a cache-loading batch timer tick.
   */
  void HandleBatchTimer();

  /**
   * @brief Applies a batch of media item updates from cache/scanner.
   * @param msg Batch message containing repeated item fields.
   */
  void HandleMediaBatch(BMessage* msg);

  /**
   * @brief Performs a debounced partial refresh of filtered list views.
   */
  void RefreshPartialViews();

  /**
   * @brief Applies an incremental single-item metadata update.
   * @param msg Message containing item path and updated fields.
   */
  void HandleMediaItemFound(BMessage* msg);

  /**
   * @brief Removes a media item from views and in-memory index.
   * @param msg Message containing the item path.
   */
  void HandleMediaItemRemoved(BMessage* msg);

  /**
   * @brief Rebuilds the path-to-index lookup for `fAllItems`.
   */
  void RebuildPathIndex();

private:
  /** @brief Owning main window and shared state access. */
  MainWindow* fWindow;
};

#endif // BETON_LIBRARY_CONTROLLER_H
