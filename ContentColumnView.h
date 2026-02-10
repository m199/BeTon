#ifndef CONTENT_COLUMN_VIEW_H
#define CONTENT_COLUMN_VIEW_H

#include "MediaItem.h"
#include "Messages.h"
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <MessageFilter.h>
#include <PopUpMenu.h>
#include <map>
#include <vector>

/**
 * @class ContentColumnView
 * @brief The main list view displaying the audio library.
 *
 * It supports:
 * - Multi-column display (Title, Artist, Album, etc.).
 * - Sorting by clicking column headers.
 * - Drag & Drop of items.
 * - Context menus.
 * - Asynchronous chunked loading to keep the UI responsive.
 * - Graying out missing files.
 */
class ContentColumnView : public BColumnListView {
public:
  ContentColumnView(const char *name);
  virtual ~ContentColumnView();

  /**
   * @brief Adds a single media item to the view.
   */
  void AddEntry(const MediaItem &mi);

  /**
   * @brief Adds a list of items asynchronously (chunked).
   */
  void AddEntries(const std::vector<MediaItem> &items);

  void ClearEntries();
  void RefreshScrollbars();

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
   * @brief Updates the rating display for a specific file path.
   * @param path The file path to update
   * @param rating The new rating value (0-10)
   */
  void UpdateRating(const BString &path, int32 rating);

  /**
   * @brief Reloads metadata for a single file from disk.
   */
  void ReloadEntry(const BString &path);

  void SaveState(BMessage *msg);
  void LoadState(BMessage *msg);

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
  static constexpr uint32 kMsgChunkAdd = 'chnk';
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
};

#endif
