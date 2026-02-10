#include "PlaylistListView.h"
#include "Debug.h"
#include "Messages.h"
#include "NamePrompt.h"
#include "PlaylistUtils.h"
#include <Alert.h>
#include <Application.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <InterfaceDefs.h>
#include <MenuItem.h>
#include <Path.h>
#include <Resources.h>
#include <String.h>
#include <TextControl.h>
#include <algorithm>
#include <cinttypes>
#include <map>
#include <stdio.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaylistListView"

static constexpr int32 ICON_LIB_ID = 1001;
static constexpr int32 ICON_PL_ID = 1002;

static BBitmap *LoadVectorIconFromResourceID(int32 id, float size) {
  if (!be_app || !be_app->AppResources())
    return nullptr;

  size_t len = 0;
  const void *data =
      be_app->AppResources()->LoadResource(B_VECTOR_ICON_TYPE, id, &len);
  if (!data || len == 0) {
    fprintf(stderr, "[PlaylistListView] Icon ID %ld not found\n", (long)id);
    return nullptr;
  }

  BRect r(0, 0, size - 1, size - 1);
  auto *bmp = new BBitmap(r, 0, B_RGBA32);
  if (BIconUtils::GetVectorIcon(static_cast<const uint8 *>(data), len, bmp) !=
      B_OK) {
    delete bmp;
    fprintf(stderr, "[PlaylistListView] Icon ID %ld: decoding failed\n",
            (long)id);
    return nullptr;
  }
  return bmp;
}

static float BaselineForRow(BView *v, const BRect &rowRect) {
  font_height fh;
  v->GetFontHeight(&fh);
  const float textH = ceilf(fh.ascent + fh.descent + fh.leading);
  return rowRect.top + floorf((rowRect.Height() - textH) / 2.0f) + fh.ascent;
}

PlaylistListView::PlaylistListView(const char *name, BMessenger target)
    : SimpleColumnView(name), fTarget(target), fContextMenu(nullptr) {

  AddItem("Library", false, PlaylistItemKind::Library);

  fContextMenu = new BPopUpMenu("PlaylistMenu");
  fContextMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Delete"), new BMessage(MSG_DELETE_PLAYLIST)));
  fContextMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Rename"), new BMessage(MSG_RENAME_PLAYLIST)));
  fContextMenu->SetTargetForItems(this);
}

PlaylistListView::~PlaylistListView() { delete fContextMenu; }

void PlaylistListView::SelectionChanged(int32 index) {
  if (index >= 0) {
    BMessage msg(MSG_PLAYLIST_SELECTION);
    msg.AddInt32("index", index);
    msg.AddString("name", ItemAt(index));

    fTarget.SendMessage(&msg);

    DEBUG_PRINT("[PlaylistListView] SelectionChanged → %ld (%s)\n", (long)index,
                ItemAt(index).String());
  }
}

void PlaylistListView::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case B_SIMPLE_DATA: {
    int32 sourceIndex;
    if (msg->FindInt32("playlist_index", &sourceIndex) == B_OK) {

      if (fDropLineIndex >= 0) {
        _ReorderItem(sourceIndex, fDropLineIndex);

        BMessage orderChanged(MSG_PLAYLIST_ORDER_CHANGED);
        fTarget.SendMessage(&orderChanged);
      }

      fDragIndex = -1;
      fDropLineIndex = -1;
      fIsDragging = false;
      Invalidate();
      break;
    }

    if (!msg->HasRef("refs"))
      break;

    int32 dropIndex = (int32)(fLastDropPoint.y / LineHeight());
    if (dropIndex < 0 || dropIndex >= CountItems()) {

      BMessage newMsg(MSG_NEW_PLAYLIST);
      newMsg.AddMessage("files", msg);
      fTarget.SendMessage(&newMsg);
    } else {
      if (!IsWritableAt(dropIndex)) {
        DEBUG_PRINT("[PlaylistListView] Drop on non-writable playlist -> "
                    "ignored (idx=%ld)\n",
                    (long)dropIndex);
        SetHoverIndex(-1);
        break;
      }

      BString playlistName = ItemAt(dropIndex);
      entry_ref ref;
      int32 i = 0;
      while (msg->FindRef("refs", i++, &ref) == B_OK) {
        BPath path(&ref);
        DEBUG_PRINT("[PlaylistListView] File '%s' → Playlist '%s'\n",
                    path.Path(), playlistName.String());
        AddFileToPlaylist(dropIndex, ref);
      }
    }

    SetHoverIndex(-1);
    break;
  }

  case MSG_RENAME_PLAYLIST: {
    int32 index = CurrentSelection();
    if (index > 0 && index < CountItems() && IsWritableAt(index)) {
      BString oldName = ItemAt(index);
      NamePrompt *prompt = new NamePrompt(BMessenger(Window()));
      prompt->SetInitialName(oldName);
      prompt->SetMessageWhat(MSG_NAME_PROMPT_RENAME);
      prompt->Show();
      prompt->SetTitle(oldName.String());
    }
    break;
  }

  case MSG_DELETE_PLAYLIST:
    DEBUG_PRINT("[PlaylistListView] MSG_DELETE_PLAYLIST received\n");
    RemoveSelectedPlaylist();
    break;

  default:
    SimpleColumnView::MessageReceived(msg);
  }
}

int32 PlaylistListView::AddItem(const char *title, bool writable) {
  return AddItem(title, writable, PlaylistItemKind::Playlist);
}

int32 PlaylistListView::AddItem(const char *title, bool writable,
                                PlaylistItemKind kind) {
  SimpleColumnView::AddItem(title);
  fRows.push_back({BString(title), writable, kind});
  Invalidate();
  return CountItems() - 1;
}

int32 PlaylistListView::FindIndexByName(const BString &name) const {
  for (int32 i = 0; i < CountItems(); ++i) {
    if (ItemAt(i) == name)
      return i;
  }
  return -1;
}

void PlaylistListView::MouseDown(BPoint where) {
  SetHoverIndex(-1);
  MakeFocus(true);

  int32 index = (int32)(where.y / LineHeight());
  if (index < 0 || index >= CountItems())
    index = -1;

  uint32 buttons;
  GetMouse(&where, &buttons);
  if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0 && index >= 0) {
    Select(index);
    ConvertToScreen(&where);

    BString clickedName = ItemAt(index);
    delete fContextMenu;
    fContextMenu = new BPopUpMenu("PlaylistMenu");

    if (!IsWritableAt(index)) {

    } else {
      fContextMenu->AddItem(new BMenuItem(B_TRANSLATE("Rename"),
                                          new BMessage(MSG_RENAME_PLAYLIST)));
      fContextMenu->AddItem(new BMenuItem(B_TRANSLATE("Delete"),
                                          new BMessage(MSG_DELETE_PLAYLIST)));
    }

    if (fContextMenu->CountItems() > 0) {
      fContextMenu->SetTargetForItems(this);
      BMenuItem *chosen =
          fContextMenu->Go(where, false, false, BRect(where, where), false);
      if (chosen) {
        DEBUG_PRINT("[PlaylistListView] Menu selected: %s\n", chosen->Label());
        BMessage *m = chosen->Message();
        if (m)
          MessageReceived(m);
      }
    }
    return;
  }

  if (index >= 0) {
    fDragStartPoint = where;
    fDragIndex = index;
  }

  SimpleColumnView::MouseDown(where);
}

void PlaylistListView::MouseMoved(BPoint point, uint32 transit,
                                  const BMessage *dragMsg) {

  if (!fIsDragging && fDragIndex >= 0) {
    uint32 buttons;
    GetMouse(NULL, &buttons);

    if (buttons & B_PRIMARY_MOUSE_BUTTON) {

      float dx = point.x - fDragStartPoint.x;
      float dy = point.y - fDragStartPoint.y;
      float distance = sqrtf(dx * dx + dy * dy);

      if (distance > 5.0f) {
        fIsDragging = true;

        BMessage dragMsg(B_SIMPLE_DATA);
        dragMsg.AddInt32("playlist_index", fDragIndex);

        float rowTop = fDragIndex * LineHeight();
        BRect dragRect(0, rowTop, Bounds().Width(), rowTop + LineHeight() - 1);
        DragMessage(&dragMsg, dragRect, this);
        return;
      }
    } else {
      fDragIndex = -1;
      fIsDragging = false;
    }
  }

  if (dragMsg && dragMsg->what == B_SIMPLE_DATA) {
    int32 sourceIndex;
    if (dragMsg->FindInt32("playlist_index", &sourceIndex) == B_OK) {
      float rowH = LineHeight();
      int32 targetRow = (int32)((point.y + rowH / 2.0f) / rowH);
      targetRow = std::max((int32)0, std::min(targetRow, CountItems()));

      fDropLineIndex = targetRow;
      return;
    }

    if (dragMsg->HasRef("refs")) {
      fLastDropPoint = point;
      int32 idx = HitIndex(point);
      if (idx >= 0 && IsWritableAt(idx))
        SetHoverIndex(idx);
      else
        SetHoverIndex(-1);
    }
  } else {
    SetHoverIndex(-1);
    if (fDropLineIndex >= 0) {
      fDropLineIndex = -1;
    }
  }
  SimpleColumnView::MouseMoved(point, transit, dragMsg);
}

void PlaylistListView::RenameItem(const BString &oldName,
                                  const BString &newName) {
  for (auto &item : fItems) {
    if (item.text == oldName) {
      item.text = newName;
      break;
    }
  }
  for (auto &r : fRows) {
    if (r.label == oldName) {
      r.label = newName;
      break;
    }
  }
  Invalidate();
}

int32 PlaylistListView::CreateNewPlaylist(const char *title) {
  CreatePlaylist(title);
  int32 index = AddItem(title, true, PlaylistItemKind::Playlist);
  Select(index);
  UpdateScrollbars();
  DEBUG_PRINT("[PlaylistListView] New playlist '%s' created\n", title);
  return index;
}

void PlaylistListView::AddFileToPlaylist(int32 index, const entry_ref &ref) {
  if (index < 0 || index >= CountItems())
    return;
  if (!IsWritableAt(index))
    return;

  BString playlistName = ItemAt(index);
  BPath path(&ref);
  AddItemToPlaylist(path.Path(), playlistName);
  DEBUG_PRINT("[PlaylistListView] File '%s' saved to playlist '%s'\n",
              path.Path(), playlistName.String());
}

void PlaylistListView::RemoveSelectedPlaylist() {
  int32 index = CurrentSelection();
  if (index > 0 && index < CountItems()) {
    if (!IsWritableAt(index))
      return;
    BString name = ItemAt(index);
    DeletePlaylist(name);
    SimpleColumnView::RemoveItemAt(index);
    if ((size_t)index < fRows.size())
      fRows.erase(fRows.begin() + index);
    fCurrentSelection = -1;
    UpdateScrollbars();
    Invalidate();
    DEBUG_PRINT("[PlaylistListView] Playlist '%s' deleted\n", name.String());
  }
}

bool PlaylistListView::RemovePlaylistAt(int32 index) {
  if (index < 0 || index >= CountItems())
    return false;
  SimpleColumnView::RemoveItemAt(index);
  if ((size_t)index < fRows.size())
    fRows.erase(fRows.begin() + index);
  Invalidate();
  UpdateScrollbars();
  return true;
}

int32 PlaylistListView::HitIndex(BPoint p) const {
  if (p.y < 0)
    return -1;
  int32 idx = (int32)(p.y / LineHeight());
  return (idx >= 0 && idx < CountItems()) ? idx : -1;
}

void PlaylistListView::SetHoverIndex(int32 idx) {
  if (idx == fHoverIndex)
    return;
  int32 old = fHoverIndex;
  fHoverIndex = idx;
  BRect bounds = Bounds();
  if (old >= 0 && old < CountItems()) {
    BRect r(bounds.left, old * LineHeight(), bounds.right,
            (old + 1) * LineHeight() - 1);
    Invalidate(r);
  }
  if (fHoverIndex >= 0 && fHoverIndex < CountItems()) {
    BRect r(bounds.left, fHoverIndex * LineHeight(), bounds.right,
            (fHoverIndex + 1) * LineHeight() - 1);
    Invalidate(r);
  }
}

void PlaylistListView::_EnsureIconsLoaded() const {
  if (!fIconLibrary) {
    float rowH = const_cast<PlaylistListView *>(this)->LineHeight();
    fIconSize = rowH * 0.7f;
    fIconLibrary = LoadVectorIconFromResourceID(ICON_LIB_ID, fIconSize);
  }
  if (!fIconPlaylist)
    fIconPlaylist = LoadVectorIconFromResourceID(ICON_PL_ID, fIconSize);
}

BBitmap *PlaylistListView::_IconFor(PlaylistItemKind kind) const {
  _EnsureIconsLoaded();
  switch (kind) {
  case PlaylistItemKind::Library:
    return fIconLibrary;
  case PlaylistItemKind::Playlist:
    return fIconPlaylist;
  }
  return nullptr;
}

bool PlaylistListView::IsWritableAt(int32 index) const {
  if (index < 0 || index >= CountItems())
    return false;
  if ((size_t)index >= fRows.size())
    return false;
  return fRows[index].writable;
}

void PlaylistListView::SetIsUnwritableAt(int32 index, bool v) {
  if (index < 0 || index >= CountItems())
    return;
  if ((size_t)index >= fRows.size())
    return;
  fRows[index].writable = !v;
  Invalidate();
}

void PlaylistListView::SetIsUnwritableByName(const BString &name, bool v) {
  for (int32 i = 0; i < CountItems(); ++i) {
    if (ItemAt(i) == name) {
      SetIsUnwritableAt(i, v);
      break;
    }
  }
}

void PlaylistListView::Draw(BRect updateRect) {
  const float rowH = LineHeight();
  BRect bounds = Bounds();

  const rgb_color base = ui_color(B_LIST_BACKGROUND_COLOR);
  const float avg = (base.red + base.green + base.blue) / 3.0f;
  const bool isDark = (avg < 128.0f);
  const rgb_color stripe = tint_color(base, isDark ? 0.90f : 1.05f);

  const rgb_color selBG = fUseCustomColor
                              ? fSelectionColor
                              : ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
  const rgb_color textColor = ui_color(B_LIST_ITEM_TEXT_COLOR);
  const rgb_color selTextCol = fUseCustomColor
                                   ? fSelectionTextColor
                                   : ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);

  const int32 first = std::max<int32>(0, (int32)floorf(updateRect.top / rowH));
  const int32 last = std::min<int32>(CountItems() - 1,
                                     (int32)floorf(updateRect.bottom / rowH));

  for (int32 i = first; i <= last; ++i) {
    BRect rowRect(bounds.left, i * rowH, bounds.right, (i + 1) * rowH - 1);
    if (!rowRect.Intersects(updateRect))
      continue;

    const bool selected =
        ((size_t)i < fItems.size() ? fItems[i].selected
                                   : (i == fCurrentSelection));

    if (selected) {

      SetHighColor(selBG);
      FillRect(rowRect);
      SetHighColor((rgb_color){152, 152, 152, 255});
      StrokeRect(rowRect);
    } else {

      SetHighColor(((i & 1) == 0) ? base : stripe);
      FillRect(rowRect);
    }

    const float iconX = rowRect.left + fIconPadX;
    const float iconY =
        rowRect.top + floorf((rowRect.Height() + 1 - fIconSize) / 2.0f);

    if ((size_t)i < fRows.size()) {
      if (BBitmap *icon = _IconFor(fRows[i].kind)) {

        SetDrawingMode(B_OP_ALPHA);
        SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
        DrawBitmap(icon, BPoint(iconX, iconY));
        SetDrawingMode(B_OP_COPY);
      }
    }

    const float textLeft = iconX + fIconSize + fIconPadX;
    const float baseline = BaselineForRow(this, rowRect);
    SetHighColor(selected ? selTextCol : textColor);
    MovePenTo(textLeft, baseline);
    const BString &label =
        (size_t)i < fRows.size() ? fRows[i].label : fItems[i].text;
    DrawString(label.String());
  }

  {
    int32 nextRow =
        std::max<int32>(CountItems(), (int32)floorf(updateRect.top / rowH));
    float y = nextRow * rowH;
    while (y <= updateRect.bottom) {
      SetHighColor(((nextRow & 1) == 0) ? base : stripe);
      FillRect(BRect(bounds.left, y, bounds.right,
                     std::min(y + rowH - 1.0f, updateRect.bottom)));
      ++nextRow;
      y += rowH;
    }
  }

  if (fHoverIndex >= 0 && fHoverIndex < CountItems() &&
      fHoverIndex != fCurrentSelection && IsWritableAt(fHoverIndex)) {

    BRect rowRect(bounds.left, fHoverIndex * rowH, bounds.right,
                  (fHoverIndex + 1) * rowH - 1);
    if (rowRect.Intersects(updateRect)) {
      rgb_color c = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
      c.alpha = 60;
      SetDrawingMode(B_OP_ALPHA);
      SetHighColor(c);
      FillRect(rowRect);

      SetDrawingMode(B_OP_COPY);
      rgb_color frame = tint_color(c, B_DARKEN_1_TINT);
      frame.alpha = 255;
      SetHighColor(frame);
      StrokeRect(rowRect);
    }
  }

  // Drop line removed per user request
}

void PlaylistListView::_ReorderItem(int32 from, int32 to) {
  if (from < 0 || from >= CountItems() || to < 0 || to > CountItems() ||
      from == to)
    return;

  int32 targetIndex = to;
  if (to > from)
    targetIndex--;

  SimpleItem movedItem = fItems[from];
  PlaylistRow movedRow = fRows[from];

  fItems.erase(fItems.begin() + from);
  fRows.erase(fRows.begin() + from);

  fItems.insert(fItems.begin() + targetIndex, movedItem);
  fRows.insert(fRows.begin() + targetIndex, movedRow);

  Select(targetIndex);

  Invalidate();
  ScrollToSelection();

  DEBUG_PRINT("[PlaylistListView] Reordered: %ld -> %ld\n", (long)from,
              (long)targetIndex);
}

std::vector<BString> PlaylistListView::GetPlaylistOrder() const {
  std::vector<BString> order;
  for (int32 i = 0; i < CountItems(); ++i) {
    order.push_back(ItemAt(i));
  }
  return order;
}

void PlaylistListView::SetPlaylistOrder(const std::vector<BString> &order) {
  DEBUG_PRINT("[PlaylistListView] SetPlaylistOrder called with %zu items\n",
              order.size());

  std::map<BString, int32> targetPositions;
  for (size_t i = 0; i < order.size(); ++i) {
    targetPositions[order[i]] = i;
    DEBUG_PRINT("[PlaylistListView]   Order[%zu] = '%s'\n", i,
                order[i].String());
  }

  std::vector<SimpleItem> newItems;
  std::vector<PlaylistRow> newRows;

  for (const auto &name : order) {
    int32 currentIndex = FindIndexByName(name);
    if (currentIndex >= 0 && currentIndex < (int32)fItems.size()) {
      newItems.push_back(fItems[currentIndex]);
      newRows.push_back(fRows[currentIndex]);
      DEBUG_PRINT("[PlaylistListView]   Found '%s' at index %ld\n",
                  name.String(), (long)currentIndex);
    }
  }

  for (size_t i = 0; i < fItems.size(); ++i) {
    const BString &name = fItems[i].text;
    if (targetPositions.find(name) == targetPositions.end()) {
      newItems.push_back(fItems[i]);
      newRows.push_back(fRows[i]);
      DEBUG_PRINT("[PlaylistListView]   Appending '%s' (not in saved order)\n",
                  name.String());
    }
  }

  fItems = newItems;
  fRows = newRows;

  Invalidate();
  UpdateScrollbars();

  DEBUG_PRINT(
      "[PlaylistListView] SetPlaylistOrder complete, now have %zu items\n",
      fItems.size());
}

