#ifndef BETON_LIBRARY_MESSAGE_HANDLER_H
#define BETON_LIBRARY_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;

class LibraryMessageHandler {
public:
  explicit LibraryMessageHandler(MainWindow *window);

  bool HandleMessage(BMessage *msg);

private:
  MainWindow *fWindow;
};

#endif // BETON_LIBRARY_MESSAGE_HANDLER_H
