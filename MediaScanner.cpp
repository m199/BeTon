#include "MediaScanner.h"
#include "Debug.h"
#include "Messages.h"
#include "TagSync.h"

#include <Node.h>
#include <Path.h>
#include <SupportDefs.h>
#include <stack>
#include <sys/stat.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tpropertymap.h>

/**
 * @brief Constructor.
 *
 * Initializes the scanner, sets up the worker thread and semaphores.
 * Does NOT start the scan immediately; waits for MSG_START_SCAN.
 *
 * @param startDir The root directory this scanner is responsible for.
 * @param cacheTarget Messenger to the CacheManager (for batch updates).
 * @param liveTarget Messenger to the UI (MainWindow) for progress updates.
 */
MediaScanner::MediaScanner(const entry_ref &startDir, BMessenger cacheTarget,
                           BMessenger liveTarget)
    : BLooper("MediaScanner"), fStartRef(startDir), fCacheTarget(cacheTarget),
      fLiveTarget(liveTarget), fScanRequested(false), fStopRequested(false),
      fIsScanning(false), fScannedDirs(0), fFoundFiles(0) {
  fLastUpdate = std::chrono::steady_clock::now();

  BPath p(&fStartRef);
  fBasePath = p.Path();

  fControlSem = create_sem(0, "MediaScanner Control");

  fWorkerThread =
      spawn_thread(WorkerEntry, "MediaScanner Worker", B_LOW_PRIORITY, this);
  resume_thread(fWorkerThread);
}

/**
 * @brief Destructor.
 *
 * Stops the worker thread and cleans up semaphores.
 */
MediaScanner::~MediaScanner() {
  fStopRequested = true;
  release_sem(fControlSem);

  status_t exitValue;
  wait_for_thread(fWorkerThread, &exitValue);

  delete_sem(fControlSem);
}

/**
 * @brief Message handler for the BLooper.
 *
 * Handles MSG_START_SCAN to wake up the worker thread.
 *
 * @param msg The received message.
 */
void MediaScanner::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_START_SCAN: {
    if (fIsScanning)
      break;

    fScanRequested = true;
    release_sem(fControlSem);
    break;
  }
  default:
    BLooper::MessageReceived(msg);
  }
}

/**
 * @brief Helper to check file extensions.
 *
 * Supported: mp3, wav, flac, ogg, m4a, aac, wma.
 *
 * @param path The file path to check.
 * @return True if the extension is supported.
 */
static bool IsSupportedAudioFile(const BString &path) {
  BString lower(path);
  lower.ToLower();

  static const char *exts[] = {".mp3", ".wav", ".flac", ".ogg",
                               ".m4a", ".aac", ".wma"};

  for (auto ext : exts) {
    if (lower.EndsWith(ext))
      return true;
  }
  return false;
}

/**
 * @brief Processes a single file entry.
 *
 * Workflow:
 * 1. Validates file extension and existence.
 * 2. FAST SKIP: Checks against `fCache` to see if file is unchanged
 * (mtime/size).
 * 3. METADATA: Extracts tags (Title, Artist, Album, Year, MBIDs) using TagLib.
 * 4. BATCHING: Adds the resulting `MediaItem` to `fBatchBuffer` and flushes if
 * full.
 *
 * @param entry The file entry to process.
 */
void MediaScanner::ProcessFile(BEntry &entry) {
  BPath path;

  if (entry.GetPath(&path) != B_OK)
    return;

  // Unconditional trace log
  DEBUG_PRINT("[MediaScanner] Checking file: %s\n", path.Path());

  BString filePath(path.Path());

  if (!IsSupportedAudioFile(filePath))
    return;

  struct stat st{};
  if (stat(path.Path(), &st) != 0)
    return;

  /**
   * Fast Skip with Smart Rating Check
   *
   * Checks if the file's modification time and size are unchanged in the cache.
   * If they match, it performs a lightweight check of the "Media:Rating"
   * attribute to detect rating updates that might not have changed the file
   * mtime.
   *
   * If the rating differs from the cached version, the cache entry is updated
   * and the file is still skipped for full re-parsing.
   */
  if (!fCache.empty()) {
    auto it = fCache.find(filePath);
    if (it != fCache.end()) {
      const MediaItem &old = it->second;
      if (old.mtime == st.st_mtime && old.size == st.st_size) {
        int32 currentRating = 0;
        BNode node(path.Path());
        ssize_t bytesRead = 0;
        if (node.InitCheck() == B_OK) {
          bytesRead = node.ReadAttr("Media:Rating", B_INT32_TYPE, 0,
                                    &currentRating, sizeof(currentRating));
        }

        // Debug Log to see what happens
        // DEBUG_PRINT("[MediaScanner] SmartSkip: %s | Cache: %d | BFS: %d
        // (bytes: %ld)\n",
        //             path.Leaf(), (int)old.rating, (int)currentRating,
        //             (long)bytesRead);

        if (bytesRead < (ssize_t)sizeof(currentRating)) {
          currentRating = 0; // Attribute missing or invalid
        }

        // Normalize rating (0-10)
        if (currentRating < 0 || currentRating > 10)
          currentRating = 0;

        if (currentRating == (int32)old.rating) {
          return;
        }

        MediaItem updated = old;
        updated.rating = (uint32)currentRating;

        DEBUG_PRINT("[MediaScanner] FastUpdate rating %d (was %d) for %s\n",
                    (int)updated.rating, (int)old.rating, filePath.String());

        fBatchLock.Lock();
        fBatchBuffer.push_back(updated);
        bool needsFlush = (fBatchBuffer.size() >= 50);
        fBatchLock.Unlock();

        if (needsFlush)
          FlushBatch();

        return;
      }
    }
  }

  fFoundFiles++;
  ReportProgress();

  BString title, artist, album, genre;
  int32 year = 0;
  int32 track = 0;
  int32 disc = 0;
  int32 bitrate = 0;
  int32 duration = 0;
  BString mbTrackId, mbAlbumId, mbArtistId;

  try {
    TagLib::FileRef f(path.Path());

    if (!f.isNull() && f.tag()) {
      TagLib::Tag *tag = f.tag();
      title = tag->title().toCString(true);
      artist = tag->artist().toCString(true);
      album = tag->album().toCString(true);
      genre = tag->genre().toCString(true);
      year = tag->year();
      track = tag->track();

      TagLib::PropertyMap props = f.file()->properties();
      if (props.contains("DISCNUMBER")) {
        TagLib::StringList l = props["DISCNUMBER"];
        if (!l.isEmpty()) {
          disc = l.front().toInt();
        }
      }

      auto getProp = [&](const char *key, BString &target) {
        if (props.contains(key)) {
          TagLib::StringList l = props[key];
          if (!l.isEmpty()) {
            target = l.front().toCString(true);
          }
        }
      };

      BString localMbTrackId, localMbAlbumId, localMbArtistId;
      getProp("MUSICBRAINZ_TRACKID", localMbTrackId);
      getProp("MUSICBRAINZ_ALBUMID", localMbAlbumId);
      getProp("MUSICBRAINZ_ARTISTID", localMbArtistId);

      if (!localMbArtistId.IsEmpty())
        mbArtistId = localMbArtistId;
    }

    if (!f.isNull() && f.audioProperties()) {
      TagLib::AudioProperties *props = f.audioProperties();
      duration = props->lengthInSeconds();
      bitrate = props->bitrate();
    }
  } catch (...) {
  }

  TagData bfsData;
  bool hasBfsData = TagSync::ReadBfsAttributes(path, bfsData);

  if (title.IsEmpty() || artist.IsEmpty()) {
    if (hasBfsData) {
      if (title.IsEmpty())
        title = bfsData.title;
      if (artist.IsEmpty())
        artist = bfsData.artist;
      if (album.IsEmpty())
        album = bfsData.album;
      if (genre.IsEmpty())
        genre = bfsData.genre;
      if (year == 0)
        year = bfsData.year;
      if (track == 0)
        track = bfsData.track;
      if (disc == 0)
        disc = bfsData.disc;

      if (duration == 0)
        duration = bfsData.lengthSec;
      if (bitrate == 0)
        bitrate = bfsData.bitrate;
    }
  }

  if (title.IsEmpty()) {
    title = path.Leaf();
  }

  MediaItem item;
  BPath parentPath;
  if (path.GetParent(&parentPath) == B_OK) {
    item.base = parentPath.Path();
  } else {
    item.base = fBasePath;
  }
  item.path = filePath;
  item.title = title;
  item.artist = artist;
  item.album = album;
  item.genre = genre;
  item.year = year;
  item.track = track;
  item.disc = disc;
  item.duration = duration;
  item.bitrate = bitrate;
  item.size = st.st_size;
  item.mtime = st.st_mtime;
  item.inode = st.st_ino;
  item.mbTrackId = mbTrackId;
  item.mbAlbumId = mbAlbumId;
  item.mbArtistId = mbArtistId;
  if (hasBfsData) {
    item.rating = bfsData.rating;
    if (item.rating > 0)
      DEBUG_PRINT("[MediaScanner] Read rating %d for %s\n", (int)item.rating,
                  item.path.String());
    else
      DEBUG_PRINT("[MediaScanner] Rating is 0 for %s\n", item.path.String());
  } else {
    DEBUG_PRINT("[MediaScanner] No BFS data for %s\n", item.path.String());
  }

  bool needsFlush = false;

  fBatchLock.Lock();
  fBatchBuffer.push_back(item);
  if (fBatchBuffer.size() >= 100) {
    needsFlush = true;
  }
  fBatchLock.Unlock();

  if (needsFlush) {
    FlushBatch();
  }
}

/**
 * @brief Sends the current batch of found items to the CacheManager.
 *
 * Uses MSG_MEDIA_BATCH. Clears the buffer after sending.
 */
void MediaScanner::FlushBatch() {
  fBatchLock.Lock();
  if (fBatchBuffer.empty()) {
    fBatchLock.Unlock();
    return;
  }

  BMessage msg(MSG_MEDIA_BATCH);
  msg.AddString("base", fBasePath);

  for (const auto &item : fBatchBuffer) {
    msg.AddString("path", item.path);
    msg.AddString("item_base", item.base);
    msg.AddString("title", item.title);
    msg.AddString("artist", item.artist);
    msg.AddString("album", item.album);
    msg.AddString("genre", item.genre);
    msg.AddInt32("year", item.year);
    msg.AddInt32("track", item.track);
    msg.AddInt32("disc", item.disc);
    msg.AddInt32("duration", item.duration);
    msg.AddInt32("bitrate", item.bitrate);
    msg.AddInt64("size", item.size);
    msg.AddInt64("mtime", item.mtime);
    msg.AddInt64("inode", item.inode);
    msg.AddInt32("rating", item.rating);
    if (item.rating > 0)
      DEBUG_PRINT("[MediaScanner] FlushBatch: Sending rating %d for %s\n",
                  (int)item.rating, item.path.String());
  }

  fBatchBuffer.clear();
  fBatchLock.Unlock();

  if (fCacheTarget.IsValid())
    fCacheTarget.SendMessage(&msg);
}

/**
 * @brief Reports scan progress to the UI.
 *
 * Rates limited to avoid flooding the message queue.
 * Sends MSG_SCAN_PROGRESS with dirs and files counts.
 */
void MediaScanner::ReportProgress() {
  auto now = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - fLastUpdate)
          .count();

  if (elapsed > 100) {
    fLastUpdate = now;
    if (fLiveTarget.IsValid()) {
      BMessage msg(MSG_SCAN_PROGRESS);
      msg.AddInt32("dirs", fScannedDirs);
      msg.AddInt32("files", fFoundFiles);

      auto totalElapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - fStartTime)
              .count();
      msg.AddInt64("elapsed_sec", totalElapsed);

      fLiveTarget.SendMessage(&msg);
    }
  }
}

/**
 * @brief Static entry point for the worker thread.
 *
 * @param data Pointer to the MediaScanner instance.
 * @return B_OK on exit.
 */
status_t MediaScanner::WorkerEntry(void *data) {
  MediaScanner *self = (MediaScanner *)data;
  self->WorkerMethod();
  return B_OK;
}

/**
 * @brief Helper method for thread loop.
 *
 * Waits on fControlSem for scan requests.
 * Performs iterative DFS to traverse directories.
 * Sends MSG_SCAN_DONE when finished.
 */
void MediaScanner::WorkerMethod() {
  while (true) {
    status_t err = acquire_sem(fControlSem);
    if (err == B_INTERRUPTED)
      continue;
    if (err != B_OK)
      break;

    if (fStopRequested)
      break;

    if (fScanRequested) {
      fScanRequested = false;
      fIsScanning = true;

      fScannedDirs = 0;
      fFoundFiles = 0;
      fStartTime = std::chrono::steady_clock::now();

      std::stack<BString> stack;
      stack.push(fBasePath);

      while (!stack.empty() && !fStopRequested) {
        BString currentPath = stack.top();
        stack.pop();

        BDirectory dir(currentPath.String());
        if (dir.InitCheck() != B_OK)
          continue;

        fScannedDirs++;
        ReportProgress();

        BEntry entry;
        dir.Rewind();
        while (dir.GetNextEntry(&entry, true) == B_OK) {
          if (fStopRequested)
            break;

          BPath p;
          if (entry.GetPath(&p) != B_OK)
            continue;

          BString leaf(p.Leaf());
          if (leaf.Length() > 0 && leaf.ByteAt(0) == '.')
            continue;

          if (entry.IsDirectory()) {
            stack.push(p.Path());
          } else {
            ProcessFile(entry);
          }
        }
      }
    }

    FlushBatch();

    if (!fStopRequested) {
      DEBUG_PRINT("[MediaScanner] Worker: Scan finished\\n");

      if (fCacheTarget.IsValid()) {
        fCacheTarget.SendMessage(MSG_SCAN_DONE);
      }

      if (fLiveTarget.IsValid()) {
        BMessage doneMsg(MSG_SCAN_DONE);
        auto totalElapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::steady_clock::now() - fStartTime)
                                .count();
        doneMsg.AddInt64("elapsed_sec", totalElapsed);
        fLiveTarget.SendMessage(&doneMsg);

        BMessage progress(MSG_SCAN_PROGRESS);
        progress.AddInt32("dirs", fScannedDirs);
        progress.AddInt32("files", fFoundFiles);
        fLiveTarget.SendMessage(&progress);
      }
    }

    fIsScanning = false;
    PostMessage(B_QUIT_REQUESTED);
    return;
  }
}
