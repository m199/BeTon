#include "PropertiesWindow.h"
#include "CoverView.h"
#include "Debug.h"
#include "Messages.h"
#include "TagSync.h"

#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <DataIO.h>
#include <File.h>
#include <FilePanel.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StringItem.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <new>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PropertiesWindow"

/**
 * @brief Constructor for editing a single file, taking a file path string.
 * @param filePath The absolute path to the file.
 * @param target The messenger to which updates are sent.
 */
PropertiesWindow::PropertiesWindow(const BString &filePath,
                                   const BMessenger &target)
    : PropertiesWindow(BRect(100, 100, 940, 680), BPath(filePath.String()),
                       target) {
  fFiles.push_back(BPath(filePath.String()));
  fCurrentIndex = 0;
}

/**
 * @brief Constructor for editing a single file, taking a BPath.
 * @param filePath The BPath to the file.
 * @param target The messenger to which updates are sent.
 */
PropertiesWindow::PropertiesWindow(const BPath &filePath,
                                   const BMessenger &target)
    : PropertiesWindow(BRect(100, 100, 940, 680), filePath, target) {
  fFiles.push_back(filePath);
  fCurrentIndex = 0;
}

/**
 * @brief Main constructor implementation for single file mode.
 * @param frame The initial window frame.
 * @param filePath The BPath to the file.
 * @param target The messenger to which updates are sent.
 */
PropertiesWindow::PropertiesWindow(BRect frame, const BPath &filePath,
                                   const BMessenger &target)
    : BWindow(frame, B_TRANSLATE("Properties"), B_TITLED_WINDOW, 0),
      fFilePath(filePath), fTarget(target) {
  fIsMulti = false;
  _BuildUI();
  ResizeToPreferred();

  BRect bounds = Bounds();
  SetSizeLimits(bounds.Width(), 10000, bounds.Height(), 10000);

  CenterOnScreen();
  SetTitle(BString(B_TRANSLATE("Properties - ")) << filePath.Leaf());
  Show();
}

/**
 * @brief Constructor for editing multiple files.
 * @param filePaths A vector of BPaths to the files.
 * @param target The messenger to which updates are sent.
 */
PropertiesWindow::PropertiesWindow(const std::vector<BPath> &filePaths,
                                   const BMessenger &target)
    : PropertiesWindow(BRect(100, 100, 620, 800), filePaths, target) {}

/**
 * @brief Main constructor implementation for multi-file mode.
 * @param frame The initial window frame.
 * @param filePaths A vector of BPaths to the files.
 * @param target The messenger to which updates are sent.
 */
PropertiesWindow::PropertiesWindow(BRect frame,
                                   const std::vector<BPath> &filePaths,
                                   const BMessenger &target)
    : BWindow(frame, B_TRANSLATE("Properties"), B_TITLED_WINDOW, 0),
      fTarget(target) {
  fIsMulti = true;
  fFiles = filePaths;
  fCurrentIndex = 0;
  if (!fFiles.empty())
    fFilePath = fFiles.front();
  _BuildUI();
  ResizeToPreferred();

  BRect bounds = Bounds();
  SetSizeLimits(bounds.Width(), 10000, bounds.Height(), 10000);

  CenterOnScreen();
  BString t(B_TRANSLATE("Properties - "));
  t << fFiles.size() << B_TRANSLATE(" Files");
  SetTitle(t);
  Show();
}

/**
 * @brief Constructor for editing multiple files with an initial index.
 *
 * Allows navigating through the list of files in single-file editing mode.
 *
 * @param filePaths A vector of BPaths to the files.
 * @param initialIndex The index of the file to start editing.
 * @param target The messenger to which updates are sent.
 */
PropertiesWindow::PropertiesWindow(const std::vector<BPath> &filePaths,
                                   int32 initialIndex, const BMessenger &target)
    : BWindow(BRect(0, 0, 500, 600), B_TRANSLATE("Properties"), B_TITLED_WINDOW,
              0),
      fTarget(target) {

  fFiles = filePaths;
  fIsMulti = false;
  fCurrentIndex = initialIndex;

  if (fCurrentIndex >= 0 && fCurrentIndex < (int32)fFiles.size()) {
    fFilePath = fFiles[fCurrentIndex];
  } else {
    fCurrentIndex = 0;
    if (!fFiles.empty())
      fFilePath = fFiles[0];
  }

  _BuildUI();

  ResizeToPreferred();

  BRect bounds = Bounds();
  SetSizeLimits(bounds.Width(), 10000, bounds.Height(), 10000);

  CenterOnScreen();
  SetTitle(BString(B_TRANSLATE("Properties - ")) << fFilePath.Leaf());

  fBtnPrev->SetEnabled(fCurrentIndex > 0);
  fBtnNext->SetEnabled(fCurrentIndex < (int32)fFiles.size() - 1);

  Show();
}

PropertiesWindow::~PropertiesWindow() {
  delete fOpenPanel;
  fOpenPanel = nullptr;
}

/**
 * @brief Determines the state of a string field across multiple files.
 * @param vals Vector of string values for the field from all selected files.
 * @param outCommon Output parameter to store the common value if all are same.
 * @return FieldState: AllSame, AllEmpty, or Mixed.
 */
PropertiesWindow::FieldState
PropertiesWindow::_StateForStrings(const std::vector<BString> &vals,
                                   BString &outCommon) {
  if (vals.empty()) {
    outCommon.Truncate(0);
    return FieldState::AllEmpty;
  }
  const BString &first = vals.front();
  bool allSame = true;
  for (size_t i = 1; i < vals.size(); ++i) {
    if (vals[i] != first)
      allSame = false;
  }
  if (allSame) {
    outCommon = first;
    return first.IsEmpty() ? FieldState::AllEmpty : FieldState::AllSame;
  }
  return FieldState::Mixed;
}

/**
 * @brief Determines the state of an integer field across multiple files.
 * @param vals Vector of integer values for the field from all selected files.
 * @param outCommon Output parameter to store the common value if all are same.
 * @return FieldState: AllSame, AllEmpty, or Mixed.
 */
PropertiesWindow::FieldState
PropertiesWindow::_StateForInts(const std::vector<uint32> &vals,
                                uint32 &outCommon) {
  if (vals.empty()) {
    outCommon = 0;
    return FieldState::AllEmpty;
  }
  uint32 first = vals.front();
  bool allSame = true;
  bool anyNonZero = (first != 0);
  for (size_t i = 1; i < vals.size(); ++i) {
    if (vals[i] != first)
      allSame = false;
    if (vals[i] != 0)
      anyNonZero = true;
  }
  if (allSame) {
    outCommon = first;
    return anyNonZero ? FieldState::AllSame : FieldState::AllEmpty;
  }
  return FieldState::Mixed;
}

/**
 * @brief Constructs the main UI layout, including tabs and buttons.
 */
void PropertiesWindow::_BuildUI() {
  SetLayout(new BGroupLayout(B_VERTICAL));

  fTabs = new BTabView("propsTabs", B_WIDTH_FROM_LABEL);

  auto *tagsPage = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
  auto *coverPage = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
  auto *mbPage = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);

  tagsPage->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  coverPage->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  mbPage->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  fTabs->AddTab(tagsPage);
  fTabs->TabAt(0)->SetLabel(B_TRANSLATE("Details"));
  fTabs->AddTab(coverPage);
  fTabs->TabAt(1)->SetLabel(B_TRANSLATE("Artwork"));
  fTabs->AddTab(mbPage);
  fTabs->TabAt(2)->SetLabel(B_TRANSLATE("MusicBrainz"));

  _BuildTab_Tags(tagsPage);
  _BuildTab_Cover(coverPage);
  _BuildTab_MB(mbPage);

  fBtnApply = new BButton("Übernehmen", B_TRANSLATE("Apply"),
                          new BMessage(MSG_PROP_APPLY));
  fBtnSave = new BButton("Speichern", B_TRANSLATE("Save"),
                         new BMessage(MSG_PROP_SAVE));
  fBtnCancel = new BButton("Abbrechen", B_TRANSLATE("Cancel"),
                           new BMessage(MSG_PROP_CANCEL));

  fBtnPrev = new BButton("Prev", B_TRANSLATE("◀ Previous"),
                         new BMessage(MSG_PREV_FILE));
  fBtnNext =
      new BButton("Next", B_TRANSLATE("Next ▶"), new BMessage(MSG_NEXT_FILE));

  if (fFiles.size() <= 1) {
    fBtnPrev->SetEnabled(false);
    fBtnNext->SetEnabled(false);
  } else {
    fBtnPrev->SetEnabled(false);
  }

  BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
      .SetInsets(B_USE_WINDOW_INSETS)
      .Add(fTabs, 1.0f)
      .AddStrut(B_USE_DEFAULT_SPACING)
      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .AddGlue()
      .Add(fBtnPrev)
      .Add(fBtnNext)
      .AddStrut(B_USE_DEFAULT_SPACING)
      .Add(fBtnApply)
      .Add(fBtnSave)
      .Add(fBtnCancel)
      .End();

  fTabs->Select(0);

  if (fIsMulti)
    _LoadInitialDataMulti();
  else
    _LoadInitialData();
}

/**
 * @brief Builds the 'Details' tab for editing metadata tags.
 * @param parent The parent view for the tab content.
 */
void PropertiesWindow::_BuildTab_Tags(BView *parent) {

  auto *root = new BView("detailsRoot", B_WILL_DRAW);
  root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  float coverDim = std::max(128.0f, fontHeight * 8.0f);

  fCoverView = new CoverView("propCover");
  fCoverView->SetExplicitMinSize(BSize(coverDim, coverDim));
  fCoverView->SetExplicitMaxSize(BSize(coverDim, coverDim));

  fHdrTitle = new BStringView(nullptr, "");
  fHdrSub1 = new BStringView(nullptr, "");
  fHdrSub2 = new BStringView(nullptr, "");

  BFont big(*be_plain_font);
  big.SetSize(be_plain_font->Size() * 1.25f);
  big.SetFace(B_BOLD_FACE);
  BFont mid(*be_plain_font);
  mid.SetSize(be_plain_font->Size() * 1.05f);

  fHdrTitle->SetFont(&big);
  fHdrSub1->SetFont(&mid);
  fHdrSub2->SetFont(&mid);

  fEdTitle = new BTextControl(nullptr, "", nullptr);
  fEdArtist = new BTextControl(nullptr, "", nullptr);
  fEdAlbum = new BTextControl(nullptr, "", nullptr);
  fEdAlbumArtist = new BTextControl(nullptr, "", nullptr);
  fEdComposer = new BTextControl(nullptr, "", nullptr);
  fEdGenre = new BTextControl(nullptr, "", nullptr);
  fEdYear = new BTextControl(nullptr, "", nullptr);
  fEdTrack = new BTextControl(nullptr, "", nullptr);
  fEdTrackTotal = new BTextControl(nullptr, "", nullptr);
  fEdDisc = new BTextControl(nullptr, "", nullptr);
  fEdDiscTotal = new BTextControl(nullptr, "", nullptr);
  fEdComment = new BTextControl(nullptr, "", nullptr);
  fEdMBTrackID = new BTextControl(nullptr, "", nullptr);
  fEdMBAlbumID = new BTextControl(nullptr, "", nullptr);

  float fourDigits = std::ceil(be_plain_font->StringWidth("88888")) + 40.0f;
  auto setSmall = [&](BTextControl *c) {
    c->SetExplicitMinSize(BSize(fourDigits, B_SIZE_UNSET));
    c->SetExplicitMaxSize(BSize(fourDigits, B_SIZE_UNSET));
  };
  setSmall(fEdYear);
  setSmall(fEdTrack);
  setSmall(fEdTrackTotal);
  setSmall(fEdDisc);
  setSmall(fEdDiscTotal);

  BLayoutBuilder::Group<>(root, B_VERTICAL, B_USE_DEFAULT_SPACING)
      .SetInsets(B_USE_WINDOW_INSETS)

      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .Add(fCoverView)
      .AddGroup(B_VERTICAL, 0)
      .SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP))
      .Add(fHdrTitle)
      .Add(fHdrSub1)
      .Add(fHdrSub2)
      .AddGlue()
      .End()
      .AddGlue()
      .End()
      .AddStrut(B_USE_DEFAULT_SPACING)

      .AddGrid(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
      .SetColumnWeight(0, 0.0f)
      .SetColumnWeight(1, 10.0f)

      .Add(new BStringView(nullptr, B_TRANSLATE("Title:")), 0, 0)
      .Add(fEdTitle, 1, 0)

      .Add(new BStringView(nullptr, B_TRANSLATE("Artist:")), 0, 1)
      .Add(fEdArtist, 1, 1)

      .Add(new BStringView(nullptr, B_TRANSLATE("Album:")), 0, 2)
      .Add(fEdAlbum, 1, 2)

      .Add(new BStringView(nullptr, B_TRANSLATE("Album Artist:")), 0, 3)
      .Add(fEdAlbumArtist, 1, 3)

      .Add(new BStringView(nullptr, B_TRANSLATE("Composer:")), 0, 4)
      .Add(fEdComposer, 1, 4)

      .Add(new BStringView(nullptr, B_TRANSLATE("Genre:")), 0, 5)
      .Add(fEdGenre, 1, 5)

      .Add(new BStringView(nullptr, B_TRANSLATE("Year:")), 0, 6)
      .AddGroup(B_HORIZONTAL, 0, 1, 6)
      .Add(fEdYear)
      .AddGlue()
      .End()

      .Add(new BStringView(nullptr, B_TRANSLATE("Track:")), 0, 7)
      .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 1, 7)
      .Add(fEdTrack)
      .Add(new BStringView(nullptr, B_TRANSLATE("of")))
      .Add(fEdTrackTotal)
      .AddGlue()
      .End()

      .Add(new BStringView(nullptr, B_TRANSLATE("Disc:")), 0, 8)
      .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING, 1, 8)
      .Add(fEdDisc)
      .Add(new BStringView(nullptr, B_TRANSLATE("of")))
      .Add(fEdDiscTotal)
      .AddGlue()
      .End()

      .Add(new BStringView(nullptr, B_TRANSLATE("Comment:")), 0, 9)
      .Add(fEdComment, 1, 9)

      .Add(new BStringView(nullptr, B_TRANSLATE("MB Track ID:")), 0, 10)
      .Add(fEdMBTrackID, 1, 10)

      .Add(new BStringView(nullptr, B_TRANSLATE("MB Album ID:")), 0, 11)
      .Add(fEdMBAlbumID, 1, 11)
      .End();

  BGroupLayout *parentLayout =
      dynamic_cast<BGroupLayout *>(parent->GetLayout());
  if (!parentLayout) {
    parentLayout = new BGroupLayout(B_VERTICAL, 0);
    parent->SetLayout(parentLayout);
  }
  parentLayout->AddView(root);
}

/**
 * @brief Builds the 'Artwork' tab for managing cover art.
 * @param parent The parent view for the tab content.
 */
void PropertiesWindow::_BuildTab_Cover(BView *parent) {
  auto *root = new BView("coverRoot", B_WILL_DRAW);
  root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  auto *gl = new BGroupLayout(B_VERTICAL);
  root->SetLayout(gl);

  fBtnCoverLoad = new BButton("CoverLoad", B_TRANSLATE("Load Cover..."),
                              new BMessage(MSG_COVER_LOAD));
  fBtnCoverClr = new BButton("CoverClr", B_TRANSLATE("Remove Cover"),
                             new BMessage(MSG_COVER_CLEAR));

  fBtnCoverApplyAlbum =
      new BButton("CoverApplyAlbum", B_TRANSLATE("Add to Album"),
                  new BMessage(MSG_COVER_APPLY_ALBUM));
  fBtnCoverClearAlbum =
      new BButton("CoverClearAlbum", B_TRANSLATE("Remove from Album"),
                  new BMessage(MSG_COVER_CLEAR_ALBUM));

  fBtnCoverFromMB =
      new BButton("CoverFromMB", B_TRANSLATE("Fetch from MusicBrainz"),
                  new BMessage(MSG_COVER_FETCH_MB));

  gl->SetInsets(B_USE_WINDOW_INSETS);
  gl->AddView(new BStringView(
      nullptr,
      fIsMulti
          ? B_TRANSLATE(
                "Manage Cover (Multi-selection: Drop image to set for all)")
          : B_TRANSLATE("Manage Cover")));

  auto *row1 = new BView(nullptr, B_WILL_DRAW);
  row1->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  auto *row1gl = new BGroupLayout(B_HORIZONTAL);
  row1->SetLayout(row1gl);
  row1gl->AddView(fBtnCoverLoad);
  row1gl->AddView(fBtnCoverClr);
  row1gl->AddItem(BSpaceLayoutItem::CreateGlue());
  gl->AddView(row1);

  gl->AddItem(BSpaceLayoutItem::CreateVerticalStrut(B_USE_DEFAULT_SPACING));
  gl->AddView(new BStringView(nullptr, B_TRANSLATE("Album Functions")));

  auto *row2 = new BView(nullptr, B_WILL_DRAW);
  row2->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  auto *row2gl = new BGroupLayout(B_HORIZONTAL);
  row2->SetLayout(row2gl);
  row2gl->AddView(fBtnCoverApplyAlbum);
  row2gl->AddView(fBtnCoverClearAlbum);
  row2gl->AddItem(BSpaceLayoutItem::CreateGlue());
  gl->AddView(row2);

  (void)fBtnCoverFromMB;

  gl->AddItem(BSpaceLayoutItem::CreateGlue());

  if (auto *pg = dynamic_cast<BGroupLayout *>(parent->GetLayout()))
    pg->AddView(root);
  else
    parent->AddChild(root);
}

/**
 * @brief Builds the 'MusicBrainz' tab for online metadata search.
 * @param parent The parent view for the tab content.
 */
void PropertiesWindow::_BuildTab_MB(BView *parent) {
  auto *root = new BView("mbRoot", B_WILL_DRAW);
  root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  auto *gl = new BGroupLayout(B_VERTICAL);
  root->SetLayout(gl);

  fMbSearchArtist = new BTextControl("Artist:", B_TRANSLATE("Artist:"), "",
                                     new BMessage(MSG_MB_SEARCH));
  fMbSearchAlbum = new BTextControl("Album:", B_TRANSLATE("Album:"), "",
                                    new BMessage(MSG_MB_SEARCH));
  fMbSearchTitle = new BTextControl("Titel:", B_TRANSLATE("Title:"), "",
                                    new BMessage(MSG_MB_SEARCH));

  fMbSearch =
      new BButton("Suchen", B_TRANSLATE("Search"), new BMessage(MSG_MB_SEARCH));
  fMbCancel = new BButton("Abbrechen", B_TRANSLATE("Cancel"),
                          new BMessage(MSG_MB_CANCEL));
  fMbCancel->SetEnabled(false);

  fMbStatusView = new BStringView("mbStatus", B_TRANSLATE("Ready."));

  fMbResults = new BListView("mbResults");
  fMbApplyTrack =
      new BButton("ApplyTrack", B_TRANSLATE("Apply Selection (Track)"),
                  new BMessage(MSG_MB_APPLY));
  fMbApplyAlbum =
      new BButton("ApplyAlbum", B_TRANSLATE("Apply Selection (Album)"),
                  new BMessage(MSG_MB_APPLY_ALBUM));

  auto *resultsScroll =
      new BScrollView("mbResultsScroll", fMbResults, 0, true, true);

  gl->SetInsets(B_USE_WINDOW_INSETS);

  auto *form = new BGridView();
  auto *grid = form->GridLayout();
  grid->SetSpacing(5.0f, 5.0f);

  int32 r = 0;
  auto mkRow = [&](BTextControl *tc) {
    grid->AddItem(tc->CreateLabelLayoutItem(), 0, r);
    grid->AddItem(tc->CreateTextViewLayoutItem(), 1, r);
    r++;
  };
  mkRow(fMbSearchArtist);
  mkRow(fMbSearchAlbum);
  mkRow(fMbSearchTitle);

  {
    auto *container = new BView("mbButtons", B_WILL_DRAW);
    container->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    auto *sub = new BGroupLayout(B_HORIZONTAL);
    container->SetLayout(sub);

    sub->AddView(fMbSearch);
    sub->AddView(fMbCancel);
    sub->AddView(fMbStatusView);
    sub->AddItem(BSpaceLayoutItem::CreateGlue());

    grid->AddView(container, 0, r++, 2, 1);
  }
  gl->AddView(form);

  gl->AddView(resultsScroll, 1.0f);

  auto *brow = new BView(nullptr, B_WILL_DRAW);
  brow->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  auto *bgl = new BGroupLayout(B_HORIZONTAL);
  brow->SetLayout(bgl);
  bgl->AddView(fMbApplyTrack);
  bgl->AddView(fMbApplyAlbum);
  bgl->AddItem(BSpaceLayoutItem::CreateGlue());
  gl->AddView(brow);

  if (auto *pg = dynamic_cast<BGroupLayout *>(parent->GetLayout()))
    pg->AddView(root);
  else
    parent->AddChild(root);
}

/**
 * @brief Handles messages sent to the window.
 * @param msg The message to be processed.
 */
void PropertiesWindow::MessageReceived(BMessage *msg) {

  if (msg->what == MSG_MB_RESULTS) {
    printf("[PropertiesWindow] MessageReceived: MSG_MB_RESULTS detected!\n");
  }

  switch (msg->what) {
  case MSG_PROP_APPLY:
    _SendApply(false);
    break;
  case MSG_PROP_SAVE:
    _SendApply(true);
    break;
  case MSG_PROP_CANCEL:
    Quit();
    break;

  case MSG_MB_CANCEL:
    if (fMbCancel)
      fMbCancel->SetEnabled(false);
    if (fMbStatusView)
      fMbStatusView->SetText(B_TRANSLATE("Cancelled."));
    _SendMessageToTarget(MSG_MB_CANCEL, new BMessage(MSG_MB_CANCEL));
    break;

  case MSG_PREV_FILE:
    if (fCurrentIndex > 0) {
      _LoadFileAtIndex(fCurrentIndex - 1);
    }
    break;

  case MSG_NEXT_FILE:
    if (fCurrentIndex < (int32)fFiles.size() - 1) {
      _LoadFileAtIndex(fCurrentIndex + 1);
    }
    break;

  case MSG_COVER_LOAD:
    _OpenCoverPanel();
    break;

  case MSG_COVER_CLEAR: {

    if (!fIsMulti) {
      auto *payload = new BMessage(MSG_COVER_CLEAR);
      payload->AddString("file", fFilePath.Path());
      _SendMessageToTarget(MSG_COVER_CLEAR, payload);
    } else {

      auto *payload = new BMessage(MSG_COVER_CLEAR);
      for (auto &p : fFiles)
        payload->AddString("file", p.Path());
      _SendMessageToTarget(MSG_COVER_CLEAR, payload);
    }
    if (fCoverView)
      fCoverView->SetBitmap(nullptr);
    fCoverMixed = false;
    fCurrentCoverBytes.clear();
    break;
  }

  case MSG_COVER_APPLY_ALBUM: {
    if (fCurrentCoverBytes.empty()) {

      break;
    }
    auto *payload = new BMessage(MSG_COVER_APPLY_ALBUM);
    if (!fIsMulti)
      payload->AddString("file", fFilePath.Path());
    else if (!fFiles.empty())
      payload->AddString("file", fFiles.front().Path());

    payload->AddData("bytes", B_RAW_TYPE, fCurrentCoverBytes.data(),
                     fCurrentCoverBytes.size());

    _SendMessageToTarget(MSG_COVER_APPLY_ALBUM, payload);
    break;
  }

  case MSG_COVER_CLEAR_ALBUM: {
    auto *payload = new BMessage(MSG_COVER_CLEAR_ALBUM);
    if (!fIsMulti)
      payload->AddString("file", fFilePath.Path());
    else if (!fFiles.empty())
      payload->AddString("file", fFiles.front().Path());

    _SendMessageToTarget(MSG_COVER_CLEAR_ALBUM, payload);
    break;
  }

  case B_REFS_RECEIVED: {
    entry_ref ref;
    if (msg->FindRef("refs", 0, &ref) == B_OK)
      _HandleCoverChosen(ref);
    break;
  }

  case B_SIMPLE_DATA: {
    entry_ref ref;
    if (msg->FindRef("refs", 0, &ref) == B_OK) {

      _HandleCoverChosen(ref);

      BFile f(&ref, B_READ_ONLY);
      off_t sz = 0;
      if (f.InitCheck() == B_OK && f.GetSize(&sz) == B_OK && sz > 0) {
        std::unique_ptr<uint8[]> buf(new (std::nothrow) uint8[(size_t)sz]);
        if (buf) {
          ssize_t rd = f.Read(buf.get(), (size_t)sz);
          if (rd > 0) {
            auto *out = new BMessage(MSG_COVER_DROPPED_APPLY_ALL);
            out->AddData("bytes", B_RAW_TYPE, buf.get(), (size_t)rd);
            if (!fIsMulti)
              out->AddString("file", fFilePath.Path());
            else
              for (auto &p : fFiles)
                out->AddString("file", p.Path());
            _SendMessageToTarget(MSG_COVER_DROPPED_APPLY_ALL, out);
          }
        }
      }
    }
    break;
  }

  case MSG_COVER_FETCH_MB: {
    auto *payload = new BMessage(MSG_COVER_FETCH_MB);
    if (!fIsMulti)
      payload->AddString("file", fFilePath.Path());
    else
      for (auto &p : fFiles)
        payload->AddString("file", p.Path());
    _SendMessageToTarget(MSG_COVER_FETCH_MB, payload);
    break;
  }

  case MSG_PROP_SET_COVER_DATA: {
    const void *buf = nullptr;
    ssize_t sz = 0;
    if (msg->FindData("bytes", B_RAW_TYPE, &buf, &sz) == B_OK && buf &&
        sz > 0) {

      fCurrentCoverBytes.assign((const uint8_t *)buf,
                                (const uint8_t *)buf + sz);

      BMemoryIO io(buf, (size_t)sz);
      if (BBitmap *bmp = BTranslationUtils::GetBitmap(&io)) {
        if (fCoverView)
          fCoverView->SetBitmap(bmp);
        delete bmp;
      }
    }
    break;
  }

  case MSG_MB_SEARCH: {
    if (fMbCancel)
      fMbCancel->SetEnabled(true);
    if (fMbStatusView)
      fMbStatusView->SetText(B_TRANSLATE("Searching..."));
    if (fMbResults)
      fMbResults->MakeEmpty();

    auto *q = new BMessage(MSG_MB_SEARCH);
    q->AddString("artist", fMbSearchArtist ? fMbSearchArtist->Text() : "");
    q->AddString("title", fMbSearchTitle ? fMbSearchTitle->Text() : "");
    q->AddString("album", fMbSearchAlbum ? fMbSearchAlbum->Text() : "");

    if (!fIsMulti)
      q->AddString("file", fFilePath.Path());
    else
      for (auto &p : fFiles)
        q->AddString("file", p.Path());
    _SendMessageToTarget(MSG_MB_SEARCH, q);
    break;
  }

  case MSG_MB_RESULTS: {
    printf("[PropertiesWindow] MSG_MB_RESULTS received.\n");
    if (fMbCancel)
      fMbCancel->SetEnabled(false);
    if (fMbStatusView)
      fMbStatusView->SetText(B_TRANSLATE("Results received."));

    if (fMbResults) {
      fMbResults->MakeEmpty();
      fMbCache.clear();

      BString item, recId, relId;
      int32 i = 0;
      while (msg->FindString("item", i, &item) == B_OK) {
        printf("[PropertiesWindow] Adding item: %s\n", item.String());
        fMbResults->AddItem(new BStringItem(item.String()));

        msg->FindString("id", i, &recId);
        msg->FindString("releaseId", i, &relId);
        fMbCache.push_back({recId, relId});

        i++;
      }
      printf("[PropertiesWindow] Added %ld items.\n", (long)i);
    } else {
      printf("[PropertiesWindow] Error: fMbResults is NULL!\n");
    }
    break;
  }

  case MSG_MB_APPLY: {
    int32 sel = fMbResults ? fMbResults->CurrentSelection() : -1;
    if (sel >= 0 && sel < (int32)fMbCache.size()) {
      if (fMbCancel)
        fMbCancel->SetEnabled(true);
      if (fMbStatusView)
        fMbStatusView->SetText(B_TRANSLATE("Fetching metadata..."));

      auto *payload = new BMessage(MSG_MB_APPLY);
      if (!fIsMulti)
        payload->AddString("file", fFilePath.Path());
      else
        for (auto &p : fFiles)
          payload->AddString("file", p.Path());

      payload->AddString("id", fMbCache[sel].recId);
      payload->AddString("releaseId", fMbCache[sel].relId);

      _SendMessageToTarget(MSG_MB_APPLY, payload);
    }
    break;
  }

  case MSG_MB_APPLY_ALBUM: {
    int32 sel = fMbResults ? fMbResults->CurrentSelection() : -1;
    if (sel >= 0 && sel < (int32)fMbCache.size()) {
      if (fMbCancel)
        fMbCancel->SetEnabled(true);
      if (fMbStatusView)
        fMbStatusView->SetText("Hole Metadaten...");

      auto *payload = new BMessage(MSG_MB_APPLY_ALBUM);
      if (!fIsMulti)
        payload->AddString("file", fFilePath.Path());
      else
        for (auto &p : fFiles)
          payload->AddString("file", p.Path());

      payload->AddString("id", fMbCache[sel].recId);
      payload->AddString("releaseId", fMbCache[sel].relId);

      _SendMessageToTarget(MSG_MB_APPLY_ALBUM, payload);
    }
    break;
  }

  case MSG_MEDIA_ITEM_FOUND: {

    BString path;
    if (msg->FindString("path", &path) == B_OK) {
      bool needReload = false;
      if (!fIsMulti) {
        if (path == fFilePath.Path())
          needReload = true;
      } else {
        for (const auto &p : fFiles) {
          if (path == p.Path()) {
            needReload = true;
            break;
          }
        }
      }

      if (needReload) {
        if (fIsMulti)
          _LoadInitialDataMulti();
        else
          _LoadInitialData();

        if (fMbCancel)
          fMbCancel->SetEnabled(false);
        if (fMbStatusView)
          fMbStatusView->SetText("Metadaten aktualisiert.");
      }
    }
    break;
  }

  default:
    BWindow::MessageReceived(msg);
    break;
  }
}

/**
 * @brief Collects data from input fields and sends an update message to the
 * target.
 * @param saveToDisk If true, sends MSG_PROP_SAVE; otherwise MSG_PROP_APPLY.
 *
 * MSG_PROP_SAVE implies closing the window after saving, effectively "OK".
 * MSG_PROP_APPLY implies "Apply" without closing.
 */
void PropertiesWindow::_SendApply(bool saveToDisk) {
  auto *m = new BMessage(saveToDisk ? MSG_PROP_SAVE : MSG_PROP_APPLY);

  if (!fIsMulti)
    m->AddString("file", fFilePath.Path());
  else
    for (auto &p : fFiles)
      m->AddString("file", p.Path());

  auto addIfEnabled = [&](BTextControl *tc, const char *name) {
    if (!tc) {
      DEBUG_PRINT("[PropertiesWindow] _SendApply: Field '%s' is NULL\\n", name);
      return;
    }
    if (!tc->IsEnabled()) {
      DEBUG_PRINT("[PropertiesWindow] _SendApply: Field '%s' is DISABLED\\n",
                  name);
      return;
    }
    const char *t = tc->Text();
    DEBUG_PRINT("[PropertiesWindow] _SendApply: Field '%s' Text='%s'\\n", name,
                t ? t : "(null)");
    if (t && t[0] != '\0')
      m->AddString(name, t);
  };

  addIfEnabled(fEdTitle, "title");
  addIfEnabled(fEdArtist, "artist");
  addIfEnabled(fEdAlbum, "album");
  addIfEnabled(fEdAlbumArtist, "albumArtist");
  addIfEnabled(fEdComposer, "composer");
  addIfEnabled(fEdGenre, "genre");
  addIfEnabled(fEdComment, "comment");

  addIfEnabled(fEdYear, "year");
  addIfEnabled(fEdTrack, "track");
  addIfEnabled(fEdTrackTotal, "tracktotal");
  addIfEnabled(fEdDisc, "disc");
  addIfEnabled(fEdDiscTotal, "disctotal");
  addIfEnabled(fEdMBTrackID, "mbTrackID");
  addIfEnabled(fEdMBAlbumID, "mbAlbumID");

  _SendMessageToTarget(saveToDisk ? MSG_PROP_SAVE : MSG_PROP_APPLY, m);

  if (saveToDisk) {
    Quit();
  }
}

/**
 * @brief Loads file data for a specific index in multi-file mode (navigating).
 * @param index The index of the file in the file list.
 */
void PropertiesWindow::_LoadFileAtIndex(int32 index) {
  if (index < 0 || index >= (int32)fFiles.size())
    return;

  fCurrentIndex = index;
  fFilePath = fFiles[fCurrentIndex];

  fIsMulti = false;

  _LoadInitialData();

  fBtnPrev->SetEnabled(fCurrentIndex > 0);
  fBtnNext->SetEnabled(fCurrentIndex < (int32)fFiles.size() - 1);

  SetTitle(BString("Eigenschaften — ") << fFilePath.Leaf());
}

/**
 * @brief Opens the file panel to select a cover image.
 */
void PropertiesWindow::_OpenCoverPanel() {
  if (!fOpenPanel) {
    BMessage *msg = new BMessage(B_REFS_RECEIVED);
    fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr,
                                B_FILE_NODE, false, msg);
  }
  fOpenPanel->Show();
}

/**
 * @brief Handles the user's choice of a cover image file.
 * @param ref The entry_ref of the chosen file.
 */
void PropertiesWindow::_HandleCoverChosen(const entry_ref &ref) {
  BFile f(&ref, B_READ_ONLY);
  off_t sz = 0;
  if (f.InitCheck() == B_OK && f.GetSize(&sz) == B_OK && sz > 0) {
    void *buf = malloc((size_t)sz);
    if (buf) {
      ssize_t rd = f.Read(buf, (size_t)sz);
      if (rd > 0) {
        BMemoryIO io(buf, (size_t)rd);
        if (BBitmap *bmp = BTranslationUtils::GetBitmap(&io)) {
          if (fCoverView)
            fCoverView->SetBitmap(bmp);
          delete bmp;
          fCoverMixed = false;

          fCurrentCoverBytes.assign((const uint8_t *)buf,
                                    (const uint8_t *)buf + sz);
        }
      }
      free(buf);
    }
  }

  if (!fIsMulti) {
    auto *m = new BMessage(MSG_COVER_LOAD);
    m->AddString("file", fFilePath.Path());
    m->AddRef("ref", &ref);
    _SendMessageToTarget(MSG_COVER_LOAD, m);
  } else {

    BFile f2(&ref, B_READ_ONLY);
    off_t sz2 = 0;
    if (f2.InitCheck() == B_OK && f2.GetSize(&sz2) == B_OK && sz2 > 0) {
      std::unique_ptr<uint8[]> buf(new (std::nothrow) uint8[(size_t)sz2]);
      if (buf) {
        ssize_t rd2 = f2.Read(buf.get(), (size_t)sz2);
        if (rd2 > 0) {

          fCurrentCoverBytes.assign(buf.get(), buf.get() + rd2);

          auto *out = new BMessage(MSG_COVER_DROPPED_APPLY_ALL);
          out->AddData("bytes", B_RAW_TYPE, buf.get(), (size_t)rd2);
          for (auto &p : fFiles)
            out->AddString("file", p.Path());
          _SendMessageToTarget(MSG_COVER_DROPPED_APPLY_ALL, out);
        }
      }
    }
  }
}

/**
 * @brief Helper function to send a message to the target messenger.
 * @param what The command constant for the message.
 * @param payload The message to send (takes ownership).
 */
void PropertiesWindow::_SendMessageToTarget(uint32 what, BMessage *payload) {
  if (!payload)
    payload = new BMessage(what);
  if (payload->what != what)
    payload->what = what;

  if (fTarget.IsValid())
    fTarget.SendMessage(payload, this);
  delete payload;
}

/**
 * @brief Loads the initial metadata for the single active file.
 */
void PropertiesWindow::_LoadInitialData() {
  TagData td;
  if (TagSync::ReadTags(fFilePath, td)) {
    if (fEdTitle)
      fEdTitle->SetText(td.title.String());
    if (fEdArtist)
      fEdArtist->SetText(td.artist.String());
    if (fEdAlbum)
      fEdAlbum->SetText(td.album.String());
    if (fEdAlbumArtist)
      fEdAlbumArtist->SetText(td.albumArtist.String());
    if (fEdComposer)
      fEdComposer->SetText(td.composer.String());
    if (fEdYear)
      fEdYear->SetText(
          td.year ? BString().SetToFormat("%lu", (unsigned long)td.year) : "");
    if (fEdTrack)
      fEdTrack->SetText(
          td.track ? BString().SetToFormat("%lu", (unsigned long)td.track)
                   : "");
    if (fEdTrackTotal)
      fEdTrackTotal->SetText(
          td.trackTotal
              ? BString().SetToFormat("%lu", (unsigned long)td.trackTotal)
              : "");
    if (fEdDisc)
      fEdDisc->SetText(
          td.disc ? BString().SetToFormat("%lu", (unsigned long)td.disc) : "");
    if (fEdDiscTotal)
      fEdDiscTotal->SetText(
          td.discTotal
              ? BString().SetToFormat("%lu", (unsigned long)td.discTotal)
              : "");
    if (fEdGenre)
      fEdGenre->SetText(td.genre.String());
    if (fEdComment && !td.comment.IsEmpty())
      fEdComment->SetText(td.comment.String());
    if (fEdMBTrackID)
      fEdMBTrackID->SetText(td.mbTrackID.String());
    if (fEdMBAlbumID)
      fEdMBAlbumID->SetText(td.mbAlbumID.String());

    if (fMbSearchArtist)
      fMbSearchArtist->SetText(td.artist.String());
    if (fMbSearchAlbum)
      fMbSearchAlbum->SetText(td.album.String());
    if (fMbSearchTitle)
      fMbSearchTitle->SetText(td.title.String());
  }

  CoverBlob cover;
  if (TagSync::ExtractEmbeddedCover(fFilePath, cover) && cover.data() &&
      cover.size() > 0) {
    BMemoryIO io(cover.data(), cover.size());
    if (BBitmap *bmp = BTranslationUtils::GetBitmap(&io)) {
      if (fCoverView)
        fCoverView->SetBitmap(bmp);
      delete bmp;

      fCurrentCoverBytes.assign((const uint8_t *)cover.data(),
                                (const uint8_t *)cover.data() + cover.size());
    }
  } else if (fTarget.IsValid()) {
    auto *req = new BMessage(MSG_PROP_REQUEST_COVER);
    req->AddString("file", fFilePath.Path());
    _SendMessageToTarget(MSG_PROP_REQUEST_COVER, req);
  }
  _UpdateHeaderFromFields();
}

/**
 * @brief Loads aggregated metadata for multiple files, detecting mixed values.
 */
void PropertiesWindow::_LoadInitialDataMulti() {

  std::vector<BString> titles, artists, albums, albumArtists, composers, genres,
      comments;
  std::vector<uint32> years, tracks, trackTotals, discs, discTotals;

  titles.reserve(fFiles.size());
  artists.reserve(fFiles.size());
  albums.reserve(fFiles.size());
  albumArtists.reserve(fFiles.size());
  composers.reserve(fFiles.size());
  genres.reserve(fFiles.size());
  comments.reserve(fFiles.size());
  years.reserve(fFiles.size());
  tracks.reserve(fFiles.size());
  trackTotals.reserve(fFiles.size());
  discs.reserve(fFiles.size());
  discs.reserve(fFiles.size());
  discTotals.reserve(fFiles.size());
  std::vector<BString> mbTrackIDs, mbAlbumIDs;
  mbTrackIDs.reserve(fFiles.size());
  mbAlbumIDs.reserve(fFiles.size());

  fCoverMixed = false;
  bool anyCover = false;
  CoverBlob firstCoverBlob;

  for (const auto &p : fFiles) {
    TagData td;
    TagSync::ReadTags(p, td);

    titles.push_back(td.title);
    artists.push_back(td.artist);
    albums.push_back(td.album);
    albumArtists.push_back(td.albumArtist);
    composers.push_back(td.composer);
    genres.push_back(td.genre);
    comments.push_back(td.comment);
    years.push_back(td.year);
    tracks.push_back(td.track);
    trackTotals.push_back(td.trackTotal);
    discs.push_back(td.disc);
    discs.push_back(td.disc);
    discTotals.push_back(td.discTotal);
    mbTrackIDs.push_back(td.mbTrackID);
    mbAlbumIDs.push_back(td.mbAlbumID);

    if (!fCoverMixed) {
      CoverBlob cb;
      if (TagSync::ExtractEmbeddedCover(p, cb) && cb.data() && cb.size() > 0) {
        if (!anyCover) {
          firstCoverBlob.assign(cb.data(), cb.size());
          anyCover = true;
        } else {
          if (cb.size() != firstCoverBlob.size() ||
              (cb.size() > 0 &&
               memcmp(cb.data(), firstCoverBlob.data(),
                      std::min(cb.size(), firstCoverBlob.size())) != 0)) {
            fCoverMixed = true;
          }
        }
      } else {

        if (anyCover)
          fCoverMixed = true;
      }
    }
  }

  auto setText = [&](BTextControl *ed, const std::vector<BString> &vals) {
    if (!ed)
      return;
    BString common;
    switch (_StateForStrings(vals, common)) {
    case FieldState::AllSame:
      ed->SetEnabled(true);
      ed->SetText(common.String());
      break;
    case FieldState::AllEmpty:
      ed->SetEnabled(true);
      ed->SetText("");
      break;
    case FieldState::Mixed:
      ed->SetEnabled(false);
      ed->SetText("— Mehrere Dateien —");
      break;
    }
  };
  auto setInt = [&](BTextControl *ed, const std::vector<uint32> &vals) {
    if (!ed)
      return;
    uint32 common = 0;
    switch (_StateForInts(vals, common)) {
    case FieldState::AllSame: {
      ed->SetEnabled(true);
      BString s;
      s.SetToFormat("%lu", (unsigned long)common);
      if (common == 0)
        s = "";
      ed->SetText(s.String());
      break;
    }
    case FieldState::AllEmpty:
      ed->SetEnabled(true);
      ed->SetText("");
      break;
    case FieldState::Mixed:
      ed->SetEnabled(false);
      ed->SetText("");
      break;
    }
  };

  setText(fEdTitle, titles);
  setText(fEdArtist, artists);
  setText(fEdAlbum, albums);
  setText(fEdAlbumArtist, albumArtists);
  setText(fEdComposer, composers);
  setText(fEdGenre, genres);
  setText(fEdGenre, genres);
  setText(fEdComment, comments);
  setText(fEdMBTrackID, mbTrackIDs);
  setText(fEdMBAlbumID, mbAlbumIDs);

  setInt(fEdYear, years);
  setInt(fEdTrack, tracks);
  setInt(fEdTrackTotal, trackTotals);
  setInt(fEdDisc, discs);
  setInt(fEdDiscTotal, discTotals);

  if (fMbSearchArtist) {
    BString common;
    if (_StateForStrings(artists, common) == FieldState::AllSame)
      fMbSearchArtist->SetText(common.String());
    else
      fMbSearchArtist->SetText("");
  }
  if (fMbSearchAlbum) {
    BString common;
    if (_StateForStrings(albums, common) == FieldState::AllSame)
      fMbSearchAlbum->SetText(common.String());
    else
      fMbSearchAlbum->SetText("");
  }
  if (fMbSearchTitle) {
    BString common;
    if (_StateForStrings(titles, common) == FieldState::AllSame)
      fMbSearchTitle->SetText(common.String());
    else
      fMbSearchTitle->SetText("");
  }

  if (fCoverView) {
    if (fCoverMixed) {
      fCoverView->SetBitmap(nullptr);
    } else if (anyCover && firstCoverBlob.data() && firstCoverBlob.size() > 0) {
      BMemoryIO io(firstCoverBlob.data(), firstCoverBlob.size());
      if (BBitmap *bmp = BTranslationUtils::GetBitmap(&io)) {
        fCoverView->SetBitmap(bmp);
        delete bmp;
      } else {
        fCoverView->SetBitmap(nullptr);
      }
    } else {
      fCoverView->SetBitmap(nullptr);
    }
  }

  _UpdateHeaderFromFields();
}

/**
 * @brief Updates the window header based on current field values.
 */
void PropertiesWindow::_UpdateHeaderFromFields() {
  if (fHdrTitle)
    fHdrTitle->SetText(fEdTitle ? fEdTitle->Text() : "");
  if (fHdrSub1)
    fHdrSub1->SetText(fEdArtist ? fEdArtist->Text() : "");

  BString sub2;
  if (fEdAlbum && fEdAlbum->Text() && *fEdAlbum->Text())
    sub2 << fEdAlbum->Text();
  BString y = fEdYear ? fEdYear->Text() : "";
  if (!y.IsEmpty()) {
    if (!sub2.IsEmpty())
      sub2 << " ";
    sub2 << "(" << y << ")";
  }
  if (fIsMulti) {
    if (!sub2.IsEmpty())
      sub2 << "   ";
    sub2 << "[" << (int)fFiles.size() << " Dateien]";
  }
  if (fHdrSub2)
    fHdrSub2->SetText(sub2.String());
}
