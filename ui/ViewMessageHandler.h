#ifndef BETON_VIEW_MESSAGE_HANDLER_H
#define BETON_VIEW_MESSAGE_HANDLER_H

class BMessage;
class MainWindow;

class ViewMessageHandler {
public:
  explicit ViewMessageHandler(MainWindow *window);

  bool HandleMessage(BMessage *msg);

private:
  MainWindow *fWindow;
};

#endif // BETON_VIEW_MESSAGE_HANDLER_H
