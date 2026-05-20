#include "NowPlayingInfoPanel.h"
#include "ArtworkView.h"

#include "MarqueeTextView.h"
#include <Catalog.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <StringView.h>


#include <Window.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "NowPlayingInfoPanel"

namespace {

const float kTopSpacing = 3.0f;

}
/* namespace */

/**
 * @brief Constructs the NowPlayingInfoPanel.
 *
 * Creates a vertical layout with:
 * Text Info Pane: Displays textual metadata in a BBox.
 * Cover Pane: Displays the album art in a ArtworkView within a BBox.
 * 
 * Maybe later more (visuals?). This implemenation isnt perfect, but it works for now.
 * 
 * Both can be toggled independently.
 */
NowPlayingInfoPanel::NowPlayingInfoPanel() : BView("NowPlayingInfoPanel", B_WILL_DRAW) {
  SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  /**
   * @brief Calculate font-relative sizes for DPI-aware layout.
   */
  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  fTitleMarquee = new MarqueeTextView("infoTitle");
  fTitleMarquee->SetSyncGroup("nowPlayingInfo");
  BFont boldFont(be_bold_font);
  boldFont.SetSize(boldFont.Size() + 2.0f);
  fTitleMarquee->SetFont(&boldFont);
  fTitleMarquee->SetText(B_TRANSLATE("No Media"));

  fArtistMarquee = new MarqueeTextView("infoArtist");
  fArtistMarquee->SetSyncGroup("nowPlayingInfo");
  BFont normalFont(be_plain_font);
  fArtistMarquee->SetFont(&normalFont);
  fArtistMarquee->SetText("");

  fAlbumMarquee = new MarqueeTextView("infoAlbum");
  fAlbumMarquee->SetSyncGroup("nowPlayingInfo");
  BFont italicFont(be_plain_font);
  italicFont.SetFace(B_ITALIC_FACE);
  fAlbumMarquee->SetFont(&italicFont);
  fAlbumMarquee->SetText("");

  fTechView = new BStringView("infoTech", "");
  BFont tinyFont(be_plain_font);
  tinyFont.SetSize(tinyFont.Size() - 2.0f);
  fTechView->SetFont(&tinyFont);
  fTechView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

  fInfoBox = new BBox("infoBox");
  fInfoBox->SetLabel(B_TRANSLATE("File Information"));
  fInfoBox->SetBorder(B_FANCY_BORDER);

  fInfoBox->SetExplicitMinSize(BSize(fontHeight * 10, fontHeight * 6));
  fInfoBox->SetExplicitPreferredSize(BSize(fontHeight * 14, fontHeight * 14));
  fInfoBox->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, fontHeight * 17));

  BLayoutBuilder::Group<>(fInfoBox, B_VERTICAL, 2)
      .Add(fTitleMarquee)
      .Add(fArtistMarquee)
      .Add(fAlbumMarquee)
      .Add(fTechView)
      .AddGlue()
      .SetInsets(10, 15, 10, 10);

  fArtworkView = new ArtworkView("cover");
  fCoverPane = fArtworkView;

  BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
      .SetInsets(0, kTopSpacing, 0, 0)
      .Add(fInfoBox)
      .Add(fCoverPane);

  _ApplyColors();
}

NowPlayingInfoPanel::~NowPlayingInfoPanel() {}

void NowPlayingInfoPanel::AttachedToWindow() {
  BView::AttachedToWindow();
  _UpdatePanelVisibility();
}

void NowPlayingInfoPanel::SetInfoVisible(bool visible) {
  if (fInfoBox && fIsInfoVisible != visible) {
    fIsInfoVisible = visible;
    if (visible) {
      fInfoBox->Show();
    } else {
      fInfoBox->Hide();
    }
    InvalidateLayout();
    Invalidate();
    if (Parent())
      Parent()->InvalidateLayout();
  }
  _UpdatePanelVisibility();
}

void NowPlayingInfoPanel::SetCoverVisible(bool visible) {
  if (fCoverPane && fIsCoverVisible != visible) {
    fIsCoverVisible = visible;
    if (visible) {
      fCoverPane->Show();
    } else {
      fCoverPane->Hide();
    }
    InvalidateLayout();
    Invalidate();
    if (Parent())
      Parent()->InvalidateLayout();
  }
  _UpdatePanelVisibility();
}

void NowPlayingInfoPanel::_UpdatePanelVisibility() {
  bool shouldShow = fIsInfoVisible || fIsCoverVisible;
  if (shouldShow) {
    SetExplicitMinSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
    SetExplicitPreferredSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
    SetExplicitMaxSize(BSize(B_SIZE_UNSET, B_SIZE_UNSET));
  } else {
    SetExplicitSize(BSize(0.0f, 0.0f));
  }

  if (shouldShow && IsHidden())
    Show();
  else if (!shouldShow && !IsHidden())
    Hide();

  if (Parent()) {
    Parent()->InvalidateLayout(true);
    Parent()->Relayout();
  }
  if (Window()) {
    Window()->InvalidateLayout(true);
    Window()->Layout(true);
  }
}

void NowPlayingInfoPanel::_ApplyColors() {
  rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);
  rgb_color panelText = ui_color(B_PANEL_TEXT_COLOR);

  SetViewColor(panelBg);
  SetLowColor(panelBg);

  if (fInfoBox) {
    fInfoBox->SetViewColor(panelBg);
    fInfoBox->SetLowColor(panelBg);
    fInfoBox->Invalidate();
  }
  if (fTitleMarquee) {
    fTitleMarquee->SetViewColor(panelBg);
    fTitleMarquee->SetLowColor(panelBg);
    fTitleMarquee->SetHighColor(panelText);
    fTitleMarquee->Invalidate();
  }
  if (fArtistMarquee) {
    rgb_color dimmed = tint_color(panelText, B_LIGHTEN_1_TINT);
    fArtistMarquee->SetViewColor(panelBg);
    fArtistMarquee->SetLowColor(panelBg);
    fArtistMarquee->SetHighColor(dimmed);
    fArtistMarquee->Invalidate();
  }
  if (fAlbumMarquee) {
    rgb_color dimmed = tint_color(panelText, B_LIGHTEN_1_TINT);
    fAlbumMarquee->SetViewColor(panelBg);
    fAlbumMarquee->SetLowColor(panelBg);
    fAlbumMarquee->SetHighColor(dimmed);
    fAlbumMarquee->Invalidate();
  }

  if (fTechView) {
    rgb_color techColor = tint_color(panelText, B_LIGHTEN_2_TINT);
    fTechView->SetViewColor(panelBg);
    fTechView->SetLowColor(panelBg);
    fTechView->SetHighColor(techColor);
    fTechView->Invalidate();
  }

  if (fCoverPane)
    fCoverPane->Invalidate();

  Invalidate();
}

void NowPlayingInfoPanel::SetFileInfo(const BString &t) {
  if (fTitleMarquee)
    fTitleMarquee->SetText(t);
  if (fArtistMarquee)
    fArtistMarquee->SetText("");
  if (fAlbumMarquee)
    fAlbumMarquee->SetText("");
}

void NowPlayingInfoPanel::SetTags(const BString &artist, const BString &title,
                        const BString &album) {
  if (fTitleMarquee)
    fTitleMarquee->SetText(title.IsEmpty() ? BString(B_TRANSLATE("Unknown"))
                                           : title);
  if (fArtistMarquee)
    fArtistMarquee->SetText(artist.IsEmpty() ? BString(B_TRANSLATE("Unknown"))
                                             : artist);
  if (fAlbumMarquee)
    fAlbumMarquee->SetText(album.IsEmpty() ? BString(B_TRANSLATE("Unknown"))
                                           : album);
}

void NowPlayingInfoPanel::SetTechInfo(const BString &info) {
  if (fTechView)
    fTechView->SetText(info);
}

void NowPlayingInfoPanel::SetBoxLabel(const char *label) {
  if (fInfoBox)
    fInfoBox->SetLabel(label);
}

void NowPlayingInfoPanel::SetWordWrap(bool wrap) {
  /**
   * @brief Kept for API compatibility; word-wrapping is no longer used.
   */
}

/**
 * @brief Sets the cover image.
 * @param bmp The bitmap to display.
 */
void NowPlayingInfoPanel::SetCover(BBitmap *bmp) {
  if (fArtworkView)
    fArtworkView->SetBitmap(bmp);
}

void NowPlayingInfoPanel::ClearCover() {
  if (fArtworkView)
    fArtworkView->SetBitmap(nullptr);
}

void NowPlayingInfoPanel::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case B_COLORS_UPDATED: {
    _ApplyColors();
    break;
  }
  default:
    BView::MessageReceived(msg);
  }
}
