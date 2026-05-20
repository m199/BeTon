#ifndef BETON_RADIO_MESSAGE_HANDLER_H
#define BETON_RADIO_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;

/**
 * @class RadioMessageHandler
 * @brief Dispatches radio-related UI/application messages to the controller.
 */
class RadioMessageHandler {
public:
  explicit RadioMessageHandler(MainWindow *window);

  bool HandleMessage(BMessage *msg);

private:
  MainWindow *fWindow;
};

#endif // BETON_RADIO_MESSAGE_HANDLER_H
