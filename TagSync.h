#ifndef TAG_SYNC_H
#define TAG_SYNC_H
#include "Debug.h"

#include <Path.h>
#include <String.h>
#include <SupportDefs.h>

#include <cstdint>
#include <vector>

/**
 * @struct TagData
 * @brief Holds metadata read from or written to audio files.
 */
struct TagData {
  BString title, artist, album, genre, comment;

  uint32 year = 0;
  uint32 track = 0;
  BString albumArtist;
  BString composer;
  uint32 trackTotal = 0;
  uint32 disc = 0;
  uint32 discTotal = 0;

  uint32 lengthSec = 0;
  uint32 bitrate = 0;
  uint32 sampleRate = 0;
  uint32 channels = 0;

  BString mbAlbumID, mbArtistID, mbTrackID;
  BString acoustId, acoustIdFp;

  uint32 rating = 0;

  /**
   * @brief Checks if any syncable fields differ from another TagData.
   * @param other The other TagData to compare.
   * @return True if there are differences worth syncing.
   */
  bool HasDifferences(const TagData &other) const {
    return title != other.title || artist != other.artist ||
           album != other.album || genre != other.genre || year != other.year ||
           track != other.track || albumArtist != other.albumArtist ||
           composer != other.composer || mbTrackID != other.mbTrackID ||
           mbAlbumID != other.mbAlbumID || mbArtistID != other.mbArtistID ||
           acoustId != other.acoustId;
  }

  void LogDifferences(const TagData &other) const {
    if (title != other.title)
      DEBUG_PRINT("Diff: Title '%s' vs '%s'\n", title.String(),
                  other.title.String());
    if (artist != other.artist)
      DEBUG_PRINT("Diff: Artist '%s' vs '%s'\n", artist.String(),
                  other.artist.String());
    if (album != other.album)
      DEBUG_PRINT("Diff: Album '%s' vs '%s'\n", album.String(),
                  other.album.String());
    if (genre != other.genre)
      DEBUG_PRINT("Diff: Genre '%s' vs '%s'\n", genre.String(),
                  other.genre.String());
    if (year != other.year)
      DEBUG_PRINT("Diff: Year %u vs %u\n", (unsigned int)year,
                  (unsigned int)other.year);
    if (track != other.track)
      DEBUG_PRINT("Diff: Track %u vs %u\n", (unsigned int)track,
                  (unsigned int)other.track);
    if (albumArtist != other.albumArtist)
      DEBUG_PRINT("Diff: AlbumArtist '%s' vs '%s'\n", albumArtist.String(),
                  other.albumArtist.String());
    if (composer != other.composer)
      DEBUG_PRINT("Diff: Composer '%s' vs '%s'\n", composer.String(),
                  other.composer.String());
    if (mbTrackID != other.mbTrackID)
      DEBUG_PRINT("Diff: MBTrackID '%s' vs '%s'\n", mbTrackID.String(),
                  other.mbTrackID.String());
    if (mbAlbumID != other.mbAlbumID)
      DEBUG_PRINT("Diff: MBAlbumID '%s' vs '%s'\n", mbAlbumID.String(),
                  other.mbAlbumID.String());
    if (mbArtistID != other.mbArtistID)
      DEBUG_PRINT("Diff: MBArtistID '%s' vs '%s'\n", mbArtistID.String(),
                  other.mbArtistID.String());
    if (acoustId != other.acoustId)
      DEBUG_PRINT("Diff: AcoustID '%s' vs '%s'\n", acoustId.String(),
                  other.acoustId.String());
    if (acoustIdFp != other.acoustIdFp)
      DEBUG_PRINT("Diff: AcoustIDFp '%s' vs '%s'\n", acoustIdFp.String(),
                  other.acoustIdFp.String());
    if (comment != other.comment)
      DEBUG_PRINT("Diff: Comment '%s' vs '%s'\n", comment.String(),
                  other.comment.String());
    if (trackTotal != other.trackTotal)
      DEBUG_PRINT("Diff: TrackTotal %u vs %u\n", (unsigned int)trackTotal,
                  (unsigned int)other.trackTotal);
    if (disc != other.disc)
      DEBUG_PRINT("Diff: Disc %u vs %u\n", (unsigned int)disc,
                  (unsigned int)other.disc);
    if (discTotal != other.discTotal)
      DEBUG_PRINT("Diff: DiscTotal %u vs %u\n", (unsigned int)discTotal,
                  (unsigned int)other.discTotal);
    if (rating != other.rating)
      DEBUG_PRINT("Diff: Rating %u vs %u\n", (unsigned int)rating,
                  (unsigned int)other.rating);
  }
};

/**
 * @struct CoverBlob
 * @brief Simple container for binary cover art data.
 */
struct CoverBlob {
  std::vector<uint8_t> bytes;

  void clear() { bytes.clear(); }
  void assign(const void *p, size_t n) {
    const auto *b = static_cast<const uint8_t *>(p);
    bytes.assign(b, b + n);
  }
  const void *data() const { return bytes.empty() ? nullptr : bytes.data(); }
  size_t size() const { return bytes.size(); }
};

/**
 * @namespace TagSync
 * @brief Utilities for reading and writing audio file metadata.
 */
namespace TagSync {

/**
 * @brief Reads metadata from the specified file.
 * @param path The path to the audio file.
 * @param out Output struct to populate with metadata.
 * @return True on success, false otherwise.
 */
bool ReadTags(const BPath &path, TagData &out);

/**
 * @brief Reads metadata from Haiku BFS attributes.
 * @param path File path.
 * @param out Output TagData structure.
 * @return True if successful, false if not on BFS or error.
 */
bool ReadBfsAttributes(const BPath &path, TagData &out);

/**
 * @brief Merges metadata from two sources based on conflict mode.
 * @param primary Primary metadata source.
 * @param secondary Secondary metadata source.
 * @param mode Conflict resolution strategy.
 * @return Merged TagData.
 */
TagData MergeMetadata(const TagData &primary, const TagData &secondary,
                      int32 mode);

/**
 * @brief Merges metadata from two sources, filling gaps where possible.
 * @param primary Primary metadata source.
 * @param secondary Secondary metadata source.
 * @param outMerged Output merged metadata.
 * @param hasConflict Set to true if a conflict (diff non-empty values) exists.
 * @return True if outMerged contains data that differs from either input.
 */
bool SmartMerge(const TagData &primary, const TagData &secondary,
                TagData &outMerged, bool &hasConflict);

/**
 * @brief Extracts embedded cover art from the file.
 * @param path The path to the audio file.
 * @param out Output struct to populate with cover data.
 * @return True if cover art was found and extracted.
 */
bool ExtractEmbeddedCover(const BPath &path, CoverBlob &out);

/**
 * @brief Writes embedded cover art to the file.
 * @param file The path to the audio file.
 * @param data Pointer to the image data.
 * @param size Size of the image data in bytes.
 * @param mimeOpt Optional MIME type string (e.g., "image/jpeg").
 * @return True on success.
 */
bool WriteEmbeddedCover(const BPath &file, const uint8_t *data, size_t size,
                        const char *mimeOpt = nullptr);

/**
 * @brief Overload that takes a CoverBlob.
 */
bool WriteEmbeddedCover(const BPath &file, const CoverBlob &blob,
                        const char *mimeOpt = nullptr);

/**
 * @brief Writes metadata and optionally cover art to the file.
 * @param path The path to the audio file.
 * @param td The new metadata to write.
 * @param coverOpt Optional pointer to cover art data to write.
 * @return True on success.
 */
bool WriteTagsToFile(const BPath &path, const TagData &td,
                     const CoverBlob *coverOpt);

/**
 * @brief Writes only metadata tags to the file.
 * @param path The path to the audio file.
 * @param in The new metadata to write.
 * @return True on success.
 */
bool WriteTags(const BPath &path, const TagData &in);

/**
 * @brief Mirrors metadata and cover art to BFS attributes.
 * @param path The path to the file.
 * @param in The metadata to write as attributes.
 * @param cover Optional cover data to write as a thumbnail attribute.
 * @param coverMaxBytes Maximum allowed size for the cover attribute.
 * @return True on success.
 */
bool WriteBfsAttributes(const BPath &path, const TagData &in,
                        const CoverBlob *cover,
                        size_t coverMaxBytes = 512 * 1024);

/**
 * @brief Checks if the file resides on a BFS volume.
 * @param path The path to check.
 * @return True if on a BFS volume, false otherwise.
 */
bool IsBeFsVolume(const BPath &path);

/**
 * @brief Applies sync by writing source data to the appropriate destination.
 * @param path The file to sync.
 * @param source The TagData to write.
 * @param towardsBfs If true, writes to BFS attributes; if false, writes to
 * Tags.
 * @return True on success.
 */
bool ApplySync(const BPath &path, const TagData &source, bool towardsBfs);

} // namespace TagSync

#endif // TAG_SYNC_H
