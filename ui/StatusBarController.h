#ifndef BETON_STATUS_BAR_CONTROLLER_H
#define BETON_STATUS_BAR_CONTROLLER_H

#include <String.h>

class BMessage;
class MainWindow;

class StatusBarController {
public:
  explicit StatusBarController(MainWindow *window);

  bool HandleMessage(BMessage *msg);
  void UpdateStatus(const BString &text, bool isPermanent = false);
  void UpdateLibraryStatus();

private:
  void ScheduleSearchUpdate();
  void ApplyStatusUpdate(BMessage *msg);
  void UpdateLibraryPreview(BMessage *msg);

  MainWindow *fWindow;
};

#endif // BETON_STATUS_BAR_CONTROLLER_H
