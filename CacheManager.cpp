#include "CacheManager.h"
#include "Debug.h"
#include "MediaScanner.h"
#include "Messages.h"
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <set>

/**
 * @brief Constructor.
 * Determines the path to the cache file (user settings) but does not load it
 * yet.
 */
CacheManager::CacheManager(const BMessenger &target)
    : BLooper("CacheManager"), fTarget(target) {
  BPath settingsPath;
  find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath);
  settingsPath.Append("BeTon/media.cache");
  fCachePath = settingsPath.Path();
}

/**
 * @brief Loads the list of watched directories from 'directories.txt'.
 * @param outDirs Vector to populate with directory paths.
 */
#include "MusicSource.h"

/**
 * @brief Loads the list of watched directories from 'directories.settings' or
 * legacy 'directories.txt'.
 * @param outDirs Vector to populate with directory paths.
 */
void CacheManager::LoadDirectories(std::vector<BString> &outDirs) {
  BPath p;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &p) != B_OK)
    return;

  // Try loading from new settings format first
  BPath settingsPath = p;
  settingsPath.Append("BeTon/directories.settings");
  BFile file(settingsPath.Path(), B_READ_ONLY);

  if (file.InitCheck() == B_OK) {
    BMessage archive;
    if (archive.Unflatten(&file) == B_OK) {
      BMessage srcMsg;
      for (int32 i = 0; archive.FindMessage("source", i, &srcMsg) == B_OK;
           i++) {
        MusicSource src;
        src.LoadFrom(&srcMsg);
        if (!src.path.IsEmpty()) {
          outDirs.push_back(src.path);
        }
      }
      if (!outDirs.empty()) {
        DEBUG_PRINT("[CacheManager] Loaded %zu directories from settings\n",
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
 * 3. Mark existing known files as missing if they are gone from disk (quick
 * check).
 *
 * Note: Real sync happens via Scanners reporting back.
 */
void CacheManager::StartScan() {
  std::vector<BString> dirs;
  LoadDirectories(dirs);

  // 1. Remove entries that belong to directories no longer monitored
  std::set<BString> validBases(dirs.begin(), dirs.end());

  for (auto it = fEntries.begin(); it != fEntries.end();) {
    const MediaItem &e = it->second;
    if (validBases.find(e.base) == validBases.end()) {
      it = fEntries.erase(it);
    } else {
      ++it;
    }
  }

  // Notify UI that we are starting with the current known state
  if (fTarget.IsValid()) {
    BMessage update(MSG_CACHE_LOADED);
    fTarget.SendMessage(&update);
  }

  // 2. Start Scanners
  fActiveScanners = 0;
  for (const auto &dirPath : dirs) {
    entry_ref ref;
    status_t s = get_ref_for_path(dirPath.String(), &ref);
    if (s != B_OK) {
      MarkBaseOffline(dirPath);
      continue;
    }

    BDirectory dir(&ref);
    if (dir.InitCheck() != B_OK) {
      MarkBaseOffline(dirPath);
      continue;
    }

    // Launch scanner. It will report back via
    // MSG_MEDIA_ITEM_FOUND/MSG_SCAN_DONE
    auto *scanner = new MediaScanner(ref, BMessenger(this), fTarget);
    scanner->SetCache(fEntries);
    scanner->Run();

    BMessenger msgr(scanner);
    msgr.SendMessage(MSG_START_SCAN);
    fActiveScanners++;
  }

  // 3. Mark existing known files as missing if they are gone from disk
  // NOTE: This is a quick check on the cache, the real sync happens via
  // Scanners.
  for (auto &kv : fEntries) {
    const BString &path = kv.first;
    MediaItem &entry = kv.second;

    BEntry e(path.String());
    if (!e.Exists() && !entry.missing) {
      entry.missing = true;
      DEBUG_PRINT("[CacheManager] Mark missing: %s\n", path.String());

      if (fTarget.IsValid()) {
        BMessage gone(MSG_MEDIA_ITEM_REMOVED);
        gone.AddString("path", path);
        fTarget.SendMessage(&gone);
      }
    }
  }

  // If no scanners were started (e.g. no dirs), finish immediately
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
void CacheManager::SaveCache() {
  BMessage archive;
  for (auto &[key, entry] : fEntries) {
    BMessage item;
    item.AddString("path", entry.path);
    item.AddString("base", entry.base);
    item.AddString("title", entry.title);
    item.AddString("artist", entry.artist);
    item.AddString("album", entry.album);
    item.AddString("genre", entry.genre);
    item.AddInt32("year", entry.year);
    item.AddInt32("track", entry.track);
    item.AddInt32("disc", entry.disc);
    item.AddInt32("duration", entry.duration);
    item.AddInt32("bitrate", entry.bitrate);
    item.AddInt64("size", entry.size);
    item.AddInt64("mtime", entry.mtime);
    item.AddInt64("inode", entry.inode);
    item.AddBool("missing", entry.missing);
    item.AddInt32("rating", entry.rating);

    item.AddString("mbAlbumId", entry.mbAlbumId);
    item.AddString("mbArtistId", entry.mbArtistId);
    item.AddString("mbTrackId", entry.mbTrackId);
    archive.AddMessage("entry", &item);
  }

  BFile file(fCachePath, B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
  if (file.InitCheck() == B_OK) {
    archive.Flatten(&file);
    DEBUG_PRINT("[CacheManager] SaveCache: Saved to %s\n", fCachePath.String());
  } else {
    DEBUG_PRINT("[CacheManager] SaveCache: Failed to save to %s\n",
                fCachePath.String());
  }
}

/**
 * @brief Loads the cache from disk into memory.
 */
void CacheManager::LoadCache() {
  fEntries.clear();

  BFile file(fCachePath, B_READ_ONLY);
  if (file.InitCheck() != B_OK) {
    DEBUG_PRINT("Kein Cache gefunden (%s)\n", fCachePath.String());
    return;
  }

  BMessage archive;
  if (archive.Unflatten(&file) != B_OK) {
    DEBUG_PRINT("Konnte Cache nicht unflatten (%s)\n", fCachePath.String());
    return;
  }

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
    if (entry.rating > 0)
      DEBUG_PRINT("[CacheManager] Loaded rating %d for %s\n", (int)entry.rating,
                  entry.path.String());

    fEntries[entry.path] = entry;
  }

  DEBUG_PRINT("[CacheManager] LoadCache: Loaded %zu items\n", fEntries.size());

  if (fTarget.IsValid()) {
    BMessage msg(MSG_CACHE_LOADED);
    fTarget.SendMessage(&msg);
  }
}

/**
 * @brief Returns a copy of all current media items.
 * @return std::vector<MediaItem>
 */
std::vector<MediaItem> CacheManager::AllEntries() const {
  std::vector<MediaItem> out;
  out.reserve(fEntries.size());

  for (const auto &kv : fEntries) {
    out.push_back(kv.second);
  }
  return out;
}

/**
 * @brief Main message loop for the CacheManager looper.
 * Handles loading, batch updates, and scanning notifications.
 */
void CacheManager::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_LOAD_CACHE:
    DEBUG_PRINT("[CacheManager] Asynchronous cache load started\\n");
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
      if (baseStr)
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
        DEBUG_PRINT("[CacheManager] Received rating %d for %s\n", (int)e.rating,
                    e.path.String());

      if (msg->FindString("mbAlbumId", i, &tmp) == B_OK)
        e.mbAlbumId = tmp;
      if (msg->FindString("mbArtistId", i, &tmp) == B_OK)
        e.mbArtistId = tmp;
      if (msg->FindString("mbTrackId", i, &tmp) == B_OK)
        e.mbTrackId = tmp;

      AddOrUpdateEntry(e);
    }

    DEBUG_PRINT("[CacheManager] Processed batch of %d items\n", (int)count);

    if (fTarget.IsValid())
      fTarget.SendMessage(msg);
    break;
  }

  case MSG_MEDIA_ITEM_FOUND: {
    MediaItem e;
    const char *tmpStr = nullptr;

    if (msg->FindString("path", &tmpStr) == B_OK)
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

    msg->FindInt32("year", &e.year);
    msg->FindInt32("track", &e.track);
    msg->FindInt32("disc", &e.disc);
    msg->FindInt32("duration", &e.duration);
    msg->FindInt32("bitrate", &e.bitrate);
    msg->FindInt64("size", &e.size);
    msg->FindInt64("mtime", &e.mtime);
    msg->FindInt64("inode", &e.inode);

    if (msg->FindString("mbAlbumId", &tmpStr) == B_OK)
      e.mbAlbumId = tmpStr;
    if (msg->FindString("mbArtistId", &tmpStr) == B_OK)
      e.mbArtistId = tmpStr;
    if (msg->FindString("mbTrackId", &tmpStr) == B_OK)
      e.mbTrackId = tmpStr;

    AddOrUpdateEntry(e);

    SaveCache();

    DEBUG_PRINT("[CacheManager] Item found: path=%s, title=%s\n",
                e.path.String(), e.title.String());

    if (fTarget.IsValid())
      fTarget.SendMessage(msg);
    break;
  }

  case MSG_REGISTER_TARGET: {
    BMessenger newTarget;
    if (msg->FindMessenger("target", &newTarget) == B_OK) {
      fTarget = newTarget;
      DEBUG_PRINT("[CacheManager] UI target registered\n");
    }
    break;
  }
  case MSG_RESCAN:
    DEBUG_PRINT("[CacheManager] received MSG_RESCAN, starting new scan\n");
    StartScan();
    break;

  case MSG_SCAN_DONE: {
    DEBUG_PRINT("[CacheManager] received MSG_SCAN_DONE (scanners left: %ld)\\n",
                (long)(fActiveScanners - 1));

    if (--fActiveScanners <= 0) {
      DEBUG_PRINT(
          "[CacheManager] all scanners finished, writing media.cache\\n");
      SaveCache();

      if (fTarget.IsValid()) {
        DEBUG_PRINT("[CacheManager] forward MSG_SCAN_DONE to MainWindow\\n");
        BMessage done(MSG_SCAN_DONE);
        fTarget.SendMessage(&done);
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
void CacheManager::AddOrUpdateEntry(const MediaItem &entry) {
  auto it = fEntries.find(entry.path);
  if (it == fEntries.end()) {
    fEntries[entry.path] = entry;

  } else {
    const MediaItem &old = it->second;
    if (!old.mbTrackId.IsEmpty() && entry.mbTrackId.IsEmpty()) {
      DEBUG_PRINT("[CacheManager] WARNING: Overwriting existing MB Track ID "
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
void CacheManager::MarkBaseOffline(const BString &basePath) {
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
