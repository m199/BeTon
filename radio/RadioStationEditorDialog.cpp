#include "RadioStationEditorDialog.h"
#include "Debug.h"

#include <Button.h>
#include <Catalog.h>
#include <GridLayout.h>
#include <GridView.h>
#include <LayoutBuilder.h>
#include <SeparatorView.h>
#include <StringView.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RadioStationEditorDialog"

static const uint32 kMsgOK = 'rsOK';
static const uint32 kMsgCancel = 'rsCn';

/**
 * @brief Constructs and lays out the radio station dialog.
 *
 * Uses BLayoutBuilder for a clean vertical layout with labeled
 * text fields and OK/Cancel buttons.
 *
 * @param target Messenger to receive MSG_RADIO_SAVE on confirm.
 * @param editIndex Index of station being edited, or -1 for new.
 * @param name Pre-filled station name.
 * @param url Pre-filled stream URL.
 * @param genre Pre-filled genre.
 * @param country Pre-filled country.
 * @param language Pre-filled language.
 */
RadioStationEditorDialog::RadioStationEditorDialog(BMessenger target, int32 editIndex,
                                       const char *name, const char *url,
                                       const char *genre, const char *country,
                                       const char *language)
    : BWindow(BRect(200, 200, 560, 440),
              editIndex >= 0 ? B_TRANSLATE("Edit Station")
                             : B_TRANSLATE("Add Station"),
              B_TITLED_WINDOW,
              B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
      fTarget(target), fEditIndex(editIndex) {

  fNameField = new BTextControl("name", B_TRANSLATE("Name:"), name, nullptr);
  fUrlField = new BTextControl("url", B_TRANSLATE("URL:"), url, nullptr);
  fGenreField =
      new BTextControl("genre", B_TRANSLATE("Genre:"), genre, nullptr);
  fCountryField =
      new BTextControl("country", B_TRANSLATE("Country:"), country, nullptr);
  fLanguageField =
      new BTextControl("language", B_TRANSLATE("Language:"), language, nullptr);

  BButton *okBtn =
      new BButton(B_TRANSLATE("OK"), new BMessage(kMsgOK));
  BButton *cancelBtn =
      new BButton(B_TRANSLATE("Cancel"), new BMessage(kMsgCancel));

  okBtn->MakeDefault(true);

  BGridView *form = new BGridView("stationForm");
  BGridLayout *grid = form->GridLayout();
  grid->SetSpacing(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
  grid->SetColumnWeight(0, 0.0f);
  grid->SetColumnWeight(1, 1.0f);

  int32 row = 0;
  auto addField = [&](BTextControl *field) {
    BLayoutItem *labelItem = field->CreateLabelLayoutItem();
    labelItem->SetExplicitAlignment(BAlignment(B_ALIGN_RIGHT, B_ALIGN_MIDDLE));
    grid->AddItem(labelItem, 0, row);

    BLayoutItem *textItem = field->CreateTextViewLayoutItem();
    textItem->SetExplicitMinSize(BSize(260.0f, B_SIZE_UNSET));
    textItem->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    grid->AddItem(textItem, 1, row);
    row++;
  };

  addField(fNameField);
  addField(fUrlField);
  addField(fGenreField);
  addField(fCountryField);
  addField(fLanguageField);

  BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
      .SetInsets(B_USE_WINDOW_INSETS)
      .Add(form)
      .Add(new BSeparatorView(B_HORIZONTAL))
      .AddGroup(B_HORIZONTAL)
          .AddGlue()
          .Add(cancelBtn)
          .Add(okBtn)
      .End();

  fNameField->MakeFocus(true);
}

/**
 * @brief Handles OK and Cancel button presses.
 *
 * On OK, validates that at least a URL is provided, then sends
 * MSG_RADIO_SAVE to the target with the station data.
 */
void RadioStationEditorDialog::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case kMsgOK: {
    BString url = fUrlField->Text();
    if (url.IsEmpty()) {
      fUrlField->MakeFocus(true);
      break;
    }

    BMessage save(MSG_RADIO_SAVE);
    save.AddString("name", fNameField->Text());
    save.AddString("url", url);
    save.AddString("genre", fGenreField->Text());
    save.AddString("country", fCountryField->Text());
    save.AddString("language", fLanguageField->Text());
    save.AddInt32("edit_index", fEditIndex);
    fTarget.SendMessage(&save);
    PostMessage(B_QUIT_REQUESTED);
    break;
  }
  case kMsgCancel:
    PostMessage(B_QUIT_REQUESTED);
    break;
  default:
    BWindow::MessageReceived(msg);
  }
}
