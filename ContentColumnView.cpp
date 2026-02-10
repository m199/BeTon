
#include "ContentColumnView.h"
#include "Entry.h"
#include "MainWindow.h"
#include "Messages.h"
#include "MusicSource.h"
#include "TagSync.h"
#include <Catalog.h>
#include <Font.h>
#include <Handler.h>
#include <Looper.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <View.h>
#include <Window.h>
#include <cinttypes>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ContentColumnView"

/**
 * @brief Calculate row height based on font for HiDPI scaling.
 * @return The calculated row height with 40% padding.
 */
static float CalculateRowHeight() {
  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;
  return ceilf(fontHeight * 1.4f);
}

/**
 * @class MediaRow
 * @brief Custom BRow subclass to store the associated MediaItem.
 */
class MediaRow : public BRow {
public:
  explicit MediaRow(const MediaItem &mi)
      : BRow(CalculateRowHeight()), fItem(mi) {}

  const MediaItem &Item() const { return fItem; }

private:
  MediaItem fItem;
};

/**
 * @class RightClickFilter
 * @brief Message filter for handling mouse events on the content list view.
 *
 * This filter handles:
 * - Right-click: Shows context menu via kMsgShowCtx
 * - Left-click on selected row: Initiates drag & drop if mouse moves >16px
 *
 * The filter checks if the click target is within the owner view hierarchy
 * before processing. Modifier keys (Shift, Cmd, Ctrl, Option) disable drag.
 */
class ContentColumnView::RightClickFilter : public BMessageFilter {
public:
  explicit RightClickFilter(ContentColumnView *owner)
      : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
        fOwner(owner) {}

  filter_result Filter(BMessage *msg, BHandler **target) override {
    if (!fOwner || !msg || msg->what != B_MOUSE_DOWN)
      return B_DISPATCH_MESSAGE;

    int32 buttons = 0;
    if (msg->FindInt32("buttons", &buttons) != B_OK)
      return B_DISPATCH_MESSAGE;

    BView *v = dynamic_cast<BView *>(*target);
    if (!v)
      return B_DISPATCH_MESSAGE;

    bool inside = false;
    for (BView *p = v; p; p = p->Parent()) {
      if (p == static_cast<BView *>(fOwner)) {
        inside = true;
        break;
      }
    }
    if (!inside)
      return B_DISPATCH_MESSAGE;

    BPoint screenWhere;
    if (msg->FindPoint("screen_where", &screenWhere) != B_OK) {
      BPoint where;
      if (msg->FindPoint("where", &where) != B_OK)
        return B_DISPATCH_MESSAGE;
      screenWhere = v->ConvertToScreen(where);
    }

    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
      BMessage show(ContentColumnView::kMsgShowCtx);
      show.AddPoint("screen_where", screenWhere);

      if (fOwner->Looper())
        fOwner->Looper()->PostMessage(&show, fOwner);

      return B_SKIP_MESSAGE;
    }

    if (buttons & B_PRIMARY_MOUSE_BUTTON) {
      int32 clicks = 1;
      msg->FindInt32("clicks", &clicks);
      if (clicks >= 2) {
        return B_DISPATCH_MESSAGE;
      }

      int32 modifiers = 0;
      if (msg->FindInt32("modifiers", &modifiers) == B_OK) {
        if (modifiers &
            (B_SHIFT_KEY | B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY)) {
          return B_DISPATCH_MESSAGE;
        }
      }

      BPoint where;
      if (msg->FindPoint("where", &where) != B_OK)
        return B_DISPATCH_MESSAGE;

      BRow *row = fOwner->RowAt(where);

      bool isSelected = false;
      if (row) {
        for (BRow *r = fOwner->CurrentSelection(); r;
             r = fOwner->CurrentSelection(r)) {
          if (r == row) {
            isSelected = true;
            break;
          }
        }
      }

      if (isSelected) {
        BPoint p;
        uint32 btns;
        v->GetMouse(&p, &btns);

        BPoint startP = v->ConvertFromScreen(screenWhere);

        while (btns) {
          float deltaX = p.x - startP.x;
          float deltaY = p.y - startP.y;

          if ((deltaX * deltaX + deltaY * deltaY) > 16.0f) {
            fOwner->fDragSourceIndex = fOwner->IndexOf(row);
            fOwner->InitiateDrag(where, true);
            return B_SKIP_MESSAGE;
          }

          snooze(10000);
          v->GetMouse(&p, &btns);
        }
      }
    }

    return B_DISPATCH_MESSAGE;
  }

private:
  ContentColumnView *fOwner;
};

/**
 * @class DropFilter
 * @brief Message filter for handling internal drag & drop reordering.
 *
 * Intercepts B_SIMPLE_DATA messages on the ScrollView when fDragSourceIndex
 * is set (indicating an internal drag). On drop, sends MSG_REORDER_PLAYLIST
 * to perform the actual reordering.
 */
class ContentColumnView::DropFilter : public BMessageFilter {
public:
  explicit DropFilter(ContentColumnView *owner)
      : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_SIMPLE_DATA),
        fOwner(owner) {}

  filter_result Filter(BMessage *msg, BHandler **target) override {
    if (!fOwner || !msg || msg->what != B_SIMPLE_DATA)
      return B_DISPATCH_MESSAGE;

    if (fOwner->fDragSourceIndex < 0)
      return B_DISPATCH_MESSAGE;

    BView *v = dynamic_cast<BView *>(*target);
    if (!v)
      return B_DISPATCH_MESSAGE;

    BPoint dropPoint;
    v->GetMouse(&dropPoint, nullptr);

    BRow *targetRow = fOwner->RowAt(dropPoint);
    int32 targetIndex =
        targetRow ? fOwner->IndexOf(targetRow) : fOwner->CountRows() - 1;
    int32 sourceIndex = fOwner->fDragSourceIndex;

    printf("[DropFilter] Drop detected: source=%ld, target=%ld"
           "\n",
           (long)sourceIndex, (long)targetIndex);
    fflush(stdout);

    if (sourceIndex != targetIndex && sourceIndex >= 0 && targetIndex >= 0) {
      BMessage reorderMsg(MSG_REORDER_PLAYLIST);
      reorderMsg.AddInt32("from_index", sourceIndex);
      reorderMsg.AddInt32("to_index", targetIndex);

      if (fOwner->Looper()) {
        fOwner->Looper()->PostMessage(&reorderMsg);
      }
    }

    fOwner->fDragSourceIndex = -1;
    return B_SKIP_MESSAGE;
  }

private:
  ContentColumnView *fOwner;
};

/**
 * @brief Appends indices of all selected rows to a message.
 * @param view The content view to query selections from.
 * @param into The message to append "index" fields to.
 */
static void AppendSelectedIndices(ContentColumnView *view, BMessage &into) {
  for (BRow *r = view->CurrentSelection(); r; r = view->CurrentSelection(r)) {
    int32 idx = view->IndexOf(r);
    if (idx >= 0)
      into.AddInt32("index", idx);
  }
}

/**
 * @brief Builds a message with file refs for all selected items.
 * @param view The content view to query selections from.
 * @param filesMsg The message to populate with "refs" entries.
 */
static void BuildFilesMessage(ContentColumnView *view, BMessage &filesMsg) {
  filesMsg.MakeEmpty();
  filesMsg.what = 0;
  for (BRow *r = view->CurrentSelection(); r; r = view->CurrentSelection(r)) {
    auto *mr = dynamic_cast<MediaRow *>(r);
    if (!mr)
      continue;
    entry_ref ref;
    if (get_ref_for_path(mr->Item().path.String(), &ref) == B_OK)
      filesMsg.AddRef("refs", &ref);
  }
}

/**
 * @class StatusStringField
 * @brief BStringField subclass that tracks whether the file is missing.
 *
 * Used to gray out text for missing files in the list view.
 * Also stores the item path for now-playing comparison.
 */
class StatusStringField : public BStringField {
public:
  StatusStringField(const char *string, bool missing, const BString &path = "",
                    SourceType source = SOURCE_TAGS)
      : BStringField(string), fMissing(missing), fPath(path), fSource(source) {}
  bool IsMissing() const { return fMissing; }
  const BString &Path() const { return fPath; }
  SourceType Source() const { return fSource; }

private:
  bool fMissing;
  BString fPath;
  SourceType fSource;
};

/**
 * @class StatusIntegerField
 * @brief BIntegerField subclass that tracks whether the file is missing.
 */
class StatusIntegerField : public BIntegerField {
public:
  StatusIntegerField(int32 number, bool missing,
                     SourceType source = SOURCE_TAGS, const BString &path = "")
      : BIntegerField(number), fMissing(missing), fSource(source), fPath(path) {
  }
  bool IsMissing() const { return fMissing; }
  SourceType Source() const { return fSource; }
  const BString &Path() const { return fPath; }

private:
  bool fMissing;
  SourceType fSource;
  BString fPath;
};

/**
 * @class StatusStringColumn
 * @brief Column that renders text in gray if the file is missing,
 *        and bold if the row is currently playing.
 */
class StatusStringColumn : public BStringColumn {
public:
  StatusStringColumn(const char *title, float width, float minWidth,
                     float maxWidth, uint32 truncate,
                     const char *attrName = nullptr,
                     alignment align = B_ALIGN_LEFT,
                     ContentColumnView *owner = nullptr)
      : BStringColumn(title, width, minWidth, maxWidth, truncate, align),
        fAttrName(attrName), fOwner(owner), fTitle(title) {}

  void SetOwner(ContentColumnView *owner) { fOwner = owner; }
  const char *Title() const { return fTitle.String(); }

  void DrawField(BField *field, BRect rect, BView *parent) override {
    StatusStringField *f = dynamic_cast<StatusStringField *>(field);
    rgb_color oldColor = parent->HighColor();
    bool isGray = (f && f->IsMissing());
    bool isBold = false;

    if (f && f->Source() == SOURCE_BFS && !fAttrName.IsEmpty() &&
        !f->Path().IsEmpty()) {
      BNode node(f->Path().String());
      if (node.InitCheck() == B_OK) {
        BString val;
        if (node.ReadAttrString(fAttrName.String(), &val) == B_OK) {
          f->SetString(val.String());
        }
      }
    }

    if (f && fOwner && !fOwner->NowPlayingPath().IsEmpty() &&
        !f->Path().IsEmpty() && f->Path() == fOwner->NowPlayingPath()) {
      isBold = true;
    }

    BFont oldFont;
    if (isBold) {
      parent->GetFont(&oldFont);
      BFont boldFont = oldFont;
      boldFont.SetFace(B_BOLD_FACE);
      parent->SetFont(&boldFont);
    }

    if (isGray) {
      parent->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
                                      B_DISABLED_LABEL_TINT));
    }

    if (field)
      BStringColumn::DrawField(field, rect, parent);

    parent->SetHighColor(oldColor);

    if (isBold) {
      parent->SetFont(&oldFont);
    }
  }

private:
  BString fAttrName;
  ContentColumnView *fOwner = nullptr;
  BString fTitle;
};

/**
 * @class StatusIntegerColumn
 * @brief Column that renders integers in gray if the file is missing.
 */
class StatusIntegerColumn : public BIntegerColumn {
public:
  StatusIntegerColumn(const char *title, float width, float minWidth,
                      float maxWidth, const char *attrName = nullptr,
                      alignment align = B_ALIGN_LEFT)
      : BIntegerColumn(title, width, minWidth, maxWidth, align),
        fAttrName(attrName), fTitle(title) {}

  const char *Title() const { return fTitle.String(); }

  void DrawField(BField *field, BRect rect, BView *parent) override {
    StatusIntegerField *f = dynamic_cast<StatusIntegerField *>(field);
    rgb_color oldColor = parent->HighColor();
    bool isGray = (f && f->IsMissing());

    if (f && f->Source() == SOURCE_BFS && !fAttrName.IsEmpty() &&
        !f->Path().IsEmpty()) {
      BNode node(f->Path().String());
      if (node.InitCheck() == B_OK) {
        int32 val = 0;
        if (node.ReadAttr(fAttrName.String(), B_INT32_TYPE, 0, &val,
                          sizeof(int32)) > 0) {
          f->SetValue(val);
        }
      }
    }

    if (isGray) {
      parent->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
                                      B_DISABLED_LABEL_TINT));
    }

    if (field)
      BIntegerColumn::DrawField(field, rect, parent);

    parent->SetHighColor(oldColor);
  }

private:
  BString fAttrName;
  BString fTitle;
};

/**
 * @class RatingColumn
 * @brief Column that renders rating stars.
 */
class RatingColumn : public BStringColumn {
public:
  RatingColumn(const char *title, float width, float minWidth, float maxWidth)
      : BStringColumn(title, width, minWidth, maxWidth, B_TRUNCATE_END,
                      B_ALIGN_LEFT),
        fTitle(title) {}

  const char *Title() const { return fTitle.String(); }

  /**
   * @brief Converts a numeric rating (0-10) to a star string representation.
   * @param rating The rating value from 0 to 10.
   * @return A BString containing the star representation.
   */
  static BString RatingToStars(int32 rating) {
    if (rating < 0)
      rating = 0;
    if (rating > 10)
      rating = 10;

    int fullStars = rating / 2;
    bool halfStar = (rating % 2) == 1;
    int emptyStars = 5 - fullStars - (halfStar ? 1 : 0);

    BString result;
    for (int i = 0; i < fullStars; i++) {
      result << "★";
    }
    if (halfStar) {
      result << "⯪";
    }
    for (int i = 0; i < emptyStars; i++) {
      result << "☆";
    }
    return result;
  }

  void DrawField(BField *field, BRect rect, BView *parent) override {
    StatusStringField *sf = dynamic_cast<StatusStringField *>(field);
    if (!sf)
      return;

    if (sf && !sf->Path().IsEmpty()) {
      BNode node(sf->Path().String());
      if (node.InitCheck() == B_OK) {
        int32 rating = 0;
        if (node.ReadAttr("Media:Rating", B_INT32_TYPE, 0, &rating,
                          sizeof(int32)) > 0) {
          sf->SetString(RatingToStars(rating).String());
        }
      }
    }

    BFont oldFont;
    parent->GetFont(&oldFont);

    BStringColumn::DrawField(field, rect, parent);

    parent->SetFont(&oldFont);
  }

private:
  BString fTitle;
};

/**
 * @brief Constructor for the ContentColumnView.
 * @param name The name of the view.
 */
ContentColumnView::ContentColumnView(const char *name)
    : BColumnListView(name, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE) {
  SetSelectionMode(B_MULTIPLE_SELECTION_LIST);

  SetColor(B_COLOR_BACKGROUND, ui_color(B_LIST_BACKGROUND_COLOR));
  SetColor(B_COLOR_TEXT, ui_color(B_LIST_ITEM_TEXT_COLOR));
  SetColor(B_COLOR_SELECTION, ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
  SetColor(B_COLOR_SELECTION_TEXT, ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
  SetColor(B_COLOR_ROW_DIVIDER, B_TRANSPARENT_COLOR);
  SetColor(B_COLOR_HEADER_BACKGROUND, ui_color(B_PANEL_BACKGROUND_COLOR));
  SetColor(B_COLOR_HEADER_TEXT, ui_color(B_PANEL_TEXT_COLOR));

  AddColumn(new StatusStringColumn(B_TRANSLATE("Title"), 200, 50, 500,
                                   B_TRUNCATE_END, "Media:Title"),
            0);
  AddColumn(new StatusStringColumn(B_TRANSLATE("Artist"), 150, 50, 300,
                                   B_TRUNCATE_END, "Audio:Artist"),
            1);
  AddColumn(new StatusStringColumn(B_TRANSLATE("Album"), 150, 50, 300,
                                   B_TRUNCATE_END, "Audio:Album"),
            2);
  AddColumn(new StatusStringColumn(B_TRANSLATE("Album Artist"), 150, 50, 300,
                                   B_TRUNCATE_END, "Media:AlbumArtist"),
            3);
  AddColumn(new StatusStringColumn(B_TRANSLATE("Genre"), 100, 30, 200,
                                   B_TRUNCATE_END, "Media:Genre"),
            4);
  AddColumn(new StatusStringColumn(B_TRANSLATE("Year"), 60, 30, 80,
                                   B_TRUNCATE_END, "Media:Year", B_ALIGN_RIGHT),
            5);
  AddColumn(new StatusStringColumn(B_TRANSLATE("Duration"), 60, 30, 80,
                                   B_TRUNCATE_END, "Media:Length",
                                   B_ALIGN_RIGHT),
            6);
  AddColumn(new StatusIntegerColumn(B_TRANSLATE("Track"), 50, 20, 80,
                                    "Audio:Track", B_ALIGN_RIGHT),
            7);
  AddColumn(new StatusIntegerColumn(B_TRANSLATE("Disc"), 50, 20, 80,
                                    "Media:Disc", B_ALIGN_RIGHT),
            8);
  AddColumn(new StatusIntegerColumn(B_TRANSLATE("Bitrate"), 80, 50, 100,
                                    "Audio:Bitrate", B_ALIGN_RIGHT),
            9);
  AddColumn(new StatusStringColumn(B_TRANSLATE("Path"), 300, 100, 1000,
                                   B_TRUNCATE_END),
            10);
  AddColumn(new RatingColumn(B_TRANSLATE("Rating"), 80, 60, 100), 11);

  SetInvocationMessage(new BMessage(MSG_PLAY));
  SetSelectionMessage(new BMessage(MSG_SELECTION_CHANGED_CONTENT));

  for (int32 i = 0; i < CountColumns(); ++i) {
    if (auto *col = dynamic_cast<StatusStringColumn *>(ColumnAt(i))) {
      col->SetOwner(this);
    }
  }
}

/**
 * @brief Sets the path of the currently playing media item.
 * @param path The file path of the currently playing item.
 *
 * This triggers a redraw of the relevant rows to update the bold state.
 */
void ContentColumnView::SetNowPlayingPath(const BString &path) {
  if (fNowPlayingPath != path) {
    BString oldPath = fNowPlayingPath;
    fNowPlayingPath = path;

    for (int32 i = 0; i < CountRows(); ++i) {
      MediaRow *mr = dynamic_cast<MediaRow *>(RowAt(i));
      if (mr) {
        if (mr->Item().path == oldPath || mr->Item().path == path) {
          InvalidateRow(mr);
        }
      }
    }
  }
}

/**
 * @brief Destructor.
 */
ContentColumnView::~ContentColumnView() {}

/**
 * @brief Adds a single media item to the list view.
 * @param mi The media item to add.
 */
void ContentColumnView::AddEntry(const MediaItem &mi) {
  MediaRow *row = new MediaRow(mi);
  bool m = mi.missing;

  SourceType src = SOURCE_TAGS;
  if (!mi.path.IsEmpty()) {
    MusicSource ms = MusicSource::GetSourceForPath(mi.path);
    src = ms.primary;
  }

  row->SetField(new StatusStringField(mi.title, m, mi.path, src), 0);
  row->SetField(new StatusStringField(mi.artist, m, mi.path, src), 1);
  row->SetField(new StatusStringField(mi.album, m, mi.path, src), 2);
  row->SetField(new StatusStringField(mi.albumArtist, m, mi.path, src), 3);
  row->SetField(new StatusStringField(mi.genre, m, mi.path, src), 4);

  BString yearStr;
  yearStr << mi.year;
  row->SetField(new StatusStringField(yearStr, m, mi.path, src), 5);

  BString durStr;
  int32 min = mi.duration / 60;
  int32 sec = mi.duration % 60;
  durStr.SetToFormat("%ld:%02ld", (long)min, (long)sec);
  row->SetField(new StatusStringField(durStr, m, mi.path, src), 6);

  row->SetField(new StatusIntegerField(mi.track, m, src, mi.path), 7);
  row->SetField(new StatusIntegerField(mi.disc, m, src, mi.path), 8);
  row->SetField(new StatusIntegerField(mi.bitrate, m, src, mi.path), 9);
  row->SetField(new StatusStringField(mi.path, m, mi.path, src), 10);
  row->SetField(new StatusStringField(RatingColumn::RatingToStars(mi.rating), m,
                                      mi.path, src),
                11);

  AddRow(row);
}

/**
 * @brief Adds multiple media items to the list view in batches.
 * @param items The vector of media items to add.
 */
void ContentColumnView::AddEntries(const std::vector<MediaItem> &items) {
  fPendingItems = items;
  fPendingIndex = 0;
  _AddBatch(50);
}

/**
 * @brief Adds a batch of pending items to the list view.
 * @param count The number of items to add in this batch.
 */
void ContentColumnView::_AddBatch(size_t count) {
  if (fPendingIndex >= fPendingItems.size())
    return;

  bool bulk = (count > 100);
  BWindow *win = Window();
  if (bulk && win)
    win->DisableUpdates();

  SetSortingEnabled(false);

  size_t end = fPendingIndex + count;
  if (end > fPendingItems.size())
    end = fPendingItems.size();

  for (size_t i = fPendingIndex; i < end; ++i) {
    AddEntry(fPendingItems[i]);
  }

  fPendingIndex = end;
  SetSortingEnabled(true);

  if (bulk && win)
    win->EnableUpdates();

  if (fPendingIndex < fPendingItems.size()) {
    if (Looper())
      Looper()->PostMessage(kMsgChunkAdd, this);
  } else {
    if (Looper())
      Looper()->PostMessage(MSG_COUNT_UPDATED);
  }
}

/**
 * @brief Clears all entries from the list view.
 */
void ContentColumnView::ClearEntries() {
  Clear();
  RefreshScrollbars();
}

/**
 * @brief Refreshes the scrollbars by invalidating the layout.
 */
void ContentColumnView::RefreshScrollbars() { InvalidateLayout(); }

/**
 * @brief Initiates a drag operation for selected items.
 * @param point The point where the drag started.
 * @param wasSelected Whether the item at the point was selected.
 * @return True if drag was initiated, false otherwise.
 */
bool ContentColumnView::InitiateDrag(BPoint point, bool wasSelected) {
  BMessage dragMsg(B_SIMPLE_DATA);

  BRow *firstSelected = CurrentSelection();
  if (firstSelected) {
    fDragSourceIndex = IndexOf(firstSelected);
    dragMsg.AddInt32("source_index", fDragSourceIndex);
  } else {
    fDragSourceIndex = -1;
  }

  MediaRow *row = nullptr;
  while ((row = dynamic_cast<MediaRow *>(CurrentSelection(row))) != nullptr) {
    const MediaItem &mi = row->Item();
    entry_ref ref;
    if (get_ref_for_path(mi.path.String(), &ref) == B_OK) {
      dragMsg.AddRef("refs", &ref);
    }
  }

  if (dragMsg.HasRef("refs")) {
    BRow *firstRow = RowAt(point);
    if (firstRow) {
      BRect dragRect;
      GetRowRect(firstRow, &dragRect);
      DragMessage(&dragMsg, dragRect, this);
    } else {
      DragMessage(&dragMsg, Bounds(), this);
    }
    return true;
  }
  fDragSourceIndex = -1;
  return false;
}

/**
 * @brief Handles key down events.
 * @param bytes The raw key data.
 * @param numBytes The number of bytes in the key data.
 *
 * Handles deletion of items and moving items up/down with Option+Arrow keys.
 */
void ContentColumnView::KeyDown(const char *bytes, int32 numBytes) {
  if (numBytes == 1 && bytes[0] == B_DELETE) {
    BMessage msg(MSG_DELETE_ITEM);
    Looper()->PostMessage(&msg);
    return;
  }

  if (numBytes == 1) {
    uint32 modifiers = 0;
    BMessage *currentMsg = Window() ? Window()->CurrentMessage() : nullptr;
    if (currentMsg)
      currentMsg->FindInt32("modifiers", (int32 *)&modifiers);

    if (modifiers & B_OPTION_KEY) {
      if (bytes[0] == B_UP_ARROW) {
        BMessage msg(MSG_MOVE_UP);
        BRow *row = CurrentSelection();
        if (row) {
          msg.AddInt32("index", IndexOf(row));
          Looper()->PostMessage(&msg);
        }
        return;
      } else if (bytes[0] == B_DOWN_ARROW) {
        BMessage msg(MSG_MOVE_DOWN);
        BRow *row = CurrentSelection();
        if (row) {
          msg.AddInt32("index", IndexOf(row));
          Looper()->PostMessage(&msg);
        }
        return;
      }
    }
  }

  BColumnListView::KeyDown(bytes, numBytes);
}

/**
 * @brief Handles mouse movement events.
 * @param where The point where the mouse is.
 * @param transit The transit code (entered, exited, inside).
 * @param dragMsg The drag message, if any.
 */
void ContentColumnView::MouseMoved(BPoint where, uint32 transit,
                                   const BMessage *dragMsg) {
  if (fDragSourceIndex >= 0 && dragMsg && dragMsg->what == B_SIMPLE_DATA) {
    fLastDropPoint = where;
  }
  BColumnListView::MouseMoved(where, transit, dragMsg);
}

/**
 * @brief Called when the view is attached to a window.
 *
 * Adds the RightClickFilter and DropFilter to the scroll view.
 */
void ContentColumnView::AttachedToWindow() {
  BColumnListView::AttachedToWindow();
  if (BView *outline = ScrollView()) {
    outline->AddFilter(new RightClickFilter(this));
    outline->AddFilter(new DropFilter(this));
    outline->SetViewColor(B_TRANSPARENT_COLOR);
  }
}

/**
 * @brief Called when the view is detached from a window.
 */
void ContentColumnView::DetachedFromWindow() {
  BColumnListView::DetachedFromWindow();
}

/**
 * @brief Handles received messages.
 * @param msg The message to handle.
 *
 * Handles context menu, chunk addition, color updates, and drag & drop
 * reordering.
 */
void ContentColumnView::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case kMsgShowCtx: {
    BPoint screen;
    if (msg->FindPoint("screen_where", &screen) != B_OK)
      break;

    BPoint where = screen;
    if (BView *outline = ScrollView()) {
      outline->ConvertFromScreen(&where);
    } else {
      ConvertFromScreen(&where);
    }
    BRow *row = RowAt(where);
    if (!row)
      break;

    bool rowAlreadySelected = false;
    for (BRow *r = CurrentSelection(); r; r = CurrentSelection(r)) {
      if (r == row) {
        rowAlreadySelected = true;
        break;
      }
    }
    if (!rowAlreadySelected) {
      if (SelectionMode() != B_MULTIPLE_SELECTION_LIST)
        DeselectAll();
      AddToSelection(row);
    }

    BPopUpMenu menu("content-ctx", false, false);
    menu.AddItem(new BMenuItem(B_TRANSLATE("Play"), new BMessage(MSG_PLAY)));

    BMenu *addSub = new BMenu(B_TRANSLATE("Add to Playlist"));

    {
      BMessage *m = new BMessage(MSG_NEW_PLAYLIST);
      BMessage files;
      BuildFilesMessage(this, files);
      if (files.HasRef("refs"))
        m->AddMessage("files", &files);
      addSub->AddItem(new BMenuItem(B_TRANSLATE("New Playlist..."), m));
    }

    addSub->AddSeparatorItem();

    BMessage reply;
    if (auto *mw = dynamic_cast<MainWindow *>(Window())) {
      mw->GetPlaylistNames(reply, true);
    }

    int32 count = 0;
    reply.GetInfo("name", nullptr, &count);
    if (count == 0) {
      auto *none = new BMenuItem(B_TRANSLATE("<no playlists>"), nullptr);
      none->SetEnabled(false);
      addSub->AddItem(none);
    } else {
      for (int32 i = 0; i < count; ++i) {
        const char *pname = nullptr;
        if (reply.FindString("name", i, &pname) == B_OK && pname) {
          BMessage *m = new BMessage(MSG_ADD_TO_PLAYLIST);
          AppendSelectedIndices(this, *m);
          m->AddString("playlist", pname);
          addSub->AddItem(new BMenuItem(pname, m));
        }
      }
    }

    menu.AddItem(addSub);

    // menu.AddSeparatorItem(); //maybe looks better without it?

    BMenu *ratingMenu = new BMenu(B_TRANSLATE("Rating"));
    BMessage filesMsg;
    BuildFilesMessage(this, filesMsg);

    for (int32 i = 0; i <= 10; ++i) {
      BString label = RatingColumn::RatingToStars(i);

      BMessage *msg = new BMessage(MSG_SET_RATING);
      msg->AddInt32("rating", i);
      if (filesMsg.HasRef("refs")) {
        msg->AddMessage("files", &filesMsg);
      }
      ratingMenu->AddItem(new BMenuItem(label.String(), msg));
    }
    menu.AddItem(ratingMenu);

    menu.AddSeparatorItem();
    {
      BMessage *m = new BMessage(MSG_REVEAL_IN_TRACKER);
      BMessage files;
      BuildFilesMessage(this, files);
      if (files.HasRef("refs"))
        m->AddMessage("files", &files);
      menu.AddItem(new BMenuItem(B_TRANSLATE("Show in Tracker"), m));
    }

    bool inPlaylist = false;
    if (auto *mw = dynamic_cast<MainWindow *>(Window())) {
      inPlaylist = mw->IsPlaylistSelected();
    }

    if (inPlaylist) {
      menu.AddSeparatorItem();
      {
        BMessage *m = new BMessage(MSG_MOVE_UP);
        BRow *row = CurrentSelection();
        if (row)
          m->AddInt32("index", IndexOf(row));
        menu.AddItem(new BMenuItem(B_TRANSLATE("Move Up"), m));
      }
      {
        BMessage *m = new BMessage(MSG_MOVE_DOWN);
        BRow *row = CurrentSelection();
        if (row)
          m->AddInt32("index", IndexOf(row));
        menu.AddItem(new BMenuItem(B_TRANSLATE("Move Down"), m));
      }
      menu.AddItem(new BMenuItem(B_TRANSLATE("Remove from Playlist"),
                                 new BMessage(MSG_DELETE_ITEM)));
    }

    menu.AddSeparatorItem();
    menu.AddItem(new BMenuItem(B_TRANSLATE("Properties..."),
                               new BMessage(MSG_PROPERTIES)));

    if (BMenuItem *chosen =
            menu.Go(screen, true, false, BRect(screen, screen), false)) {
      if (Looper())
        Looper()->PostMessage(chosen->Message(), this);
    }
    break;
  }

  case kMsgChunkAdd:
    _AddBatch(200);
    break;

  case B_COLORS_UPDATED: {
    SetColor(B_COLOR_BACKGROUND, ui_color(B_LIST_BACKGROUND_COLOR));
    SetColor(B_COLOR_TEXT, ui_color(B_LIST_ITEM_TEXT_COLOR));
    SetColor(B_COLOR_SELECTION, ui_color(B_LIST_SELECTED_BACKGROUND_COLOR));
    SetColor(B_COLOR_ROW_DIVIDER, ui_color(B_LIST_BACKGROUND_COLOR));
    SetColor(B_COLOR_HEADER_BACKGROUND, ui_color(B_PANEL_BACKGROUND_COLOR));
    SetColor(B_COLOR_HEADER_TEXT, ui_color(B_PANEL_TEXT_COLOR));
    Invalidate();
    break;
  }

  case B_SIMPLE_DATA: {
    printf("[ContentColumnView] B_SIMPLE_DATA received, fDragSourceIndex=%ld"
           "\n",
           (long)fDragSourceIndex);
    printf("[ContentColumnView] fLastDropPoint=(%f,%f)\n", fLastDropPoint.x,
           fLastDropPoint.y);
    fflush(stdout);

    if (fDragSourceIndex < 0) {
      printf("[ContentColumnView] Not internal drag, forwarding\n");
      fflush(stdout);
      BColumnListView::MessageReceived(msg);
      break;
    }

    int32 sourceIndex = fDragSourceIndex;
    fDragSourceIndex = -1;

    BRow *targetRow = RowAt(fLastDropPoint);
    int32 targetIndex = targetRow ? IndexOf(targetRow) : CountRows() - 1;
    printf("[ContentColumnView] sourceIndex=%ld, targetIndex=%ld"
           "\n",
           (long)sourceIndex, (long)targetIndex);
    fflush(stdout);

    if (sourceIndex == targetIndex || sourceIndex < 0 || targetIndex < 0)
      break;

    printf("[ContentColumnView] Sending MSG_REORDER_PLAYLIST\n");
    fflush(stdout);
    BMessage reorderMsg(MSG_REORDER_PLAYLIST);
    reorderMsg.AddInt32("from_index", sourceIndex);
    reorderMsg.AddInt32("to_index", targetIndex);
    if (Looper())
      Looper()->PostMessage(&reorderMsg);
    break;
  }

  default:
    BColumnListView::MessageReceived(msg);
  }
}

/**
 * @brief Returns the internal MediaItem pointer for the currently selected row.
 * @return Pointer to the MediaItem of the selected row, or nullptr if none
 * selected.
 */
const MediaItem *ContentColumnView::SelectedItem() const {
  MediaRow *row = dynamic_cast<MediaRow *>(CurrentSelection());
  if (row)
    return &row->Item();
  return nullptr;
}

/**
 * @brief Returns the MediaItem pointer for a specific row index.
 * @param index The index of the row.
 * @return Pointer to the MediaItem, or nullptr if not found.
 */
const MediaItem *ContentColumnView::ItemAt(int32 index) const {
  const BRow *r = RowAt(index);
  if (!r)
    return nullptr;

  const MediaRow *row = dynamic_cast<const MediaRow *>(r);
  if (row)
    return &row->Item();
  return nullptr;
}

/**
 * @brief Checks if the media file for a given row is missing.
 * @param row The row to check.
 * @return True if the file is missing, false otherwise.
 */
bool ContentColumnView::IsRowMissing(BRow *row) const {
  MediaRow *mrow = dynamic_cast<MediaRow *>(row);
  if (mrow) {
    return mrow->Item().missing;
  }
  return false;
}

/**
 * @brief Updates the rating display for a specific media item path.
 * @param path The file path of the media item.
 * @param rating The new rating value.
 */
void ContentColumnView::UpdateRating(const BString &path, int32 rating) {
  for (int32 i = 0; i < CountRows(); ++i) {
    MediaRow *row = dynamic_cast<MediaRow *>(RowAt(i));
    if (row && row->Item().path == path) {
      BStringField *ratingField =
          dynamic_cast<BStringField *>(row->GetField(11));
      if (ratingField) {
        ratingField->SetString(RatingColumn::RatingToStars(rating));
        InvalidateRow(row);
      }
      return;
    }
  }
}

/**
 * @brief Reloads a single entry in the list view.
 * @param path The file path of the item to reload.
 */
void ContentColumnView::ReloadEntry(const BString &path) {
  for (int32 i = 0; i < CountRows(); ++i) {
    MediaRow *row = dynamic_cast<MediaRow *>(RowAt(i));
    if (row && row->Item().path == path) {
      InvalidateRow(row);
      return;
    }
  }
}

/**
 * @brief Saves the current column layout to a message.
 *
 * Persists the column titles, widths, and visibility states.
 * Columns are identified by their titles to ensure robustness against
 * reordering or insertion of new columns.
 *
 * @param msg The message to store the state in.
 */
void ContentColumnView::SaveState(BMessage *msg) {
  if (!msg)
    return;

  msg->RemoveName("col_index");
  msg->RemoveName("col_width");
  msg->RemoveName("col_visible");

  for (int32 i = 0; i < CountColumns(); ++i) {
    BColumn *col = ColumnAt(i);
    BString name;
    if (auto *sc = dynamic_cast<StatusStringColumn *>(col)) {
      name = sc->Title();
    } else if (auto *ic = dynamic_cast<StatusIntegerColumn *>(col)) {
      name = ic->Title();
    } else if (auto *rc = dynamic_cast<RatingColumn *>(col)) {
      name = rc->Title();
    }

    if (!name.IsEmpty()) {
      msg->AddString("col_name", name);
      msg->AddFloat("col_width", col->Width());
      msg->AddBool("col_visible", col->IsVisible());
    }
  }
}

/**
 * @brief Loads the column layout from a message.
 *
 * Restores the column order, widths, and visibility states.
 * It maps the saved column names to the current columns and applies
 * the saved properties. Columns are reordered to match the saved sequence.
 *
 * @param msg The message containing the saved state.
 */
void ContentColumnView::LoadState(BMessage *msg) {
  if (!msg)
    return;

  BString colName;
  float colWidth;
  bool colVisible;
  int32 i = 0;

  std::map<BString, BColumn *> cols;
  for (int32 c = 0; c < CountColumns(); ++c) {
    BColumn *col = ColumnAt(c);
    BString name;
    if (auto *sc = dynamic_cast<StatusStringColumn *>(col)) {
      name = sc->Title();
    } else if (auto *ic = dynamic_cast<StatusIntegerColumn *>(col)) {
      name = ic->Title();
    } else if (auto *rc = dynamic_cast<RatingColumn *>(col)) {
      name = rc->Title();
    }
    if (!name.IsEmpty()) {
      cols[name] = col;
    }
  }

  while (msg->FindString("col_name", i, &colName) == B_OK &&
         msg->FindFloat("col_width", i, &colWidth) == B_OK &&
         msg->FindBool("col_visible", i, &colVisible) == B_OK) {

    if (cols.count(colName)) {
      BColumn *col = cols[colName];
      col->SetWidth(colWidth);
      col->SetVisible(colVisible);
      MoveColumn(col, i);
    }
    i++;
  }
}
