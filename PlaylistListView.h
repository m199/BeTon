#ifndef PLAYLIST_LIST_VIEW_H
#define PLAYLIST_LIST_VIEW_H

#include "SimpleColumnView.h"
#include <Bitmap.h>
#include <Entry.h>
#include <Messenger.h>
#include <PopUpMenu.h>
#include <String.h>
#include <vector>

enum class PlaylistItemKind { Library, Playlist };

struct PlaylistRow {
  /** @name Data */
  ///@{
  BString label;
  bool writable;
  PlaylistItemKind kind;
  ///@}
};

class PlaylistListView : public SimpleColumnView {
public:
  PlaylistListView(const char *name, BMessenger target);
  virtual ~PlaylistListView();

  void SelectionChanged(int32 index) override;
  void MessageReceived(BMessage *msg) override;
  void MouseDown(BPoint where) override;
  void MouseMoved(BPoint point, uint32 transit,
                  const BMessage *dragMsg) override;
  void Draw(BRect updateRect) override;

  /** @name Modification Logic */
  ///@{
  int32 CreateNewPlaylist(const char *title);
  void AddFileToPlaylist(int32 index, const entry_ref &ref);
  void RemoveSelectedPlaylist();
  void RenameItem(const BString &oldName, const BString &newName);

  bool IsWritableAt(int32 index) const;
  void SetIsUnwritableAt(int32 index, bool v);
  void SetIsUnwritableByName(const BString &name, bool v);
  bool RemovePlaylistAt(int32 index);

  int32 AddItem(const char *title, bool writable);
  int32 AddItem(const char *title, bool writable, PlaylistItemKind kind);

  std::vector<BString> GetPlaylistOrder() const;
  void SetPlaylistOrder(const std::vector<BString> &order);
  ///@}
  /** @name Internal Helpers */
  ///@{
  int32 FindIndexByName(const BString &name) const;

private:
  int32 HitIndex(BPoint p) const;
  void SetHoverIndex(int32 idx);

  void _EnsureIconsLoaded() const;
  BBitmap *_IconFor(PlaylistItemKind kind) const;

  void _ReorderItem(int32 from, int32 to);
  ///@}

  /** @name Data & State */
  ///@{
  BMessenger fTarget;
  BPopUpMenu *fContextMenu;
  BPoint fLastDropPoint;

  std::vector<PlaylistRow> fRows;

  int32 fHoverIndex{-1};

  int32 fDragIndex{-1};
  int32 fDropLineIndex{-1};
  BPoint fDragStartPoint;
  bool fIsDragging{false};

  mutable BBitmap *fIconLibrary = nullptr;
  mutable BBitmap *fIconPlaylist = nullptr;

  mutable float fIconSize = 16.0f;
  float fIconPadX = 6.0f;
  float fIconPadY = 2.0f;
  ///@}
};

#endif
