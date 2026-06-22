#include "MetadataPropertiesWindow.h"
#include "ArtworkView.h"
#include "Debug.h"
#include "Messages.h"
#include "MetadataTagIO.h"

#include <Bitmap.h>
#include <Button.h>
#include <Catalog.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <ControlLook.h>
#include <DataIO.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GridLayout.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Message.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TabView.h>
#include <TextControl.h>
#include <TextView.h>
#include <TranslationUtils.h>
#include <View.h>
#include <Window.h>
#include <algorithm>
#include <cinttypes>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <unistd.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MetadataPropertiesWindow"

namespace {

const float kDefaultSingleWidth = 840.0f;
const float kDefaultSingleHeight = 580.0f;
const float kDefaultMultiWidth = 520.0f;
const float kDefaultMultiHeight = 700.0f;

static BRect
DefaultPropertiesFrame(float width, float height)
{
  return BRect(100.0f, 100.0f, 100.0f + width, 100.0f + height);
}

static BRect
LoadPropertiesFrame(float width, float height)
{
  BRect frame = DefaultPropertiesFrame(width, height);

  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
    return frame;

  settingsPath.Append("BeTon/properties_window.settings");
  BFile file(settingsPath.Path(), B_READ_ONLY);
  if (file.InitCheck() != B_OK)
    return frame;

  BMessage state;
  if (state.Unflatten(&file) != B_OK)
    return frame;

  BRect savedFrame;
  if (state.FindRect("properties_window_frame", &savedFrame) == B_OK
      && savedFrame.Width() > 0.0f && savedFrame.Height() > 0.0f) {
    frame.right = frame.left + savedFrame.Width();
    frame.bottom = frame.top + savedFrame.Height();
  }

  return frame;
}

static void
ApplyPropertiesColors(BView *view)
{
  if (!view)
    return;

  rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);
  rgb_color panelText = ui_color(B_PANEL_TEXT_COLOR);
  rgb_color documentBg = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
  rgb_color documentText = ui_color(B_DOCUMENT_TEXT_COLOR);

  if (auto *text = dynamic_cast<BTextView *>(view)) {
    text->SetViewColor(documentBg);
    text->SetLowColor(documentBg);
    text->SetHighColor(documentText);
  } else if (auto *list = dynamic_cast<BListView *>(view)) {
    list->SetViewColor(documentBg);
    list->SetLowColor(documentBg);
    list->SetHighColor(documentText);
  } else {
    view->SetViewColor(panelBg);
    view->SetLowColor(panelBg);
    if (auto *label = dynamic_cast<BStringView *>(view))
      label->SetHighColor(panelText);
  }

  for (BView *child = view->ChildAt(0); child; child = child->NextSibling())
    ApplyPropertiesColors(child);

  view->Invalidate();
}

static const char*
MultipleFilesPlaceholder()
{
  return B_TRANSLATE("Multiple files");
}

static bool
IsMultipleFilesPlaceholder(const char *text)
{
  return text != nullptr && strcmp(text, MultipleFilesPlaceholder()) == 0;
}

static void
ClearColumnRows(BColumnListView *view)
{
  if (!view)
    return;

  while (view->CountRows() > 0) {
    BRow *row = view->RowAt(0);
    if (!row)
      break;
    view->RemoveRow(row);
    delete row;
  }
}

static BString
TrimmedString(const char *text)
{
  BString value(text ? text : "");
  value.Trim();
  return value;
}

static bool
IsYearText(const BString &value)
{
  if (value.Length() != 4)
    return false;

  const char *text = value.String();
  for (int32 i = 0; i < value.Length(); ++i) {
    if (!std::isdigit((unsigned char)text[i]))
      return false;
  }
  return true;
}

static BString
YearRangeStart(const BString &year)
{
  return year;
}

static BString
YearRangeEnd(const BString &year)
{
  BString value;
  value << year << "-9999";
  return value;
}

static void
AddYearSearchToMessage(BMessage *message, const char *text)
{
  if (!message)
    return;

  BString value = TrimmedString(text);
  if (value.IsEmpty())
    return;

  int32 sep = value.FindFirst("..");
  int32 sepLen = 2;
  if (sep < 0) {
    sep = value.FindFirst('-');
    sepLen = 1;
  }

  if (sep >= 0) {
    BString from;
    value.CopyInto(from, 0, sep);
    from.Trim();
    BString to;
    value.CopyInto(to, sep + sepLen, value.Length() - sep - sepLen);
    to.Trim();

    if (IsYearText(from))
      message->AddString("date_from", YearRangeStart(from));
    if (IsYearText(to))
      message->AddString("date_to", YearRangeEnd(to));
    return;
  }

  if (IsYearText(value)) {
    message->AddString("date_from", YearRangeStart(value));
    message->AddString("date_to", YearRangeEnd(value));
  }
}

} // namespace

class RatingStringView final : public BStringView {
public:
  RatingStringView()
      : BStringView("rating", "☆☆☆☆☆") {}

  void SetEditable(bool editable) { fEditable = editable; }

  void MouseDown(BPoint where) override {
    if (!fEditable)
      return;

    int32 buttons = B_PRIMARY_MOUSE_BUTTON;
    if (Window() && Window()->CurrentMessage())
      Window()->CurrentMessage()->FindInt32("buttons", &buttons);

    int32 rating = 0;
    if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0) {
      float starWidth = Bounds().Width() / 5.0f;
      if (starWidth <= 0.0f)
        return;

      int32 star = (int32)(where.x / starWidth);
      star = std::max((int32)0, std::min((int32)4, star));
      float xInStar = where.x - (starWidth * star);
      rating = star * 2 + (xInStar < starWidth / 2.0f ? 1 : 2);
    }

    BMessage msg(MSG_SET_RATING);
    msg.AddInt32("rating", rating);
    BMessenger(Window()).SendMessage(&msg);
  }

private:
  bool fEditable = true;
};

class PropertiesArtworkView final : public ArtworkView {
public:
  PropertiesArtworkView(const char *name, MetadataPropertiesWindow *owner)
      : ArtworkView(name), fOwner(owner) {}

  void MessageReceived(BMessage *msg) override {
    switch (msg->what) {
    case B_SIMPLE_DATA:
    case B_REFS_RECEIVED:
      if (Window())
        Window()->PostMessage(msg);
      break;
    default:
      ArtworkView::MessageReceived(msg);
      break;
    }
  }

  void MouseDown(BPoint where) override {
    int32 buttons = B_PRIMARY_MOUSE_BUTTON;
    if (Window() && Window()->CurrentMessage())
      Window()->CurrentMessage()->FindInt32("buttons", &buttons);

    if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0 && fOwner) {
      fOwner->_ShowCoverContextMenu(ConvertToScreen(where));
      return;
    }

    ArtworkView::MouseDown(where);
  }

private:
  MetadataPropertiesWindow *fOwner;
};

class MusicBrainzResultRow final : public BRow {
public:
  explicit MusicBrainzResultRow(int32 cacheIndex)
      : BRow(), fCacheIndex(cacheIndex) {}

  int32 CacheIndex() const { return fCacheIndex; }

private:
  int32 fCacheIndex;
};

/**
 * @brief Constructor for editing a single file, taking a file path string.
 * @param filePath The absolute path to the file.
 * @param target The messenger to which updates are sent.
 */
MetadataPropertiesWindow::MetadataPropertiesWindow(const BString &filePath,
                                   const BMessenger &target)
    : MetadataPropertiesWindow(
          LoadPropertiesFrame(kDefaultSingleWidth, kDefaultSingleHeight),
          BPath(filePath.String()), target) {
  fFiles.push_back(BPath(filePath.String()));
  fCurrentIndex = 0;
}

/**
 * @brief Constructor for editing a single file, taking a BPath.
 * @param filePath The BPath to the file.
 * @param target The messenger to which updates are sent.
 */
MetadataPropertiesWindow::MetadataPropertiesWindow(const BPath &filePath,
                                   const BMessenger &target)
    : MetadataPropertiesWindow(
          LoadPropertiesFrame(kDefaultSingleWidth, kDefaultSingleHeight),
          filePath, target) {
  fFiles.push_back(filePath);
  fCurrentIndex = 0;
}

/**
 * @brief Main constructor implementation for single file mode.
 * @param frame The initial window frame.
 * @param filePath The BPath to the file.
 * @param target The messenger to which updates are sent.
 */
MetadataPropertiesWindow::MetadataPropertiesWindow(BRect frame, const BPath &filePath,
                                   const BMessenger &target)
    : BWindow(frame, B_TRANSLATE("Properties"), B_TITLED_WINDOW, 0),
      fFilePath(filePath), fTarget(target) {
  const float initialWidth = frame.Width();
  const float initialHeight = frame.Height();
  fIsMulti = false;
  _BuildUI();
  ResizeToPreferred();

  BRect bounds = Bounds();
  SetSizeLimits(bounds.Width(), 10000, bounds.Height(), 10000);
  ResizeTo(std::max(bounds.Width(), initialWidth),
           std::max(bounds.Height(), initialHeight));

  CenterOnScreen();
  Show();
}

/**
 * @brief Constructor for editing multiple files.
 * @param filePaths A vector of BPaths to the files.
 * @param target The messenger to which updates are sent.
 */
MetadataPropertiesWindow::MetadataPropertiesWindow(const std::vector<BPath> &filePaths,
                                   const BMessenger &target)
    : MetadataPropertiesWindow(LoadPropertiesFrame(kDefaultMultiWidth,
                                           kDefaultMultiHeight),
                       filePaths, target) {}

MetadataPropertiesWindow::MetadataPropertiesWindow(
    const std::vector<BPath> &filePaths,
    const std::vector<MediaItem> &preloadedItems, const BMessenger &target)
    : BWindow(LoadPropertiesFrame(kDefaultMultiWidth, kDefaultMultiHeight),
              B_TRANSLATE("Properties"), B_TITLED_WINDOW, 0),
      fTarget(target) {
  BRect initialFrame = Frame();
  const float initialWidth = initialFrame.Width();
  const float initialHeight = initialFrame.Height();
  fIsMulti = true;
  fFiles = filePaths;
  fPreloadedItems = preloadedItems;
  fCurrentIndex = 0;
  if (!fFiles.empty())
    fFilePath = fFiles.front();
  _BuildUI();
  ResizeToPreferred();

  BRect bounds = Bounds();
  SetSizeLimits(bounds.Width(), 10000, bounds.Height(), 10000);
  ResizeTo(std::max(bounds.Width(), initialWidth),
           std::max(bounds.Height(), initialHeight));

  CenterOnScreen();
  Show();
}

/**
 * @brief Main constructor implementation for multi-file mode.
 * @param frame The initial window frame.
 * @param filePaths A vector of BPaths to the files.
 * @param target The messenger to which updates are sent.
 */
MetadataPropertiesWindow::MetadataPropertiesWindow(BRect frame,
                                   const std::vector<BPath> &filePaths,
                                   const BMessenger &target)
    : BWindow(frame, B_TRANSLATE("Properties"), B_TITLED_WINDOW, 0),
      fTarget(target) {
  const float initialWidth = frame.Width();
  const float initialHeight = frame.Height();
  fIsMulti = true;
  fFiles = filePaths;
  fCurrentIndex = 0;
  if (!fFiles.empty())
    fFilePath = fFiles.front();
  _BuildUI();
  ResizeToPreferred();

  BRect bounds = Bounds();
  SetSizeLimits(bounds.Width(), 10000, bounds.Height(), 10000);
  ResizeTo(std::max(bounds.Width(), initialWidth),
           std::max(bounds.Height(), initialHeight));

  CenterOnScreen();
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
MetadataPropertiesWindow::MetadataPropertiesWindow(const std::vector<BPath> &filePaths,
                                   int32 initialIndex, const BMessenger &target)
    : BWindow(LoadPropertiesFrame(kDefaultSingleWidth, kDefaultSingleHeight),
              B_TRANSLATE("Properties"), B_TITLED_WINDOW, 0),
      fTarget(target) {
  BRect initialFrame = Frame();
  const float initialWidth = initialFrame.Width();
  const float initialHeight = initialFrame.Height();

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
  ResizeTo(std::max(bounds.Width(), initialWidth),
           std::max(bounds.Height(), initialHeight));

  CenterOnScreen();

  fBtnPrev->SetEnabled(fCurrentIndex > 0);
  fBtnNext->SetEnabled(fCurrentIndex < (int32)fFiles.size() - 1);

  Show();
}

MetadataPropertiesWindow::~MetadataPropertiesWindow() {
  if (fTarget.IsValid()) {
    BMessage closed(MSG_PROP_CLOSED);
    fTarget.SendMessage(&closed);
  }
  delete fOpenPanel;
  fOpenPanel = nullptr;
}

bool MetadataPropertiesWindow::QuitRequested() {
  _SaveWindowFrame();
  return BWindow::QuitRequested();
}

/**
 * @brief Determines the state of a string field across multiple files.
 * @param vals Vector of string values for the field from all selected files.
 * @param outCommon Output parameter to store the common value if all are same.
 * @return FieldState: AllSame, AllEmpty, or Mixed.
 */
MetadataPropertiesWindow::FieldState
MetadataPropertiesWindow::_StateForStrings(const std::vector<BString> &vals,
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
MetadataPropertiesWindow::FieldState
MetadataPropertiesWindow::_StateForInts(const std::vector<uint32> &vals,
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
void MetadataPropertiesWindow::_BuildUI() {
  SetLayout(new BGroupLayout(B_VERTICAL));

  fTabs = new BTabView("propsTabs", B_WIDTH_FROM_LABEL);

  auto *tagsPage = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
  auto *mbPage = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);

  tagsPage->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  mbPage->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  fTabs->AddTab(tagsPage);
  fTabs->TabAt(0)->SetLabel(B_TRANSLATE("Details"));
  fTabs->AddTab(mbPage);
  fTabs->TabAt(1)->SetLabel(B_TRANSLATE("MusicBrainz"));

  _BuildTab_Tags(tagsPage);
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
      .Add(fBtnPrev)
      .Add(fBtnNext)
      .AddGlue()
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

  _UpdateReadOnlyState();
  _ApplyColors();
}

void
MetadataPropertiesWindow::_ApplyColors()
{
  for (BView *child = ChildAt(0); child; child = child->NextSibling())
    ApplyPropertiesColors(child);
}

/**
 * @brief Builds the 'Details' tab for editing metadata tags.
 * @param parent The parent view for the tab content.
 */
void MetadataPropertiesWindow::_BuildTab_Tags(BView *parent) {

  auto *root = new BView("detailsRoot", B_WILL_DRAW);
  root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  float coverDim = std::max(128.0f, fontHeight * 8.0f);

  fArtworkView = new PropertiesArtworkView("propCover", this);
  fArtworkView->SetExplicitMinSize(BSize(coverDim, coverDim));
  fArtworkView->SetExplicitMaxSize(BSize(coverDim, coverDim));
  fCoverStatus = new BStringView("coverStatus", "");
  fCoverStatus->SetExplicitMaxSize(BSize(coverDim, B_SIZE_UNSET));

  fHdrTitle = new BStringView(nullptr, "");
  fHdrSub1 = new BStringView(nullptr, "");
  fHdrSub2 = new BStringView(nullptr, "");
  fHdrRating = new RatingStringView();

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
      .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
      .Add(fArtworkView)
      .Add(fCoverStatus)
      .End()
      .AddGroup(B_VERTICAL, 0)
      .SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP))
      .Add(fHdrTitle)
      .Add(fHdrSub1)
      .Add(fHdrSub2)
      .Add(fHdrRating)
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
 * @brief Builds the 'MusicBrainz' tab for online metadata search.
 * @param parent The parent view for the tab content.
 */
void MetadataPropertiesWindow::_BuildTab_MB(BView *parent) {
  auto *root = new BView("mbRoot", B_WILL_DRAW);
  root->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
  auto *gl = new BGroupLayout(B_VERTICAL);
  root->SetLayout(gl);

  fMbSearchArtist = new BTextControl("Artist:", B_TRANSLATE("Artist:"), "",
                                     nullptr);
  fMbSearchAlbum = new BTextControl("Album:", B_TRANSLATE("Album:"), "",
                                    nullptr);
  fMbSearchTitle = new BTextControl("Titel:", B_TRANSLATE("Title:"), "",
                                    nullptr);
  fMbSearchTag = new BTextControl("Tag:", B_TRANSLATE("Genre:"), "",
                                  nullptr);
  fMbSearchYear = new BTextControl("Year:", B_TRANSLATE("Year:"), "",
                                   nullptr);
  fMbSearchCountry = new BTextControl("Country:", B_TRANSLATE("Country:"), "",
                                      nullptr);

  fMbSearch =
      new BButton("Suchen", B_TRANSLATE("Search"), new BMessage(MSG_MB_SEARCH));
  fMbCancel = new BButton("Abbrechen", B_TRANSLATE("Cancel"),
                          new BMessage(MSG_MB_CANCEL));
  fMbCancel->SetEnabled(false);

  fMbStatusView = new BStringView("mbStatus", B_TRANSLATE("Ready."));

  fMbResults = new BColumnListView("mbResults",
                                   B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE);
  fMbResults->SetSelectionMode(B_SINGLE_SELECTION_LIST);
  fMbResults->SetColor(B_COLOR_BACKGROUND, ui_color(B_LIST_BACKGROUND_COLOR));
  fMbResults->SetColor(B_COLOR_TEXT, ui_color(B_LIST_ITEM_TEXT_COLOR));
  fMbResults->SetColor(B_COLOR_SELECTION,
                       ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
  fMbResults->SetColor(B_COLOR_SELECTION_TEXT,
                       ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
  fMbResults->SetColor(B_COLOR_ROW_DIVIDER, B_TRANSPARENT_COLOR);
  fMbResults->SetColor(B_COLOR_HEADER_BACKGROUND,
                       ui_color(B_PANEL_BACKGROUND_COLOR));
  fMbResults->SetColor(B_COLOR_HEADER_TEXT, ui_color(B_PANEL_TEXT_COLOR));
  fMbResults->AddColumn(new BStringColumn(B_TRANSLATE("Artist"), 150, 60, 320,
                                          B_TRUNCATE_END),
                        0);
  fMbResults->AddColumn(new BStringColumn(B_TRANSLATE("Title"), 190, 70, 420,
                                          B_TRUNCATE_END),
                        1);
  fMbResults->AddColumn(new BStringColumn(B_TRANSLATE("Album"), 190, 70, 420,
                                          B_TRUNCATE_END),
                        2);
  fMbResults->AddColumn(new BStringColumn(B_TRANSLATE("Genre"), 100, 50, 180,
                                          B_TRUNCATE_END),
                        3);
  fMbResults->AddColumn(new BStringColumn(B_TRANSLATE("Year"), 60, 40, 90,
                                          B_TRUNCATE_END, B_ALIGN_RIGHT),
                        4);
  fMbResults->AddColumn(new BStringColumn(B_TRANSLATE("Country"), 70, 45, 100,
                                          B_TRUNCATE_END),
                        5);
  fMbResults->AddColumn(new BStringColumn(B_TRANSLATE("Tracks"), 70, 45, 100,
                                          B_TRUNCATE_END, B_ALIGN_RIGHT),
                        6);
  fMbApplyTrack =
      new BButton("ApplyTrack", B_TRANSLATE("Apply Selection (Track)"),
                  new BMessage(MSG_MB_APPLY));
  fMbApplyAlbum =
      new BButton("ApplyAlbum", B_TRANSLATE("Apply Selection (Album)"),
                  new BMessage(MSG_MB_APPLY_ALBUM));

  gl->SetInsets(B_USE_WINDOW_INSETS);

  auto *form = new BGridView();
  auto *grid = form->GridLayout();
  grid->SetSpacing(5.0f, 5.0f);
  grid->SetColumnWeight(0, 0.0f);
  grid->SetColumnWeight(1, 1.0f);
  grid->SetColumnWeight(2, 0.0f);
  grid->SetColumnWeight(3, 0.45f);
  form->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

  int32 r = 0;
  auto mkRow = [&](BTextControl *left, BTextControl *right) {
    grid->AddItem(left->CreateLabelLayoutItem(), 0, r);
    BLayoutItem *leftText = left->CreateTextViewLayoutItem();
    leftText->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    grid->AddItem(leftText, 1, r);

    grid->AddItem(right->CreateLabelLayoutItem(), 2, r);
    BLayoutItem *rightText = right->CreateTextViewLayoutItem();
    rightText->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    grid->AddItem(rightText, 3, r);
    r++;
  };
  mkRow(fMbSearchArtist, fMbSearchTag);
  mkRow(fMbSearchAlbum, fMbSearchYear);
  mkRow(fMbSearchTitle, fMbSearchCountry);

  {
    auto *container = new BView("mbButtons", B_WILL_DRAW);
    container->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
    auto *sub = new BGroupLayout(B_HORIZONTAL);
    container->SetLayout(sub);

    sub->AddView(fMbSearch);
    sub->AddView(fMbCancel);
    sub->AddView(fMbStatusView);
    sub->AddItem(BSpaceLayoutItem::CreateGlue());

    grid->AddView(container, 0, r++, 4, 1);
  }
  gl->AddView(form);

  gl->AddView(fMbResults, 1.0f);

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
void MetadataPropertiesWindow::MessageReceived(BMessage *msg) {

  if (msg->what == MSG_MB_RESULTS) {
    DEBUG_PRINT("MessageReceived: MSG_MB_RESULTS detected!\n");
  }

  switch (msg->what) {
  case MSG_PROP_APPLY:
    _SendApply(false);
    break;
  case MSG_PROP_SAVE:
    _SendApply(true);
    break;
  case MSG_PROP_CANCEL:
    _SaveWindowFrame();
    Quit();
    break;

  case MSG_SET_RATING: {
    int32 rating = 0;
    if (msg->FindInt32("rating", &rating) == B_OK)
      _SetRating(rating, true);
    break;
  }

  case B_COLORS_UPDATED:
    _ApplyColors();
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
    auto *payload = new BMessage(MSG_COVER_DROPPED_APPLY_ALL);
    payload->AddBool("clear_cover", true);
    if (!fIsMulti)
      payload->AddString("file", fFilePath.Path());
    else
      for (auto &p : fFiles)
        payload->AddString("file", p.Path());
    _SendMessageToTarget(MSG_COVER_DROPPED_APPLY_ALL, payload);

    if (fArtworkView)
      fArtworkView->SetBitmap(nullptr);
    if (fCoverStatus)
      fCoverStatus->SetText("");
    fCoverMixed = false;
    fCoverDirty = false;
    fCurrentCoverMime.Truncate(0);
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
    }
    break;
  }

  case MSG_COVER_FETCH_MB: {
    if (fCoverStatus)
      fCoverStatus->SetText(B_TRANSLATE("Fetching cover..."));
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
      const char *mime = nullptr;
      if (msg->FindString("mime", &mime) == B_OK && mime)
        fCurrentCoverMime = mime;
      else
        fCurrentCoverMime.Truncate(0);
      fCoverDirty = true;
      fCoverMixed = false;

      BMemoryIO io(buf, (size_t)sz);
      if (BBitmap *bmp = BTranslationUtils::GetBitmap(&io)) {
        if (fArtworkView)
          fArtworkView->SetBitmap(bmp);
        delete bmp;
      }
      if (fCoverStatus)
        fCoverStatus->SetText("");
    } else {
      if (fCoverStatus)
        fCoverStatus->SetText(B_TRANSLATE("No cover found."));
    }
    break;
  }

  case MSG_MB_SEARCH: {
    if (fMbCancel)
      fMbCancel->SetEnabled(true);
    if (fMbStatusView)
      fMbStatusView->SetText(B_TRANSLATE("Searching..."));
    if (fMbResults)
      ClearColumnRows(fMbResults);
    fMbCache.clear();
    fMbAlbumResults = false;
    if (fMbApplyTrack)
      fMbApplyTrack->SetEnabled(false);
    if (fMbApplyAlbum)
      fMbApplyAlbum->SetEnabled(false);

    auto *q = new BMessage(MSG_MB_SEARCH);
    q->AddString("artist", fMbSearchArtist ? fMbSearchArtist->Text() : "");
    q->AddString("title",
                 (!fIsMulti && fMbSearchTitle) ? fMbSearchTitle->Text() : "");
    q->AddString("album", fMbSearchAlbum ? fMbSearchAlbum->Text() : "");
    if (fMbSearchTag && fMbSearchTag->Text() && *fMbSearchTag->Text())
      q->AddString("tag", TrimmedString(fMbSearchTag->Text()));
    AddYearSearchToMessage(q, fMbSearchYear ? fMbSearchYear->Text() : "");
    if (fMbSearchCountry && fMbSearchCountry->Text() &&
        *fMbSearchCountry->Text())
      q->AddString("country", TrimmedString(fMbSearchCountry->Text()));
    q->AddBool("album_search", fIsMulti);

    if (!fIsMulti)
      q->AddString("file", fFilePath.Path());
    else
      for (auto &p : fFiles)
        q->AddString("file", p.Path());
    _SendMessageToTarget(MSG_MB_SEARCH, q);
    break;
  }

  case MSG_MB_RESULTS: {
    DEBUG_PRINT("MSG_MB_RESULTS received.\n");
    if (fMbCancel)
      fMbCancel->SetEnabled(false);

    if (fMbResults) {
      ClearColumnRows(fMbResults);
      fMbCache.clear();
      fMbAlbumResults = false;
      msg->FindBool("album_results", &fMbAlbumResults);

      BString item, recId, relId, artist, title, release, genre, country;
      int32 i = 0;
      while (msg->FindString("item", i, &item) == B_OK) {
        DEBUG_PRINT("Adding item: %s\n", item.String());

        recId.Truncate(0);
        relId.Truncate(0);
        artist.Truncate(0);
        title.Truncate(0);
        release.Truncate(0);
        genre.Truncate(0);
        country.Truncate(0);
        msg->FindString("id", i, &recId);
        msg->FindString("releaseId", i, &relId);
        msg->FindString("artist", i, &artist);
        msg->FindString("title", i, &title);
        msg->FindString("release", i, &release);
        msg->FindString("genre", i, &genre);
        msg->FindString("country", i, &country);
        int32 year = 0;
        int32 trackCount = 0;
        msg->FindInt32("year", i, &year);
        msg->FindInt32("trackCount", i, &trackCount);

        fMbCache.push_back({recId, relId});

        auto *row = new MusicBrainzResultRow((int32)fMbCache.size() - 1);
        row->SetField(new BStringField(artist.String()), 0);
        row->SetField(new BStringField(title.String()), 1);
        row->SetField(new BStringField(release.String()), 2);
        row->SetField(new BStringField(genre.String()), 3);
        BString yearText;
        if (year > 0)
          yearText.SetToFormat("%ld", (long)year);
        row->SetField(new BStringField(yearText.String()), 4);
        row->SetField(new BStringField(country.String()), 5);
        BString tracksText;
        if (trackCount > 0)
          tracksText.SetToFormat("%ld", (long)trackCount);
        row->SetField(new BStringField(tracksText.String()), 6);
        fMbResults->AddRow(row);

        i++;
      }
      DEBUG_PRINT("Added %ld items.\n", (long)i);

      if (i == 0) {
        if (fMbStatusView)
          fMbStatusView->SetText(B_TRANSLATE("No results found."));
        if (fMbApplyTrack)
          fMbApplyTrack->SetEnabled(false);
        if (fMbApplyAlbum)
          fMbApplyAlbum->SetEnabled(false);
      } else {
        BString status;
        status.SetToFormat(B_TRANSLATE("%ld results found."), (long)i);
        if (fMbStatusView)
          fMbStatusView->SetText(status);
        if (fMbApplyTrack)
          fMbApplyTrack->SetEnabled(!fMbAlbumResults);
        if (fMbApplyAlbum)
          fMbApplyAlbum->SetEnabled(true);
      }
      _UpdateReadOnlyState();
    } else {
      DEBUG_PRINT("Error: fMbResults is NULL!\n");
    }
    break;
  }

  case MSG_MB_APPLY: {
    auto *row = fMbResults
                    ? dynamic_cast<MusicBrainzResultRow *>(
                          fMbResults->CurrentSelection())
                    : nullptr;
    int32 sel = row ? row->CacheIndex() : -1;
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
    auto *row = fMbResults
                    ? dynamic_cast<MusicBrainzResultRow *>(
                          fMbResults->CurrentSelection())
                    : nullptr;
    int32 sel = row ? row->CacheIndex() : -1;
    if (sel >= 0 && sel < (int32)fMbCache.size()) {
      if (fMbCancel)
        fMbCancel->SetEnabled(true);
      if (fMbStatusView)
        fMbStatusView->SetText(B_TRANSLATE("Fetching metadata..."));

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
          fMbStatusView->SetText(B_TRANSLATE("Metadata updated."));
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
void MetadataPropertiesWindow::_SendApply(bool saveToDisk) {
  auto *m = new BMessage(saveToDisk ? MSG_PROP_SAVE : MSG_PROP_APPLY);

  if (!fIsMulti)
    m->AddString("file", fFilePath.Path());
  else
    for (auto &p : fFiles)
      m->AddString("file", p.Path());

  auto addIfEnabled = [&](BTextControl *tc, const char *name) {
    if (!tc) {
      DEBUG_PRINT("_SendApply: Field '%s' is NULL\n", name);
      return;
    }
    if (!tc->IsEnabled()) {
      DEBUG_PRINT("_SendApply: Field '%s' is DISABLED\n",
                  name);
      return;
    }
    if (!_FieldValueChanged(name, tc)) {
      DEBUG_PRINT("_SendApply: Field '%s' unchanged\n",
                  name);
      return;
    }
    const char *t = tc->Text();
    DEBUG_PRINT("_SendApply: Field '%s' Text='%s'\n", name,
                t ? t : "(null)");
    if (IsMultipleFilesPlaceholder(t)) {
      DEBUG_PRINT("_SendApply: Field '%s' is placeholder, skipping\n",
                  name);
      return;
    }
    m->AddString(name, t ? t : "");
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

  if (fRatingDirty)
    m->AddInt32("rating", fCurrentRating);

  _SendMessageToTarget(saveToDisk ? MSG_PROP_SAVE : MSG_PROP_APPLY, m);

  if (fCoverDirty && !fCurrentCoverBytes.empty()) {
    DEBUG_PRINT("_SendApply: sending cover apply, size=%zu\n",
                fCurrentCoverBytes.size());
    auto *cover = new BMessage(MSG_COVER_DROPPED_APPLY_ALL);
    cover->AddData("bytes", B_RAW_TYPE, fCurrentCoverBytes.data(),
                   fCurrentCoverBytes.size());
    if (!fCurrentCoverMime.IsEmpty())
      cover->AddString("mime", fCurrentCoverMime.String());
    if (!fIsMulti) {
      cover->AddString("file", fFilePath.Path());
    } else {
      for (auto &p : fFiles)
        cover->AddString("file", p.Path());
    }
    _SendMessageToTarget(MSG_COVER_DROPPED_APPLY_ALL, cover);
    fCoverDirty = false;
  }

  if (saveToDisk) {
    _SaveWindowFrame();
    Quit();
  }
}

/**
 * @brief Loads file data for a specific index in multi-file mode (navigating).
 * @param index The index of the file in the file list.
 */
void MetadataPropertiesWindow::_LoadFileAtIndex(int32 index) {
  if (index < 0 || index >= (int32)fFiles.size())
    return;

  _ClearMusicBrainzResults(true);

  fCurrentIndex = index;
  fFilePath = fFiles[fCurrentIndex];

  fIsMulti = false;

  _LoadInitialData();

  fBtnPrev->SetEnabled(fCurrentIndex > 0);
  fBtnNext->SetEnabled(fCurrentIndex < (int32)fFiles.size() - 1);

  _UpdateReadOnlyState();
}

void MetadataPropertiesWindow::_ClearMusicBrainzResults(bool cancelPendingSearch) {
  if (cancelPendingSearch && fMbCancel && fMbCancel->IsEnabled())
    _SendMessageToTarget(MSG_MB_CANCEL, new BMessage(MSG_MB_CANCEL));

  if (fMbCancel)
    fMbCancel->SetEnabled(false);
  if (fMbStatusView)
    fMbStatusView->SetText(B_TRANSLATE("Ready."));
  if (fMbResults)
    ClearColumnRows(fMbResults);

  fMbCache.clear();
  fMbAlbumResults = false;

  if (fMbApplyTrack)
    fMbApplyTrack->SetEnabled(false);
  if (fMbApplyAlbum)
    fMbApplyAlbum->SetEnabled(false);
}

void MetadataPropertiesWindow::_SetRating(int32 rating, bool markDirty) {
  if (fRatingReadOnly && markDirty)
    return;

  if (rating < 0)
    rating = 0;
  if (rating > 10)
    rating = 10;

  fCurrentRating = rating;
  fRatingMixed = false;
  if (markDirty)
    fRatingDirty = true;
  _UpdateRatingStars();
}

void MetadataPropertiesWindow::_UpdateRatingStars() {
  if (!fHdrRating)
    return;

  if (fRatingMixed) {
    fHdrRating->SetText(B_TRANSLATE("Mixed rating"));
    return;
  }

  int rating = fCurrentRating;
  if (rating < 0)
    rating = 0;
  if (rating > 10)
    rating = 10;

  int fullStars = rating / 2;
  bool halfStar = (rating % 2) == 1;
  int emptyStars = 5 - fullStars - (halfStar ? 1 : 0);

  BString starStr;
  for (int i = 0; i < fullStars; i++)
    starStr << "★";
  if (halfStar)
    starStr << "⯪";
  for (int i = 0; i < emptyStars; i++)
    starStr << "☆";

  fHdrRating->SetText(starStr);
}

void MetadataPropertiesWindow::_UpdateReadOnlyState() {
  bool isReadOnly = false;
  bool fileMissing = false;
  if (fIsMulti) {
    for (const auto &path : fFiles) {
      BEntry entry(path.Path(), true);
      if (entry.InitCheck() != B_OK || !entry.Exists()) {
        fileMissing = true;
      } else if (access(path.Path(), W_OK) != 0) {
        isReadOnly = true;
      }
    }
  } else {
    BEntry entry(fFilePath.Path(), true);
    if (entry.InitCheck() != B_OK || !entry.Exists()) {
      fileMissing = true;
    } else if (access(fFilePath.Path(), W_OK) != 0) {
      isReadOnly = true;
    }
  }

  bool canEdit = !isReadOnly && !fileMissing;

  fEdTitle->SetEnabled(canEdit);
  fEdArtist->SetEnabled(canEdit);
  fEdAlbum->SetEnabled(canEdit);
  fEdAlbumArtist->SetEnabled(canEdit);
  fEdComposer->SetEnabled(canEdit);
  fEdGenre->SetEnabled(canEdit);
  fEdYear->SetEnabled(canEdit);
  fEdTrack->SetEnabled(canEdit);
  fEdTrackTotal->SetEnabled(canEdit);
  fEdDisc->SetEnabled(canEdit);
  fEdDiscTotal->SetEnabled(canEdit);
  fEdComment->SetEnabled(canEdit);
  fEdMBTrackID->SetEnabled(canEdit);
  fEdMBAlbumID->SetEnabled(canEdit);

  if (fIsMulti) {
    fEdTitle->SetEnabled(false);
    fEdTrack->SetEnabled(false);
    fEdMBTrackID->SetEnabled(false);

    if (fEdArtist && IsMultipleFilesPlaceholder(fEdArtist->Text()))
      fEdArtist->SetEnabled(canEdit);
    if (fEdAlbum && IsMultipleFilesPlaceholder(fEdAlbum->Text()))
      fEdAlbum->SetEnabled(canEdit);
    if (fEdAlbumArtist && IsMultipleFilesPlaceholder(fEdAlbumArtist->Text()))
      fEdAlbumArtist->SetEnabled(canEdit);
    if (fEdComposer && IsMultipleFilesPlaceholder(fEdComposer->Text()))
      fEdComposer->SetEnabled(canEdit);
    if (fEdGenre && IsMultipleFilesPlaceholder(fEdGenre->Text()))
      fEdGenre->SetEnabled(canEdit);
    if (fEdYear && IsMultipleFilesPlaceholder(fEdYear->Text()))
      fEdYear->SetEnabled(canEdit);
    if (fEdTrackTotal && IsMultipleFilesPlaceholder(fEdTrackTotal->Text()))
      fEdTrackTotal->SetEnabled(canEdit);
    if (fEdDisc && IsMultipleFilesPlaceholder(fEdDisc->Text()))
      fEdDisc->SetEnabled(canEdit);
    if (fEdDiscTotal && IsMultipleFilesPlaceholder(fEdDiscTotal->Text()))
      fEdDiscTotal->SetEnabled(canEdit);
    if (fEdComment && IsMultipleFilesPlaceholder(fEdComment->Text()))
      fEdComment->SetEnabled(canEdit);
    if (fEdMBAlbumID && IsMultipleFilesPlaceholder(fEdMBAlbumID->Text()))
      fEdMBAlbumID->SetEnabled(canEdit);
  }

  fBtnApply->SetEnabled(canEdit);
  fBtnSave->SetEnabled(canEdit);

  bool hasMbResults = !fMbCache.empty();
  if (fMbApplyTrack)
    fMbApplyTrack->SetEnabled(canEdit && hasMbResults && !fMbAlbumResults);
  if (fMbApplyAlbum)
    fMbApplyAlbum->SetEnabled(canEdit && hasMbResults);
  fRatingReadOnly = !canEdit;
  if (fHdrRating)
    static_cast<RatingStringView *>(fHdrRating)->SetEditable(canEdit);

  BString title;
  if (fIsMulti) {
    title << B_TRANSLATE("Properties - ") << fFiles.size() << " " << B_TRANSLATE("Files");
  } else {
    title << B_TRANSLATE("Properties - ") << fFilePath.Leaf();
  }

  bool isMidi = false;
  if (fIsMulti) {
    for (const auto &path : fFiles) {
      BString lower = path.Path();
      lower.ToLower();
      if (lower.EndsWith(".mid") || lower.EndsWith(".midi")) {
        isMidi = true;
        break;
      }
    }
  } else {
    BString lower = fFilePath.Path();
    lower.ToLower();
    if (lower.EndsWith(".mid") || lower.EndsWith(".midi")) {
      isMidi = true;
    }
  }

  if (fileMissing) {
    title << " [" << B_TRANSLATE("File not found") << "]";
  } else if (isReadOnly) {
    title << " [" << B_TRANSLATE("Read-Only") << "]";
  }
  if (isMidi) {
    title << " [" << B_TRANSLATE("MIDI: Only BFS attributes supported") << "]";
  }
  SetTitle(title);
}

bool MetadataPropertiesWindow::_FilesCanBeModified() const {
  if (fIsMulti) {
    for (const auto &path : fFiles) {
      BEntry entry(path.Path(), true);
      if (entry.InitCheck() != B_OK || !entry.Exists() ||
          access(path.Path(), W_OK) != 0) {
        return false;
      }
    }
    return !fFiles.empty();
  }

  BEntry entry(fFilePath.Path(), true);
  return entry.InitCheck() == B_OK && entry.Exists() &&
         access(fFilePath.Path(), W_OK) == 0;
}

void MetadataPropertiesWindow::_ShowCoverContextMenu(BPoint screenWhere) {
  BPopUpMenu menu("coverContext", false, false);

  auto *load =
      new BMenuItem(B_TRANSLATE("Load Cover..."), new BMessage(MSG_COVER_LOAD));
  auto *remove =
      new BMenuItem(B_TRANSLATE("Remove Cover"), new BMessage(MSG_COVER_CLEAR));
  auto *addToAlbum = new BMenuItem(B_TRANSLATE("Add to Album"),
                                   new BMessage(MSG_COVER_APPLY_ALBUM));
  auto *removeFromAlbum = new BMenuItem(B_TRANSLATE("Remove from Album"),
                                        new BMessage(MSG_COVER_CLEAR_ALBUM));
  auto *fetchFromMb =
      new BMenuItem(B_TRANSLATE("Fetch from MusicBrainz"),
                    new BMessage(MSG_COVER_FETCH_MB));

  bool canEdit = _FilesCanBeModified();
  load->SetEnabled(canEdit);
  remove->SetEnabled(canEdit);
  addToAlbum->SetEnabled(canEdit && !fCurrentCoverBytes.empty());
  removeFromAlbum->SetEnabled(canEdit);
  fetchFromMb->SetEnabled(canEdit);

  menu.AddItem(load);
  menu.AddItem(remove);
  menu.AddSeparatorItem();
  menu.AddItem(addToAlbum);
  menu.AddItem(removeFromAlbum);
  menu.AddSeparatorItem();
  menu.AddItem(fetchFromMb);

  menu.SetTargetForItems(this);
  menu.Go(screenWhere, true, true);
}

/**
 * @brief Opens the file panel to select a cover image.
 */
void MetadataPropertiesWindow::_OpenCoverPanel() {
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
void MetadataPropertiesWindow::_HandleCoverChosen(const entry_ref &ref) {
  BFile f(&ref, B_READ_ONLY);
  off_t sz = 0;
  if (f.InitCheck() != B_OK || f.GetSize(&sz) != B_OK || sz <= 0)
    return;

  std::unique_ptr<uint8[]> buf(new (std::nothrow) uint8[(size_t)sz]);
  if (!buf)
    return;

  ssize_t rd = f.Read(buf.get(), (size_t)sz);
  if (rd <= 0)
    return;

  BMemoryIO io(buf.get(), (size_t)rd);
  BBitmap *bmp = BTranslationUtils::GetBitmap(&io);
  if (!bmp)
    return;

  if (fArtworkView)
    fArtworkView->SetBitmap(bmp);
  delete bmp;
  if (fCoverStatus)
    fCoverStatus->SetText("");

  fCoverMixed = false;
  fCurrentCoverBytes.assign(buf.get(), buf.get() + rd);
  fCurrentCoverMime.Truncate(0);
  fCoverDirty = false;

  auto *out = new BMessage(MSG_COVER_DROPPED_APPLY_ALL);
  out->AddData("bytes", B_RAW_TYPE, buf.get(), (size_t)rd);
  if (!fIsMulti) {
    out->AddString("file", fFilePath.Path());
  } else {
    for (auto &p : fFiles)
      out->AddString("file", p.Path());
  }
  _SendMessageToTarget(MSG_COVER_DROPPED_APPLY_ALL, out);
}

/**
 * @brief Helper function to send a message to the target messenger.
 * @param what The command constant for the message.
 * @param payload The message to send (takes ownership).
 */
void MetadataPropertiesWindow::_SendMessageToTarget(uint32 what, BMessage *payload) {
  if (!payload)
    payload = new BMessage(what);
  if (payload->what != what)
    payload->what = what;

  if (fTarget.IsValid())
    fTarget.SendMessage(payload, this);
  delete payload;
}

void MetadataPropertiesWindow::_SaveWindowFrame() const {
  BPath settingsDir;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsDir) != B_OK)
    return;

  settingsDir.Append("BeTon");
  create_directory(settingsDir.Path(), 0755);

  BPath settingsPath(settingsDir);
  settingsPath.Append("properties_window.settings");

  BMessage state;
  {
    BFile in(settingsPath.Path(), B_READ_ONLY);
    if (in.InitCheck() == B_OK)
      state.Unflatten(&in);
  }

  BRect frame = Frame();
  if (state.ReplaceRect("properties_window_frame", frame) != B_OK)
    state.AddRect("properties_window_frame", frame);

  BFile out(settingsPath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
  if (out.InitCheck() == B_OK)
    state.Flatten(&out);
}

void MetadataPropertiesWindow::_RememberInitialFieldValue(const char *name,
                                                  BTextControl *field) {
  if (!name || !field)
    return;
  fInitialFieldValues[name] = field->Text() ? field->Text() : "";
}

bool MetadataPropertiesWindow::_FieldValueChanged(const char *name,
                                          BTextControl *field) const {
  if (!name || !field)
    return false;

  const char *text = field->Text();
  BString current = text ? text : "";

  auto it = fInitialFieldValues.find(name);
  if (it == fInitialFieldValues.end())
    return true;

  return current != it->second;
}

/**
 * @brief Loads the initial metadata for the single active file.
 */
void MetadataPropertiesWindow::_LoadInitialData() {
  fInitialFieldValues.clear();
  fCurrentCoverBytes.clear();
  fCurrentCoverMime.Truncate(0);
  fCoverDirty = false;
  if (fArtworkView)
    fArtworkView->SetBitmap(nullptr);
  if (fCoverStatus)
    fCoverStatus->SetText("");

  auto clearText = [](BTextControl *field) {
    if (field)
      field->SetText("");
  };
  clearText(fEdTitle);
  clearText(fEdArtist);
  clearText(fEdAlbum);
  clearText(fEdAlbumArtist);
  clearText(fEdComposer);
  clearText(fEdYear);
  clearText(fEdTrack);
  clearText(fEdTrackTotal);
  clearText(fEdDisc);
  clearText(fEdDiscTotal);
  clearText(fEdGenre);
  clearText(fEdComment);
  clearText(fEdMBTrackID);
  clearText(fEdMBAlbumID);
  clearText(fMbSearchArtist);
  clearText(fMbSearchAlbum);
  clearText(fMbSearchTitle);
  clearText(fMbSearchTag);
  clearText(fMbSearchYear);
  clearText(fMbSearchCountry);

  TagData td;
  if (MetadataTagIO::ReadTags(fFilePath, td)) {
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
    if (fEdComment)
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
    if (fMbSearchTag)
      fMbSearchTag->SetText(td.genre.String());
    if (fMbSearchYear)
      fMbSearchYear->SetText(
          td.year ? BString().SetToFormat("%lu", (unsigned long)td.year) : "");
  }

  CoverBlob cover;
  if (MetadataTagIO::ExtractEmbeddedCover(fFilePath, cover) && cover.data() &&
      cover.size() > 0) {
    BMemoryIO io(cover.data(), cover.size());
    if (BBitmap *bmp = BTranslationUtils::GetBitmap(&io)) {
      if (fArtworkView)
        fArtworkView->SetBitmap(bmp);
      delete bmp;

      fCurrentCoverBytes.assign((const uint8_t *)cover.data(),
                                (const uint8_t *)cover.data() + cover.size());
    }
  } else if (fTarget.IsValid()) {
    auto *req = new BMessage(MSG_PROP_REQUEST_COVER);
    req->AddString("file", fFilePath.Path());
    _SendMessageToTarget(MSG_PROP_REQUEST_COVER, req);
  }

  {
    BFile file(fFilePath.Path(), B_READ_ONLY);
    if (file.InitCheck() == B_OK) {
      int32 rating = 0;
      if (file.ReadAttr("Media:Rating", B_INT32_TYPE, 0, &rating,
                        sizeof(rating)) == sizeof(rating)) {
        fCurrentRating = rating;
      } else {
        fCurrentRating = 0;
      }
    }
    fRatingDirty = false;
    fRatingMixed = false;
    _UpdateRatingStars();
  }

  _RememberInitialFieldValue("title", fEdTitle);
  _RememberInitialFieldValue("artist", fEdArtist);
  _RememberInitialFieldValue("album", fEdAlbum);
  _RememberInitialFieldValue("albumArtist", fEdAlbumArtist);
  _RememberInitialFieldValue("composer", fEdComposer);
  _RememberInitialFieldValue("genre", fEdGenre);
  _RememberInitialFieldValue("comment", fEdComment);
  _RememberInitialFieldValue("year", fEdYear);
  _RememberInitialFieldValue("track", fEdTrack);
  _RememberInitialFieldValue("tracktotal", fEdTrackTotal);
  _RememberInitialFieldValue("disc", fEdDisc);
  _RememberInitialFieldValue("disctotal", fEdDiscTotal);
  _RememberInitialFieldValue("mbTrackID", fEdMBTrackID);
  _RememberInitialFieldValue("mbAlbumID", fEdMBAlbumID);

  _UpdateHeaderFromFields();
}

/**
 * @brief Loads aggregated metadata for multiple files, detecting mixed values.
 */
void MetadataPropertiesWindow::_LoadInitialDataMulti() {
  fInitialFieldValues.clear();
  fCurrentCoverBytes.clear();
  fCurrentCoverMime.Truncate(0);
  fCoverDirty = false;
  if (fArtworkView)
    fArtworkView->SetBitmap(nullptr);
  if (fCoverStatus)
    fCoverStatus->SetText("");

  std::vector<BString> titles, artists, albums, albumArtists, composers, genres,
      comments;
  std::vector<uint32> years, tracks, trackTotals, discs, discTotals, ratings;

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
  discTotals.reserve(fFiles.size());
  ratings.reserve(fFiles.size());
  std::vector<BString> mbTrackIDs, mbAlbumIDs;
  mbTrackIDs.reserve(fFiles.size());
  mbAlbumIDs.reserve(fFiles.size());

  fCoverMixed = false;
  bool anyCover = false;
  bool compareCovers = fFiles.size() <= 8;
  CoverBlob firstCoverBlob;

  for (size_t index = 0; index < fFiles.size(); ++index) {
    const BPath &p = fFiles[index];
    TagData td;
    if (index < fPreloadedItems.size() &&
        fPreloadedItems[index].path == p.Path()) {
      const MediaItem &mi = fPreloadedItems[index];
      td.title = mi.title;
      td.artist = mi.artist;
      td.album = mi.album;
      td.albumArtist = mi.albumArtist;
      td.composer = mi.composer;
      td.genre = mi.genre;
      td.comment = mi.comment;
      td.year = mi.year;
      td.track = mi.track;
      td.trackTotal = mi.trackTotal;
      td.disc = mi.disc;
      td.discTotal = mi.discTotal;
      td.rating = mi.rating;
      td.mbTrackID = mi.mbTrackId;
      td.mbAlbumID = mi.mbAlbumId;
    } else {
      MetadataTagIO::ReadTags(p, td);
    }

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
    discTotals.push_back(td.discTotal);
    ratings.push_back(td.rating);
    mbTrackIDs.push_back(td.mbTrackID);
    mbAlbumIDs.push_back(td.mbAlbumID);

    if ((compareCovers || index == 0) && !fCoverMixed) {
      CoverBlob cb;
      if (MetadataTagIO::ExtractEmbeddedCover(p, cb) && cb.data() && cb.size() > 0) {
        if (!anyCover) {
          firstCoverBlob.assign(cb.data(), cb.size());
          anyCover = true;
        } else if (compareCovers) {
          if (cb.size() != firstCoverBlob.size() ||
              (cb.size() > 0 &&
               memcmp(cb.data(), firstCoverBlob.data(),
                      std::min(cb.size(), firstCoverBlob.size())) != 0)) {
            fCoverMixed = true;
          }
        }
      } else if (compareCovers) {
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
      ed->SetText(MultipleFilesPlaceholder());
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
      ed->SetText(MultipleFilesPlaceholder());
      break;
    }
  };

  setText(fEdTitle, titles);
  setText(fEdArtist, artists);
  setText(fEdAlbum, albums);
  setText(fEdAlbumArtist, albumArtists);
  setText(fEdComposer, composers);
  setText(fEdGenre, genres);
  setText(fEdComment, comments);
  setText(fEdMBTrackID, mbTrackIDs);
  setText(fEdMBAlbumID, mbAlbumIDs);

  setInt(fEdYear, years);
  setInt(fEdTrack, tracks);
  setInt(fEdTrackTotal, trackTotals);
  setInt(fEdDisc, discs);
  setInt(fEdDiscTotal, discTotals);

  if (fIsMulti) {
    if (fEdTitle) {
      fEdTitle->SetText(MultipleFilesPlaceholder());
      fEdTitle->SetEnabled(false);
    }
    if (fEdTrack) {
      fEdTrack->SetText(MultipleFilesPlaceholder());
      fEdTrack->SetEnabled(false);
    }
    if (fEdMBTrackID) {
      fEdMBTrackID->SetText(MultipleFilesPlaceholder());
      fEdMBTrackID->SetEnabled(false);
    }
  }

  if (!compareCovers)
    fCoverMixed = true;

  uint32 commonRating = 0;
  FieldState ratingState = _StateForInts(ratings, commonRating);
  fRatingDirty = false;
  if (ratingState == FieldState::Mixed) {
    fCurrentRating = 0;
    fRatingMixed = true;
  } else {
    fCurrentRating = (int32)commonRating;
    fRatingMixed = false;
  }
  _UpdateRatingStars();

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
  if (fMbSearchTag) {
    BString common;
    if (_StateForStrings(genres, common) == FieldState::AllSame)
      fMbSearchTag->SetText(common.String());
    else
      fMbSearchTag->SetText("");
  }
  if (fMbSearchYear) {
    uint32 common = 0;
    if (_StateForInts(years, common) == FieldState::AllSame && common > 0) {
      BString year;
      year.SetToFormat("%lu", (unsigned long)common);
      fMbSearchYear->SetText(year.String());
    } else {
      fMbSearchYear->SetText("");
    }
  }
  if (fMbSearchCountry)
    fMbSearchCountry->SetText("");

  if (fArtworkView) {
    if (anyCover && firstCoverBlob.data() && firstCoverBlob.size() > 0) {
      BMemoryIO io(firstCoverBlob.data(), firstCoverBlob.size());
      if (BBitmap *bmp = BTranslationUtils::GetBitmap(&io)) {
        fArtworkView->SetBitmap(bmp);
        delete bmp;
        fCurrentCoverBytes.assign(
            (const uint8_t *)firstCoverBlob.data(),
            (const uint8_t *)firstCoverBlob.data() + firstCoverBlob.size());
      } else {
        fArtworkView->SetBitmap(nullptr);
      }
    } else {
      fArtworkView->SetBitmap(nullptr);
    }
  }

  _RememberInitialFieldValue("title", fEdTitle);
  _RememberInitialFieldValue("artist", fEdArtist);
  _RememberInitialFieldValue("album", fEdAlbum);
  _RememberInitialFieldValue("albumArtist", fEdAlbumArtist);
  _RememberInitialFieldValue("composer", fEdComposer);
  _RememberInitialFieldValue("genre", fEdGenre);
  _RememberInitialFieldValue("comment", fEdComment);
  _RememberInitialFieldValue("year", fEdYear);
  _RememberInitialFieldValue("track", fEdTrack);
  _RememberInitialFieldValue("tracktotal", fEdTrackTotal);
  _RememberInitialFieldValue("disc", fEdDisc);
  _RememberInitialFieldValue("disctotal", fEdDiscTotal);
  _RememberInitialFieldValue("mbTrackID", fEdMBTrackID);
  _RememberInitialFieldValue("mbAlbumID", fEdMBAlbumID);

  _UpdateHeaderFromFields();
}

/**
 * @brief Updates the window header based on current field values.
 */
void MetadataPropertiesWindow::_UpdateHeaderFromFields() {
  if (fHdrTitle) {
    if (fIsMulti)
      fHdrTitle->SetText(MultipleFilesPlaceholder());
    else
      fHdrTitle->SetText(fEdTitle ? fEdTitle->Text() : "");
  }
  if (fHdrSub1)
    fHdrSub1->SetText(fEdArtist ? fEdArtist->Text() : "");

  BString sub2;
  if (fEdAlbum && fEdAlbum->Text() && *fEdAlbum->Text() &&
      !IsMultipleFilesPlaceholder(fEdAlbum->Text()))
    sub2 << fEdAlbum->Text();
  BString y = fEdYear ? fEdYear->Text() : "";
  if (!y.IsEmpty() && y != MultipleFilesPlaceholder()) {
    if (!sub2.IsEmpty())
      sub2 << " ";
    sub2 << "(" << y << ")";
  }
  if (fIsMulti) {
    if (!sub2.IsEmpty())
      sub2 << "   ";
    BString countText;
    countText.SetToFormat(B_TRANSLATE("%ld files"), (long)fFiles.size());
    sub2 << countText;
 }
  if (fHdrSub2) {
    fHdrSub2->SetText(sub2.String());
  }
}

