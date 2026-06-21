#include "ArtworkView.h"
#include <algorithm>
#include <Bitmap.h>
#include <Catalog.h>
#include <cmath>
#include <Message.h>
#include <View.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ArtworkView"

namespace {

static float Luminance(rgb_color color) {
  return (0.299f * color.red + 0.587f * color.green + 0.114f * color.blue) /
         255.0f;
}

static int32 PixelWidth(const BBitmap *bitmap) {
  return bitmap ? bitmap->Bounds().IntegerWidth() + 1 : 0;
}

static int32 PixelHeight(const BBitmap *bitmap) {
  return bitmap ? bitmap->Bounds().IntegerHeight() + 1 : 0;
}

static BBitmap *ScaleBitmap(const BBitmap *source, int32 width, int32 height) {
  if (!source || !source->IsValid() || width <= 0 || height <= 0)
    return nullptr;

  BRect bounds(0, 0, width - 1, height - 1);
  BBitmap *target = new BBitmap(bounds, B_RGB32, true);
  if (!target->IsValid()) {
    delete target;
    return nullptr;
  }

  BView *canvas = new BView(bounds, "artwork scaler", B_FOLLOW_NONE, 0);
  target->AddChild(canvas);
  canvas->LockLooper();
  canvas->SetDrawingMode(B_OP_COPY);
  canvas->DrawBitmap(source, source->Bounds(), bounds, B_FILTER_BITMAP_BILINEAR);
  canvas->Sync();
  canvas->UnlockLooper();
  target->RemoveChild(canvas);
  delete canvas;

  return target;
}

static BBitmap *ScaleBitmapStepwise(const BBitmap *source, int32 targetWidth,
                                    int32 targetHeight) {
  int32 currentWidth = PixelWidth(source);
  int32 currentHeight = PixelHeight(source);
  if (currentWidth <= 0 || currentHeight <= 0)
    return nullptr;

  const BBitmap *current = source;
  BBitmap *owned = nullptr;

  while (currentWidth / 2 >= targetWidth && currentHeight / 2 >= targetHeight) {
    int32 nextWidth = std::max(targetWidth, currentWidth / 2);
    int32 nextHeight = std::max(targetHeight, currentHeight / 2);
    BBitmap *next = ScaleBitmap(current, nextWidth, nextHeight);
    if (!next)
      break;

    delete owned;
    owned = next;
    current = owned;
    currentWidth = nextWidth;
    currentHeight = nextHeight;
  }

  BBitmap *result = ScaleBitmap(current, targetWidth, targetHeight);
  delete owned;
  return result;
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
  delete fScaledBitmap;
  fScaledBitmap = nullptr;
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
  _InvalidateScaledBitmap();

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

  BRect frame = _CoverFrame();
  _UpdateScaledBitmap();

  if (fScaledBitmap) {
    SetDrawingMode(B_OP_COPY);
    DrawBitmapAsync(fScaledBitmap, fScaledFrame.LeftTop());
  } else if (frame.IsValid()) {
    SetDrawingMode(B_OP_ALPHA);
    DrawBitmapAsync(fBitmap, fBitmap->Bounds(), frame, B_FILTER_BITMAP_BILINEAR);
  }
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

void ArtworkView::FrameResized(float width, float height) {
  BView::FrameResized(width, height);
  _InvalidateScaledBitmap();
}

bool ArtworkView::HasHeightForWidth() {
  return true;
}

void ArtworkView::GetHeightForWidth(float width, float* min, float* max, float* pref) {
  if (min) *min = width;
  if (max) *max = width;
  if (pref) *pref = width;
}

BRect ArtworkView::_CoverFrame() const {
  BRect bounds = Bounds();
  if (!fBitmap || !fBitmap->IsValid() || !bounds.IsValid())
    return BRect();

  int32 sourceWidth = PixelWidth(fBitmap);
  int32 sourceHeight = PixelHeight(fBitmap);
  int32 boundsWidth = bounds.IntegerWidth() + 1;
  int32 boundsHeight = bounds.IntegerHeight() + 1;
  if (sourceWidth <= 0 || sourceHeight <= 0 || boundsWidth <= 0 ||
      boundsHeight <= 0)
    return BRect();

  float scale = std::min(static_cast<float>(boundsWidth) / sourceWidth,
                         static_cast<float>(boundsHeight) / sourceHeight);
  int32 targetWidth =
      std::max<int32>(1, static_cast<int32>(std::round(sourceWidth * scale)));
  int32 targetHeight =
      std::max<int32>(1, static_cast<int32>(std::round(sourceHeight * scale)));

  float left = bounds.left + std::floor((boundsWidth - targetWidth) / 2.0f);
  float top = bounds.top + std::floor((boundsHeight - targetHeight) / 2.0f);
  return BRect(left, top, left + targetWidth - 1, top + targetHeight - 1);
}

void ArtworkView::_InvalidateScaledBitmap() {
  delete fScaledBitmap;
  fScaledBitmap = nullptr;
  fScaledFrame = BRect();
}

void ArtworkView::_UpdateScaledBitmap() {
  BRect frame = _CoverFrame();
  if (!frame.IsValid())
    return;

  int32 width = frame.IntegerWidth() + 1;
  int32 height = frame.IntegerHeight() + 1;
  if (fScaledBitmap && fScaledFrame == frame &&
      PixelWidth(fScaledBitmap) == width && PixelHeight(fScaledBitmap) == height)
    return;

  delete fScaledBitmap;
  fScaledBitmap = ScaleBitmapStepwise(fBitmap, width, height);
  fScaledFrame = fScaledBitmap ? frame : BRect();
}
