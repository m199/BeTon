#ifndef BETON_UNDO_MANAGER_H
#define BETON_UNDO_MANAGER_H

#include <Message.h>
#include <deque>
#include <vector>

class BWindow;

/**
 * @class UndoManager
 * @brief Generic undo/redo stack based on replayable BMessages.
 *
 * Each recorded action carries two message lists: posting the undo list
 * reverts the action, posting the redo list applies it again. Messages
 * are replayed through the main window's message loop, so undo/redo take
 * the exact same code paths as the original user action. All replayed
 * messages carry "undo_replay" = true so the handlers that performed the
 * original action do not record the replay as a new action.
 */
class UndoManager {
public:
  static constexpr int32 kMaxDepth = 10;

  UndoManager(BWindow *window);

  /**
   * @brief Records an undoable action and clears the redo stack.
   * @param undoMsgs Messages that revert the action when posted.
   * @param redoMsgs Messages that re-apply the action when posted.
   */
  void RecordAction(std::vector<BMessage> undoMsgs,
                    std::vector<BMessage> redoMsgs);

  bool Undo();
  bool Redo();

  bool CanUndo() const { return !fUndoStack.empty(); }
  bool CanRedo() const { return !fRedoStack.empty(); }

private:
  struct Action {
    std::vector<BMessage> undoMsgs;
    std::vector<BMessage> redoMsgs;
  };

  void _Post(std::vector<BMessage> &msgs);

  BWindow *fWindow;
  std::deque<Action> fUndoStack;
  std::deque<Action> fRedoStack;
};

#endif // BETON_UNDO_MANAGER_H
