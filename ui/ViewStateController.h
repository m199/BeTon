#ifndef BETON_VIEW_STATE_CONTROLLER_H
#define BETON_VIEW_STATE_CONTROLLER_H

#include <Message.h>

class MainWindow;

class ViewStateController {
public:
  ViewStateController(MainWindow *window);
  ~ViewStateController();

  void ToggleArtworkVisible();
  void ToggleFileInfoVisible();
  void ShowFileInfo();
  void ShowCoverArt();
  void ApplyDroppedSeekBarColor(BMessage *msg);
  void UseSystemSelectionColor();
  void UseSeekBarSelectionColor();
  void SetTooltipsEnabled(bool enabled);
  void UpdateTooltips();
  void SetFastEditEnabled(bool enabled);
  void ToggleFilters();
  void HandleColorsUpdated();
  void HandleContentSelectionChanged();

private:
  MainWindow *fWindow;
};

#endif // BETON_VIEW_STATE_CONTROLLER_H
