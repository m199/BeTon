#include "InfoPanel.h"
#include "CoverView.h"

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "InfoPanel"

#include <CardLayout.h>
#include <LayoutBuilder.h>
#include <StringView.h>

/**
 * @brief Constructs the InfoPanel.
 *
 * Creates a two-card layout:
 * 1. Text Info Pane: Displays textual metadata (Artist, Album, Title, etc.) in
 * a BBox.
 * 2. Cover Pane: Displays the album art in a CoverView within a BBox.
 *
 * Uses BCardLayout to switch between these two views.
 */
InfoPanel::InfoPanel() : BView("InfoPanel", B_WILL_DRAW) {
  SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  // Calculate font-relative sizes
  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  fInfoText = new BStringView(
      "info", B_TRANSLATE("Artist:\nAlbum:\nTitle:\nYear:\nGenre:"
                          "\n\nBitrate:\nSample Rate:\nChannels:"));
  fInfoText->SetTruncation(B_TRUNCATE_END);
  fInfoText->SetExplicitMinSize(BSize(0, B_SIZE_UNSET));
  fInfoText->SetExplicitPreferredSize(BSize(0, B_SIZE_UNSET));
  // Allow growing horizontally
  fInfoText->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

  fInfoBox = new BBox("infoBox");
  fInfoBox->SetLabel(B_TRANSLATE("File Information"));
  fInfoBox->SetBorder(B_FANCY_BORDER);

  fInfoBox->SetExplicitMinSize(BSize(fontHeight * 13, B_SIZE_UNSET));
  fInfoBox->SetExplicitPreferredSize(BSize(fontHeight * 17, B_SIZE_UNSET));

  BLayoutBuilder::Group<>(fInfoBox, B_VERTICAL, 0)
      .Add(fInfoText)
      .SetInsets(10, 15, 10, 10);

  fCoverView = new CoverView("cover");

  BBox *coverBox = new BBox("coverBox");
  coverBox->SetBorder(B_NO_BORDER);
  coverBox->SetLabel((const char *)nullptr);

  BLayoutBuilder::Group<>(coverBox, B_VERTICAL, 0)
      .Add(fCoverView)
      .SetInsets(0, 0, 0, 0);

  fCoverPane = coverBox;
  fCoverPane->SetExplicitMinSize(BSize(fontHeight * 13, fontHeight * 13));
  fCoverPane->SetExplicitPreferredSize(BSize(fontHeight * 17, fontHeight * 17));
  fCoverPane->SetExplicitMaxSize(BSize(fontHeight * 17, fontHeight * 17));

  BView *cardHost = new BView("cardHost", 0);
  cardHost->SetViewColor(B_TRANSPARENT_COLOR);

  fCards = new BCardLayout();
  cardHost->SetLayout(fCards);
  fCards->AddView(fInfoBox);
  fCards->AddView(fCoverPane);
  fCards->SetVisibleItem((int32)0);

  BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
      .SetInsets(0, 0, 0, 0)
      .Add(cardHost);
}

InfoPanel::~InfoPanel() {}

/**
 * @brief Switches the displayed view (Info Text vs Cover Art).
 * @param mode The desired mode (Info or Cover).
 */
void InfoPanel::Switch(Mode mode) {
  if (fMode == mode)
    return;
  fMode = mode;
  fCards->SetVisibleItem((int)mode);
  Invalidate();
}

InfoPanel::Mode InfoPanel::GetMode() const { return fMode; }

/**
 * @brief Updates the text in the Info Pane.
 * @param t New text to display.
 */
void InfoPanel::SetFileInfo(const BString &t) {
  if (fInfoText)
    fInfoText->SetText(t);
}

/**
 * @brief Sets the cover image and automatically switches to Cover Pane.
 * @param bmp The bitmap to display.
 */
void InfoPanel::SetCover(BBitmap *bmp) {
  if (fCoverView)
    fCoverView->SetBitmap(bmp);

  if (fMode != Cover)
    Switch(Cover);
}

void InfoPanel::ClearCover() {
  if (fCoverView)
    fCoverView->SetBitmap(nullptr);

  if (fMode != Info)
    Switch(Info);
}

void InfoPanel::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case B_COLORS_UPDATED:
    SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    if (fInfoText)
      fInfoText->SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
    Invalidate();
    break;
  default:
    BView::MessageReceived(msg);
  }
}
