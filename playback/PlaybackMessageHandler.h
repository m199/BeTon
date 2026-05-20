#ifndef BETON_PLAYBACK_MESSAGE_HANDLER_H
#define BETON_PLAYBACK_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;

/**
 * @class PlaybackMessageHandler
 * @brief Routes playback-related messages to transport/queue controllers.
 */
class PlaybackMessageHandler {
public:
  /**
   * @brief Creates playback message router.
   * @param window Owning main window context.
   */
  explicit PlaybackMessageHandler(MainWindow *window);

  /**
   * @brief Handles one playback-domain message.
   * @param msg Incoming message.
   * @return `true` if handled.
   */
  bool HandleMessage(BMessage *msg);

private:
  /** @brief Owning main window. */
  MainWindow *fWindow;
};

#endif // BETON_PLAYBACK_MESSAGE_HANDLER_H
