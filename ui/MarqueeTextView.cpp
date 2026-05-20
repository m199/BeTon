#include "MarqueeTextView.h"
#include <MessageRunner.h>
#include <OS.h>
#include <Window.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

static const uint32 MSG_MARQUEE_TICK = 'mQtk';
/** @brief Marquee timer interval (~30 fps). */
static const bigtime_t kFrameInterval = 33333;
static const bigtime_t kEndPause = 1500000;
/** @brief Horizontal marquee speed in pixels per second. */
static const float kScrollSpeed = 30.0f;

static std::vector<MarqueeTextView *> sMarqueeViews;
static std::map<std::string, bigtime_t> sGroupCycleStart;

MarqueeTextView::MarqueeTextView(const char *name)
    : BView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE),
      fTextWidth(0), fRunner(nullptr) {
  sMarqueeViews.push_back(this);
  fSyncGroup = name ? name : "";
  _ResetCycle();
  SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
}

MarqueeTextView::~MarqueeTextView() {
  delete fRunner;
  sMarqueeViews.erase(
      std::remove(sMarqueeViews.begin(), sMarqueeViews.end(), this),
      sMarqueeViews.end());
}

void MarqueeTextView::SetText(const char *text) {
  SetText(BString(text));
}

void MarqueeTextView::SetText(const BString &text) {
  if (fText == text)
    return;

  fText = text;
  fTextWidth = StringWidth(fText.String());
  _ResetCycle();

  _UpdateScrolling();
  Invalidate();
}

void MarqueeTextView::SetSyncGroup(const char *group) {
  fSyncGroup = group ? group : "";
  _ResetCycle();
  _UpdateScrolling();
  Invalidate();
}

const char *MarqueeTextView::Text() const {
  return fText.String();
}

void MarqueeTextView::UpdateSystemColors() {
  rgb_color bg = Parent() ? Parent()->ViewColor()
                          : ui_color(B_PANEL_BACKGROUND_COLOR);
  SetViewColor(bg);
  SetLowColor(bg);
  SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
  Invalidate();
}

void MarqueeTextView::AttachedToWindow() {
  BView::AttachedToWindow();
  UpdateSystemColors();
  _UpdateScrolling();
}

void MarqueeTextView::DetachedFromWindow() {
  delete fRunner;
  fRunner = nullptr;
  BView::DetachedFromWindow();
}

void MarqueeTextView::FrameResized(float width, float height) {
  BView::FrameResized(width, height);
  _ResetCycle();
  _UpdateScrolling();
}

void MarqueeTextView::_UpdateScrolling() {
  if (!Window()) return;

  bool needsScroll = _NeedsScroll();

  if (needsScroll && !fRunner) {
    BMessage msg(MSG_MARQUEE_TICK);
    fRunner = new BMessageRunner(BMessenger(this), &msg, kFrameInterval);
  } else if (!needsScroll && fRunner) {
    delete fRunner;
    fRunner = nullptr;
  }
}

void MarqueeTextView::MessageReceived(BMessage *msg) {
  if (msg->what == MSG_MARQUEE_TICK) {
    if (_NeedsScroll())
      Invalidate();
  } else if (msg->what == B_COLORS_UPDATED) {
    UpdateSystemColors();
  } else {
    BView::MessageReceived(msg);
  }
}

void MarqueeTextView::Draw(BRect updateRect) {
  if (fText.IsEmpty()) return;

  SetDrawingMode(B_OP_OVER);

  font_height fh;
  GetFontHeight(&fh);
  float textHeight = fh.ascent + fh.descent;
  float baseline = Bounds().top + (Bounds().Height() - textHeight) / 2.0f + fh.ascent;

  float startX = 0;
  if (fTextWidth < Bounds().Width()) {
      startX = 0;
  } else {
      startX = -_ScrollOffset();
  }

  DrawString(fText.String(), BPoint(startX, baseline));
}

bool MarqueeTextView::_NeedsScroll() const {
  return _Overflow() > 0.0f;
}

float MarqueeTextView::_Overflow() const {
  return std::max(0.0f, fTextWidth - Bounds().Width());
}

float MarqueeTextView::_GroupMaxOverflow() const {
  float maxOverflow = _Overflow();
  for (auto *view : sMarqueeViews) {
    if (!view || view == this)
      continue;
    if (view->Window() != Window())
      continue;
    if (view->fSyncGroup != fSyncGroup)
      continue;
    maxOverflow = std::max(maxOverflow, view->_Overflow());
  }
  return maxOverflow;
}

float MarqueeTextView::_ScrollOffset() const {
  float overflow = _Overflow();
  if (overflow <= 0.0f)
    return 0.0f;

  float groupOverflow = _GroupMaxOverflow();
  if (groupOverflow <= 0.0f)
    return 0.0f;

  std::string group(fSyncGroup.String());
  auto it = sGroupCycleStart.find(group);
  bigtime_t start = it != sGroupCycleStart.end() ? it->second : system_time();

  bigtime_t travelTime =
      (bigtime_t)((groupOverflow / kScrollSpeed) * 1000000.0f);
  bigtime_t cycle = kEndPause + travelTime + kEndPause + travelTime;
  if (cycle <= 0)
    return 0.0f;

  bigtime_t elapsed = (system_time() - start) % cycle;
  if (elapsed < kEndPause)
    return 0.0f;

  elapsed -= kEndPause;
  if (elapsed < travelTime) {
    float t = travelTime > 0 ? (float)elapsed / (float)travelTime : 1.0f;
    return overflow * t;
  }

  elapsed -= travelTime;
  if (elapsed < kEndPause)
    return overflow;

  elapsed -= kEndPause;
  float t = travelTime > 0 ? (float)elapsed / (float)travelTime : 1.0f;
  return overflow * (1.0f - t);
}

void MarqueeTextView::_ResetCycle() {
  sGroupCycleStart[std::string(fSyncGroup.String())] = system_time();
}

BSize MarqueeTextView::MinSize() {
  font_height fh;
  GetFontHeight(&fh);
  return BSize(10, fh.ascent + fh.descent + fh.leading);
}

BSize MarqueeTextView::PreferredSize() {
  font_height fh;
  GetFontHeight(&fh);
  return BSize(fTextWidth + 4.0f, fh.ascent + fh.descent + fh.leading);
}

BSize MarqueeTextView::MaxSize() {
  font_height fh;
  GetFontHeight(&fh);
  return BSize(B_SIZE_UNLIMITED, fh.ascent + fh.descent + fh.leading);
}
