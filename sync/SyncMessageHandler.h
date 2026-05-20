#ifndef BETON_SYNC_MESSAGE_HANDLER_H
#define BETON_SYNC_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;

class SyncMessageHandler {
public:
  explicit SyncMessageHandler(MainWindow *window);

  bool HandleMessage(BMessage *msg);

private:
  MainWindow *fWindow;
};

#endif // BETON_SYNC_MESSAGE_HANDLER_H
