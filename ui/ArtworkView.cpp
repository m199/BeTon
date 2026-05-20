#include "ArtworkView.h"
#include <Bitmap.h>
#include <Catalog.h>
#include <Message.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ArtworkView"

namespace {

static float Luminance(rgb_color color) {
  return (0.299f * color.red + 0.587f * color.green + 0.114f * color.blue) /
         255.0f;
}

}
/* namespace */

ArtworkView::ArtworkView(const char *name)
    : BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE) {
  SetViewColor(B_TRANSPARENT_COLOR);
}

ArtworkView::~ArtworkView() {
  delete fBitmap;
  fBitmap = nullptr;
}

/**
 * @brief Updates the displayed cover image.
 * Makes a defensive copy of the provided bitmap.
 *
 * @param bmp The new bitmap to display (can be nullptr to clear).
 */
void ArtworkView::SetBitmap(BBitmap *bmp) {
  if (fBitmap == nullptr && bmp == nullptr)
    return;

  BBitmap *clone = nullptr;
  if (bmp && bmp->IsValid()) {
    clone = new BBitmap(bmp);
    if (!clone->IsValid()) {
      delete clone;
      clone = nullptr;
    }
  }
  delete fBitmap;
  fBitmap = clone;

  Invalidate();
}

/**
 * @brief Draws the cover scaled to fit the view bounds.
 */
void ArtworkView::Draw(BRect updateRect) {
  rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);
  SetHighColor(panelBg);
  FillRect(Bounds());

  if (!fBitmap || !fBitmap->IsValid()) {
    BRect b = Bounds();
    rgb_color placeholderBg =
        Luminance(panelBg) > 0.5f ? tint_color(panelBg, B_DARKEN_1_TINT)
                                  : tint_color(panelBg, B_LIGHTEN_1_TINT);
    rgb_color mutedText =
        tint_color(ui_color(B_PANEL_TEXT_COLOR), B_DISABLED_LABEL_TINT);
    rgb_color placeholderText = tint_color(mutedText, B_LIGHTEN_2_TINT);
    rgb_color borderCol = tint_color(mutedText, B_LIGHTEN_2_TINT);

    SetHighColor(placeholderBg);
    FillRect(b);

    SetHighColor(borderCol);
    StrokeRect(b);

    SetHighColor(placeholderText);
    const char *text = B_TRANSLATE("No Cover");
    font_height fh;
    GetFontHeight(&fh);
    float textWidth = StringWidth(text);
    BPoint textPt(b.left + (b.Width() - textWidth) / 2.0f,
                  b.top + (b.Height() + (fh.ascent - fh.descent)) / 2.0f);
    DrawString(text, textPt);
    return;
  }

  BRect src = fBitmap->Bounds();
  BRect dst = Bounds();

  float srcRatio = src.Width() / src.Height();
  float dstRatio = dst.Width() / dst.Height();

  if (srcRatio > dstRatio) {
    float newHeight = dst.Width() / srcRatio;
    float offset = (dst.Height() - newHeight) / 2.0f;
    dst.top += offset;
    dst.bottom = dst.top + newHeight;
  } else {
    float newWidth = dst.Height() * srcRatio;
    float offset = (dst.Width() - newWidth) / 2.0f;
    dst.left += offset;
    dst.right = dst.left + newWidth;
  }

  SetDrawingMode(B_OP_ALPHA);
  DrawBitmapAsync(fBitmap, src, dst);
  SetDrawingMode(B_OP_COPY);
}

void ArtworkView::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case B_COLORS_UPDATED:
    Invalidate();
    break;
  default:
    BView::MessageReceived(msg);
    break;
  }
}

void ArtworkView::GetPreferredSize(float *w, float *h) {
  if (w)
    *w = 200;
  if (h)
    *h = 200;
}

bool ArtworkView::HasHeightForWidth() {
  return true;
}

void ArtworkView::GetHeightForWidth(float width, float* min, float* max, float* pref) {
  if (min) *min = width;
  if (max) *max = width;
  if (pref) *pref = width;
}
