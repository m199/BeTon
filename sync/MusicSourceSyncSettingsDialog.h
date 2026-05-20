/**
 * @file MusicSourceSyncSettingsDialog.h
 * @brief Dialog for configuring metadata synchronization settings.
 */

#ifndef BETON_MUSIC_SOURCE_SYNC_SETTINGS_DIALOG_H
#define BETON_MUSIC_SOURCE_SYNC_SETTINGS_DIALOG_H

#include <Button.h>
#include <MenuField.h>
#include <String.h>
#include <Window.h>

class MusicSourceSettings;

/**
 * @class MusicSourceSyncSettingsDialog
 * @brief Modal dialog to configure sync settings when adding a music directory.
 *
 * Presents the user with options to configure:
 * - Primary metadata source (Tags, BFS, or None)
 * - Secondary metadata source (Tags, BFS, or None)
 * - Conflict resolution mode
 * - Auto-sync on file changes
 */
class MusicSourceSyncSettingsDialog : public BWindow {
public:
  /**
   * @brief Constructor.
   * @param target Target to send results to.
   * @param index Index of the item being edited (-1 for new).
   * @param path Directory path being configured.
   * @param isBfs Whether the volume is BFS (affects available options).
   * @param existing Optional existing MusicSourceSettings settings to edit.
   */
  MusicSourceSyncSettingsDialog(
      BMessenger target, int32 index, const BString &path, bool isBfs,
      const MusicSourceSettings *existing = nullptr);

  /**
   * @brief Destructor.
   */
  virtual ~MusicSourceSyncSettingsDialog();

  /**
   * @brief Message handler.
   */
  virtual void MessageReceived(BMessage *msg);

private:
  void _BuildUI();
  void _LoadExistingSettings();
  void _UpdateControls();
  void _SendSettings();

  BMessenger fTarget;
  int32 fIndex;
  BString fPath;
  bool fIsBfs;
  const MusicSourceSettings *fExisting;

  BMenuField *fPrimaryMenu;
  BMenuField *fSecondaryMenu;
  BMenuField *fConflictMenu;
  BButton *fOkButton;
  BButton *fCancelButton;
};

#endif // BETON_MUSIC_SOURCE_SYNC_SETTINGS_DIALOG_H
