#include "MediaLibraryCache.h"
#include "Config.h"
#include "Debug.h"
#include "MediaLibraryScanner.h"
#include "Messages.h"
#include "MusicSourceSettings.h"
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Node.h>
#include <OS.h>
#include <Path.h>
#include <set>
#include <sys/stat.h>

/**
 * @brief Returns whether a path points to MIDI while MIDI playback is disabled.
 */
static bool IsDisabledMidiPath(const BString &path) {
#if ENABLE_MIDI_PLAYBACK
  (void)path;
  return false;
#else
  BString lower(path);
  lower.ToLower();
  return lower.EndsWith(".mid") || lower.EndsWith(".midi");
#endif
}

/**
 * @brief Constructor.
 * Determines the path to the cache file (user settings) but does not load it
 * yet.
 */
MediaLibraryCache::MediaLibraryCache(const BMessenger &target)
    : BLooper("MediaLibraryCache"), fTarget(target) {
  BPath settingsPath;
  find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath);
  settingsPath.Append("BeTon/media.cache");
  fCachePath = settingsPath.Path();
}

MediaLibraryCache::~MediaLibraryCache() {
  if (fCacheDirty) {
    DEBUG_PRINT("Saving dirty cache before shutdown...\n");
    SaveCache();
  }

  for (BQuery *q : fRatingQueries) {
    delete q;
  }
  fRatingQueries.clear();
}

/**
 * @brief Loads the list of watched directories from 'directories.settings' or
 * legacy 'directories.txt'.
 * @param outDirs Vector to populate with directory paths.
 */
void MediaLibraryCache::LoadDirectories(std::vector<BString> &outDirs) {
  BPath p;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &p) != B_OK)
    return;

  // Try loading from the current settings format first.
  BPath settingsPath = p;
  settingsPath.Append("BeTon/directories.settings");
  BFile file(settingsPath.Path(), B_READ_ONLY);

  if (file.InitCheck() == B_OK) {
    BMessage archive;
    if (archive.Unflatten(&file) == B_OK) {
      BMessage srcMsg;
      for (int32 i = 0; archive.FindMessage("source", i, &srcMsg) == B_OK;
           i++) {
        MusicSourceSettings src;
        src.LoadFrom(&srcMsg);
        if (!src.path.IsEmpty()) {
          outDirs.push_back(src.path);
        }
      }
      if (!outDirs.empty()) {
        DEBUG_PRINT("Loaded %zu directories from settings\n",
                    outDirs.size());
        return;
      }
    }
  }
}

/**
 * @brief Triggers a full rescan of all configured directories.
 *
 * Scanning Process:
 * 1. Remove entries that belong to directories no longer monitored.
 * 2. Start Scanners for each directory.
 * 3. Remove existing known files if they are gone from reachable sources.
 *
 * Note: Real sync happens via Scanners reporting back.
 */
void MediaLibraryCache::StartScan() {
  std::vector<BString> dirs;
  LoadDirectories(dirs);

  // 1) Remove entries that belong to directories no longer monitored.
  std::set<BString> validBases(dirs.begin(), dirs.end());

  for (auto it = fEntries.begin(); it != fEntries.end();) {
    const MediaItem &e = it->second;
    if (validBases.find(e.base) == validBases.end()) {
      it = fEntries.erase(it);
      fCacheDirty = true;
    } else {
      ++it;
    }
  }

  // Notify UI that scan starts from current known state.
  if (fTarget.IsValid()) {
    BMessage update(MSG_CACHE_LOADED);
    fTarget.SendMessage(&update);
  }

  // 2) Start scanners.
  fActiveScanners = 0;
  std::set<BString> offlineBases;
  for (const auto &dirPath : dirs) {
    entry_ref ref;
    status_t s = get_ref_for_path(dirPath.String(), &ref);
    if (s != B_OK) {
      offlineBases.insert(dirPath);
      MarkBaseOffline(dirPath);
      continue;
    }

    BDirectory dir(&ref);
    if (dir.InitCheck() != B_OK) {
      offlineBases.insert(dirPath);
      MarkBaseOffline(dirPath);
      continue;
    }

    BVolume vol(ref.device);
    if (vol.KnowsQuery() &&
        fQueriedVolumes.find(ref.device) == fQueriedVolumes.end()) {
      _InitRatingLiveQueries(ref.device);
    }

    // Launch scanner. It reports via MSG_MEDIA_ITEM_FOUND/MSG_SCAN_DONE.
    auto *scanner = new MediaLibraryScanner(ref, BMessenger(this), fTarget);
    scanner->SetCache(fEntries);
    scanner->Run();

    BMessenger msgr(scanner);
    msgr.SendMessage(MSG_START_SCAN);
    fActiveScanners++;
  }

  // 3) Remove stale files from reachable sources.
  for (auto it = fEntries.begin(); it != fEntries.end();) {
    const BString path = it->first;
    bool wasMissing = it->second.missing;

    bool baseOffline = false;
    for (const auto &base : offlineBases) {
      BString basePrefix(base);
      basePrefix << "/";
      if (path == base || path.StartsWith(basePrefix)) {
        baseOffline = true;
        break;
      }
    }
    if (baseOffline) {
      ++it;
      continue;
    }

    BEntry e(path.String());
    if (!e.Exists()) {
      DEBUG_PRINT("Remove missing file: %s\n", path.String());

      if (fTarget.IsValid()) {
        BMessage gone(MSG_MEDIA_ITEM_REMOVED);
        gone.AddString("path", path);
        fTarget.SendMessage(&gone);
      }

      it = fEntries.erase(it);
      fCacheDirty = true;
      continue;
    }

    if (wasMissing) {
      it->second.missing = false;
      fCacheDirty = true;
    }

    ++it;
  }

  // If no scanners were started (e.g. no directories), finish immediately.
  if (fActiveScanners == 0) {
    SaveCache();
    if (fTarget.IsValid()) {
      BMessage done(MSG_SCAN_DONE);
      fTarget.SendMessage(&done);
    }
  }
}

/**
 * @brief Saves the current in-memory cache to disk.
 * The cache is flattened into a BMessage and saved to 'media.cache'.
 */
void MediaLibraryCache::SaveCache() {
  bigtime_t t0 = system_time();

  BFile file(fCachePath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
  if (file.InitCheck() != B_OK) {
    DEBUG_PRINT("SaveCache: Failed to open %s\n",
                fCachePath.String());
    return;
  }

  uint32 magic = 'BTCA';
  uint32 version = 2;
  uint32 count = (uint32)fEntries.size();

  file.Write(&magic, sizeof(magic));
  file.Write(&version, sizeof(version));
  file.Write(&count, sizeof(count));

  auto writeStr = [&file](const BString &s) {
    uint32 len = (uint32)s.Length();
    file.Write(&len, sizeof(len));
    if (len > 0)
      file.Write(s.String(), len);
  };

  for (const auto &[key, e] : fEntries) {
    writeStr(e.path);
    writeStr(e.base);
    writeStr(e.title);
    writeStr(e.artist);
    writeStr(e.album);
    writeStr(e.albumArtist);
    writeStr(e.genre);
    writeStr(e.comment);
    writeStr(e.composer);
    writeStr(e.mbTrackId);
    writeStr(e.mbAlbumId);
    writeStr(e.mbArtistId);
    writeStr(e.acoustId);

    file.Write(&e.year, sizeof(e.year));
    file.Write(&e.track, sizeof(e.track));
    file.Write(&e.trackTotal, sizeof(e.trackTotal));
    file.Write(&e.disc, sizeof(e.disc));
    file.Write(&e.discTotal, sizeof(e.discTotal));
    file.Write(&e.duration, sizeof(e.duration));
    file.Write(&e.bitrate, sizeof(e.bitrate));
    file.Write(&e.sampleRate, sizeof(e.sampleRate));
    file.Write(&e.channels, sizeof(e.channels));
    file.Write(&e.rating, sizeof(e.rating));
    file.Write(&e.size, sizeof(e.size));
    file.Write(&e.mtime, sizeof(e.mtime));
    file.Write(&e.inode, sizeof(e.inode));

    uint8 flags = e.missing ? 1 : 0;
    file.Write(&flags, sizeof(flags));
  }

  bigtime_t t1 = system_time();
  DEBUG_PRINT("SaveCache: %lu items in %lld us to %s\n", (unsigned long)count,
              (long long)(t1 - t0), fCachePath.String());
  fCacheDirty = false;
}

/**
 * @brief Loads the cache from disk into memory.
 *
 * Supports both the new binary format (version 2, magic 'BTCA')
 * and the legacy BMessage format for migration.
 */
void MediaLibraryCache::LoadCache() {
  fEntries.clear();

  bigtime_t t0 = system_time();

  BFile file(fCachePath, B_READ_ONLY);
  if (file.InitCheck() != B_OK) {
    DEBUG_PRINT("Kein Cache gefunden (%s)\n", fCachePath.String());
    return;
  }

  uint32 magic = 0;
  if (file.Read(&magic, sizeof(magic)) != sizeof(magic)) {
    DEBUG_PRINT("Cache file too small\n");
    return;
  }

  if (magic == 'BTCA') {
    uint32 version = 0;
    uint32 count = 0;
    file.Read(&version, sizeof(version));
    file.Read(&count, sizeof(count));

    auto readStr = [&file](BString &s) {
      uint32 len = 0;
      file.Read(&len, sizeof(len));
      if (len > 0) {
        char *buf = s.LockBuffer(len + 1);
        file.Read(buf, len);
        buf[len] = '\0';
        s.UnlockBuffer(len);
      } else {
        s = "";
      }
    };

    for (uint32 i = 0; i < count; i++) {
      MediaItem e;
      readStr(e.path);
      readStr(e.base);
      readStr(e.title);
      readStr(e.artist);
      readStr(e.album);
      readStr(e.albumArtist);
      readStr(e.genre);
      readStr(e.comment);
      readStr(e.composer);
      readStr(e.mbTrackId);
      readStr(e.mbAlbumId);
      readStr(e.mbArtistId);
      readStr(e.acoustId);

      file.Read(&e.year, sizeof(e.year));
      file.Read(&e.track, sizeof(e.track));
      file.Read(&e.trackTotal, sizeof(e.trackTotal));
      file.Read(&e.disc, sizeof(e.disc));
      file.Read(&e.discTotal, sizeof(e.discTotal));
      file.Read(&e.duration, sizeof(e.duration));
      file.Read(&e.bitrate, sizeof(e.bitrate));
      file.Read(&e.sampleRate, sizeof(e.sampleRate));
      file.Read(&e.channels, sizeof(e.channels));
      file.Read(&e.rating, sizeof(e.rating));
      file.Read(&e.size, sizeof(e.size));
      file.Read(&e.mtime, sizeof(e.mtime));
      file.Read(&e.inode, sizeof(e.inode));

      uint8 flags = 0;
      file.Read(&flags, sizeof(flags));
      e.missing = (flags & 1) != 0;

      if (IsDisabledMidiPath(e.path)) {
        fCacheDirty = true;
        continue;
      }

      fEntries[e.path] = std::move(e);
    }

    bigtime_t t1 = system_time();
    DEBUG_PRINT("LoadCache (binary v%lu): %zu items in %lld us\n",
                (unsigned long)version, fEntries.size(), (long long)(t1 - t0));
  } else {
    file.Seek(0, SEEK_SET);

    BMessage archive;
    if (archive.Unflatten(&file) != B_OK) {
      DEBUG_PRINT("Konnte Cache nicht unflatten (%s)\n", fCachePath.String());
      return;
    }

    bigtime_t t1 = system_time();

    MediaItem entry;
    for (int32 i = 0;; i++) {
      BMessage item;
      if (archive.FindMessage("entry", i, &item) != B_OK)
        break;

      entry.path = item.GetString("path", "");
      entry.base = item.GetString("base", "");
      entry.title = item.GetString("title", "");
      entry.artist = item.GetString("artist", "");
      entry.album = item.GetString("album", "");
      entry.genre = item.GetString("genre", "");
      entry.year = item.GetInt32("year", 0);
      entry.track = item.GetInt32("track", 0);
      entry.disc = item.GetInt32("disc", 0);
      entry.duration = item.GetInt32("duration", 0);
      entry.bitrate = item.GetInt32("bitrate", 0);
      entry.size = item.GetInt64("size", 0);
      entry.mtime = item.GetInt64("mtime", 0);
      entry.inode = item.GetInt64("inode", 0);
      entry.missing = item.GetBool("missing", false);

      entry.mbAlbumId = item.GetString("mbAlbumId", "");
      entry.mbArtistId = item.GetString("mbArtistId", "");
      entry.mbTrackId = item.GetString("mbTrackId", "");
      entry.rating = item.GetInt32("rating", 0);

      if (IsDisabledMidiPath(entry.path)) {
        fCacheDirty = true;
        continue;
      }

      fEntries[entry.path] = entry;
    }

    bigtime_t t2 = system_time();
    DEBUG_PRINT("LoadCache (BMessage legacy): %zu items "
                "(unflatten=%lld us, extract=%lld us)\n",
                fEntries.size(), (long long)(t1 - t0), (long long)(t2 - t1));

    fCacheDirty = true;
  }

  if (fTarget.IsValid()) {
    BMessage msg(MSG_CACHE_LOADED);
    fTarget.SendMessage(&msg);
  }

  _InitAllLiveQueries();
}

/**
 * @brief Returns a copy of all current media items.
 * @return std::vector<MediaItem>
 */
std::vector<MediaItem> MediaLibraryCache::AllEntries() const {
  std::vector<MediaItem> out;
  out.reserve(fEntries.size());

  for (const auto &kv : fEntries) {
    out.push_back(kv.second);
  }
  return out;
}

/**
 * @brief Main message loop for the MediaLibraryCache looper.
 * Handles loading, batch updates, and scanning notifications.
 */
void MediaLibraryCache::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_LOAD_CACHE:
    DEBUG_PRINT("Asynchronous cache load started\\n");
    LoadCache();
    break;

  case MSG_MEDIA_BATCH: {
    type_code type;
    int32 count = 0;
    if (msg->GetInfo("path", &type, &count) != B_OK)
      break;

    const char *baseStr = nullptr;
    msg->FindString("base", &baseStr);

    for (int32 i = 0; i < count; i++) {
      MediaItem e;
      const char *itemBaseStr = nullptr;
      if (msg->FindString("item_base", i, &itemBaseStr) == B_OK)
        e.base = itemBaseStr;
      else if (baseStr)
        e.base = baseStr;

      const char *tmp = nullptr;
      if (msg->FindString("path", i, &tmp) == B_OK)
        e.path = tmp;
      if (msg->FindString("title", i, &tmp) == B_OK)
        e.title = tmp;
      if (msg->FindString("artist", i, &tmp) == B_OK)
        e.artist = tmp;
      if (msg->FindString("album", i, &tmp) == B_OK)
        e.album = tmp;
      if (msg->FindString("genre", i, &tmp) == B_OK)
        e.genre = tmp;

      msg->FindInt32("year", i, &e.year);
      msg->FindInt32("track", i, &e.track);
      msg->FindInt32("disc", i, &e.disc);
      msg->FindInt32("duration", i, &e.duration);
      msg->FindInt32("bitrate", i, &e.bitrate);
      msg->FindInt64("size", i, &e.size);
      msg->FindInt64("mtime", i, &e.mtime);
      msg->FindInt64("inode", i, &e.inode);
      msg->FindInt32("rating", i, &e.rating);
      if (e.rating > 0)
        DEBUG_PRINT("Received rating %d for %s\n", (int)e.rating,
                    e.path.String());

      if (msg->FindString("mbAlbumId", i, &tmp) == B_OK)
        e.mbAlbumId = tmp;
      if (msg->FindString("mbArtistId", i, &tmp) == B_OK)
        e.mbArtistId = tmp;
      if (msg->FindString("mbTrackId", i, &tmp) == B_OK)
        e.mbTrackId = tmp;

      AddOrUpdateEntry(e);
    }

    DEBUG_PRINT("Processed batch of %d items\n", (int)count);

    // Ensure cache is saved after scan completion.
    fCacheDirty = true;

    if (fTarget.IsValid())
      fTarget.SendMessage(msg);
    break;
  }

  case MSG_MEDIA_ITEM_FOUND: {
    MediaItem e;
    const char *tmpStr = nullptr;

    if (msg->FindString("path", &tmpStr) != B_OK)
      break;
    e.path = tmpStr;

    auto existing = fEntries.find(e.path);
    if (existing != fEntries.end())
      e = existing->second;
    e.path = tmpStr;

    if (msg->FindString("base", &tmpStr) == B_OK)
      e.base = tmpStr;
    if (msg->FindString("title", &tmpStr) == B_OK)
      e.title = tmpStr;
    if (msg->FindString("artist", &tmpStr) == B_OK)
      e.artist = tmpStr;
    if (msg->FindString("album", &tmpStr) == B_OK)
      e.album = tmpStr;
    if (msg->FindString("genre", &tmpStr) == B_OK)
      e.genre = tmpStr;
    if (msg->FindString("comment", &tmpStr) == B_OK)
      e.comment = tmpStr;
    if (msg->FindString("albumArtist", &tmpStr) == B_OK)
      e.albumArtist = tmpStr;
    if (msg->FindString("composer", &tmpStr) == B_OK)
      e.composer = tmpStr;

    msg->FindInt32("year", &e.year);
    msg->FindInt32("track", &e.track);
    msg->FindInt32("trackTotal", &e.trackTotal);
    msg->FindInt32("disc", &e.disc);
    msg->FindInt32("discTotal", &e.discTotal);
    msg->FindInt32("duration", &e.duration);
    msg->FindInt32("bitrate", &e.bitrate);
    msg->FindInt32("sampleRate", &e.sampleRate);
    msg->FindInt32("channels", &e.channels);
    msg->FindInt32("rating", &e.rating);
    msg->FindInt64("size", &e.size);
    msg->FindInt64("mtime", &e.mtime);
    msg->FindInt64("inode", &e.inode);

    if (msg->FindString("mbAlbumId", &tmpStr) == B_OK ||
        msg->FindString("mbAlbumID", &tmpStr) == B_OK)
      e.mbAlbumId = tmpStr;
    if (msg->FindString("mbArtistId", &tmpStr) == B_OK ||
        msg->FindString("mbArtistID", &tmpStr) == B_OK)
      e.mbArtistId = tmpStr;
    if (msg->FindString("mbTrackId", &tmpStr) == B_OK ||
        msg->FindString("mbTrackID", &tmpStr) == B_OK)
      e.mbTrackId = tmpStr;

    AddOrUpdateEntry(e);

    fCacheDirty = true;

    DEBUG_PRINT("Item found: path=%s, title=%s\n",
                e.path.String(), e.title.String());

    if (fTarget.IsValid())
      fTarget.SendMessage(msg);
    break;
  }

  case MSG_FILE_MOVED: {
    BString from, to;
    if (msg->FindString("from", &from) != B_OK ||
        msg->FindString("to", &to) != B_OK)
      break;

    auto it = fEntries.find(from);
    if (it == fEntries.end())
      break;

    MediaItem e = it->second;
    fEntries.erase(it);
    e.path = to;
    fEntries[e.path] = e;

    DEBUG_PRINT("File moved in cache: %s -> %s\n", from.String(),
                to.String());
    SaveCache();
    fCacheDirty = false;
    break;
  }

  case MSG_REGISTER_TARGET: {
    BMessenger newTarget;
    if (msg->FindMessenger("target", &newTarget) == B_OK) {
      fTarget = newTarget;
      DEBUG_PRINT("UI target registered\n");
    }
    break;
  }
  case MSG_RESCAN:
    DEBUG_PRINT("received MSG_RESCAN, starting new scan\n");
    StartScan();
    break;

  case MSG_SCAN_DONE: {
    DEBUG_PRINT("received MSG_SCAN_DONE (scanners left: %ld)\\n",
                (long)(fActiveScanners - 1));

    if (--fActiveScanners <= 0) {
      if (fCacheDirty) {
        DEBUG_PRINT(
            "all scanners finished, writing media.cache\\n");
        SaveCache();
        fCacheDirty = false;
      }

      if (fTarget.IsValid()) {
        DEBUG_PRINT("forward MSG_SCAN_DONE to MainWindow\\n");
        BMessage done(MSG_SCAN_DONE);
        fTarget.SendMessage(&done);
      }
    }
    break;
  }

  case B_QUERY_UPDATE: {
    int32 opcode;
    if (msg->FindInt32("opcode", &opcode) == B_OK) {
      DEBUG_PRINT("B_QUERY_UPDATE received with opcode %ld\n",
                  (long)opcode);

      dev_t device;
      ino_t directory;
      const char *name;
      if (msg->FindInt32("device", &device) == B_OK &&
          msg->FindInt64("directory", &directory) == B_OK &&
          msg->FindString("name", &name) == B_OK) {
        entry_ref ref(device, directory, name);
        BPath path(&ref);
        if (path.InitCheck() == B_OK) {
          BString pathStr = path.Path();
          DEBUG_PRINT("B_QUERY_UPDATE path: %s\n",
                      pathStr.String());

          auto it = fEntries.find(pathStr);
          if (it != fEntries.end()) {
            DEBUG_PRINT("Path found in fEntries, rereading "
                        "attributes...\n");
            if (_RereadBfsAttributes(it->second)) {
              DEBUG_PRINT("Attributes changed! Sending "
                          "MSG_MEDIA_ITEM_FOUND\n");
              fCacheDirty = true;
              if (fTarget.IsValid()) {
                BMessage update(MSG_MEDIA_ITEM_FOUND);
                update.AddString("path", it->second.path);
                update.AddString("title", it->second.title);
                update.AddString("artist", it->second.artist);
                update.AddString("album", it->second.album);
                update.AddString("genre", it->second.genre);
                update.AddString("comment", it->second.comment);
                update.AddString("albumArtist", it->second.albumArtist);
                update.AddString("composer", it->second.composer);
                update.AddInt32("year", it->second.year);
                update.AddInt32("track", it->second.track);
                update.AddInt32("rating", it->second.rating);
                update.AddInt32("duration", it->second.duration);
                update.AddInt32("bitrate", it->second.bitrate);
                fTarget.SendMessage(&update);
              }
            }
          }
        }
      }
    }
    break;
  }

  default:
    BLooper::MessageReceived(msg);
  }
}

/**
 * @brief Updates or inserts a media item into the internal map.
 * Also checks for potential conflicts or data integrity issues (warns on DB ID
 * loss).
 * @param entry The item to store.
 */
void MediaLibraryCache::AddOrUpdateEntry(const MediaItem &entry) {
  auto it = fEntries.find(entry.path);
  if (it == fEntries.end()) {
    fEntries[entry.path] = entry;

  } else {
    const MediaItem &old = it->second;
    if (!old.mbTrackId.IsEmpty() && entry.mbTrackId.IsEmpty()) {
      DEBUG_PRINT("WARNING: Overwriting existing MB Track ID "
                  "for %s with empty value!\n",
                  entry.path.String());
    }
    fEntries[entry.path] = entry;
  }
}

/**
 * @brief Marks all entries belonging to a specific base path as "missing".
 * This is used when a configured directory is not found/mounted.
 */
void MediaLibraryCache::MarkBaseOffline(const BString &basePath) {
  for (auto &kv : fEntries) {
    if (kv.first.StartsWith(basePath)) {
      kv.second.missing = true;
    }
  }

  if (fTarget.IsValid()) {
    BMessage off(MSG_BASE_OFFLINE);
    off.AddString("base", basePath);
    fTarget.SendMessage(&off);
  }
}

/**
 * @brief Creates live queries for `Media:Rating == 1..10` on one device.
 * @param device Target BFS volume device id.
 */
void MediaLibraryCache::_InitRatingLiveQueries(dev_t device) {
  BVolume vol(device);
  if (vol.InitCheck() != B_OK || !vol.KnowsQuery())
    return;

  for (int32 i = 1; i <= 10; i++) {
    BQuery *q = new BQuery();
    q->SetVolume(&vol);
    q->PushAttr("Media:Rating");
    q->PushInt32(i);
    q->PushOp(B_EQ);
    q->SetTarget(BMessenger(this));
    status_t status = q->Fetch();
    if (status == B_OK) {
      entry_ref dummy;
      while (q->GetNextRef(&dummy) == B_OK) {
        // Drain initial results so the live query starts emitting updates.
      }
    } else {
      DEBUG_PRINT("Live Query failed for rating %ld on device "
                  "%d (status %d)\n",
                  (long)i, (int)device, (int)status);
    }
    fRatingQueries.push_back(q);
  }

  fQueriedVolumes.insert(device);
  DEBUG_PRINT(
      "Initialized 10 Rating Live Queries for device %d\n",
      (int)device);
}

/**
 * @brief Initializes live queries for all configured directories.
 * This is called after the cache is loaded to ensure queries are active
 * even if a full scan is not performed.
 */
void MediaLibraryCache::_InitAllLiveQueries() {
  std::vector<BString> dirs;
  LoadDirectories(dirs);
  for (const BString &dirPath : dirs) {
    BEntry entry(dirPath.String(), true);
    if (entry.InitCheck() == B_OK && entry.Exists() && entry.IsDirectory()) {
      entry_ref ref;
      entry.GetRef(&ref);
      BVolume vol(ref.device);
      if (vol.KnowsQuery() &&
          fQueriedVolumes.find(ref.device) == fQueriedVolumes.end()) {
        _InitRatingLiveQueries(ref.device);
      }
    }
  }
}

/**
 * @brief Re-reads BFS attributes for a single media item.
 *
 * Opens the file's BNode and reads Haiku's standard audio BFS attributes
 * (Media:Title, Audio:Artist, Media:Rating, etc.). Updates the
 * MediaItem fields in-place.
 *
 * @param item The item to update.
 * @return True if any attribute value changed, false otherwise.
 */
bool MediaLibraryCache::_RereadBfsAttributes(MediaItem &item) {
  BNode node(item.path.String());
  if (node.InitCheck() != B_OK)
    return false;

  char buffer[512];
  memset(buffer, 0, sizeof(buffer));
  int32 intVal = 0;
  bool changed = false;

  auto readStr = [&](const char *attr, BString &field) {
    memset(buffer, 0, sizeof(buffer));
    if (node.ReadAttr(attr, B_STRING_TYPE, 0, buffer, sizeof(buffer)) > 0) {
      BString val(buffer);
      if (val != field) {
        field = val;
        changed = true;
      }
    }
  };

  auto readInt = [&](const char *attr, int32 &field) {
    intVal = 0;
    if (node.ReadAttr(attr, B_INT32_TYPE, 0, &intVal, sizeof(intVal)) > 0) {
      if (intVal != field) {
        field = intVal;
        changed = true;
      }
    }
  };

  readStr("Media:Title", item.title);
  readStr("Audio:Artist", item.artist);
  readStr("Audio:Album", item.album);
  readStr("Media:Genre", item.genre);
  readStr("Media:Comment", item.comment);
  readInt("Media:Year", item.year);
  readInt("Audio:Track", item.track);
  readInt("Media:Rating", item.rating);
  readInt("Media:Length", item.duration);
  readInt("Audio:Bitrate", item.bitrate);

  return changed;
}
