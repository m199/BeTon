#ifndef BETON_PLAYLIST_NAME_DIALOG_H
#define BETON_PLAYLIST_NAME_DIALOG_H

#include <Messenger.h>
#include <Window.h>

class BTextControl;

/**
 * @class PlaylistNameDialog
 * @brief A simple modal-like window to prompt the user for a name (e.g., for a
 * playlist).
 *
 * It contains a text field and OK/Cancel buttons. It sends a message back to
 * the target BMessenger upon completion.
 */
class PlaylistNameDialog : public BWindow {
public:
  /**
   * @brief Constructs the prompt window.
   * @param target The messenger to receive the result message.
   */
  PlaylistNameDialog(BMessenger target);

  void MessageReceived(BMessage *msg) override;

  /**
   * @brief Sets the initial text in the input field.
   * @param name The name to display.
   */
  void SetInitialName(const BString &name);

  /**
   * @brief Sets the 'what' code for the result message.
   * @param what The command constant (e.g. MSG_PLAYLIST_CREATED).
   */
  void SetMessageWhat(uint32 what);

private:
  /** @name UI & State */
  ///@{
  BTextControl *fText;
  BMessenger fTarget;
  uint32 fMessageWhat;
  ///@}
};

#endif // BETON_PLAYLIST_NAME_DIALOG_H
