#ifndef METADATA_HANDLER_H
#define METADATA_HANDLER_H

#include "MediaItem.h"

#include <Message.h>
#include <Messenger.h>
#include <String.h>

#include <vector>

/**
 * @class MetadataHandler
 * @brief Helper class for managing metadata operations (tags, covers).
 *
 * Handles reading and writing audio metadata, including embedded cover art.
 * It encapsulates interactions with `TagSync` and processes batched updates
 * received via BMessages from the UI (e.g., PropertiesWindow).
 */
class MetadataHandler {
public:
  /**
   * @brief Constructs the handler.
   * @param target Messenger (usually MainWindow or CacheManager) to notify of
   * changes.
   */
  MetadataHandler(BMessenger target);
  ~MetadataHandler();

  /**
   * @brief Applies album cover to all files in the same album directory.
   * @param filePath Path to one file in the album.
   * @param data Raw image data.
   * @param size Size of image data.
   */
  void ApplyAlbumCover(const BString &filePath, const void *data, ssize_t size);

  /**
   * @brief Clears album cover from all files in the same album directory.
   * @param filePath Path to one file in the album.
   */
  void ClearAlbumCover(const BString &filePath);

  /**
   * @brief Applies cover art to a list of specific files.
   * @param msg Message containing "file" strings, "bytes" data, and "mime"
   * string.
   */
  void ApplyCoverToAll(const BMessage *msg);

  /**
   * @brief Saves metadata tags (Project, Artist, etc.) to files.
   * @param msg Message containing tag fields and list of "file" paths.
   */
  void SaveTags(const BMessage *msg);

  /**
   * @brief Synchronizes metadata between Tags and BFS attributes.
   * @param files List of file paths to sync.
   * @param towardsBfs true = Tags→BFS, false = BFS→Tags.
   */
  void SyncMetadata(const std::vector<BString> &files);

private:
  BMessenger fTarget;

  /**
   * @brief Internal helper to iterate directory and update embedded covers.
   */
  void _ProcessDirectoryForCover(const BString &filePath, bool clear,
                                 const void *data = nullptr, size_t size = 0);
};

#endif // METADATA_HANDLER_H
