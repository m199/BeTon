#ifndef ARTWORK_CONTROLLER_H
#define ARTWORK_CONTROLLER_H

#include <String.h>

class BMessage;
class MainWindow;

/**
 * @class ArtworkController
 * @brief Handles cover-art visibility, loading, and metadata-driven updates.
 *
 * Coordinates artwork operations between UI (`NowPlayingInfoPanel`),
 * playback state, radio/DLNA sources, and metadata services.
 */
class ArtworkController {
public:
  /**
   * @brief Creates the artwork controller for the main window context.
   * @param window Owning main window.
   */
  explicit ArtworkController(MainWindow *window);

  /**
   * @brief Toggles cover visibility in the sidebar panel and persists settings.
   */
  void ToggleArtworkVisible();

  /**
   * @brief Forces cover visibility on and persists settings.
   */
  void ShowCoverArt();

  /**
   * @brief Handles async cover fetch completion.
   * @param msg Message containing `path` and optional `bitmap`.
   */
  void HandleCoverBitmapReady(BMessage *msg);

  /**
   * @brief Resolves and requests cover art for the current playback item.
   * @param path Current media path or stream URL.
   * @param isStream True when playback source is a stream.
   */
  void FetchNowPlayingCover(const BString &path, bool isStream);

  /**
   * @brief Extracts embedded artwork from a local media file asynchronously.
   * @param path Local media file path.
   */
  void FetchEmbeddedCoverBitmap(const BString &path);

  /**
   * @brief Applies album cover bytes to a single file via metadata service.
   * @param msg Message containing file path and raw image bytes.
   */
  void ApplyAlbumCover(BMessage *msg);

  /**
   * @brief Clears embedded album cover from a single file.
   * @param msg Message containing file path.
   */
  void ClearAlbumCover(BMessage *msg);

  /**
   * @brief Applies dropped cover/clear action to multiple files.
   * @param msg Message containing target file list and action flags.
   */
  void ApplyDroppedCoverToAll(BMessage *msg);

  /**
   * @brief Returns embedded cover bytes for a file to a requester.
   * @param msg Message containing target file path and return address.
   */
  void RequestEmbeddedCover(BMessage *msg);

private:
  /**
   * @brief Downloads remote artwork and posts the decoded bitmap back to UI.
   * @param path Media path associated with the cover request.
   * @param coverUrl Remote cover URL.
   */
  void DownloadCoverBitmap(const BString &path, const BString &coverUrl);

  /** Window context used to access controllers, services, and UI targets. */
  MainWindow *fWindow;
};

#endif // ARTWORK_CONTROLLER_H
