#ifndef BETON_PLAYLIST_MESSAGE_HANDLER_H
#define BETON_PLAYLIST_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;

class PlaylistMessageHandler {
public:
  explicit PlaylistMessageHandler(MainWindow *window);

  bool HandleMessage(BMessage *msg);

private:
  MainWindow *fWindow;
};

#endif // BETON_PLAYLIST_MESSAGE_HANDLER_H
