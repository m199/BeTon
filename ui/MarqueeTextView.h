#ifndef BETON_MARQUEE_TEXT_VIEW_H
#define BETON_MARQUEE_TEXT_VIEW_H

#include <View.h>
#include <String.h>

class BMessageRunner;

class MarqueeTextView : public BView {
public:
  MarqueeTextView(const char *name);
  virtual ~MarqueeTextView();

  void SetText(const BString &text);
  void SetText(const char *text);
  void SetSyncGroup(const char *group);
  const char *Text() const;
  void UpdateSystemColors();

  virtual void AttachedToWindow() override;
  virtual void DetachedFromWindow() override;
  virtual void MessageReceived(BMessage *msg) override;
  virtual void Draw(BRect updateRect) override;
  virtual void FrameResized(float width, float height) override;

  virtual BSize MinSize() override;
  virtual BSize PreferredSize() override;
  virtual BSize MaxSize() override;

private:
  void _UpdateScrolling();
  bool _NeedsScroll() const;
  float _Overflow() const;
  float _GroupMaxOverflow() const;
  float _ScrollOffset() const;
  void _ResetCycle();

  BString fText;
  BString fSyncGroup;
  float fTextWidth;

  BMessageRunner *fRunner;
};

#endif // BETON_MARQUEE_TEXT_VIEW_H
