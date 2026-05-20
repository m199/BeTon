#ifndef BETON_MEDIA_LIBRARY_SCANNER_H
#define BETON_MEDIA_LIBRARY_SCANNER_H

#include "MediaItem.h"

#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <Locker.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>
#include <String.h>
#include <atomic>
#include <chrono>
#include <map>
#include <vector>

/**
 * @class MediaLibraryScanner
 * @brief Background worker for recursive directory scanning and metadata
 * extraction.
 *
 * Runs in its own thread (via BLooper and a separate worker thread).
 * Scans a directory tree, identifies audio files, extracts metadata using
 * TagLib, and sends batches of `MediaItem`s to the `MediaLibraryCache` for storage.
 *
 * Supports incremental scanning by checking file modification times against
 * a provided cache map.
 */
class MediaLibraryScanner : public BLooper {
public:
  /**
   * @brief Constructs the scanner.
   * @param startDir Root directory to scan.
   * @param cacheTarget Messenger to receive batched MediaItems
   * (MSG_MEDIA_BATCH).
   * @param liveTarget Messenger to receive progress updates
   * (MSG_SCAN_PROGRESS).
   */
  MediaLibraryScanner(const entry_ref &startDir, BMessenger cacheTarget,
               BMessenger liveTarget);
  virtual ~MediaLibraryScanner();

  void MessageReceived(BMessage *msg) override;

  /**
   * @brief Pre-loads the cache to enable incremental scanning.
   * @param cache Map of existing file paths to MediaItems.
   */
  void SetCache(const std::map<BString, MediaItem> &cache) { fCache = cache; }

private:
  void ProcessFile(BEntry &entry);
  void FlushBatch();
  void ReportProgress();

  static status_t WorkerEntry(void *data);
  void WorkerMethod();

  /** @name Configuration & Messaging */
  ///@{
  entry_ref fStartRef;
  BMessenger fCacheTarget;
  BMessenger fLiveTarget;
  BString fBasePath;
  ///@}

  /** @name Data */
  ///@{
  std::map<BString, MediaItem> fCache;
  std::vector<MediaItem> fBatchBuffer;
  BLocker fBatchLock;
  ///@}

  /** @name Threading */
  ///@{
  thread_id fWorkerThread;
  sem_id fControlSem;
  ///@}

  /** @name State Flags */
  ///@{
  bool fScanRequested;
  std::atomic<bool> fStopRequested;
  std::atomic<bool> fIsScanning;
  ///@}

  /** @name Progress Tracking */
  ///@{
  std::atomic<int> fScannedDirs;
  std::atomic<int> fFoundFiles;
  std::chrono::steady_clock::time_point fLastUpdate;
  std::chrono::steady_clock::time_point fStartTime;
  ///@}
};

#endif // BETON_MEDIA_LIBRARY_SCANNER_H
