#include "ViewStateController.h"
#include "MainWindow.h"
#include "ArtworkController.h"
#include "NowPlayingInfoPanel.h"
#include "MetadataPropertiesWindow.h"
#include "AudioPlaybackEngine.h"
#include "LibraryBrowserController.h"
#include "PlaylistSelectionController.h"
#include "MediaTableView.h"
#include "Debug.h"
#include <MenuItem.h>
#include <Button.h>
#include <GroupView.h>
#include <Path.h>
#include <Entry.h>
#include <ToolTip.h>
#include <StringView.h>
#include <Catalog.h>
#include "IconButtonView.h"
#include "PlaybackQueueManager.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ViewStateController"

class CompactToolTipView : public BView {
public:
  CompactToolTipView(const char* text)
      : BView(BRect(0, 0, 0, 0), "tip_view", B_FOLLOW_ALL, B_WILL_DRAW),
        fText(text) {
    SetViewColor(ui_color(B_TOOL_TIP_BACKGROUND_COLOR));
    SetLowColor(ui_color(B_TOOL_TIP_BACKGROUND_COLOR));
    SetHighColor(ui_color(B_TOOL_TIP_TEXT_COLOR));
    SetFont(be_plain_font);

    font_height fh;
    be_plain_font->GetHeight(&fh);
    fAscent = fh.ascent;
    /**
     * @brief Tooltip content padding: 2px horizontal, 1px vertical.
     */
    float width = be_plain_font->StringWidth(fText.String()) + 4;
    float height = fh.ascent + fh.descent + 2;

    BSize exactSize(width, height);
    ResizeTo(exactSize.width, exactSize.height);
    SetExplicitMinSize(exactSize);
    SetExplicitMaxSize(exactSize);
    SetExplicitPreferredSize(exactSize);
  }

  virtual void Draw(BRect /*updateRect*/) override {
    DrawString(fText.String(), BPoint(2, fAscent + 1));
  }

  virtual void GetPreferredSize(float* width, float* height) override {
    font_height fh;
    be_plain_font->GetHeight(&fh);
    *width = be_plain_font->StringWidth(fText.String()) + 4;
    *height = fh.ascent + fh.descent + 2;
  }

  virtual BSize MinSize() override {
    float w, h;
    GetPreferredSize(&w, &h);
    return BSize(w, h);
  }

  virtual BSize MaxSize() override {
    return MinSize();
  }

  virtual BSize PreferredSize() override {
    return MinSize();
  }

private:
  BString fText;
  float fAscent;
};

class CompactToolTip : public BToolTip {
public:
  CompactToolTip(const char* text) : BToolTip(), fText(text) {}
  
  virtual BView* View() const override {
    return new CompactToolTipView(fText.String());
  }
private:
  BString fText;
};

ViewStateController::ViewStateController(MainWindow* window) : fWindow(window) {}
ViewStateController::~ViewStateController() {}

void ViewStateController::ToggleArtworkVisible() {
  if (fWindow->fArtworkController)
    fWindow->fArtworkController->ToggleArtworkVisible();
}

void ViewStateController::ToggleFileInfoVisible() {
  fWindow->fShowFileInfo = !fWindow->fShowFileInfo;
  if (fWindow->fViewInfoItem)
    fWindow->fViewInfoItem->SetMarked(fWindow->fShowFileInfo);
  if (fWindow->fNowPlayingInfoPanel)
    fWindow->fNowPlayingInfoPanel->SetInfoVisible(fWindow->fShowFileInfo);
}

void ViewStateController::ShowFileInfo() {
  fWindow->fShowFileInfo = true;
  if (fWindow->fViewInfoItem)
    fWindow->fViewInfoItem->SetMarked(true);
  if (fWindow->fNowPlayingInfoPanel)
    fWindow->fNowPlayingInfoPanel->SetInfoVisible(fWindow->fShowFileInfo);
}

void ViewStateController::ShowCoverArt() {
  if (fWindow->fArtworkController)
    fWindow->fArtworkController->ShowCoverArt();
}

void ViewStateController::ApplyDroppedSeekBarColor(BMessage *msg) {
  rgb_color *color;
  ssize_t size;
  if (msg->FindData("color", B_RGB_COLOR_TYPE, (const void **)&color, &size) ==
          B_OK &&
      size == sizeof(rgb_color)) {
    fWindow->fSeekBarColor = *color;
    fWindow->fUseCustomSeekBarColor = true;
    fWindow->ApplyColors();
    fWindow->SaveSettings();
  }
}

void ViewStateController::UseSystemSelectionColor() {
  fWindow->fUseSeekBarColorForSelection = false;
  fWindow->fUseCustomSeekBarColor = false;
  if (fWindow->fSelColorSystemItem)
    fWindow->fSelColorSystemItem->SetMarked(true);
  if (fWindow->fSelColorMatchItem)
    fWindow->fSelColorMatchItem->SetMarked(false);
  fWindow->ApplyColors();
  fWindow->SaveSettings();
}

void ViewStateController::UseSeekBarSelectionColor() {
  fWindow->fUseSeekBarColorForSelection = true;
  if (fWindow->fSelColorSystemItem)
    fWindow->fSelColorSystemItem->SetMarked(false);
  if (fWindow->fSelColorMatchItem)
    fWindow->fSelColorMatchItem->SetMarked(true);
  fWindow->ApplyColors();
  fWindow->SaveSettings();
}

void ViewStateController::SetTooltipsEnabled(bool enabled) {
  fWindow->fShowTooltips = enabled;
  if (fWindow->fTooltipsOnItem)
    fWindow->fTooltipsOnItem->SetMarked(enabled);
  if (fWindow->fTooltipsOffItem)
    fWindow->fTooltipsOffItem->SetMarked(!enabled);
    
  UpdateTooltips();
  fWindow->SaveSettings();
}

void ViewStateController::SetFastEditEnabled(bool enabled) {
  fWindow->fFastEditEnabled = enabled;
  if (fWindow->fFastEditItem)
    fWindow->fFastEditItem->SetMarked(enabled);

  if (fWindow->fLibraryManager && fWindow->fLibraryManager->ContentView()) {
    MediaTableView *cv = fWindow->fLibraryManager->ContentView();
    if (!enabled)
      cv->CommitCellEdit();
    cv->SetFastEditEnabled(enabled);
  }
}

void ViewStateController::UpdateTooltips() {
  if (!fWindow->fShowTooltips) {
    if (fWindow->fBtnPrev) fWindow->fBtnPrev->SetToolTip((BToolTip*)nullptr);
    if (fWindow->fBtnPlayPause) fWindow->fBtnPlayPause->SetToolTip((BToolTip*)nullptr);
    if (fWindow->fBtnStop) fWindow->fBtnStop->SetToolTip((BToolTip*)nullptr);
    if (fWindow->fBtnNext) fWindow->fBtnNext->SetToolTip((BToolTip*)nullptr);
    if (fWindow->fBtnShuffle) fWindow->fBtnShuffle->SetToolTip((BToolTip*)nullptr);
    if (fWindow->fBtnRepeat) fWindow->fBtnRepeat->SetToolTip((BToolTip*)nullptr);
    if (fWindow->fBtnMute) fWindow->fBtnMute->SetToolTip((BToolTip*)nullptr);
    return;
  }

  auto setTip = [](BView* view, const char* text) {
    if (view) view->SetToolTip(new CompactToolTip(text));
  };

  setTip(fWindow->fBtnPrev, B_TRANSLATE("Previous"));
  setTip(fWindow->fBtnNext, B_TRANSLATE("Next"));
  setTip(fWindow->fBtnStop, B_TRANSLATE("Stop"));

  if (fWindow->fPlaybackEngine && fWindow->fPlaybackEngine->IsPlaying()) {
    setTip(fWindow->fBtnPlayPause, B_TRANSLATE("Pause"));
  } else {
    setTip(fWindow->fBtnPlayPause, B_TRANSLATE("Play"));
  }

  if (fWindow->fPlaybackQueueManager) {
    if (fWindow->fPlaybackQueueManager->ShuffleEnabled()) {
      setTip(fWindow->fBtnShuffle, B_TRANSLATE("Shuffle: On"));
    } else {
      setTip(fWindow->fBtnShuffle, B_TRANSLATE("Shuffle: Off"));
    }

    int32 repeatMode = fWindow->fPlaybackQueueManager->RepeatModeValue();
    if (repeatMode == 1) {
      setTip(fWindow->fBtnRepeat, B_TRANSLATE("Repeat: All"));
    } else if (repeatMode == 2) {
      setTip(fWindow->fBtnRepeat, B_TRANSLATE("Repeat: One"));
    } else {
      setTip(fWindow->fBtnRepeat, B_TRANSLATE("Repeat: Off"));
    }
  }

  if (fWindow->fIsMuted) {
    setTip(fWindow->fBtnMute, B_TRANSLATE("Unmute"));
  } else {
    setTip(fWindow->fBtnMute, B_TRANSLATE("Mute"));
  }
}

void ViewStateController::ToggleFilters() {
  bool newState = false;
  if (fWindow->fIsLibraryMode) {
    fWindow->fShowFiltersLibrary = !fWindow->fShowFiltersLibrary;
    newState = fWindow->fShowFiltersLibrary;
  } else if (fWindow->fIsRadioMode) {
    fWindow->fShowFiltersRadio = !fWindow->fShowFiltersRadio;
    newState = fWindow->fShowFiltersRadio;
  } else if (fWindow->fIsDlnaMode) {
    fWindow->fShowFiltersDlna = !fWindow->fShowFiltersDlna;
    newState = fWindow->fShowFiltersDlna;
  } else {
    fWindow->fShowFiltersPlaylist = !fWindow->fShowFiltersPlaylist;
    newState = fWindow->fShowFiltersPlaylist;
  }

  if (fWindow->fViewFiltersItem)
    fWindow->fViewFiltersItem->SetMarked(newState);

  if (fWindow->fFilterGroup) {
    if (newState) {
      if (!fWindow->fIsFilterGroupVisible) {
        fWindow->fIsFilterGroupVisible = true;
        fWindow->fFilterGroup->Show();
      }
    } else {
      if (fWindow->fPlaylistSelectionController)
        fWindow->fPlaylistSelectionController->ResetAndHideFilters();
    }
  }
}

void ViewStateController::HandleColorsUpdated() {
  if (!fWindow->fUseCustomSeekBarColor)
    fWindow->fSeekBarColor = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
  if (!fWindow->fUseSeekBarColorForSelection)
    fWindow->fSelectionColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
  fWindow->ApplyColors();
  if (fWindow->fNowPlayingInfoPanel) {
    BMessage colors(B_COLORS_UPDATED);
    fWindow->fNowPlayingInfoPanel->MessageReceived(&colors);
  }
  if (fWindow->fMetadataPropertiesWindow) {
    BMessage colors(B_COLORS_UPDATED);
    fWindow->fMetadataPropertiesWindow->PostMessage(&colors);
  }
}

void ViewStateController::HandleContentSelectionChanged() {
  if (fWindow->fPlaybackEngine && (fWindow->fPlaybackEngine->IsPlaying() || fWindow->fPlaybackEngine->IsPaused()))
    return;

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  int32 rowIndex = cv->IndexOf(cv->CurrentSelection());
  if (rowIndex < 0)
    return;

  const MediaItem *mi = cv->SelectedItem();
  if (!mi)
    return;

  fWindow->UpdateFileInfo();

  if (mi->path.IsEmpty()) {
    if (fWindow->fNowPlayingInfoPanel)
      fWindow->fNowPlayingInfoPanel->ClearCover();
    return;
  }

  if (fWindow->fLastSelectedPath == mi->path)
    return;

  fWindow->fLastSelectedPath = mi->path;

  if (fWindow->fNowPlayingInfoPanel)
    fWindow->fNowPlayingInfoPanel->ClearCover();

  if (fWindow->fArtworkController)
    fWindow->fArtworkController->FetchEmbeddedCoverBitmap(mi->path);
}
