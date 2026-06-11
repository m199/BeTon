#ifndef BETON_MEDIA_TABLE_VIEW_H
#define BETON_MEDIA_TABLE_VIEW_H

#include "MediaItem.h"
#include "Messages.h"
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <MessageFilter.h>
#include <PopUpMenu.h>
#include <map>
#include <string>
#include <vector>

class CellTextControl;

/**
 * @class MediaTableView
 * @brief The main list view displaying the audio library.
 *
 * It supports:
 * Multi-column display (Title, Artist, Album, etc.).
 * Sorting by clicking column headers.
 * Drag & Drop of items.
 * Context menus.
 * Graying out missing files. (Here we still need a solution what todo with them, maybe delete from DB?)
 * 
 */
class MediaTableView : public BColumnListView {
public:
  MediaTableView(const char *name);
  virtual ~MediaTableView();

  /**
   * @brief Adds a single media item to the view.
   */
  void AddEntry(const MediaItem &mi);

  /**
   * @brief Adds a list of items asynchronously (chunked).
   */
  void AddEntries(std::vector<MediaItem> items);

  void ClearEntries();
  void RefreshScrollbars();

  /**
   * @brief Builds a playback queue from the current sorted view.
   * @param[out] outQueue Vector to fill with file paths in display order.
   * @note Skips missing files.
   */
  void BuildQueue(std::vector<std::string> &outQueue) const;

  static constexpr uint32 kMsgShowCtx = MSG_SHOW_CONTEXT_MENU;

  const MediaItem *SelectedItem() const;
  const MediaItem *ItemAt(int32 index) const;
  bool IsRowMissing(BRow *row) const;

  /**
   * @brief Sets the path of the currently playing track.
   * The row with this path will be rendered in bold.
   */
  void SetNowPlayingPath(const BString &path);
  const BString &NowPlayingPath() const { return fNowPlayingPath; }

  /**
   * @brief Switches between library and radio column layouts.
   * @param radio True to show radio columns (Name, Genre, URL),
   *              false to restore library columns.
   */
  void SetRadioMode(bool radio);

  /**
   * @brief Sets whether the view is displaying a playlist.
   * In playlist mode, the Track column displays the sequence index.
   */
  void SetPlaylistMode(bool isPlaylist);

  /**
   * @brief Updates a single row in-place with new metadata without rebuilding the list.
   * @param mi The updated media item.
   */
  void UpdateItem(const MediaItem &mi);

  void StartCellEdit(BRow *row, BColumn *column, int32 colIdx, float colLeft, BView *targetView);
  void CommitCellEdit();
  void CancelCellEdit();
  const char *FieldNameForColumn(int32 colIdx) const;

  static constexpr uint32 MSG_COMMIT_EDIT = 'cmed';
  static constexpr uint32 MSG_CANCEL_EDIT = 'cned';
  static constexpr uint32 MSG_NAVIGATE_EDIT = 'nved';

  bool HasActiveEditor() const { return fActiveEditor != nullptr; }
  BView* ActiveEditor() const;

  void SaveState(BMessage *msg);
  void LoadState(BMessage *msg);
  
  void SaveScrollState();

  /**
   * @brief Saves the current sort column and direction.
   * @param msg Message to store sort state into.
   * @note Delegates to BColumnListView::SaveState which includes sort info.
   */
  void SaveSortState(BMessage *msg);

  /**
   * @brief Restores a previously saved sort column and direction.
   * @param msg Message containing saved sort state.
   */
  void RestoreSortState(BMessage *msg);

protected:
  bool InitiateDrag(BPoint point, bool wasSelected) override;
  void KeyDown(const char *bytes, int32 numBytes) override;
  void MouseMoved(BPoint where, uint32 transit,
                  const BMessage *dragMsg) override;

  void AttachedToWindow() override;
  void DetachedFromWindow() override;
  void MessageReceived(BMessage *msg) override;

private:
  /** @name Filters */
  ///@{
  class RightClickFilter;
  class DropFilter;
  RightClickFilter *fRCFilter = nullptr;
  DropFilter *fDropFilter = nullptr;
  ///@}

  void ShowContextMenu(BPoint screenWhere);
  /**
   * @note fRowMap seemed unused in the .cpp, but keeping declaration if needed
   * later.
   */
  std::map<BRow *, MediaItem> fRowMap;

  /** @name Chunked loading state */
  ///@{
  std::vector<MediaItem> fPendingItems;
  size_t fPendingIndex = 0;
  void _AddBatch(size_t count);
  BRow* _CreateRow(const MediaItem &mi, int32 playlistIndex = -1);
  void _ApplyPendingSortRestore();
  void _PreSortPendingItems();
  static constexpr uint32 kMsgChunkAdd = 'chnk';
  BMessage fPendingSortRestore; ///< Deferred sort state to apply after last batch
  bool fHasPendingSortRestore{false};
  bool fSortingDisabledForBatch{false}; ///< True while batch loading is active
  BString fTopVisiblePath;
  std::vector<BString> fSavedSelectedPaths;
  ///@}

  /** @name Internal drag-drop reordering */
  ///@{
  int32 fDragSourceIndex = -1;
  BPoint fLastDropPoint;
  ///@}

  /** @name Now playing indicator */
  ///@{
  BString fNowPlayingPath;
  ///@}

  /** @name Display modes */
  ///@{
  bool fIsRadioMode{false};
  bool fIsPlaylistMode{false};
  std::vector<std::pair<int32, BColumn*>> fHiddenColumns; ///< Columns removed in radio mode
  ///@}

  /** @name Cell Inline Editing */
  ///@{
  CellTextControl *fActiveEditor{nullptr};
  BRow *fEditingRow{nullptr};
  BColumn *fEditingColumn{nullptr};
  int32 fEditingColIdx{-1};
  BView *fEditingOutlineView{nullptr};
  void _NavigateCellEdit(int32 key);
  ///@}
};

#endif // BETON_MEDIA_TABLE_VIEW_H
