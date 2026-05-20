#ifndef BETON_METADATA_MESSAGE_HANDLER_H
#define BETON_METADATA_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;
class MusicBrainzLookupController;

/**
 * @class MetadataMessageHandler
 * @brief Routes metadata-related UI/app messages to responsible controllers.
 */
class MetadataMessageHandler {
public:
  /**
   * @brief Creates the metadata message router.
   * @param window Owning main window context.
   */
  explicit MetadataMessageHandler(MainWindow *window);

  /**
   * @brief Releases owned metadata sub-controllers.
   */
  ~MetadataMessageHandler();

  /**
   * @brief Handles one metadata-related message.
   * @param msg Incoming message.
   * @return `true` if the message was handled.
   */
  bool HandleMessage(BMessage *msg);

private:
  /** @brief Owning main window. */
  MainWindow *fWindow;
  /** @brief Handles MusicBrainz search/apply flow. */
  MusicBrainzLookupController *fMusicBrainzLookupController;
};

#endif // BETON_METADATA_MESSAGE_HANDLER_H
