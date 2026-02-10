#ifndef DIRECTORY_MANAGER_WINDOW_H
#define DIRECTORY_MANAGER_WINDOW_H

#include <Box.h>
#include <Button.h>
#include <FilePanel.h>
#include <ListView.h>
#include <ScrollView.h>
#include <StringItem.h>
#include <Window.h>
#include <vector>

class MusicSource;

/**
 * @class DirectoryManagerWindow
 * @brief Window for managing the list of music directories.
 *
 * Allows the user to:
 * - View currently monitored folders.
 * - Add new folders via a standard BFilePanel.
 * - Remove folders from the list.
 *
 * Changes are saved to disk and the CacheManager is notified to rescan.
 */
class DirectoryManagerWindow : public BWindow {
public:
  /**
   * @brief Construct a new Directory Manager Window
   *
   * @param cacheManager Messenger target to receive the MSG_RESCAN command upon
   * saving.
   */
  DirectoryManagerWindow(BMessenger cacheManager);

  void MessageReceived(BMessage *msg) override;

private:
  void AddDirectory(const entry_ref &ref);
  void RemoveSelectedDirectory();
  void EditDirectory(int32 index);
  void SaveSettings();
  void LoadSettings();
  void MigrateFromOldFormat();

  /** @name UI Components */
  ///@{
  BListView *fDirectoryList;
  BButton *fBtnAdd;
  BButton *fBtnRemove;
  BButton *fBtnOK;
  BFilePanel *fAddPanel;
  ///@}

  /** @name Data */
  ///@{
  std::vector<MusicSource> fSources;
  BMessenger fCacheManager;
  ///@}
};

#endif // DIRECTORY_MANAGER_WINDOW_H
