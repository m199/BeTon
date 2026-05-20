/**
 * @file MusicSourceSettings.h
 * @brief Defines synchronization settings for music directories.
 *
 * This file contains the MusicSourceSettings struct which holds per-directory
 * configuration for metadata synchronization between embedded tags and
 * BFS attributes.
 */

#ifndef BETON_MUSIC_SOURCE_SETTINGS_H
#define BETON_MUSIC_SOURCE_SETTINGS_H

#include <Message.h>
#include <String.h>
#include <vector>

/**
 * @brief Defines the source type for metadata.
 */
enum SourceType { SOURCE_TAGS = 0, SOURCE_BFS = 1, SOURCE_NONE = 2 };

/**
 * @brief Defines conflict resolution strategy when metadata differs.
 */
enum ConflictMode {
  CONFLICT_OVERWRITE = 0,
  CONFLICT_FILL_EMPTY = 1,
  CONFLICT_ASK = 2
};

/**
 * @brief Synchronization settings for a music directory.
 *
 * Each monitored music directory has its own synchronization preferences,
 * including primary/secondary metadata sources and conflict resolution mode.
 */
struct MusicSourceSettings {
  /** @brief Absolute source directory path. */
  BString path;
  /** @brief Preferred source used first during metadata sync. */
  SourceType primary;
  /** @brief Fallback source used when primary cannot provide value. */
  SourceType secondary;
  /** @brief Conflict handling mode when both sources differ. */
  ConflictMode conflictMode;

  /**
   * @brief Default constructor.
   */
  MusicSourceSettings();

  /**
   * @brief Constructor with path.
   * @param p Directory path
   */
  MusicSourceSettings(const BString &p);

  /**
   * @brief Load settings from BMessage.
   * @param msg Message containing serialized settings
   */
  void LoadFrom(const BMessage *msg);

  /**
   * @brief Save settings to BMessage.
   * @param msg Target message for serialization
   */
  void SaveTo(BMessage *msg) const;

  /**
   * @brief Get human-readable name for source type.
   * @param t Source type
   * @return String representation
   */
  static const char *SourceTypeName(SourceType t);

  /**
   * @brief Get human-readable name for conflict mode.
   * @param m Conflict mode
   * @return String representation
   */
  static const char *ConflictModeName(ConflictMode m);

  /**
   * @brief Find MusicSourceSettings settings for a given file path.
   *
   * Uses the in-memory cache populated by InitCache() to find
   * the MusicSourceSettings whose path is a prefix of the given file path.
   *
   * @param filePath Absolute path to a media file
   * @return MusicSourceSettings for the directory, or default settings if not found
   */
  static MusicSourceSettings GetSourceForPath(const BString &filePath);

  /**
   * @brief Loads all MusicSourceSettings entries from directories.settings into memory.
   *
   * Must be called once at application startup before any GetSourceForPath().
   */
  static void InitCache();

  /**
   * @brief Discards the in-memory cache so InitCache() reloads on next call.
   */
  static void InvalidateCache();

private:
  /** @brief Cached source settings loaded from `directories.settings`. */
  static std::vector<MusicSourceSettings> sCache;
  /** @brief Indicates whether `sCache` was initialized. */
  static bool sCacheLoaded;
};

#endif // BETON_MUSIC_SOURCE_SETTINGS_H
