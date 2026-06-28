
#include "MediaTableView.h"
#include "MainWindow.h"
#include "Messages.h"
#include <ScrollBar.h>
#include "MusicSourceSettings.h"
#include "MetadataTagIO.h"
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <Font.h>
#include <Volume.h>
#include <Handler.h>
#include <Looper.h>
#include <MenuItem.h>
#include <Message.h>
#include <MessageFilter.h>
#include <OS.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <View.h>
#include <Window.h>
#include <TextControl.h>
#include <TextView.h>
#include <algorithm>
#include <cinttypes>
#include <memory>
#include <set>
#include <unistd.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MediaTableView"

#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
// Deprecated after Haiku R1/beta6: beta5 lacks AddRows(), so keep AddRow()
// chunks small enough that large libraries do not block the UI in one burst.
static constexpr size_t kBeta5RowBatchSize = 200;
#endif

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
  void SetItem(const MediaItem &mi) { fItem = mi; }

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
class MediaTableView::RightClickFilter : public BMessageFilter {
public:
  explicit RightClickFilter(MediaTableView *owner)
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

    // Exclude scrollbars
    if (dynamic_cast<BScrollBar *>(v) != nullptr)
      return B_DISPATCH_MESSAGE;

    // Exclude the column header view
    if (v->Name() && strcmp(v->Name(), "header") == 0)
      return B_DISPATCH_MESSAGE;

    // Exclude active editor or its children
    if (fOwner->HasActiveEditor()) {
      bool clickOnEditor = false;
      for (BView *p = v; p; p = p->Parent()) {
        if (p == fOwner->ActiveEditor()) {
          clickOnEditor = true;
          break;
        }
      }
      if (clickOnEditor)
        return B_DISPATCH_MESSAGE;
    }

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
      BMessage show(MediaTableView::kMsgShowCtx);
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

      BColumn *column = nullptr;
      float colLeft = 16.0f;
      int32 colIdx = -1;
      for (int32 i = 0; i < fOwner->CountColumns(); ++i) {
        BColumn *c = fOwner->ColumnAt(i);
        if (!c || !c->IsVisible())
          continue;
        if (where.x >= colLeft && where.x < colLeft + c->Width()) {
          column = c;
          colIdx = c->LogicalFieldNum();
          break;
        }
        colLeft += c->Width();
      }

      if (column && row && fOwner->fFastEditEnabled && colIdx == 11) {
        MediaRow *mr = dynamic_cast<MediaRow *>(row);
        if (mr) {
          float xInCol = where.x - colLeft;
          float starWidth = be_plain_font->StringWidth("★★★★★") / 5.0f;
          float xInStars = xInCol - 16.0f; // 16px total margin (cell padding + BStringColumn margin)
          if (starWidth > 0.0f) {
            int32 rating = 0;
            if (xInStars >= 0.0f) {
              int32 star = (int32)(xInStars / starWidth);
              star = std::max((int32)0, std::min((int32)4, star));
              float xInStar = xInStars - (starWidth * star);
              rating = star * 2 + (xInStar < starWidth / 2.0f ? 1 : 2);
            }

            if (rating == mr->Item().rating)
              rating = 0;

            if (!isSelected) {
              fOwner->DeselectAll();
              fOwner->AddToSelection(row);
            }

            DEBUG_PRINT("Rating click: xInCol=%.1f, xInStars=%.1f, starWidth=%.1f, rating=%d\n",
                        xInCol, xInStars, starWidth, (int)rating);

            MediaItem updatedItem = mr->Item();
            updatedItem.rating = rating;
            fOwner->UpdateItem(updatedItem);

            BMessage setRatingMsg(MSG_SET_RATING);
            setRatingMsg.AddInt32("rating", rating);

            BMessage filesMsg;
            entry_ref ref;
            if (get_ref_for_path(mr->Item().path.String(), &ref) == B_OK)
              filesMsg.AddRef("refs", &ref);
            setRatingMsg.AddMessage("files", &filesMsg);

            if (fOwner->Window())
              fOwner->Window()->PostMessage(&setRatingMsg);
            return B_SKIP_MESSAGE;
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

        if (column && row && fOwner->fFastEditEnabled) {
          MediaRow *mr = dynamic_cast<MediaRow *>(row);
          if (mr) {
            BString path = mr->Item().path;
            bool isRemote = path.StartsWith("http://") || path.StartsWith("https://") || path.StartsWith("dlna://");
            if (!isRemote && fOwner->FieldNameForColumn(colIdx) != nullptr) {
              fOwner->StartCellEdit(row, column, colIdx, colLeft, v);
              return B_SKIP_MESSAGE;
            }
          }
        }
      }
    }

    return B_DISPATCH_MESSAGE;
  }

private:
  MediaTableView *fOwner;
};

/**
 * @class DropFilter
 * @brief Message filter for handling internal drag & drop reordering.
 *
 * Intercepts B_SIMPLE_DATA messages on the ScrollView when fDragSourceIndex
 * is set (indicating an internal drag). On drop, sends MSG_REORDER_PLAYLIST
 * to perform the actual reordering.
 */
class MediaTableView::DropFilter : public BMessageFilter {
public:
  explicit DropFilter(MediaTableView *owner)
      : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_SIMPLE_DATA),
        fOwner(owner) {}

  filter_result Filter(BMessage *msg, BHandler **target) override {
    if (!fOwner || !msg || msg->what != B_SIMPLE_DATA)
      return B_DISPATCH_MESSAGE;

    if (fOwner->fDragSourceIndex < 0) {
      if (msg->HasRef("refs") && fOwner->Looper()) {
        fOwner->Looper()->PostMessage(msg);
        return B_SKIP_MESSAGE;
      }
      return B_DISPATCH_MESSAGE;
    }

    BView *v = dynamic_cast<BView *>(*target);
    if (!v)
      return B_DISPATCH_MESSAGE;

    BPoint dropPoint;
    v->GetMouse(&dropPoint, nullptr);

    BRow *targetRow = fOwner->RowAt(dropPoint);
    int32 targetIndex =
        targetRow ? fOwner->IndexOf(targetRow) : fOwner->CountRows() - 1;
    int32 sourceIndex = fOwner->fDragSourceIndex;

    DEBUG_PRINT("Drop detected: source=%ld, target=%ld"
                "\n",
                (long)sourceIndex, (long)targetIndex);

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
  MediaTableView *fOwner;
};

/**
 * @brief Appends indices of all selected rows to a message.
 * @param view The content view to query selections from.
 * @param into The message to append "index" fields to.
 */
static void AppendSelectedIndices(MediaTableView *view, BMessage &into) {
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
static void BuildFilesMessage(MediaTableView *view, BMessage &filesMsg) {
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
  StatusStringField(const char *string, bool missing,
                    std::shared_ptr<BString> path = nullptr,
                    SourceType source = SOURCE_TAGS)
      : BStringField(string), fMissing(missing), fPath(path), fSource(source) {}
  bool IsMissing() const { return fMissing; }
  const BString &Path() const {
    static BString empty("");
    return fPath ? *fPath : empty;
  }
  SourceType Source() const { return fSource; }

private:
  bool fMissing;
  std::shared_ptr<BString> fPath;
  SourceType fSource;
};

/**
 * @class StatusIntegerField
 * @brief BIntegerField subclass that tracks whether the file is missing.
 */
class StatusIntegerField : public BIntegerField {
public:
  StatusIntegerField(int32 number, bool missing, SourceType source = SOURCE_TAGS,
                     std::shared_ptr<BString> path = nullptr,
                     int32 sortDisc = 0, int32 sortTrack = 0)
      : BIntegerField(number), fMissing(missing), fSource(source), fPath(path),
        fSortDisc(sortDisc), fSortTrack(sortTrack) {}
  bool IsMissing() const { return fMissing; }
  SourceType Source() const { return fSource; }
  const BString &Path() const {
    static BString empty("");
    return fPath ? *fPath : empty;
  }
  int32 SortDisc() const { return fSortDisc; }
  int32 SortTrack() const { return fSortTrack; }

private:
  bool fMissing;
  SourceType fSource;
  std::shared_ptr<BString> fPath;
  int32 fSortDisc;
  int32 fSortTrack;
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
                     MediaTableView *owner = nullptr)
      : BStringColumn(title, width, minWidth, maxWidth, truncate, align),
        fAttrName(attrName), fOwner(owner), fTitle(title) {}

  void SetOwner(MediaTableView *owner) { fOwner = owner; }
  const char *Title() const { return fTitle.String(); }

  void DrawField(BField *field, BRect rect, BView *parent) override {
    StatusStringField *f = dynamic_cast<StatusStringField *>(field);
    rgb_color oldColor = parent->HighColor();
    bool isGray = (f && f->IsMissing());
    bool isBold = false;

    /**
     * @note BFS attributes are no longer read during draw.
     * Values are cached in the field during AddEntry().
     * Internal updates use UpdateRating()/ReloadEntry().
     * External changes will be detected via Node Monitoring (TODO).
     */

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

    bool isEditable = fOwner && fOwner->FastEditEnabled() && (fOwner->FieldNameForColumn(LogicalFieldNum()) != nullptr);
    if (isEditable) {
      rgb_color bg = fOwner->Color(B_COLOR_BACKGROUND);
      if (bg.red == 0 && bg.green == 0 && bg.blue == 0 && bg.alpha == 0) {
        parent->SetHighColor(255, 0, 0, 255);
      } else {
        float brightness = (bg.red * 0.299f + bg.green * 0.587f + bg.blue * 0.114f);
        if (brightness < 128.0f) {
          parent->SetHighColor(255, 182, 193, 255);
        } else {
          parent->SetHighColor(139, 0, 0, 255);
        }
      }
    } else if (isGray) {
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
  MediaTableView *fOwner = nullptr;
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
                      alignment align = B_ALIGN_LEFT,
                      MediaTableView *owner = nullptr)
      : BIntegerColumn(title, width, minWidth, maxWidth, align),
        fAttrName(attrName), fOwner(owner), fTitle(title) {}

  void SetOwner(MediaTableView *owner) { fOwner = owner; }
  const char *Title() const { return fTitle.String(); }

  int CompareFields(BField *field1, BField *field2) override {
    StatusIntegerField *a = dynamic_cast<StatusIntegerField *>(field1);
    StatusIntegerField *b = dynamic_cast<StatusIntegerField *>(field2);
    if (a && b &&
        (fAttrName == "Audio:Track" || fAttrName == "Media:Disc")) {
      int result = _CompareInt(a->SortDisc(), b->SortDisc());
      if (result == 0)
        result = _CompareInt(a->SortTrack(), b->SortTrack());
      if (result == 0)
        result = a->Path().ICompare(b->Path());
      return result;
    }
    return BIntegerColumn::CompareFields(field1, field2);
  }

  void DrawField(BField *field, BRect rect, BView *parent) override {
    StatusIntegerField *f = dynamic_cast<StatusIntegerField *>(field);
    rgb_color oldColor = parent->HighColor();
    bool isGray = (f && f->IsMissing());

    /**
     * @note BFS attributes are no longer read during draw.
     * Values are cached in the field during AddEntry().
     * External changes will be detected via Node Monitoring (TODO).
     */

    bool isEditable = fOwner && fOwner->FastEditEnabled() && (fOwner->FieldNameForColumn(LogicalFieldNum()) != nullptr);
    if (isEditable) {
      rgb_color bg = fOwner->Color(B_COLOR_BACKGROUND);
      if (bg.red == 0 && bg.green == 0 && bg.blue == 0 && bg.alpha == 0) {
        parent->SetHighColor(255, 0, 0, 255);
      } else {
        float brightness = (bg.red * 0.299f + bg.green * 0.587f + bg.blue * 0.114f);
        if (brightness < 128.0f) {
          parent->SetHighColor(255, 182, 193, 255);
        } else {
          parent->SetHighColor(139, 0, 0, 255);
        }
      }
    } else if (isGray) {
      parent->SetHighColor(tint_color(ui_color(B_PANEL_BACKGROUND_COLOR),
                                      B_DISABLED_LABEL_TINT));
    }

    if (field)
      BIntegerColumn::DrawField(field, rect, parent);

    parent->SetHighColor(oldColor);
  }

private:
  static int _CompareInt(int32 a, int32 b) {
    return (a < b) ? -1 : (a > b ? 1 : 0);
  }

  BString fAttrName;
  MediaTableView *fOwner = nullptr;
  BString fTitle;
};

/**
 * @class RatingColumn
 * @brief Column that renders rating stars.
 */
class RatingColumn : public BStringColumn {
public:
  RatingColumn(const char *title, float width, float minWidth, float maxWidth,
               MediaTableView *owner = nullptr)
      : BStringColumn(title, width, minWidth, maxWidth, B_TRUNCATE_END,
                      B_ALIGN_LEFT),
        fOwner(owner), fTitle(title) {}

  void SetOwner(MediaTableView *owner) { fOwner = owner; }
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

    /**
     * @note Rating is no longer read from BFS during draw.
     * The value is cached in the field during AddEntry().
     * Internal updates use UpdateRating().
     * External changes will be detected via Node Monitoring (TODO).
     */

    BFont oldFont;
    parent->GetFont(&oldFont);

    rgb_color oldColor = parent->HighColor();

    bool isEditable = fOwner && fOwner->FastEditEnabled() && (fOwner->FieldNameForColumn(LogicalFieldNum()) != nullptr);
    if (isEditable) {
      rgb_color bg = fOwner->Color(B_COLOR_BACKGROUND);
      if (bg.red == 0 && bg.green == 0 && bg.blue == 0 && bg.alpha == 0) {
        parent->SetHighColor(255, 0, 0, 255);
      } else {
        float brightness = (bg.red * 0.299f + bg.green * 0.587f + bg.blue * 0.114f);
        if (brightness < 128.0f) {
          parent->SetHighColor(255, 182, 193, 255);
        } else {
          parent->SetHighColor(139, 0, 0, 255);
        }
      }
    }

    BStringColumn::DrawField(field, rect, parent);

    parent->SetHighColor(oldColor);
    parent->SetFont(&oldFont);
  }

private:
  MediaTableView *fOwner = nullptr;
  BString fTitle;
};

class CellTextControl : public BTextControl {
public:
  CellTextControl(BRect frame, const char *name, const char *text, BMessage *message, BMessenger target)
      : BTextControl(frame, name, nullptr, text, message), fTarget(target) {
    SetFlags(Flags() | B_NAVIGABLE);
  }

  void MakeFocus(bool focused) override {
    BTextControl::MakeFocus(focused);
    if (!focused) {
      BMessage msg(MediaTableView::MSG_COMMIT_EDIT);
      msg.AddBool("focus_loss", true);
      fTarget.SendMessage(&msg);
    }
  }

  void KeyDown(const char *bytes, int32 numBytes) override {
    if (numBytes == 1 && bytes[0] == B_ESCAPE) {
      BMessage msg(MediaTableView::MSG_CANCEL_EDIT);
      fTarget.SendMessage(&msg);
      return;
    }
    BTextControl::KeyDown(bytes, numBytes);
  }

private:
  BMessenger fTarget;
};

/**
 * @brief Constructor for the MediaTableView.
 * @param name The name of the view.
 */
MediaTableView::MediaTableView(const char *name)
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
  AddColumn(new StatusIntegerColumn(B_TRANSLATE("Sort"), 50, 20, 80,
                                    "", B_ALIGN_RIGHT),
            12);

  SetInvocationMessage(new BMessage(MSG_PLAY));
  SetSelectionMessage(new BMessage(MSG_SELECTION_CHANGED_CONTENT));

  for (int32 i = 0; i < CountColumns(); ++i) {
    if (auto *col = dynamic_cast<StatusStringColumn *>(ColumnAt(i))) {
      col->SetOwner(this);
    } else if (auto *col = dynamic_cast<StatusIntegerColumn *>(ColumnAt(i))) {
      col->SetOwner(this);
    } else if (auto *col = dynamic_cast<RatingColumn *>(ColumnAt(i))) {
      col->SetOwner(this);
    }
    if (auto *col = ColumnAt(i)) {
      int32 field = col->LogicalFieldNum();
      fUserColumnVisibility[field] = true;
      fColumnByField[field] = col;
    }
  }

  if (BColumn *sortColumn = fColumnByField[12]) {
    sortColumn->SetVisible(false);
    fUserColumnVisibility[12] = false;
  }
}

/**
 * @brief Sets the path of the currently playing media item.
 * @param path The file path of the currently playing item.
 *
 * This triggers a redraw of the relevant rows to update the bold state.
 */
void MediaTableView::SetNowPlayingPath(const BString &path) {
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
 * @brief Toggles column layout between library and radio modes.
 *
 * Radio columns: Station (0), Country (1), Language (2), Genre (4), URL (10).
 * All other columns are hidden.
 *
 * @param radio True for radio layout, false for library layout.
 */
void MediaTableView::SetRadioMode(bool radio) {
  if (fIsRadioMode == radio)
    return;

  // Save current visibility state of columns if leaving standard mode
  if (!fIsRadioMode) {
    for (int32 i = 0; i < CountColumns(); ++i) {
      if (BColumn *col = ColumnAt(i)) {
        fUserColumnVisibility[col->LogicalFieldNum()] = col->IsVisible();
      }
    }
  }

  fIsRadioMode = radio;

  static const int32 kRadioFields[] = {0, 1, 2, 4, 10};
  static const int32 kRadioFieldCount = 5;

  for (int32 i = 0; i < CountColumns(); ++i) {
    BColumn *col = ColumnAt(i);
    if (!col)
      continue;

    int32 field = col->LogicalFieldNum();

    if (radio) {
      bool show = false;
      for (int32 r = 0; r < kRadioFieldCount; ++r) {
        if (field == kRadioFields[r]) {
          show = true;
          break;
        }
      }
      col->SetVisible(show);
    } else {
      bool visible = true;
      auto it = fUserColumnVisibility.find(field);
      if (it != fUserColumnVisibility.end())
        visible = it->second;
      if (field == 12)
        visible = fIsPlaylistMode;
      col->SetVisible(visible);
    }

    auto *titled = dynamic_cast<BTitledColumn *>(col);
    if (titled) {
      if (radio) {
        if (field == 0)
          titled->SetTitle(B_TRANSLATE("Station"));
        else if (field == 1)
          titled->SetTitle(B_TRANSLATE("Country"));
        else if (field == 2)
          titled->SetTitle(B_TRANSLATE("Language"));
        else if (field == 10)
          titled->SetTitle(B_TRANSLATE("URL"));
      } else {
        if (field == 0)
          titled->SetTitle(B_TRANSLATE("Title"));
        else if (field == 1)
          titled->SetTitle(B_TRANSLATE("Artist"));
        else if (field == 2)
          titled->SetTitle(B_TRANSLATE("Album"));
        else if (field == 10)
          titled->SetTitle(B_TRANSLATE("Path"));
      }
    }
  }
}

void MediaTableView::SetPlaylistMode(bool isPlaylist) {
  fIsPlaylistMode = isPlaylist;

  auto it = fColumnByField.find(12);
  if (it != fColumnByField.end() && it->second)
    it->second->SetVisible(isPlaylist && !fIsRadioMode);
}

/**
 * @brief Destructor.
 */
MediaTableView::~MediaTableView() {}

/**
 * @brief Adds a single media item to the list view.
 * @param mi The media item to add.
 */
void MediaTableView::AddEntry(const MediaItem &mi) {
  MediaRow *row = new MediaRow(mi);
  bool m = mi.missing;

  SourceType src = SOURCE_TAGS;

  std::shared_ptr<BString> pathPtr = std::make_shared<BString>(mi.path);

  if (!mi.path.IsEmpty()) {
    if (mi.path.StartsWith("http://") || mi.path.StartsWith("https://")) {
      src = SOURCE_TAGS;
    } else {
      MusicSourceSettings ms = MusicSourceSettings::GetSourceForPath(mi.path);
      src = ms.primary;
    }
  }

  row->SetField(new StatusStringField(mi.title, m, pathPtr, src), 0);
  row->SetField(new StatusStringField(mi.artist, m, pathPtr, src), 1);
  row->SetField(new StatusStringField(mi.album, m, pathPtr, src), 2);
  row->SetField(new StatusStringField(mi.albumArtist, m, pathPtr, src), 3);
  row->SetField(new StatusStringField(mi.genre, m, pathPtr, src), 4);

  BString yearStr;
  yearStr << mi.year;
  row->SetField(new StatusStringField(yearStr, m, pathPtr, src), 5);

  BString durStr;
  int32 min = mi.duration / 60;
  int32 sec = mi.duration % 60;
  durStr.SetToFormat("%ld:%02ld", (long)min, (long)sec);
  row->SetField(new StatusStringField(durStr, m, pathPtr, src), 6);

  row->SetField(new StatusIntegerField(mi.track, m, src, pathPtr, mi.disc,
                                       mi.track),
                7);
  row->SetField(new StatusIntegerField(mi.disc, m, src, pathPtr, mi.disc,
                                       mi.track),
                8);
  row->SetField(new StatusIntegerField(mi.bitrate, m, src, pathPtr), 9);
  row->SetField(new StatusStringField(mi.path, m, pathPtr, src), 10);
  row->SetField(new StatusStringField(RatingColumn::RatingToStars(mi.rating), m,
                                      pathPtr, src),
                11);
  row->SetField(new StatusIntegerField(fIsPlaylistMode ? CountRows() + 1 : 0,
                                       m, src, pathPtr),
                12);

  AddRow(row);
}

void MediaTableView::UpdateItem(const MediaItem &mi, const BString *matchPath) {
  const BString &key = matchPath ? *matchPath : mi.path;
  for (int32 i = 0; i < CountRows(); ++i) {
    MediaRow *mr = dynamic_cast<MediaRow *>(RowAt(i));
    if (mr && mr->Item().path == key) {
      mr->SetItem(mi);

      bool m = mi.missing;
      SourceType src = SOURCE_TAGS;

      std::shared_ptr<BString> pathPtr = std::make_shared<BString>(mi.path);

      if (!mi.path.IsEmpty()) {
        if (mi.path.StartsWith("http://") || mi.path.StartsWith("https://")) {
          src = SOURCE_TAGS;
        } else {
          MusicSourceSettings ms = MusicSourceSettings::GetSourceForPath(mi.path);
          src = ms.primary;
        }
      }

      mr->SetField(new StatusStringField(mi.title, m, pathPtr, src), 0);
      mr->SetField(new StatusStringField(mi.artist, m, pathPtr, src), 1);
      mr->SetField(new StatusStringField(mi.album, m, pathPtr, src), 2);
      mr->SetField(new StatusStringField(mi.albumArtist, m, pathPtr, src), 3);
      mr->SetField(new StatusStringField(mi.genre, m, pathPtr, src), 4);

      BString yearStr;
      yearStr << mi.year;
      mr->SetField(new StatusStringField(yearStr, m, pathPtr, src), 5);

      BString durStr;
      int32 min = mi.duration / 60;
      int32 sec = mi.duration % 60;
      durStr.SetToFormat("%ld:%02ld", (long)min, (long)sec);
      mr->SetField(new StatusStringField(durStr, m, pathPtr, src), 6);

      mr->SetField(new StatusIntegerField(mi.track, m, src, pathPtr,
                                          mi.disc, mi.track),
                   7);
      mr->SetField(new StatusIntegerField(mi.disc, m, src, pathPtr, mi.disc,
                                          mi.track),
                   8);
      mr->SetField(new StatusIntegerField(mi.bitrate, m, src, pathPtr), 9);
      mr->SetField(new StatusStringField(mi.path, m, pathPtr, src), 10);
      mr->SetField(new StatusStringField(RatingColumn::RatingToStars(mi.rating),
                                         m, pathPtr, src),
                   11);
      mr->SetField(new StatusIntegerField(fIsPlaylistMode ? i + 1 : 0, m, src,
                                          pathPtr),
                   12);

      InvalidateRow(mr);
      break;
    }
  }
}

/**
 * @brief Adds multiple media items to the list view in batches.
 *
 * If a sort state restore is pending, items are pre-sorted in memory
 * first so they can be added in chunks without re-sort overhead.
 *
 * @param items The vector of media items to add.
 */
void MediaTableView::AddEntries(std::vector<MediaItem> items) {
  bigtime_t t0 = system_time();
  size_t itemCount = items.size();
  fPendingItems = std::move(items);
  fPendingIndex = 0;

  if (!fHasPendingSortRestore && fIsPlaylistMode) {
    fPendingSortRestore.MakeEmpty();
    fPendingSortRestore.AddInt32("sortID", 12);
    fPendingSortRestore.AddBool("sortascending", true);
    fPendingSortRestore.AddInt32("sort_schema", 2);
    fHasPendingSortRestore = true;
  } else if (!fHasPendingSortRestore) {
    fPendingSortRestore.MakeEmpty();
    SaveSortState(&fPendingSortRestore);
    if (!fPendingSortRestore.IsEmpty())
      fHasPendingSortRestore = true;
  }

  bigtime_t t1 = system_time();
  if (fHasPendingSortRestore) {
    _PreSortPendingItems();
  }

  bigtime_t t2 = system_time();
  fSortingDisabledForBatch = true;
  SetSortingEnabled(false);

  DEBUG_PRINT("AddEntries(%zu): copy=%lld us, "
             "preSort=%lld us, hasPendingSort=%d\n",
             itemCount, (long long)(t1 - t0),
             (long long)(t2 - t1), (int)fHasPendingSortRestore);

#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
  _AddBatch(kBeta5RowBatchSize);
#else
  _AddBatch(fPendingItems.size());
#endif
}

/**
 * @brief Creates a MediaRow from a MediaItem without adding it to the view.
 *
 * @param mi The media item to create a row for.
 * @param playlistIndex Optional 1-based index to use for the Sort column in playlist mode.
 * @return Pointer to the newly created MediaRow.
 */
BRow *MediaTableView::_CreateRow(const MediaItem &mi, int32 playlistIndex) {
  MediaRow *row = new MediaRow(mi);
  bool m = mi.missing;
  SourceType src = SOURCE_TAGS;

  std::shared_ptr<BString> pathPtr = std::make_shared<BString>(mi.path);

  if (!mi.path.IsEmpty()) {
    /**
     * @brief Skip MusicSourceSettings lookup for remote URLs (DLNA/Radio).
     */
    if (mi.path.StartsWith("http://") || mi.path.StartsWith("https://")) {
      src = SOURCE_TAGS;
    } else {
      MusicSourceSettings ms = MusicSourceSettings::GetSourceForPath(mi.path);
      src = ms.primary;
    }
  }

  row->SetField(new StatusStringField(mi.title, m, pathPtr, src), 0);
  row->SetField(new StatusStringField(mi.artist, m, pathPtr, src), 1);
  row->SetField(new StatusStringField(mi.album, m, pathPtr, src), 2);
  row->SetField(new StatusStringField(mi.albumArtist, m, pathPtr, src), 3);
  row->SetField(new StatusStringField(mi.genre, m, pathPtr, src), 4);

  BString yearStr;
  yearStr << mi.year;
  row->SetField(new StatusStringField(yearStr, m, pathPtr, src), 5);

  BString durStr;
  int32 min = mi.duration / 60;
  int32 sec = mi.duration % 60;
  durStr.SetToFormat("%ld:%02ld", (long)min, (long)sec);
  row->SetField(new StatusStringField(durStr, m, pathPtr, src), 6);

  row->SetField(new StatusIntegerField(mi.track, m, src, pathPtr, mi.disc,
                                       mi.track),
                7);
  row->SetField(new StatusIntegerField(mi.disc, m, src, pathPtr, mi.disc,
                                       mi.track),
                8);
  row->SetField(new StatusIntegerField(mi.bitrate, m, src, pathPtr), 9);
  row->SetField(new StatusStringField(mi.path, m, pathPtr, src), 10);
  row->SetField(new StatusStringField(RatingColumn::RatingToStars(mi.rating), m,
                                      pathPtr, src),
                11);
  row->SetField(new StatusIntegerField((fIsPlaylistMode && playlistIndex > 0)
                                           ? playlistIndex
                                           : 0,
                                       m, src, pathPtr),
                12);

  return row;
}

/**
 * @brief Adds a batch of pending items.
 *
 * Builds all BRow objects first, then uses AddRows() where available
 * to avoid O(N^2) per-row array-shift overhead. Haiku R1/beta5 falls
 * back to AddRow() for API compatibility.
 *
 * @param count The number of items to add in this batch.
 */
void MediaTableView::_AddBatch(size_t count) {
  if (fPendingIndex >= fPendingItems.size())
    return;

  bigtime_t t0 = system_time();
  BWindow *win = Window();

  if (win)
    win->DisableUpdates();

  size_t end = fPendingIndex + count;
  if (end > fPendingItems.size())
    end = fPendingItems.size();

  BList rowList(end - fPendingIndex);
  for (size_t i = fPendingIndex; i < end; ++i) {
    int32 pIndex = fIsPlaylistMode ? (i + 1) : -1;
    rowList.AddItem(_CreateRow(fPendingItems[i], pIndex));
  }

  bigtime_t t1 = system_time();
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
  // Deprecated after Haiku R1/beta6: AddRows() is not available on beta5.
  // Remove this fallback once beta5 support is dropped.
  for (int32 i = 0; i < rowList.CountItems(); ++i) {
    if (BRow *row = static_cast<BRow *>(rowList.ItemAt(i)))
      AddRow(row);
  }
#else
  int32 insertAt = CountRows();
  AddRows(&rowList, insertAt);
#endif

  bigtime_t t2 = system_time();

  fPendingIndex = end;

  bool isLastBatch = (fPendingIndex >= fPendingItems.size());

  if (isLastBatch) {
    if (fSortingDisabledForBatch) {
      fSortingDisabledForBatch = false;
      SetSortingEnabled(true);
    }

    if (fHasPendingSortRestore) {
      _ApplyPendingSortRestore();
    }

    bool scrolled = false;
    float rowTop = 0;
    for (int32 i = 0; i < CountRows(); i++) {
      BRow *row = RowAt(i);
      if (auto *mr = dynamic_cast<MediaRow *>(row)) {
        for (const auto &sp : fSavedSelectedPaths) {
          if (mr->Item().path == sp) {
            AddToSelection(mr);
            break;
          }
        }
        if (!scrolled && !fTopVisiblePath.IsEmpty() &&
            mr->Item().path == fTopVisiblePath) {
          // ScrollTo(BRow*) only scrolls minimally, which would leave
          // this row at the bottom edge; scroll explicitly so the
          // previously top-visible row is at the top again.
          ScrollTo(BPoint(0, rowTop));
          scrolled = true;
        }
      }
      if (row)
        rowTop += row->Height() + 1;
    }
    fTopVisiblePath = "";
    fSavedSelectedPaths.clear();
  }

  bigtime_t t3 = system_time();

  if (win)
    win->EnableUpdates();

  DEBUG_PRINT("_AddBatch(%zu): CreateRows=%lld us, "
             "insertRows=%lld us, sortRestore=%lld us, total rows=%ld\n",
             count, (long long)(t1 - t0), (long long)(t2 - t1),
             (long long)(t3 - t2), (long)CountRows());

  if (!isLastBatch) {
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
void MediaTableView::ClearEntries() {
  Clear();
  RefreshScrollbars();
}

/**
 * @brief Refreshes the scrollbars by invalidating the layout.
 */
void MediaTableView::RefreshScrollbars() { InvalidateLayout(); }

void MediaTableView::SaveScrollState() {
  fTopVisiblePath = "";
  fSavedSelectedPaths.clear();

  // RowAt(BPoint) expects content-space coordinates: y=5 is always the
  // first row of the list. Offset by the outline view's scroll position
  // to probe the row actually visible at the top.
  BPoint topPoint(0, 5);
  if (BView *outline = ScrollView())
    topPoint.y += outline->Bounds().top;

  if (BRow *topRow = RowAt(topPoint)) {
    if (auto *mr = dynamic_cast<MediaRow *>(topRow)) {
      fTopVisiblePath = mr->Item().path;
    }
  }

  for (BRow *r = CurrentSelection(); r; r = CurrentSelection(r)) {
    if (auto *mr = dynamic_cast<MediaRow *>(r)) {
      fSavedSelectedPaths.push_back(mr->Item().path);
    }
  }
}

/**
 * @brief Initiates a drag operation for selected items.
 * @param point The point where the drag started.
 * @param wasSelected Whether the item at the point was selected.
 * @return True if drag was initiated, false otherwise.
 */
bool MediaTableView::InitiateDrag(BPoint point, bool wasSelected) {
  BMessage dragMsg(B_SIMPLE_DATA);

  BRow *firstSelected = CurrentSelection();
  if (firstSelected) {
    bool canReorder = false;
    if (auto *mw = dynamic_cast<MainWindow *>(Window()))
      canReorder = mw->IsPlaylistSelected();
    fDragSourceIndex = canReorder ? IndexOf(firstSelected) : -1;
    if (fDragSourceIndex >= 0)
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
void MediaTableView::KeyDown(const char *bytes, int32 numBytes) {
  if (numBytes == 1 && bytes[0] == B_DELETE) {
    BMessage msg(MSG_DELETE_ITEM);
    Looper()->PostMessage(&msg);
    return;
  }

  if (numBytes == 1 && bytes[0] == B_SPACE) {
    BMessage msg(MSG_PLAYPAUSE);
    if (Window()) {
      Window()->PostMessage(&msg);
    }
    return;
  }

  if (numBytes == 1) {
    uint32 modifiers = 0;
    BMessage *currentMsg = Window() ? Window()->CurrentMessage() : nullptr;
    if (currentMsg)
      currentMsg->FindInt32("modifiers", (int32 *)&modifiers);

    if (modifiers & B_OPTION_KEY) {
      bool canReorder = false;
      if (auto *mw = dynamic_cast<MainWindow *>(Window()))
        canReorder = mw->IsPlaylistSelected();
      if (!canReorder) {
        BColumnListView::KeyDown(bytes, numBytes);
        return;
      }
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
    } else {
      if (bytes[0] == B_UP_ARROW) {
        BRow *row = CurrentSelection();
        int32 index = row ? IndexOf(row) : -1;
        int32 targetIndex = -1;
        if (index > 0) {
          targetIndex = index - 1;
        } else if (index == -1 && CountRows() > 0) {
          targetIndex = CountRows() - 1;
        }
        if (targetIndex != -1) {
          BRow *targetRow = RowAt(targetIndex);
          DeselectAll();
          AddToSelection(targetRow);
          ScrollTo(targetRow);
        }
        return;
      } else if (bytes[0] == B_DOWN_ARROW) {
        BRow *row = CurrentSelection();
        int32 index = row ? IndexOf(row) : -1;
        int32 targetIndex = -1;
        if (index < CountRows() - 1) {
          targetIndex = index >= 0 ? index + 1 : 0;
        } else if (index == -1 && CountRows() > 0) {
          targetIndex = 0;
        }
        if (targetIndex != -1) {
          BRow *targetRow = RowAt(targetIndex);
          DeselectAll();
          AddToSelection(targetRow);
          ScrollTo(targetRow);
        }
        return;
      } else if (bytes[0] == B_LEFT_ARROW || bytes[0] == B_RIGHT_ARROW) {
        BRow *selRow = CurrentSelection();
        if (selRow) {
          MediaRow *mr = dynamic_cast<MediaRow *>(selRow);
          if (mr) {
            int32 currentRating = mr->Item().rating;
            int32 newRating = currentRating;
            if (bytes[0] == B_LEFT_ARROW) {
              newRating = std::max((int32)0, currentRating - 1);
            } else {
              newRating = std::min((int32)10, currentRating + 1);
            }
            if (newRating != currentRating) {
              BMessage setRatingMsg(MSG_SET_RATING);
              setRatingMsg.AddInt32("rating", newRating);

              BMessage filesMsg;
              BuildFilesMessage(this, filesMsg);
              if (filesMsg.HasRef("refs")) {
                setRatingMsg.AddMessage("files", &filesMsg);
                if (Window()) {
                  Window()->PostMessage(&setRatingMsg);
                }
              }
            }
          }
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
void MediaTableView::MouseMoved(BPoint where, uint32 transit,
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
void MediaTableView::AttachedToWindow() {
  BColumnListView::AttachedToWindow();
  if (BView *outline = ScrollView()) {
    outline->AddFilter(new RightClickFilter(this));
    outline->AddFilter(new DropFilter(this));
    outline->SetViewColor(B_TRANSPARENT_COLOR);
  }
  Window()->AddShortcut('a', B_COMMAND_KEY, new BMessage(kMsgSelectAll), this);
  Window()->AddShortcut('l', B_COMMAND_KEY, new BMessage(kMsgLocatePlaying), this);
}

/**
 * @brief Called when the view is detached from a window.
 */
void MediaTableView::DetachedFromWindow() {
  Window()->RemoveShortcut('a', B_COMMAND_KEY);
  Window()->RemoveShortcut('l', B_COMMAND_KEY);
  BColumnListView::DetachedFromWindow();
}

/**
 * @brief Handles received messages.
 * @param msg The message to handle.
 *
 * Handles context menu, chunk addition, color updates, and drag & drop
 * reordering.
 */
void MediaTableView::MessageReceived(BMessage *msg) {
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
    if (!row) {
      bool isRadioEmpty = false;
      if (auto *mw = dynamic_cast<MainWindow *>(Window()))
        isRadioEmpty = mw->IsRadioMode();
      if (isRadioEmpty) {
        BPopUpMenu emptyMenu("radio-empty-ctx", false, false);
        emptyMenu.AddItem(new BMenuItem(B_TRANSLATE("Add Station..."),
                                        new BMessage(MSG_RADIO_ADD)));
        emptyMenu.AddItem(new BMenuItem(B_TRANSLATE("Import m3u/pls..."),
                                        new BMessage(MSG_RADIO_IMPORT)));
        if (BMenuItem *chosen = emptyMenu.Go(screen, true, false,
                                             BRect(screen, screen), false)) {
          if (Looper())
            Looper()->PostMessage(chosen->Message(), this);
        }
      }
      break;
    }

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

    bool isRadio = false;
    bool isDlna = false;
    if (auto *mw = dynamic_cast<MainWindow *>(Window())) {
      isRadio = mw->IsRadioMode();
      isDlna = mw->IsDlnaMode();
    }

    if (isRadio) {
      menu.AddItem(
          new BMenuItem(B_TRANSLATE("Play"), new BMessage(MSG_RADIO_PLAY)));
      menu.AddSeparatorItem();
      menu.AddItem(new BMenuItem(B_TRANSLATE("Edit Station..."),
                                 new BMessage(MSG_RADIO_EDIT)));
      menu.AddItem(new BMenuItem(B_TRANSLATE("Delete Station"),
                                 new BMessage(MSG_RADIO_DELETE)));
      menu.AddSeparatorItem();
      menu.AddItem(new BMenuItem(B_TRANSLATE("Add Station..."),
                                 new BMessage(MSG_RADIO_ADD)));
    } else if (isDlna) {
      menu.AddItem(new BMenuItem(B_TRANSLATE("Play"), new BMessage(MSG_PLAY)));
      menu.AddSeparatorItem();
      BMenu *ratingMenu = new BMenu(B_TRANSLATE("Rating"));
      ratingMenu->SetEnabled(false);
      menu.AddItem(ratingMenu);
    } else {
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

      BMenu *ratingMenu = new BMenu(B_TRANSLATE("Rating"));
      BMessage filesMsg;
      BuildFilesMessage(this, filesMsg);

      bool isReadOnly = false;
      {
        int32 i = 0;
        entry_ref ref;
        while (filesMsg.FindRef("refs", i++, &ref) == B_OK) {
          BPath p(&ref);
          if (p.InitCheck() == B_OK) {
            if (access(p.Path(), W_OK) != 0) {
              isReadOnly = true;
              break;
            }
          }
        }
      }

      if (isReadOnly) {
        ratingMenu->SetEnabled(false);
      }

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
      menu.AddItem(new BMenuItem(B_TRANSLATE("Move File To..."),
                                 new BMessage(MSG_MOVE_TO)));
      menu.AddItem(new BMenuItem(B_TRANSLATE("Move File To Trash"),
                                 new BMessage(MSG_MOVE_TO_TRASH)));

      menu.AddSeparatorItem();
      menu.AddItem(new BMenuItem(B_TRANSLATE("Properties..."),
                                 new BMessage(MSG_PROPERTIES)));
    }

    if (BMenuItem *chosen =
            menu.Go(screen, true, false, BRect(screen, screen), false)) {
      if (Looper())
        Looper()->PostMessage(chosen->Message(), this);
    }
    break;
  }

  case kMsgChunkAdd:
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
    _AddBatch(kBeta5RowBatchSize);
#else
    _AddBatch(fPendingItems.size() - fPendingIndex);
#endif
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
    DEBUG_PRINT(
        "B_SIMPLE_DATA received, fDragSourceIndex=%ld"
        "\n",
        (long)fDragSourceIndex);
    DEBUG_PRINT("fLastDropPoint=(%f,%f)\n",
                fLastDropPoint.x, fLastDropPoint.y);

    if (fDragSourceIndex < 0) {
      DEBUG_PRINT("Not internal drag, forwarding\n");
      BColumnListView::MessageReceived(msg);
      break;
    }

    int32 sourceIndex = fDragSourceIndex;
    fDragSourceIndex = -1;

    BRow *targetRow = RowAt(fLastDropPoint);
    int32 targetIndex = targetRow ? IndexOf(targetRow) : CountRows() - 1;
    DEBUG_PRINT("sourceIndex=%ld, targetIndex=%ld"
                "\n",
                (long)sourceIndex, (long)targetIndex);

    if (sourceIndex == targetIndex || sourceIndex < 0 || targetIndex < 0)
      break;

    DEBUG_PRINT("Sending MSG_REORDER_PLAYLIST\n");
    BMessage reorderMsg(MSG_REORDER_PLAYLIST);
    reorderMsg.AddInt32("from_index", sourceIndex);
    reorderMsg.AddInt32("to_index", targetIndex);
    if (Looper())
      Looper()->PostMessage(&reorderMsg);
    break;
  }

  case MSG_COMMIT_EDIT: {
    CommitCellEdit();
    break;
  }

  case MSG_CANCEL_EDIT: {
    CancelCellEdit();
    break;
  }

  case kMsgSelectAll: {
    for (int32 i = 0; i < CountRows(); ++i)
      if (BRow *row = RowAt(i))
        AddToSelection(row);
    break;
  }

  case kMsgLocatePlaying: {
    LocatePlayingTrack();
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
const MediaItem *MediaTableView::SelectedItem() const {
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
const MediaItem *MediaTableView::ItemAt(int32 index) const {
  const BRow *r = RowAt(index);
  if (!r)
    return nullptr;

  const MediaRow *row = dynamic_cast<const MediaRow *>(r);
  if (row)
    return &row->Item();
  return nullptr;
}

/**
 * @brief Builds a playback queue from the current sorted view.
 *
 * Iterates all rows in display order and collects file paths
 * of non-missing items. This is more efficient than having
 * the caller iterate via ItemAt() individually.
 *
 * @param[out] outQueue Vector to fill with file paths.
 */
void MediaTableView::BuildQueue(std::vector<std::string> &outQueue) const {
  int32 count = CountRows();
  outQueue.clear();
  outQueue.reserve(count);

  for (int32 i = 0; i < count; ++i) {
    const MediaRow *row = dynamic_cast<const MediaRow *>(RowAt(i));
    if (!row)
      continue;
    const MediaItem &mi = row->Item();
    if (mi.missing)
      continue;
    outQueue.emplace_back(mi.path.String());
  }
}

/**
 * @brief Checks if the media file for a given row is missing.
 * @param row The row to check.
 * @return True if the file is missing, false otherwise.
 */
bool MediaTableView::IsRowMissing(BRow *row) const {
  MediaRow *mrow = dynamic_cast<MediaRow *>(row);
  if (mrow) {
    return mrow->Item().missing;
  }
  return false;
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
void MediaTableView::SaveState(BMessage *msg) {
  if (!msg)
    return;

  msg->RemoveName("col_name");
  msg->RemoveName("col_width");
  msg->RemoveName("col_visible");

  // Helper lambda to get the title from any of our column types.
  auto titleFor = [](BColumn *col) -> BString {
    if (auto *sc = dynamic_cast<StatusStringColumn *>(col))
      return sc->Title();
    if (auto *ic = dynamic_cast<StatusIntegerColumn *>(col))
      return ic->Title();
    if (auto *rc = dynamic_cast<RatingColumn *>(col))
      return rc->Title();
    return BString();
  };

  // Phase 1: iterate columns returned by ColumnAt (visible display order).
  // On some Haiku versions ColumnAt skips hidden columns, so we track which
  // field numbers we cover here and handle the rest in Phase 2.
  std::set<int32> savedFields;

  for (int32 i = 0; i < CountColumns(); ++i) {
    BColumn *col = ColumnAt(i);
    if (!col)
      continue;

    BString name = titleFor(col);
    if (name.IsEmpty())
      continue;

    int32 field = col->LogicalFieldNum();
    savedFields.insert(field);

    bool visible = col->IsVisible();
    if (!fIsRadioMode) {
      fUserColumnVisibility[field] = visible;
    } else {
      auto it = fUserColumnVisibility.find(field);
      visible = (it != fUserColumnVisibility.end()) ? it->second : true;
    }

    msg->AddString("col_name", name);
    msg->AddFloat("col_width", col->Width());
    msg->AddBool("col_visible", visible);
  }

  // Phase 2: emit any columns that ColumnAt didn't return (hidden columns).
  // fColumnByField is populated at construction when all columns are visible,
  // so it always contains the full set regardless of current visibility.
  for (auto &[field, col] : fColumnByField) {
    if (savedFields.count(field) > 0)
      continue;

    BString name = titleFor(col);
    if (name.IsEmpty())
      continue;

    // Column was not returned by ColumnAt, meaning it is hidden in the
    // current mode.  In library mode the user explicitly hid it (visible=false).
    // In radio mode, use the cached library-mode visibility from
    // fUserColumnVisibility so we don't clobber the user's library preference.
    bool visible;
    if (!fIsRadioMode) {
      visible = false;
      fUserColumnVisibility[field] = false;
    } else {
      auto it = fUserColumnVisibility.find(field);
      visible = (it != fUserColumnVisibility.end()) ? it->second : true;
    }

    msg->AddString("col_name", name);
    msg->AddFloat("col_width", col->Width());
    msg->AddBool("col_visible", visible);
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
void MediaTableView::LoadState(BMessage *msg) {
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
      int32 field = col->LogicalFieldNum();
      col->SetWidth(colWidth);
      if (field == 12 && !fIsPlaylistMode)
        colVisible = false;
      col->SetVisible(colVisible);
      fUserColumnVisibility[field] = colVisible;
      MoveColumn(col, i);
    }
    i++;
  }

  auto it = fColumnByField.find(12);
  if (it != fColumnByField.end() && it->second)
    it->second->SetVisible(fIsPlaylistMode && !fIsRadioMode);
}

/**
 * @brief Saves the current sort column and direction.
 *
 * Calls BColumnListView::SaveState on a temporary message to extract
 * the sort column field IDs and sort direction. Only the sort-relevant
 * fields (sortID, sortascending) are copied to the output message.
 *
 * @param msg Message to store sort state into.
 */
void MediaTableView::SaveSortState(BMessage *msg) {
  if (!msg)
    return;

  msg->MakeEmpty();

  BMessage tmp;
  BColumnListView::SaveState(&tmp);

  int32 sortID;
  bool wroteSortState = false;
  for (int32 i = 0; tmp.FindInt32("sortID", i, &sortID) == B_OK; ++i) {
    bool ascending = true;
    tmp.FindBool("sortascending", i, &ascending);
    msg->AddInt32("sortID", sortID);
    msg->AddBool("sortascending", ascending);
    wroteSortState = true;
  }
  if (wroteSortState)
    msg->AddInt32("sort_schema", 2);
}

/**
 * @brief Schedules a sort state restore after batch loading completes.
 *
 * The sort state is not applied immediately because batch loading
 * toggles SetSortingEnabled(false/true) which would interfere.
 * The actual restore happens in _ApplyPendingSortRestore() after
 * the final batch.
 *
 * @param msg Message containing saved sort state.
 */
void MediaTableView::RestoreSortState(BMessage *msg) {
  if (!msg)
    return;

  fPendingSortRestore = *msg;
  fHasPendingSortRestore = true;
}

/**
 * @brief Pre-sorts fPendingItems in memory based on the pending sort state.
 *
 * Maps the sort column field ID to the corresponding MediaItem field
 * and uses std::sort so items can be inserted into BColumnListView
 * in already-sorted order, avoiding O(N^2) re-sort overhead.
 *
 * Column field IDs: 0=title, 1=artist, 2=album, 3=albumArtist,
 * 4=genre, 5=year, 6=duration, 7=track, 8=disc, 9=bitrate,
 * 10=path, 11=rating, 12=playlist sort order.
 */
void MediaTableView::_PreSortPendingItems() {
  int32 sortID = -1;
  bool ascending = true;

  if (fPendingSortRestore.FindInt32("sortID", 0, &sortID) != B_OK)
    return;
  fPendingSortRestore.FindBool("sortascending", 0, &ascending);

  int32 sortSchema = 0;
  bool legacySortState =
      fPendingSortRestore.FindInt32("sort_schema", &sortSchema) != B_OK;
  if (legacySortState && fIsPlaylistMode && sortID == 7)
    sortID = 12;

  if (sortID == 12)
    return;

  auto compareInt = [](int32 a, int32 b) {
    return (a < b) ? -1 : (a > b ? 1 : 0);
  };

  auto compareAlbumOrder = [&](const MediaItem &a, const MediaItem &b,
                               bool primaryTrack) {
    int result = 0;
    if (primaryTrack) {
      result = compareInt(a.disc, b.disc);
      if (result == 0)
        result = compareInt(a.track, b.track);
    } else {
      result = compareInt(a.disc, b.disc);
      if (result == 0)
        result = compareInt(a.track, b.track);
    }
    return result;
  };

  auto cmp = [sortID, ascending, compareInt,
              compareAlbumOrder](const MediaItem &a,
                                 const MediaItem &b) -> bool {
    int result = 0;
    switch (sortID) {
    case 0:
      result = a.title.ICompare(b.title);
      break;
    case 1:
      result = a.artist.ICompare(b.artist);
      break;
    case 2:
      result = a.album.ICompare(b.album);
      break;
    case 3:
      result = a.albumArtist.ICompare(b.albumArtist);
      break;
    case 4:
      result = a.genre.ICompare(b.genre);
      break;
    case 5:
      result = compareInt(a.year, b.year);
      break;
    case 6:
      result = compareInt(a.duration, b.duration);
      break;
    case 7:
      result = compareAlbumOrder(a, b, true);
      break;
    case 8:
      result = compareAlbumOrder(a, b, false);
      break;
    case 9:
      result = compareInt(a.bitrate, b.bitrate);
      break;
    case 10:
      result = a.path.ICompare(b.path);
      break;
    case 11:
      result = compareInt(a.rating, b.rating);
      break;
    default:
      return false;
    }
    if (result == 0)
      result = a.path.ICompare(b.path);
    return ascending ? (result < 0) : (result > 0);
  };

  std::sort(fPendingItems.begin(), fPendingItems.end(), cmp);
}

/**
 * @brief Applies the deferred sort state restore.
 *
 * Called from _AddBatch after the final batch of items has been added.
 * Finds the column matching each saved field ID and calls
 * SetSortColumn to re-apply the sort state.
 */
void MediaTableView::_ApplyPendingSortRestore() {
  fHasPendingSortRestore = false;

  ClearSortColumns();

  int32 sortID;
  for (int32 i = 0; fPendingSortRestore.FindInt32("sortID", i, &sortID) == B_OK;
       ++i) {
    if (sortID == 12 && !fIsPlaylistMode)
      continue;

    int32 sortSchema = 0;
    bool legacySortState =
        fPendingSortRestore.FindInt32("sort_schema", &sortSchema) != B_OK;
    if (legacySortState && fIsPlaylistMode && sortID == 7)
      sortID = 12;

    bool ascending = true;
    fPendingSortRestore.FindBool("sortascending", i, &ascending);

    for (int32 c = 0; c < CountColumns(); ++c) {
      BColumn *col = ColumnAt(c);
      if (col && col->LogicalFieldNum() == sortID) {
        SetSortColumn(col, i > 0, ascending);
        break;
      }
    }
  }

  fPendingSortRestore.MakeEmpty();
}

void MediaTableView::StartCellEdit(BRow *row, BColumn *column, int32 colIdx, float colLeft, BView *targetView) {
  CommitCellEdit();

  if (!row || !column || !targetView)
    return;

  BRect rowRect;
  if (!GetRowRect(row, &rowRect))
    return;

  BRect cellRect(colLeft, rowRect.top, colLeft + column->Width(), rowRect.bottom);

  BField *field = row->GetField(colIdx);
  BString initialText;
  if (auto *sf = dynamic_cast<BStringField *>(field)) {
    initialText = sf->String();
  } else if (auto *ifld = dynamic_cast<BIntegerField *>(field)) {
    initialText << ifld->Value();
  }

  fEditingPathPrefix = "";
  if (colIdx == 10) {
    // Path edits are restricted to the file's volume: hide the mount
    // point from the editor and re-prepend it on commit.
    BEntry fileEntry(initialText.String());
    entry_ref ref;
    if (fileEntry.InitCheck() == B_OK && fileEntry.GetRef(&ref) == B_OK) {
      BVolume volume(ref.device);
      BDirectory rootDir;
      if (volume.InitCheck() == B_OK &&
          volume.GetRootDirectory(&rootDir) == B_OK) {
        BEntry rootEntry;
        BPath rootPath;
        if (rootDir.GetEntry(&rootEntry) == B_OK &&
            rootEntry.GetPath(&rootPath) == B_OK) {
          BString mount = rootPath.Path();
          if (mount != "/" && initialText.StartsWith(mount)) {
            fEditingPathPrefix = mount;
            initialText.Remove(0, mount.Length());
          }
        }
      }
    }
  }

  BRect editRect = cellRect;
  editRect.InsetBy(1, 1);

  CellTextControl *editor = new CellTextControl(editRect, "cell_editor", initialText.String(),
                                                new BMessage(MSG_COMMIT_EDIT), BMessenger(this));
  editor->SetTarget(this);

  targetView->AddChild(editor);

  fActiveEditor = editor;
  fEditingRow = row;
  fEditingColumn = column;
  fEditingColIdx = colIdx;
  fEditingOutlineView = targetView;

  BPoint scrollPos(0, 0);
  if (BView *outline = ScrollView()) {
    scrollPos.x = outline->Bounds().left;
    scrollPos.y = outline->Bounds().top;
  }

  editor->MakeFocus(true);
  if (editor->TextView()) {
    editor->TextView()->SelectAll();
    if (fFastEditEnabled) {
      rgb_color textColor;
      rgb_color bg = Color(B_COLOR_BACKGROUND);
      if (bg.red == 0 && bg.green == 0 && bg.blue == 0 && bg.alpha == 0) {
        textColor = (rgb_color){255, 0, 0, 255};
      } else {
        float brightness = (bg.red * 0.299f + bg.green * 0.587f + bg.blue * 0.114f);
        if (brightness < 128.0f) {
          textColor = (rgb_color){255, 182, 193, 255};
        } else {
          textColor = (rgb_color){139, 0, 0, 255};
        }
      }
      editor->TextView()->SetFontAndColor(be_plain_font, B_FONT_ALL, &textColor);
    }
  }

  if (BView *outline = ScrollView())
    outline->ScrollTo(scrollPos);
}

void MediaTableView::CommitCellEdit() {
  if (!fActiveEditor)
    return;

  CellTextControl *editor = fActiveEditor;
  fActiveEditor = nullptr;

  BString newText = editor->Text();

  BString originalText;
  if (fEditingRow) {
    BField *field = fEditingRow->GetField(fEditingColIdx);
    if (auto *sf = dynamic_cast<BStringField *>(field))
      originalText = sf->String();
    else if (auto *ifld = dynamic_cast<BIntegerField *>(field))
      originalText << ifld->Value();
    if (fEditingColIdx == 10 && !fEditingPathPrefix.IsEmpty())
      originalText.Prepend(fEditingPathPrefix);
  }

  BPoint scrollPos(0, 0);
  if (BView *outline = ScrollView()) {
    scrollPos.x = outline->Bounds().left;
    scrollPos.y = outline->Bounds().top;
  }

  if (editor->Parent())
    editor->Parent()->RemoveChild(editor);
  delete editor;

  if (BView *outline = ScrollView())
    outline->ScrollTo(scrollPos);

  if (newText == originalText) {
    fEditingRow = nullptr;
    fEditingColumn = nullptr;
    fEditingColIdx = -1;
    fEditingOutlineView = nullptr;
    fEditingPathPrefix = "";
    return;
  }

  if (fEditingRow) {
    MediaRow *mr = dynamic_cast<MediaRow *>(fEditingRow);
    if (mr) {
      BString path = mr->Item().path;
      const char *fieldName = FieldNameForColumn(fEditingColIdx);
      if (fieldName && !path.IsEmpty()) {
        if (fEditingColIdx == 10) {
          // Path edits move the file on disk instead of writing tags.
          BString newPath = newText;
          if (!newPath.IsEmpty() && !fEditingPathPrefix.IsEmpty()) {
            if (!newPath.StartsWith("/"))
              newPath.Prepend("/");
            newPath.Prepend(fEditingPathPrefix);
          }
          if (!newText.IsEmpty() && newPath != path) {
            BMessage moveMsg(MSG_FILE_MOVE);
            moveMsg.AddString("from", path);
            moveMsg.AddString("to", newPath);
            if (Window())
              Window()->PostMessage(&moveMsg);
          }
        } else {
          BMessage saveMsg(MSG_PROP_SAVE);
          saveMsg.AddString("file", path);
          saveMsg.AddString(fieldName, newText);
          if (Window())
            Window()->PostMessage(&saveMsg);
        }
      }
    }
  }

  fEditingRow = nullptr;
  fEditingColumn = nullptr;
  fEditingColIdx = -1;
  fEditingOutlineView = nullptr;
  fEditingPathPrefix = "";
}

void MediaTableView::CancelCellEdit() {
  if (!fActiveEditor)
    return;

  CellTextControl *editor = fActiveEditor;
  fActiveEditor = nullptr;

  if (editor->Parent()) {
    editor->Parent()->RemoveChild(editor);
  }
  delete editor;

  fEditingRow = nullptr;
  fEditingColumn = nullptr;
  fEditingColIdx = -1;
  fEditingOutlineView = nullptr;
  fEditingPathPrefix = "";
}

const char *MediaTableView::FieldNameForColumn(int32 colIdx) const {
  switch (colIdx) {
    case 0: return "title";
    case 1: return "artist";
    case 2: return "album";
    case 3: return "albumArtist";
    case 4: return "genre";
    case 5: return "year";
    case 7: return "track";
    case 8: return "disc";
    case 10: return "path";
    case 11: return "rating";
    default: return nullptr;
  }
}


BView* MediaTableView::ActiveEditor() const {
  return fActiveEditor;
}

void MediaTableView::LocatePlayingTrack() {
  if (fNowPlayingPath.IsEmpty())
    return;

  for (int32 i = 0; i < CountRows(); ++i) {
    MediaRow *mr = dynamic_cast<MediaRow *>(RowAt(i));
    if (mr && mr->Item().path == fNowPlayingPath) {
      DeselectAll();
      AddToSelection(mr);
      ScrollTo(mr);
      break;
    }
  }
}
