#ifndef BETON_SMART_PLAYLIST_GENERATOR_WINDOW_H
#define BETON_SMART_PLAYLIST_GENERATOR_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>

#include <vector>

class BTextControl;
class BMenuField;
class BCheckBox;
class BButton;
class BSlider;
class BPopUpMenu;
class BListView;
class BCardLayout;

/**
 * @struct Rule
 * @brief Represents a single filtering rule for playlist generation.
 */
struct Rule {
  int32 type;     ///< 0=Genre, 1=Artist, 2=Year.
  BString value;  ///< Primary search value (e.g. "Rock", "Metallica", "1990").
  BString value2; ///< Secondary value (e.g. "2000" for year range).
  bool exclude;   ///< If true, the rule is negated (NOT).

  /**
   * @brief Formats the rule as a human-readable string.
   */
  BString ToString() const;
};

/**
 * @class SmartPlaylistGeneratorWindow
 * @brief Window for creating playlists from rule-based criteria.
 *
 * Allows the user to define rules (positive or negative) based on Genre,
 * Artist, or Year, and specify optional limits and shuffle mode.
 */
class SmartPlaylistGeneratorWindow : public BWindow {
public:
  /**
   * @brief Constructs the window.
   * @param target The messenger to which the 'generate' message will be sent.
   * @param genres A list of available genres to populate the selection menu.
   */
  SmartPlaylistGeneratorWindow(BMessenger target,
                               const std::vector<BString> &genres);
  virtual ~SmartPlaylistGeneratorWindow();

  virtual void MessageReceived(BMessage *msg);

private:
  void _BuildUI(const std::vector<BString> &genres);
  void _UpdateInputFields();
  void _AddRule();
  void _RemoveRule();

  /** @name Data */
  ///@{
  BMessenger fTarget;
  std::vector<BString> fGenres;
  ///@}

  /** @name Rule Definition UI */
  ///@{
  BTextControl *fNameInput;

  BMenuField *fTypeField;

  BCardLayout *fInputCardLayout;
  BTextControl *fArtistInput;
  BTextControl *fYearFromInput;
  BTextControl *fYearToInput;
  BMenuField *fGenreSelect;
  BCheckBox *fExcludeCheck;
  BCheckBox *fShuffleCheck;
  BButton *fAddRuleBtn;
  ///@}

  /** @name Rule List UI */
  ///@{
  BListView *fRuleList;
  BButton *fRemoveRuleBtn;
  ///@}

  /** @name Limits & Actions */
  ///@{
  BMenuField *fLimitModeField;
  BTextControl *fLimitValue;

  BButton *fGenerateBtn;
  BButton *fCancelBtn;
  ///@}
};

#endif // BETON_SMART_PLAYLIST_GENERATOR_WINDOW_H
