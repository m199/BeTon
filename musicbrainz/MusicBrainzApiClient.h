#ifndef BETON_MUSICBRAINZ_API_CLIENT_H
#define BETON_MUSICBRAINZ_API_CLIENT_H

#include <String.h>
#include <SupportDefs.h>

#include <functional>
#include <vector>

/**
 * @struct MBTrack
 * @brief Represents a single track from a MusicBrainz release.
 */
struct MBTrack {
  /** @name Data */
  ///@{
  uint32 disc = 1;     ///< Disc number (1-based).
  uint32 track = 0;    ///< Track number (1-based).
  BString title;       ///< Track title.
  BString recordingId; ///< MusicBrainz Recording ID (UUID).
  int length = 0;      ///< Duration in seconds.
  ///@}
};

/**
 * @struct MBRelease
 * @brief Represents a full album/release with tracklist.
 */
struct MBRelease {
  /** @name Metadata */
  ///@{
  BString releaseId;           ///< Release ID (UUID).
  BString releaseGroupId;      ///< Release Group ID (UUID).
  BString album;               ///< Album title.
  BString albumArtist;         ///< Album artist name.
  uint32 year = 0;             ///< Release year.
  std::vector<MBTrack> tracks; ///< Tracklist.
  ///@}
};

/**
 * @struct MBHit
 * @brief Represents a search result for a recording.
 */
struct MBHit {
  /** @name Search Results */
  ///@{
  BString recordingId;  ///< Recording ID.
  BString title;        ///< Track title.
  BString artist;       ///< Artist name.
  BString releaseId;    ///< Default/Best Release ID associated with this hit.
  BString releaseTitle; ///< Title of that release.
  uint32 year = 0;      ///< Year of that release.
  BString country;      ///< Country code (e.g., "US", "DE").
  int score = 0;        ///< Search relevance score (0-100).
  int trackCount = 0;   ///< Track count of the release (context).
  ///@}
};

/**
 * @class MusicBrainzApiClient
 * @brief Handles all interactions with the MusicBrainz API and Cover Art
 * Archive.
 *
 * Provides methods for searching recordings, fetching release details,
 * and downloading cover art. It handles rate limiting and runs queries via
 * `libmusicbrainz5` (Lucene queries).
 */
class MusicBrainzApiClient {
public:
  /**
   * @brief Constructs the client.
   * @param userAgentContact Contact info to use in the User-Agent header
   * (required by MB).
   */
  explicit MusicBrainzApiClient(const BString &userAgentContact);

  /**
   * @brief Searches for recordings.
   * @param artist Artist name.
   * @param title Track title.
   * @param albumOpt Optional album name to refine search.
   * @param shouldCancel Callback to check for cancellation.
   * @return List of matching hits.
   */
  std::vector<MBHit>
  SearchRecording(const BString &artist, const BString &title,
                  const BString &albumOpt = "",
                  std::function<bool()> shouldCancel = nullptr);

  /**
   * @brief Searches for album releases.
   * @param artist Artist or album artist name.
   * @param album Album/release title.
   * @param shouldCancel Callback to check for cancellation.
   * @return List of matching releases encoded as MBHit records.
   */
  std::vector<MBHit>
  SearchRelease(const BString &artist, const BString &album,
                std::function<bool()> shouldCancel = nullptr);

  /**
   * @brief Finds the best release ID for a given recording ID.
   *
   * Useful when a "Hit" from SearchRecording needs to be mapped to a specific
   * release to fetch cover art or tracklists.
   */
  BString BestReleaseForRecording(const BString &recordingId,
                                  std::function<bool()> shouldCancel = nullptr);

  /**
   * @brief Fetches full details (tracks, year, IDs) for a release.
   * @param releaseId MBID of the release.
   * @param shouldCancel Callback for cancellation.
   * @return Populated MBRelease struct.
   */
  MBRelease GetReleaseDetails(const BString &releaseId,
                              std::function<bool()> shouldCancel = nullptr);

  /**
   * @brief Downloads cover art from Cover Art Archive.
   * @param entityId Release or Release Group ID.
   * @param outBytes Vector to write image data to.
   * @param outMime Optional pointer to write MIME type (e.g. "image/jpeg").
   * @param sizeHint Size hint (250, 500, 1200, or 0 for original).
   * @param isReleaseGroup Set to true if entityId is a Release Group.
   * @param shouldCancel Callback for cancellation.
   * @return True if successful.
   */
  bool FetchCover(const BString &entityId, std::vector<uint8_t> &outBytes,
                  BString *outMime = nullptr, int sizeHint = 500,
                  bool isReleaseGroup = false,
                  std::function<bool()> shouldCancel = nullptr);

  /**
   * @brief Helper to build a front cover URL string.
   */
  static BString BuildFrontCoverUrl(const BString &releaseId) {
    BString url("https://coverartarchive.org/release/");
    url << releaseId << "/front";
    return url;
  }

private:
  BString fContact;
  bigtime_t fLastCall = 0;

  void _RespectRateLimit();

  int _FetchUrl(const BString &url, std::vector<uint8_t> &outBytes,
                BString *outMime, int maxRedirects = 3);
};

#endif // BETON_MUSICBRAINZ_API_CLIENT_H
