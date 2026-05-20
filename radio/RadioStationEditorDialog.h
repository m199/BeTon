#ifndef BETON_RADIO_STATION_EDITOR_DIALOG_H
#define BETON_RADIO_STATION_EDITOR_DIALOG_H

#include "Messages.h"
#include <Messenger.h>
#include <TextControl.h>
#include <Window.h>

/**
 * @class RadioStationEditorDialog
 * @brief Modal dialog for adding or editing a radio station.
 *
 * Provides text fields for station name, stream URL, genre,
 * country and language. Sends MSG_RADIO_SAVE on confirmation.
 */
class RadioStationEditorDialog : public BWindow {
public:
  /**
   * @brief Constructs the dialog.
   * @param target Messenger to receive the result message.
   * @param editIndex Index of station being edited, or -1 for new.
   * @param name Initial station name.
   * @param url Initial stream URL.
   * @param genre Initial genre.
   * @param country Initial country.
   * @param language Initial language.
   */
  RadioStationEditorDialog(BMessenger target, int32 editIndex = -1,
                           const char *name = "", const char *url = "",
                           const char *genre = "", const char *country = "",
                           const char *language = "");

  void MessageReceived(BMessage *msg) override;

private:
  BMessenger fTarget;
  int32 fEditIndex;
  BTextControl *fNameField;
  BTextControl *fUrlField;
  BTextControl *fGenreField;
  BTextControl *fCountryField;
  BTextControl *fLanguageField;
};

#endif // BETON_RADIO_STATION_EDITOR_DIALOG_H
