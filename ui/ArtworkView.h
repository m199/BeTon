#ifndef BETON_ARTWORK_VIEW_H
#define BETON_ARTWORK_VIEW_H

#include <View.h>
class BBitmap;

/**
 * @class ArtworkView
 * @brief A simple view to display album cover art.
 *
 * It handles:
 * - Scaling the image to fit the view.
 * - Managing the lifecycle of the BBitmap (takes ownership).
 */
class ArtworkView : public BView {
public:
  explicit ArtworkView(const char *name);
  ~ArtworkView() override;

  /**
   * @brief Sets the cover image.
   * @param bmp The bitmap to display. The view takes a copy/ownership logic
   * depending on implementation. (Note: Implementation actually duplicates the
   * bitmap, so this pointer ownership transfer is implicit via copy).
   */
  void SetBitmap(BBitmap *bmp);

  void Draw(BRect update) override;
  void MessageReceived(BMessage *msg) override;
  void GetPreferredSize(float *w, float *h) override;
  
  bool HasHeightForWidth() override;
  void GetHeightForWidth(float width, float *min, float *max,
                         float *pref) override;

private:
  /** @name Data */
  ///@{
  BBitmap *fBitmap = nullptr;
  ///@}
};

#endif // BETON_ARTWORK_VIEW_H
