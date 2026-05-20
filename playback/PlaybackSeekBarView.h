#ifndef BETON_PLAYBACK_SEEK_BAR_VIEW_H
#define BETON_PLAYBACK_SEEK_BAR_VIEW_H

#include <InterfaceDefs.h>
#include <Message.h>
#include <Rect.h>
#include <SupportDefs.h>
#include <View.h>

/**
 * @class PlaybackSeekBarView
 * @brief A custom BView that displays a playback progress bar and allows
 * seeking.
 *
 * The view displays the current playback position and total duration.
 * Users can click or drag on the bar to seek to a specific time.
 * It sends MSG_SEEK_REQUEST messages to the window when interaction occurs.
 */
class PlaybackSeekBarView : public BView {
public:
  /**
   * @brief Constructor.
   * @param name The name of the view.
   */
  explicit PlaybackSeekBarView(const char *name);

  /**
   * @brief Sets the total duration of the media.
   * @param duration Duration in microseconds.
   */
  void SetDuration(bigtime_t duration);

  /**
   * @brief Sets the current playback position.
   * @param pos Position in microseconds.
   */
  void SetPosition(bigtime_t pos);

  /**
   * @return The currently set total duration in microseconds.
   */
  bigtime_t Duration() const { return fDuration; }

  /**
   * @return The currently set position in microseconds.
   */
  bigtime_t Position() const { return fPosition; }

  /**
   * @brief Customizes the colors of the seek bar.
   * @param bg Background color.
   * @param fill Fill color (progress).
   * @param border Border color.
   */
  void SetColors(rgb_color bg, rgb_color fill, rgb_color border);

  void Draw(BRect updateRect) override;
  void MouseDown(BPoint where) override;
  void MouseUp(BPoint where) override;
  void MouseMoved(BPoint where, uint32 transit,
                  const BMessage *dragMessage) override;
  void AttachedToWindow() override;
  void MessageReceived(BMessage *msg) override;

private:
  void _SeekFromPoint(BPoint where);
  void _DrawBar(const BRect &r);

  /** @name State */
  ///@{
  bigtime_t fDuration;
  bigtime_t fPosition;
  bool fTracking;
  ///@}

  /** @name Appearance */
  ///@{
  rgb_color fBg;
  rgb_color fFill;
  rgb_color fBorder;
  ///@}
};

#endif // BETON_PLAYBACK_SEEK_BAR_VIEW_H
