/**
 * @file MetadataSyncConflictDialog.h
 * @brief Dialog for resolving metadata conflicts between Tags and BFS.
 */

#ifndef BETON_METADATA_SYNC_CONFLICT_DIALOG_H
#define BETON_METADATA_SYNC_CONFLICT_DIALOG_H

#include "MetadataTagIO.h"

#include <Button.h>
#include <RadioButton.h>
#include <StringView.h>
#include <Window.h>

/**
 * @class MetadataSyncConflictDialog
 * @brief Modal dialog for resolving metadata sync conflicts.
 *
 * Displays Tags and BFS metadata side-by-side with radio buttons
 * beneath each column. User clicks the source they want to keep.
 */
class MetadataSyncConflictDialog : public BWindow {
public:
  /**
   * @brief Construct a new metadata sync conflict dialog.
   * @param target Messenger to receive user decisions.
   * @param filePath Path to the file with conflicting metadata.
   * @param tags Metadata from embedded tags.
   * @param bfs Metadata from BFS attributes.
   * @param index Current file index in batch.
   * @param total Total files currently pending in batch.
   */
  MetadataSyncConflictDialog(BMessenger target, const BString &filePath,
                             const TagData &tags, const TagData &bfs,
                             int32 index, int32 total);

  void MessageReceived(BMessage *msg) override;
  /**
   * @brief Updates the displayed total count.
   * @param newTotal The new total number of files.
   */
  void UpdateTotal(int32 newTotal);

private:
  void BuildLayout();
  void SendChoice(uint32 what);

  BMessenger fTarget;
  BString fFilePath;
  TagData fTags;
  TagData fBfs;
  int32 fIndex;
  int32 fTotal;

  BRadioButton *fUseTags;
  BRadioButton *fUseBfs;
  BStringView *fFileLabel{nullptr};
};

#endif // BETON_METADATA_SYNC_CONFLICT_DIALOG_H
