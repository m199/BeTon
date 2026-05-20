#ifndef BETON_DLNA_MESSAGE_HANDLER_H
#define BETON_DLNA_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;

class DLNAMessageHandler {
public:
  explicit DLNAMessageHandler(MainWindow *window);

  bool HandleMessage(BMessage *msg);

private:
  MainWindow *fWindow;
};

#endif // BETON_DLNA_MESSAGE_HANDLER_H
