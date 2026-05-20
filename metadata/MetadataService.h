#ifndef BETON_METADATA_SERVICE_H
#define BETON_METADATA_SERVICE_H

#include "MediaItem.h"

#include <Message.h>
#include <Messenger.h>
#include <String.h>

#include <vector>

/**
 * @class MetadataService
 * @brief Handles metadata operations for tags, covers, and sync.
 *
 * Encapsulates `MetadataTagIO` usage and processes batched update requests
 * originating from metadata-related UI actions.
 */
class MetadataService {
public:
  /**
   * @brief Constructs the service.
   * @param target Messenger (usually MainWindow or MediaLibraryCache) that
   * receives update messages.
   */
  MetadataService(BMessenger target);
  ~MetadataService();

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
   * @param msg Message containing "file" strings, "bytes" data, and optional
   * "mime" string.
   */
  void ApplyCoverToAll(const BMessage *msg);

  /**
   * @brief Saves metadata tags to one or more files.
   * @param msg Message containing tag fields and list of "file" paths.
   */
  void SaveTags(const BMessage *msg);

  /**
   * @brief Synchronizes metadata between embedded tags and BFS attributes.
   * @param files List of file paths to synchronize.
   */
  void SyncMetadata(const std::vector<BString> &files);

private:
  /** @brief Update target for UI/cache notifications. */
  BMessenger fTarget;

  /**
   * @brief Iterates a directory and applies/clears embedded covers.
   */
  void _ProcessDirectoryForCover(const BString &filePath, bool clear,
                                 const void *data = nullptr, size_t size = 0);
};

#endif // BETON_METADATA_SERVICE_H
