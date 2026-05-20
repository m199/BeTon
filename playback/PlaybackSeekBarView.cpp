#include "PlaybackSeekBarView.h"
#include "Messages.h"

#include <InterfaceDefs.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <algorithm>
#include <cstdio>

/**
 * @brief Constructor.
 * @param name The name of the view.
 */
PlaybackSeekBarView::PlaybackSeekBarView(const char *name)
    : BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE), fDuration(0),
      fPosition(0), fTracking(false) {
  SetViewColor(B_TRANSPARENT_COLOR);
  fBg = tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_1_TINT);
  fFill = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
  fBorder = tint_color(fBg, B_DARKEN_2_TINT);

  // Calculate font-relative sizes for DPI scaling
  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  SetExplicitMinSize(BSize(fontHeight * 14, fontHeight));
  SetExplicitPreferredSize(BSize(fontHeight * 24, fontHeight));
}

void PlaybackSeekBarView::AttachedToWindow() { SetLowColor(ViewColor()); }

/**
 * @brief Formats a time in microseconds to "MM:SS".
 * @param usec Time in microseconds.
 * @param out Output BString.
 */
static void FormatTime(bigtime_t usec, BString &out) {
  int seconds = usec / 1000000;
  int min = seconds / 60;
  int sec = seconds % 60;
  out.SetToFormat("%d:%02d", min, sec);
}

/**
 * @brief Sets the total duration of the track.
 * @param duration Duration in microseconds.
 */
void PlaybackSeekBarView::SetDuration(bigtime_t duration) {
  if (duration < 0)
    duration = 0;
  fDuration = duration;
  if (fPosition > fDuration)
    fPosition = fDuration;
  Invalidate();
}

/**
 * @brief Sets the current playback position.
 * @param pos Position in microseconds.
 */
void PlaybackSeekBarView::SetPosition(bigtime_t pos) {
  if (pos < 0)
    pos = 0;
  if (fDuration > 0 && pos > fDuration)
    pos = fDuration;
  fPosition = pos;
  Invalidate();
}

/**
 * @brief Sets custom colors for the seek bar.
 */
void PlaybackSeekBarView::SetColors(rgb_color bg, rgb_color fill, rgb_color border) {
  fBg = bg;
  fFill = fill;
  fBorder = border;
  Invalidate();
}

void PlaybackSeekBarView::Draw(BRect) { _DrawBar(Bounds()); }

/**
 * @brief Internal method to draw the seek bar.
 * @param r The rectangle to draw into (usually Bounds()).
 */
void PlaybackSeekBarView::_DrawBar(const BRect &r) {

  // Clear background first to avoid corner artifacts
  SetHighColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  FillRect(r);

  SetHighColor(fBg);
  FillRoundRect(r, 2, 2);

  SetHighColor(fBorder);
  StrokeRoundRect(r, 2, 2);

  if (fDuration > 0) {
    float ratio = static_cast<float>(fPosition) / static_cast<float>(fDuration);
    ratio = std::clamp(ratio, 0.0f, 1.0f);

    BRect fillRect = r;
    fillRect.InsetBy(1, 1);
    fillRect.right = fillRect.left + ratio * r.Width();
    if (fillRect.right < fillRect.left)
      fillRect.right = fillRect.left;

    SetHighColor(fFill);
    FillRoundRect(fillRect, 2, 2);
    BString left, right;
    FormatTime(fPosition, left);
    FormatTime(fDuration, right);

    font_height fh;
    GetFontHeight(&fh);
    float y = Bounds().top + (Bounds().Height() - fh.ascent) / 2.0f + fh.ascent;

    SetHighColor(0, 0, 0);
    DrawString(left.String(), BPoint(Bounds().left + 4, y));
    float w = StringWidth(right.String());
    DrawString(right.String(), BPoint(Bounds().right - w - 4, y));
  }
}

void PlaybackSeekBarView::MouseDown(BPoint where) {
  _SeekFromPoint(where);
  fTracking = true;
  SetMouseEventMask(B_POINTER_EVENTS, 0);
}

void PlaybackSeekBarView::MouseUp(BPoint) { fTracking = false; }

void PlaybackSeekBarView::MouseMoved(BPoint where, uint32 transit, const BMessage *) {
  if (fTracking)
    _SeekFromPoint(where);
}

/**
 * @brief Calculates seek position from mouse point and notifies target window.
 */
void PlaybackSeekBarView::_SeekFromPoint(BPoint where) {
  if (fDuration <= 0)
    return;
  float ratio = (where.x - Bounds().left) / Bounds().Width();
  ratio = std::clamp(ratio, 0.0f, 1.0f);
  bigtime_t newPos = static_cast<bigtime_t>(ratio * fDuration);

  SetPosition(newPos);

  BMessage msg(MSG_SEEK_REQUEST);
  msg.AddInt64("position", newPos);

  BMessenger msgr(NULL, Window());
  msgr.SendMessage(&msg);
}

void PlaybackSeekBarView::MessageReceived(BMessage *msg) {
  // Handle drag-and-drop color from Color Picker
  if (msg->WasDropped()) {
    rgb_color *color;
    ssize_t size;
    if (msg->FindData("RGBColor", B_RGB_COLOR_TYPE, (const void **)&color,
                      &size) == B_OK &&
        size == sizeof(rgb_color)) {
      fFill = *color;
      Invalidate();
      // Notify MainWindow about color change
      BMessage notify(MSG_SEEKBAR_COLOR_DROPPED);
      notify.AddData("color", B_RGB_COLOR_TYPE, color, sizeof(rgb_color));
      BMessenger(Window()).SendMessage(&notify);
      return;
    }
  }
  BView::MessageReceived(msg);
}
