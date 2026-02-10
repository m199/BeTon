/**
 * @file MusicSource.cpp
 * @brief Implementation of MusicSource synchronization settings.
 */

#include "MusicSource.h"
#include "Debug.h"

#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

MusicSource::MusicSource()
    : primary(SOURCE_TAGS), secondary(SOURCE_BFS), conflictMode(CONFLICT_ASK) {}

MusicSource::MusicSource(const BString &p)
    : path(p), primary(SOURCE_TAGS), secondary(SOURCE_BFS),
      conflictMode(CONFLICT_ASK) {}

void MusicSource::LoadFrom(const BMessage *msg) {
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

void MusicSource::SaveTo(BMessage *msg) const {
  if (!msg)
    return;

  msg->AddString("path", path);
  msg->AddInt32("primary", static_cast<int32>(primary));
  msg->AddInt32("secondary", static_cast<int32>(secondary));
  msg->AddInt32("conflictMode", static_cast<int32>(conflictMode));
}

const char *MusicSource::SourceTypeName(SourceType t) {
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

const char *MusicSource::ConflictModeName(ConflictMode m) {
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
 * @brief Find MusicSource settings for a given file path.
 */
MusicSource MusicSource::GetSourceForPath(const BString &filePath) {
  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK) {
    DEBUG_PRINT("[MusicSource] find_directory failed\n");
    return MusicSource();
  }

  settingsPath.Append("BeTon/directories.settings");
  DEBUG_PRINT("[MusicSource] Looking for settings at: %s\n",
              settingsPath.Path());

  BFile file(settingsPath.Path(), B_READ_ONLY);
  if (file.InitCheck() != B_OK) {
    DEBUG_PRINT("[MusicSource] Settings file not found\n");
    return MusicSource();
  }

  BMessage archive;
  if (archive.Unflatten(&file) != B_OK) {
    DEBUG_PRINT("[MusicSource] Failed to unflatten settings\n");
    return MusicSource();
  }

  MusicSource bestMatch;
  int32 bestLen = 0;

  BMessage srcMsg;
  int32 idx = 0;
  while (archive.FindMessage("source", idx++, &srcMsg) == B_OK) {
    MusicSource src;
    src.LoadFrom(&srcMsg);

    DEBUG_PRINT("[MusicSource] Source %ld: path='%s', conflictMode=%d\n",
                (long)idx, src.path.String(), (int)src.conflictMode);

    if (filePath.StartsWith(src.path.String())) {
      int32 len = src.path.Length();
      if (len > bestLen) {
        bestLen = len;
        bestMatch = src;
        DEBUG_PRINT("[MusicSource] Best match updated: len=%ld\n", (long)len);
      }
    }
  }

  DEBUG_PRINT("[MusicSource] Result for '%s': conflictMode=%d\n",
              filePath.String(), (int)bestMatch.conflictMode);
  return bestMatch;
}
