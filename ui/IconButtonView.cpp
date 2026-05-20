#include "IconButtonView.h"
#include <Cursor.h>
#include <Application.h>
#include <Window.h>

IconButtonView::IconButtonView(const char* name, BBitmap* icon, BMessage* message)
    : BView(name, B_WILL_DRAW), fIcon(icon), fMessage(message), fIsHovered(false) {
  SetViewColor(B_TRANSPARENT_COLOR);
}

IconButtonView::~IconButtonView() {
  delete fMessage;
}

void IconButtonView::AttachedToWindow() {
  BView::AttachedToWindow();
  if (!fTarget.IsValid()) {
    fTarget = BMessenger(Window());
  }
  UpdateSystemColors();
}

void IconButtonView::SetTarget(BMessenger target) {
  fTarget = target;
}

void IconButtonView::SetIcon(BBitmap* icon) {
  fIcon = icon;
  Invalidate();
}

void IconButtonView::UpdateSystemColors() {
  rgb_color bg = Parent() ? Parent()->ViewColor()
                          : ui_color(B_PANEL_BACKGROUND_COLOR);
  SetViewColor(bg);
  SetLowColor(bg);
  Invalidate();
}

/**
  * @brief Clear view background before drawing icon content.
  */
void IconButtonView::Draw(BRect updateRect) {
  SetHighColor(ViewColor());
  FillRect(updateRect);

  if (fIcon) {
    SetDrawingMode(B_OP_ALPHA);
    
    /**
     * @brief Center icon inside view bounds.
     */
    BRect bounds = Bounds();
    BRect iconBounds = fIcon->Bounds();
    BPoint pt(
      (bounds.Width() - iconBounds.Width()) / 2.0f,
      (bounds.Height() - iconBounds.Height()) / 2.0f
    );
    
    DrawBitmap(fIcon, pt);
  }
}

void IconButtonView::MouseDown(BPoint point) {
  if (fMessage && fTarget.IsValid()) {
    BMessage copy(*fMessage);
    fTarget.SendMessage(&copy);
  }
  BView::MouseDown(point);
}

void IconButtonView::MouseMoved(BPoint point, uint32 transit, const BMessage* message) {
  if (transit == B_ENTERED_VIEW) {
    if (!fIsHovered) {
      fIsHovered = true;
      BCursor handCursor(B_CURSOR_ID_FOLLOW_LINK);
      SetViewCursor(&handCursor);
    }
  } else if (transit == B_EXITED_VIEW) {
    if (fIsHovered) {
      fIsHovered = false;
      BCursor sysCursor(B_CURSOR_ID_SYSTEM_DEFAULT);
      SetViewCursor(&sysCursor);
    }
  }
  BView::MouseMoved(point, transit, message);
}
