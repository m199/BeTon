#ifndef BETON_ICON_BUTTON_VIEW_H
#define BETON_ICON_BUTTON_VIEW_H

#include <View.h>
#include <Bitmap.h>
#include <Message.h>
#include <Messenger.h>

/**
 * @class IconButtonView
 * @brief A BView that draws a BBitmap and acts as a clickable area.
 */
class IconButtonView : public BView {
public:
  IconButtonView(const char *name, BBitmap *icon, BMessage *message);
  virtual ~IconButtonView();

  virtual void AttachedToWindow() override;
  virtual void Draw(BRect updateRect) override;
  virtual void MouseDown(BPoint point) override;
  virtual void MouseMoved(BPoint point, uint32 transit,
                          const BMessage *message) override;

  void SetTarget(BMessenger target);
  void SetIcon(BBitmap *icon);
  void UpdateSystemColors();

private:
  BBitmap *fIcon;
  BMessage *fMessage;
  BMessenger fTarget;
  bool fIsHovered;
};

#endif // BETON_ICON_BUTTON_VIEW_H
