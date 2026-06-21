#include "UndoManager.h"

#include <Window.h>

UndoManager::UndoManager(BWindow *window) : fWindow(window) {}

void UndoManager::RecordAction(std::vector<BMessage> undoMsgs,
                               std::vector<BMessage> redoMsgs) {
  if (undoMsgs.empty() || redoMsgs.empty())
    return;

  for (auto &m : undoMsgs)
    if (!m.HasBool("undo_replay"))
      m.AddBool("undo_replay", true);
  for (auto &m : redoMsgs)
    if (!m.HasBool("undo_replay"))
      m.AddBool("undo_replay", true);

  Action action;
  action.undoMsgs = std::move(undoMsgs);
  action.redoMsgs = std::move(redoMsgs);
  fUndoStack.push_back(std::move(action));
  if ((int32)fUndoStack.size() > kMaxDepth)
    fUndoStack.pop_front();

  // A new action invalidates the redo history.
  fRedoStack.clear();
}

bool UndoManager::Undo() {
  if (fUndoStack.empty())
    return false;

  Action action = std::move(fUndoStack.back());
  fUndoStack.pop_back();
  _Post(action.undoMsgs);
  fRedoStack.push_back(std::move(action));
  if ((int32)fRedoStack.size() > kMaxDepth)
    fRedoStack.pop_front();
  return true;
}

bool UndoManager::Redo() {
  if (fRedoStack.empty())
    return false;

  Action action = std::move(fRedoStack.back());
  fRedoStack.pop_back();
  _Post(action.redoMsgs);
  fUndoStack.push_back(std::move(action));
  if ((int32)fUndoStack.size() > kMaxDepth)
    fUndoStack.pop_front();
  return true;
}

void UndoManager::_Post(std::vector<BMessage> &msgs) {
  if (!fWindow)
    return;
  for (auto &m : msgs)
    fWindow->PostMessage(&m);
}
