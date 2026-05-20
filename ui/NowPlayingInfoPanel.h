#ifndef BETON_NOW_PLAYING_INFO_PANEL_H
#define BETON_NOW_PLAYING_INFO_PANEL_H

#include <Box.h>
#include <String.h>
#include <View.h>

class BGroupLayout;
class BBitmap;
class ArtworkView;
class MarqueeTextView;
class BStringView;

/**
 * @class NowPlayingInfoPanel
 * @brief A view that toggles between displaying file metadata and cover art.
 * Uses a vertical BGroupLayout to stack components.
 * - `MetadataView` shows track details.
 * - `ArtworkView` shows album cover art.
 * Both can be toggled independently.
 */
class NowPlayingInfoPanel : public BView {
public:
  NowPlayingInfoPanel();
  ~NowPlayingInfoPanel() override;

  /**
   * @brief Toggle metadata visibility.
   */
  void SetInfoVisible(bool visible);

  /**
   * @brief Toggle cover visibility.
   */
  void SetCoverVisible(bool visible);

  /**
   * @brief Updates the metadata text.
   */
  void SetFileInfo(const BString &text);
  void SetTags(const BString &artist, const BString &title, const BString &album);
  void SetTechInfo(const BString &info);

  /**
   * @brief Sets the cover image.
   */
  void SetCover(BBitmap *bmp);

  void ClearCover();

  void SetBoxLabel(const char *label);
  
  void SetWordWrap(bool wrap);

  void AttachedToWindow() override;
  void MessageReceived(BMessage *msg) override;

private:
  void _UpdatePanelVisibility();
  void _ApplyColors();

  /** @name UI Components */
  ///@{
  BGroupLayout *fLayout = nullptr;

  BBox *fInfoBox = nullptr;
  MarqueeTextView *fTitleMarquee = nullptr;
  MarqueeTextView *fArtistMarquee = nullptr;
  MarqueeTextView *fAlbumMarquee = nullptr;
  BStringView *fTechView = nullptr;

  BView *fCoverPane = nullptr;
  ArtworkView *fArtworkView = nullptr;
  
  bool fIsInfoVisible = true;
  bool fIsCoverVisible = true;
  ///@}
};

#endif // BETON_NOW_PLAYING_INFO_PANEL_H
