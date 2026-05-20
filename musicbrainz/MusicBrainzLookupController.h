#ifndef BETON_MUSICBRAINZ_LOOKUP_CONTROLLER_H
#define BETON_MUSICBRAINZ_LOOKUP_CONTROLLER_H

class BMessage;
class MainWindow;

/**
 * @class MusicBrainzLookupController
 * @brief Coordinates MusicBrainz search/apply/fetch workflows for the UI.
 */
class MusicBrainzLookupController {
public:
  /**
   * @brief Constructs the lookup controller.
   * @param window Owning main window context.
   */
  explicit MusicBrainzLookupController(MainWindow *window);

  /**
   * @brief Dispatch entry for MusicBrainz-related UI messages.
   * @param msg Incoming message.
   * @return `true` if handled.
   */
  bool HandleMessage(BMessage *msg);

  /**
   * @brief Starts a MusicBrainz search from UI input.
   * @param msg Search request message.
   */
  void StartSearch(BMessage *msg);

  /**
   * @brief Cancels the currently running MusicBrainz operation.
   */
  void CancelOperation();

  /**
   * @brief Handles asynchronous search completion payload.
   * @param msg Completion message with hit data.
   */
  void HandleSearchComplete(BMessage *msg);

  /**
   * @brief Applies selected MusicBrainz metadata to target files.
   * @param msg Apply request message.
   */
  void ApplyMetadata(BMessage *msg);

  /**
   * @brief Applies manual track-to-file mapping from matcher dialog.
   * @param msg Mapping result message.
   */
  void ApplyMatch(BMessage *msg);

  /**
   * @brief Fetches cover art from MusicBrainz/CoverArtArchive.
   * @param msg Cover request message.
   */
  void FetchCover(BMessage *msg);

private:
  /** @brief Owning main window context. */
  MainWindow *fWindow;
};

#endif // BETON_MUSICBRAINZ_LOOKUP_CONTROLLER_H
