#include "SmartPlaylistGeneratorWindow.h"
#include "Messages.h"

#include <Button.h>
#include <CardLayout.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <GroupLayoutBuilder.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringItem.h>
#include <StringView.h>
#include <TextControl.h>
#include <cstdio>
#include <stdlib.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SmartPlaylistGeneratorWindow"

/** @name Internal Commands */
///@{
static const uint32 MSG_TYPE_CHANGED = 'tpch';
static const uint32 MSG_ADD_RULE = 'addR';
static const uint32 MSG_REMOVE_RULE = 'remR';
static const uint32 MSG_GEN_GENERATE = 'geng';
static const uint32 MSG_GEN_CANCEL = 'genc';
///@}

/**
 * @brief Returns a string representation of the rule for display in the
 * ListView.
 */
BString Rule::ToString() const {
  BString s;
  if (exclude)
    s << B_TRANSLATE("NOT ");

  if (type == 0)
    s << B_TRANSLATE("Genre: ") << value;
  else if (type == 1)
    s << B_TRANSLATE("Artist: ") << value;
  else if (type == 2)
    s << B_TRANSLATE("Year: ") << value << " - " << value2;

  return s;
}

/**
 * @class RuleItem
 * @brief A BStringItem wrapper for a Rule object to display in the BListView.
 */
class RuleItem : public BStringItem {
public:
  explicit RuleItem(const Rule &r) : BStringItem(r.ToString()), rule(r) {}
  Rule rule;
};

/**
 * @brief Constructs the smart playlist generator window.
 *
 * Sets up the layout, including card-based rule inputs, rule list, and
 * generation limits.
 *
 * @param target Messenger to send the final generation request to.
 * @param genres List of genres to populate the genre dropdown.
 */
SmartPlaylistGeneratorWindow::SmartPlaylistGeneratorWindow(
    BMessenger target, const std::vector<BString> &genres)
    : BWindow(BRect(100, 100, 600, 500), B_TRANSLATE("Generate Playlist"),
              B_TITLED_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS),
      fTarget(target), fGenres(genres), fInputCardLayout(nullptr),
      fRuleList(nullptr) {
  _BuildUI(genres);
  CenterOnScreen();
  _UpdateInputFields();
}

SmartPlaylistGeneratorWindow::~SmartPlaylistGeneratorWindow() {

  if (fRuleList) {
    while (BListItem *it = fRuleList->RemoveItem((int32)0))
      delete it;
  }
}

/**
 * @brief Builds the window UI layout.
 */
void SmartPlaylistGeneratorWindow::_BuildUI(const std::vector<BString> &genres) {
  // Calculate font-relative sizes for DPI scaling
  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  fNameInput = new BTextControl("Name", B_TRANSLATE("Name:"),
                                B_TRANSLATE("New Playlist"), nullptr);

  BPopUpMenu *typeMenu = new BPopUpMenu("Type");
  typeMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Genre"), new BMessage(MSG_TYPE_CHANGED)));
  typeMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Artist"), new BMessage(MSG_TYPE_CHANGED)));
  typeMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Year"), new BMessage(MSG_TYPE_CHANGED)));
  typeMenu->ItemAt(0)->SetMarked(true);
  typeMenu->SetTargetForItems(this);

  fTypeField = new BMenuField("Type", B_TRANSLATE("Criterion:"), typeMenu);

  fExcludeCheck = new BCheckBox("Exclude", B_TRANSLATE("not"), nullptr);

  fAddRuleBtn =
      new BButton("Add", B_TRANSLATE("Add"), new BMessage(MSG_ADD_RULE));
  fAddRuleBtn->SetTarget(this);

  BView *dynamicContainer = new BView("DynamicContainer", B_WILL_DRAW);
  dynamicContainer->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  fInputCardLayout = new BCardLayout();
  dynamicContainer->SetLayout(fInputCardLayout);

  {
    BGroupView *genreGroup =
        new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);

    BPopUpMenu *genreMenu = new BPopUpMenu("SelectGenre");
    for (const auto &g : genres)
      genreMenu->AddItem(new BMenuItem(g, nullptr));
    if (!genres.empty())
      genreMenu->ItemAt(0)->SetMarked(true);

    fGenreSelect = new BMenuField("GenreSel", B_TRANSLATE("Genre:"), genreMenu);

    genreGroup->GroupLayout()->AddView(fGenreSelect);
    genreGroup->GroupLayout()->AddItem(BSpaceLayoutItem::CreateGlue());

    fInputCardLayout->AddView(genreGroup);
  }

  {
    BGroupView *artistGroup =
        new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
    fArtistInput =
        new BTextControl("ArtistVal", B_TRANSLATE("Name:"), "", nullptr);
    artistGroup->AddChild(fArtistInput);
    fInputCardLayout->AddView(artistGroup);
  }

  {
    BGroupView *yearGroup = new BGroupView(B_HORIZONTAL, B_USE_DEFAULT_SPACING);
    fYearFromInput =
        new BTextControl("YearFrom", B_TRANSLATE("From:"), "", nullptr);
    fYearToInput = new BTextControl("YearTo", B_TRANSLATE("To:"), "", nullptr);
    yearGroup->AddChild(fYearFromInput);
    yearGroup->AddChild(fYearToInput);
    fInputCardLayout->AddView(yearGroup);
  }

  fInputCardLayout->SetVisibleItem((int32)0);

  fRuleList = new BListView("Rules", B_SINGLE_SELECTION_LIST);

  BScrollView *listScroll =
      new BScrollView("ScrollRule", fRuleList, B_FRAME_EVENTS, false, true);

  listScroll->SetExplicitMinSize(BSize(fontHeight * 24, fontHeight * 12));

  fRemoveRuleBtn = new BButton("Remove", B_TRANSLATE("Remove"),
                               new BMessage(MSG_REMOVE_RULE));
  fRemoveRuleBtn->SetTarget(this);

  BPopUpMenu *limitMenu = new BPopUpMenu("Modus");
  limitMenu->AddItem(new BMenuItem(B_TRANSLATE("No Limit"), nullptr));
  limitMenu->AddItem(new BMenuItem(B_TRANSLATE("Max. Tracks"), nullptr));
  limitMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Max. Duration (Min)"), nullptr));
  limitMenu->ItemAt(0)->SetMarked(true);

  fLimitModeField =
      new BMenuField("LimitMode", B_TRANSLATE("Limit:"), limitMenu);
  fLimitValue =
      new BTextControl("LimitVal", B_TRANSLATE("Value:"), "50", nullptr);

  fShuffleCheck =
      new BCheckBox("Shuffle", B_TRANSLATE("Shuffle Playback"), nullptr);

  fGenerateBtn = new BButton("Generate", B_TRANSLATE("Generate"),
                             new BMessage(MSG_GEN_GENERATE));
  fCancelBtn = new BButton("Cancel", B_TRANSLATE("Cancel"),
                           new BMessage(MSG_GEN_CANCEL));
  fGenerateBtn->SetTarget(this);
  fCancelBtn->SetTarget(this);

  BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
      .SetInsets(B_USE_WINDOW_SPACING)

      .Add(fNameInput)

      .Add(new BSeparatorView(B_HORIZONTAL))

      .AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
      .Add(new BStringView("h1", B_TRANSLATE("Define Rule")))
      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .Add(fTypeField)
      .Add(fExcludeCheck)
      .End()
      .Add(dynamicContainer)
      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .AddGlue()
      .Add(fAddRuleBtn)
      .End()
      .End()

      .Add(new BSeparatorView(B_HORIZONTAL))

      .AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING, 1.0f)
      .Add(new BStringView("h2", B_TRANSLATE("Rule List")))
      .Add(listScroll, 1.0f)
      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .AddGlue()
      .Add(fRemoveRuleBtn)
      .End()
      .End()

      .Add(new BSeparatorView(B_HORIZONTAL))

      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .Add(fLimitModeField)
      .Add(fLimitValue)
      .End()

      .Add(fShuffleCheck)

      .Add(new BSeparatorView(B_HORIZONTAL))

      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .AddGlue()
      .Add(fCancelBtn)
      .Add(fGenerateBtn)
      .End();

  fGenerateBtn->MakeDefault(true);
}

/**
 * @brief Updates the visible input fields based on the selected rule criteria
 * type.
 */
void SmartPlaylistGeneratorWindow::_UpdateInputFields() {
  if (!fInputCardLayout || !fTypeField || !fTypeField->Menu())
    return;

  BMenuItem *marked = fTypeField->Menu()->FindMarked();
  int32 type = marked ? fTypeField->Menu()->IndexOf(marked) : 0;
  if (type < 0)
    type = 0;
  if (type > 2)
    type = 2;

  fInputCardLayout->SetVisibleItem(type);
}

/**
 * @brief Creates a Rule object from current inputs and adds it to the list.
 */
void SmartPlaylistGeneratorWindow::_AddRule() {
  Rule r;
  r.type = fTypeField->Menu()->IndexOf(fTypeField->Menu()->FindMarked());
  r.exclude = (fExcludeCheck->Value() == B_CONTROL_ON);

  if (r.type == 0) {
    BMenuItem *item = fGenreSelect->Menu()->FindMarked();
    if (!item)
      return;
    r.value = item->Label();
  } else if (r.type == 1) {
    r.value = fArtistInput->Text();
    if (r.value.IsEmpty())
      return;
  } else {
    r.value = fYearFromInput->Text();
    r.value2 = fYearToInput->Text();
    if (r.value.IsEmpty())
      return;
  }

  fRuleList->AddItem(new RuleItem(r));

  int32 last = fRuleList->CountItems() - 1;
  if (last >= 0) {
    fRuleList->Select(last);
    fRuleList->ScrollToSelection();
  }
  fRuleList->Invalidate();
}

/**
 * @brief Removes the currently selected rule from the list.
 */
void SmartPlaylistGeneratorWindow::_RemoveRule() {
  int32 sel = fRuleList->CurrentSelection();
  if (sel >= 0)
    delete fRuleList->RemoveItem(sel);
}

void SmartPlaylistGeneratorWindow::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_TYPE_CHANGED:
    _UpdateInputFields();
    break;

  case MSG_ADD_RULE:
    _AddRule();
    break;

  case MSG_REMOVE_RULE:
    _RemoveRule();
    break;

  case MSG_GEN_CANCEL:
    Quit();
    break;

  case MSG_GEN_GENERATE: {
    BMessage genMsg(MSG_GENERATE_PLAYLIST);
    genMsg.AddString("name", fNameInput->Text());

    for (int32 i = 0; i < fRuleList->CountItems(); i++) {
      RuleItem *item = dynamic_cast<RuleItem *>(fRuleList->ItemAt(i));
      if (!item)
        continue;

      BMessage ruleMsg;
      ruleMsg.AddInt32("type", item->rule.type);
      ruleMsg.AddString("val1", item->rule.value);
      ruleMsg.AddString("val2", item->rule.value2);
      ruleMsg.AddBool("exclude", item->rule.exclude);
      genMsg.AddMessage("rule", &ruleMsg);
    }

    BMenuItem *limitItem = fLimitModeField->Menu()->FindMarked();
    if (limitItem) {
      int32 index = fLimitModeField->Menu()->IndexOf(limitItem);
      genMsg.AddInt32("limit_mode", index);
      genMsg.AddInt32("limit_value", atoi(fLimitValue->Text()));
    }

    genMsg.AddBool("shuffle", fShuffleCheck->Value() == B_CONTROL_ON);

    fTarget.SendMessage(&genMsg);
    Quit();
    break;
  }

  default:
    BWindow::MessageReceived(msg);
    break;
  }
}
