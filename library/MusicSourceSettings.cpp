/**
 * @file MusicSourceSettings.cpp
 * @brief Implementation of MusicSourceSettings synchronization settings.
 */

#include "MusicSourceSettings.h"
#include "Debug.h"

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

MusicSourceSettings::MusicSourceSettings()
    : primary(SOURCE_TAGS), secondary(SOURCE_BFS), conflictMode(CONFLICT_ASK) {}

/**
 * @brief Constructs settings for a specific source directory.
 * @param p Absolute directory path.
 */
MusicSourceSettings::MusicSourceSettings(const BString &p)
    : path(p), primary(SOURCE_TAGS), secondary(SOURCE_BFS),
      conflictMode(CONFLICT_ASK) {}

/**
 * @brief Deserializes settings fields from a message archive.
 * @param msg Message that contains `path`, `primary`, `secondary`, `conflictMode`.
 */
void MusicSourceSettings::LoadFrom(const BMessage *msg) {
  if (!msg)
    return;

  const char *pathStr;
  if (msg->FindString("path", &pathStr) == B_OK)
    path = pathStr;

  int32 val;
  if (msg->FindInt32("primary", &val) == B_OK)
    primary = static_cast<SourceType>(val);

  if (msg->FindInt32("secondary", &val) == B_OK)
    secondary = static_cast<SourceType>(val);

  if (msg->FindInt32("conflictMode", &val) == B_OK)
    conflictMode = static_cast<ConflictMode>(val);
}

/**
 * @brief Serializes settings fields into a message archive.
 * @param msg Destination message.
 */
void MusicSourceSettings::SaveTo(BMessage *msg) const {
  if (!msg)
    return;

  msg->AddString("path", path);
  msg->AddInt32("primary", static_cast<int32>(primary));
  msg->AddInt32("secondary", static_cast<int32>(secondary));
  msg->AddInt32("conflictMode", static_cast<int32>(conflictMode));
}

/**
 * @brief Returns display name for a source type enum value.
 * @param t Source type.
 * @return Constant display label.
 */
const char *MusicSourceSettings::SourceTypeName(SourceType t) {
  switch (t) {
  case SOURCE_TAGS:
    return "Tags";
  case SOURCE_BFS:
    return "BFS Attributes";
  case SOURCE_NONE:
    return "None";
  default:
    return "Unknown";
  }
}

/**
 * @brief Returns display name for a conflict-mode enum value.
 * @param m Conflict mode.
 * @return Constant display label.
 */
const char *MusicSourceSettings::ConflictModeName(ConflictMode m) {
  switch (m) {
  case CONFLICT_OVERWRITE:
    return "Overwrite";
  case CONFLICT_FILL_EMPTY:
    return "Fill Empty";
  case CONFLICT_ASK:
    return "Ask";
  default:
    return "Unknown";
  }
}

/**
 * @brief Find MusicSourceSettings settings for a given file path.
 */
MusicSourceSettings MusicSourceSettings::GetSourceForPath(const BString &filePath) {
  if (!sCacheLoaded)
    InitCache();

  MusicSourceSettings bestMatch;
  int32 bestLen = 0;

  for (const auto &src : sCache) {
    if (src.path.IsEmpty())
      continue;
    int32 len = src.path.Length();
    bool match = filePath.StartsWith(src.path.String()) &&
                 (filePath.Length() == len ||
                  filePath[len] == '/' ||
                  src.path[len - 1] == '/');
    if (match && len > bestLen) {
      bestLen = len;
      bestMatch = src;
    }
  }

  return bestMatch;
}

std::vector<MusicSourceSettings> MusicSourceSettings::sCache;
bool MusicSourceSettings::sCacheLoaded = false;

/**
 * @brief Loads all MusicSourceSettings entries from directories.settings into memory.
 */
void MusicSourceSettings::InitCache() {
  sCache.clear();

  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK) {
    sCacheLoaded = true;
    return;
  }

  settingsPath.Append("BeTon/directories.settings");

  BFile file(settingsPath.Path(), B_READ_ONLY);
  if (file.InitCheck() != B_OK) {
    sCacheLoaded = true;
    return;
  }

  BMessage archive;
  if (archive.Unflatten(&file) != B_OK) {
    sCacheLoaded = true;
    return;
  }

  BMessage srcMsg;
  int32 idx = 0;
  while (archive.FindMessage("source", idx++, &srcMsg) == B_OK) {
    MusicSourceSettings src;
    src.LoadFrom(&srcMsg);
    sCache.push_back(src);
  }

  sCacheLoaded = true;
  DEBUG_PRINT("Cache loaded: %zu sources\n", sCache.size());
}

/**
 * @brief Discards the in-memory cache so it is reloaded on next access.
 */
void MusicSourceSettings::InvalidateCache() {
  sCache.clear();
  sCacheLoaded = false;
}
