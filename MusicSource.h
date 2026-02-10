/**
 * @file MusicSource.h
 * @brief Defines synchronization settings for music directories.
 *
 * This file contains the MusicSource struct which holds per-directory
 * configuration for metadata synchronization between embedded tags and
 * BFS attributes.
 */

#ifndef MUSIC_SOURCE_H
#define MUSIC_SOURCE_H

#include <Message.h>
#include <String.h>

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
struct MusicSource {
  BString path;
  SourceType primary;
  SourceType secondary;
  ConflictMode conflictMode;

  /**
   * @brief Default constructor.
   */
  MusicSource();

  /**
   * @brief Constructor with path.
   * @param p Directory path
   */
  MusicSource(const BString &p);

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
   * @brief Find MusicSource settings for a given file path.
   *
   * Reads directories.settings and finds the MusicSource whose path
   * is a prefix of the given file path.
   *
   * @param filePath Absolute path to a media file
   * @return MusicSource for the directory, or default settings if not found
   */
  static MusicSource GetSourceForPath(const BString &filePath);
};

#endif
