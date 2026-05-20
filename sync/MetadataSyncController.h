#ifndef BETON_METADATA_SYNC_CONTROLLER_H
#define BETON_METADATA_SYNC_CONTROLLER_H

#include <Message.h>
#include <String.h>
#include <vector>

class MainWindow;
class MetadataSyncConflictDialog;

/**
 * @class MetadataSyncController
 * @brief Coordinates metadata sync execution and conflict resolution flow.
 */
class MetadataSyncController {
public:
  MetadataSyncController(MainWindow *window);
  ~MetadataSyncController();

  void StartSmartSync();
  void UpdateSyncProgress(BMessage *msg);
  void QueueSyncConflict(BMessage *msg);
  void ResolveSyncConflict(BMessage *msg);
  void ResolveAllSyncConflicts(BMessage *msg);
  void CancelSyncConflicts();
  void ShowNextConflictDialog();

private:
  MainWindow *fWindow;

  /**
   * @brief Runtime state for queued conflict dialogs.
   */
  bool fConflictDialogOpen{false};
  int32 fConflictsProcessed{0};
  MetadataSyncConflictDialog *fActiveMetadataConflictDialog{nullptr};
  std::vector<BMessage> fPendingConflicts;
};

#endif // BETON_METADATA_SYNC_CONTROLLER_H
