#ifndef BETON_MEDIA_ITEM_H
#define BETON_MEDIA_ITEM_H

#include <String.h>

/**
 * @struct MediaItem
 * @brief Represents a single media file and its metadata.
 *
 * Stores ID3 tags (artist, title, album, etc.) and file system information
 * (path, size, modification time). This struct is the primary data unit
 * used throughout the application's library, cache, and playback systems.
 */
struct MediaItem {
  /** @name File System Info */
  ///@{
  BString path; ///< Full absolute path to the file.
  BString base; ///< Base directory or grouping identifier (often used for album
                ///< folder).
  ///@}

  /** @name Metadata (Tags) */
  ///@{
  BString title;       ///< Track title.
  BString artist;      ///< Track artist.
  BString album;       ///< Album name.
  BString albumArtist; ///< Album artist (for grouping compilations).
  BString composer;    ///< Composer.
  BString genre;       ///< Genre.
  BString comment;     ///< Comment field.
  ///@}

  /** @name MusicBrainz IDs */
  ///@{
  BString mbTrackId;  ///< MusicBrainz Recording ID.
  BString mbAlbumId;  ///< MusicBrainz Release ID.
  BString mbArtistId; ///< MusicBrainz Artist ID.
  ///@}

  /** @name Numeric Metadata */
  ///@{
  int32 year = 0;       ///< Release year.
  int32 track = 0;      ///< Track number.
  int32 trackTotal = 0; ///< Total tracks on disc.
  int32 disc = 0;       ///< Disc number.
  int32 discTotal = 0;  ///< Total discs in set.
  ///@}

  /** @name Audio Properties */
  ///@{
  int32 duration = 0;   ///< Duration in seconds.
  int32 bitrate = 0;    ///< Bitrate in kbps.
  int32 sampleRate = 0; ///< Sample rate in Hz.
  int32 channels = 0;   ///< Number of audio channels.
  ///@}

  /** @name AcoustID */
  ///@{
  BString acoustId; ///< AcoustID fingerprint identifier.
  ///@}

  /** @name Haiku BFS Attributes */
  ///@{
  int32 rating = 0; ///< Rating 1-10 (Media:Rating).
  ///@}

  /** @name File Stats */
  ///@{
  int64 size = 0;  ///< File size in bytes.
  int64 mtime = 0; ///< Last modification time (for cache invalidation).
  int64 inode = 0; ///< File system inode number (stable identifier).
  bool missing =
      false; ///< Flag indicating if file was not found during last scan.
  ///@}

  /**
   * @brief Default constructor.
   */
  MediaItem() = default;

  /**
   * @brief Convenience constructor for simple items.
   * @param t Title.
   * @param p File path.
   */
  MediaItem(const BString &t, const BString &p) : path(p), title(t) {}

  /**
   * @brief Checks if the item has a valid file path associated.
   * @return True if path is not empty.
   */
  bool HasFile() const { return !path.IsEmpty(); }
};

#endif // BETON_MEDIA_ITEM_H
