

#include "MusicBrainzMatcherWindow.h"
#include "Debug.h"
#include "Messages.h"
#include "MetadataTagIO.h"

#include <Bitmap.h>
#include <Button.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListItem.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringItem.h>
#include <StringView.h>
#include <View.h>
#include <algorithm>
#include <cinttypes>
#include <ctype.h>
#include <stdio.h>
#include <vector>


#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MusicBrainzMatcherWindow"
/** @name Helper Classes */
///@{

/**
 * @class TrackListItem
 * @brief A list item representing a MusicBrainz track.
 */
class TrackListItem : public BStringItem {
public:
  TrackListItem(const char *text, MusicBrainzMatchTrackInfo *info)
      : BStringItem(text), fInfo(info) {}

  MusicBrainzMatchTrackInfo *TrackInfo() const { return fInfo; }

private:
  MusicBrainzMatchTrackInfo *fInfo;
};

/**
 * @class DraggableListView
 * @brief A list view that supports dragging items to reorder them.
 */
class DraggableListView : public BListView {
public:
  DraggableListView(const char *name)
      : BListView(name, B_SINGLE_SELECTION_LIST,
                  B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS) {}

  bool InitiateDrag(BPoint point, int32 index, bool wasSelected) override {
    if (index < 0)
      return false;

    BStringItem *item = dynamic_cast<BStringItem *>(ItemAt(index));
    if (!item)
      return false;

    BMessage msg(MSG_DRAG_ITEM);
    msg.AddInt32("from_index", index);

    BRect frame = ItemFrame(index);
    DragMessage(&msg, frame, this);
    return true;
  }

  void MessageReceived(BMessage *msg) override {
    if (msg->what == MSG_DRAG_ITEM && msg->WasDropped()) {
      int32 fromIndex;
      if (msg->FindInt32("from_index", &fromIndex) == B_OK) {
        BPoint dropPoint = msg->DropPoint();
        ConvertFromScreen(&dropPoint);
        int32 toIndex = IndexOf(dropPoint);
        if (toIndex < 0) {
          if (dropPoint.y > ItemFrame(CountItems() - 1).bottom)
            toIndex = CountItems() - 1;
          else
            toIndex = 0;
        }

        if (fromIndex != toIndex) {
          MoveItem(fromIndex, toIndex);
          Select(toIndex);
          SelectionChanged();
        }
      }
    } else {
      BListView::MessageReceived(msg);
    }
  }
};

///@}

/// --- MusicBrainzMatcherWindow Implementation ---

/**
 * @brief Constructs the Matcher Window.
 *
 * @param files List of local file paths to match.
 * @param tracks List of MusicBrainz track metadata to match against.
 * @param initialMapping Optional pre-calculated mapping (File Index -> Track
 * Index).
 * @param target The target messenger to receive the final mapping result.
 */
MusicBrainzMatcherWindow::MusicBrainzMatcherWindow(const std::vector<BString> &files,
                             const std::vector<MusicBrainzMatchTrackInfo> &tracks,
                             const std::vector<int> &initialMapping,
                             BMessenger target)
    : BWindow(BRect(100, 100, 800, 650), B_TRANSLATE("Adjust Album Matching"),
              B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS),
      fFiles(files), fTracks(tracks), fInitialMapping(initialMapping),
      fTarget(target) {
  DEBUG_PRINT("Files: %lu, Tracks: %lu\n", fFiles.size(),
              fTracks.size());

  _BuildUI();
  CenterOnScreen();
  Show();
}

MusicBrainzMatcherWindow::~MusicBrainzMatcherWindow() {
  DEBUG_PRINT("Destructor called.\n");
}

/**
 * @brief Builds the UI layout.
 *
 * Creates a split view with:
 * - Left: List of local files (static).
 * - Right: List of MusicBrainz tracks (reorderable).
 */
void MusicBrainzMatcherWindow::_BuildUI() {
  /// Calculate font-relative sizes for DPI scaling
  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  fFileListView = new BListView("fileList");
  fTrackListView = new DraggableListView("trackList");

  /// Populate File List
  for (const auto &f : fFiles) {
    BPath p(f.String());
    fFileListView->AddItem(new BStringItem(p.Leaf()));
  }

  std::vector<MusicBrainzMatchTrackInfo *> orderedTracks(fFiles.size(), nullptr);
  std::vector<bool> trackUsed(fTracks.size(), false);

  for (size_t i = 0; i < fInitialMapping.size(); i++) {
    int trackIdx = fInitialMapping[i];
    if (trackIdx >= 0 && trackIdx < (int)fTracks.size()) {
      if (i < orderedTracks.size()) {
        orderedTracks[i] = const_cast<MusicBrainzMatchTrackInfo *>(&fTracks[trackIdx]);
        trackUsed[trackIdx] = true;
      }
    }
  }

  // Fill gaps
  size_t trackIdx = 0;
  for (size_t i = 0; i < orderedTracks.size(); i++) {
    if (orderedTracks[i] == nullptr) {
      while (trackIdx < fTracks.size() && trackUsed[trackIdx]) {
        trackIdx++;
      }
      if (trackIdx < fTracks.size()) {
        orderedTracks[i] = const_cast<MusicBrainzMatchTrackInfo *>(&fTracks[trackIdx]);
        trackUsed[trackIdx] = true;
      }
    }
  }

  /// Add mapped tracks to view
  for (size_t i = 0; i < orderedTracks.size(); i++) {
    BString label;
    if (orderedTracks[i]) {
      label.SetToFormat("%" PRId32 ". %s (%s)", orderedTracks[i]->index,
                        orderedTracks[i]->name.String(),
                        orderedTracks[i]->duration.String());
      fTrackListView->AddItem(
          new TrackListItem(label.String(), orderedTracks[i]));
    } else {
      const char *label = "";
      if (fTracks.empty())
        label = B_TRANSLATE("Error: No data received");
      fTrackListView->AddItem(new TrackListItem(label, nullptr));
    }
  }

  /// Add remaining (unused) tracks at the bottom
  for (size_t i = 0; i < fTracks.size(); i++) {
    if (!trackUsed[i]) {
      const auto &t = fTracks[i];
      BString label;
      label.SetToFormat("%" PRId32 ". %s (%s)", t.index, t.name.String(),
                        t.duration.String());
      fTrackListView->AddItem(new TrackListItem(
          label.String(), const_cast<MusicBrainzMatchTrackInfo *>(&t)));
    }
  }

  fTrackListView->SetSelectionMessage(new BMessage(MSG_SELECTION_CHANGED));

  /// Controls
  fBtnMoveUp = new BButton("Up", B_TRANSLATE("Up"), new BMessage(MSG_MOVE_UP));
  fBtnMoveDown =
      new BButton("Down", B_TRANSLATE("Down"), new BMessage(MSG_MOVE_DOWN));
  fBtnMoveUp->SetEnabled(false);
  fBtnMoveDown->SetEnabled(false);

  fBtnApply =
      new BButton("Apply", B_TRANSLATE("Apply"), new BMessage(MSG_MATCH_APPLY));
  fBtnCancel = new BButton("Cancel", B_TRANSLATE("Cancel"),
                           new BMessage(MSG_MATCH_CANCEL));

  BScrollView *scrollFiles =
      new BScrollView("scrollFiles", fFileListView, 0, false, true);
  BScrollView *scrollTracks =
      new BScrollView("scrollTracks", fTrackListView, 0, false, true);

  scrollFiles->SetExplicitPreferredSize(BSize(fontHeight, fontHeight));
  scrollTracks->SetExplicitPreferredSize(BSize(fontHeight, fontHeight));

  BStringView *instruction = new BStringView(
      "inst", B_TRANSLATE("Match tracks (right) to files (left)."));
  BFont font(be_bold_font);
  instruction->SetFont(&font);

  BSplitView *splitView = new BSplitView(B_HORIZONTAL, 10.0f);
  BGroupView *leftPane = new BGroupView(B_VERTICAL, 0);

  /// Spacer to align headers if needed (though layout builder handles most)
  BView *spacer =
      new BView(BRect(0, 0, 1, 1), "spacer", B_FOLLOW_NONE, B_WILL_DRAW);
  spacer->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  float w, h;
  fBtnMoveUp->GetPreferredSize(&w, &h);
  spacer->SetExplicitMinSize(BSize(0, h));

  BLayoutBuilder::Group<>(leftPane)
      .AddGroup(B_HORIZONTAL, 5)
      .Add(new BStringView("l1", B_TRANSLATE("Files:")))
      .Add(spacer)
      .AddGlue()
      .End()
      .Add(scrollFiles);

  BGroupView *rightPane = new BGroupView(B_VERTICAL, 0);
  BLayoutBuilder::Group<>(rightPane)
      .AddGroup(B_HORIZONTAL, 5)
      .Add(new BStringView("l2", B_TRANSLATE("MusicBrainz Tracks:")))
      .AddGlue()
      .Add(fBtnMoveUp)
      .Add(fBtnMoveDown)
      .End()
      .Add(scrollTracks);

  splitView->AddChild(leftPane, 1.0f);
  splitView->AddChild(rightPane, 1.0f);

  BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
      .SetInsets(B_USE_WINDOW_INSETS)
      .Add(instruction)
      .Add(splitView)
      .AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
      .AddGlue()
      .Add(fBtnApply)
      .Add(fBtnCancel)
      .End();

  /// If no initial mapping was provided or it looks empty, try smart matching?
  /// The original code called _SmartMatch() at the end, but that overrides the
  /// logic above. We will keep the original behavior:
  _SmartMatch();
}

void MusicBrainzMatcherWindow::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_SELECTION_CHANGED: {
    int32 sel = fTrackListView->CurrentSelection();
    fBtnMoveUp->SetEnabled(sel > 0);
    fBtnMoveDown->SetEnabled(sel >= 0 &&
                             sel < fTrackListView->CountItems() - 1);
    break;
  }
  case MSG_MOVE_UP: {
    int32 sel = fTrackListView->CurrentSelection();
    if (sel > 0) {
      fTrackListView->SwapItems(sel, sel - 1);
      fTrackListView->Select(sel - 1);
      fTrackListView->ScrollToSelection();
    }
    break;
  }
  case MSG_MOVE_DOWN: {
    int32 sel = fTrackListView->CurrentSelection();
    if (sel >= 0 && sel < fTrackListView->CountItems() - 1) {
      fTrackListView->SwapItems(sel, sel + 1);
      fTrackListView->Select(sel + 1);
      fTrackListView->ScrollToSelection();
    }
    break;
  }
  case MSG_MATCH_APPLY:
    _Apply();
    break;
  case MSG_SMART_MATCH:
    _SmartMatch();
    break;
  case MSG_MATCH_CANCEL:
    Quit();
    break;
  default:
    BWindow::MessageReceived(msg);
  }
}

/**
 * @brief Applies the user's mapping and sends the result back to the target.
 */
void MusicBrainzMatcherWindow::_Apply() {
  BMessage result(MSG_MATCH_RESULT);

  for (int32 i = 0; i < fFileListView->CountItems(); i++) {

    if (i >= fTrackListView->CountItems()) {
      result.AddInt32("track_idx", -1);
      continue;
    }

    TrackListItem *item =
        dynamic_cast<TrackListItem *>(fTrackListView->ItemAt(i));
    if (!item || !item->TrackInfo()) {
      result.AddInt32("track_idx", -1);
    } else {
      /// Find the index of this track info in the original fTracks vector
      int foundArrIdx = -1;
      const MusicBrainzMatchTrackInfo *target = item->TrackInfo();
      if (target >= &fTracks[0] && target < &fTracks[fTracks.size()]) {
        foundArrIdx = target - &fTracks[0];
      }
      result.AddInt32("track_idx", foundArrIdx);
    }
    result.AddString("file_path", fFiles[i]);
  }

  fTarget.SendMessage(&result);
  Quit();
}

/**
 * @brief Helper to calculate Levenshtein distance for fuzzy string matching.
 */
int _LevenshteinDistance(const char *s1, const char *s2) {
  int len1 = strlen(s1);
  int len2 = strlen(s2);
  std::vector<std::vector<int>> d(len1 + 1, std::vector<int>(len2 + 1));

  for (int i = 0; i <= len1; i++)
    d[i][0] = i;
  for (int j = 0; j <= len2; j++)
    d[0][j] = j;

  for (int i = 1; i <= len1; i++) {
    for (int j = 1; j <= len2; j++) {
      int cost = (tolower(s1[i - 1]) == tolower(s2[j - 1])) ? 0 : 1;
      d[i][j] =
          std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
    }
  }
  return d[len1][len2];
}

static int ParseDuration(const BString &durStr) {
  if (durStr.IsEmpty())
    return 0;
  int min = 0, sec = 0;
  if (sscanf(durStr.String(), "%d:%d", &min, &sec) >= 2) {
    return min * 60 + sec;
  }
  return 0;
}

/**
 * @brief Attempts to automatically match files to tracks.
 *
 * Uses a weighted scoring system to determine the best match:
 * 1. Duration Match: Checks if file length matches track length (within
 * tolerance).
 * 2. Track Number Match: Checks if metadata track number matches MB index.
 * 3. Name Similarity: Checks for substring matches or Levenshtein distance
 * between filename and track name.
 *
 * Populates the track list with the best guesses.
 */
void MusicBrainzMatcherWindow::_SmartMatch() {
  DEBUG_PRINT("_SmartMatch (Weighted Scoring) start\n");

  if (fTrackListView) {
    for (int32 i = 0; i < fTrackListView->CountItems(); i++) {
      delete fTrackListView->ItemAt(i);
    }
    fTrackListView->MakeEmpty();
  }

  struct Score {
    int score;
    size_t fileIdx;
    size_t trackIdx;
  };

  std::vector<Score> allScores;
  allScores.reserve(fFiles.size() * fTracks.size());

  struct FileInfo {
    int durationSec;
    int trackNum;
    BString cleanName;
  };

  /// Pre-calculate file info (tags, duration, clean name)
  std::vector<FileInfo> fileInfos(fFiles.size());
  for (size_t i = 0; i < fFiles.size(); i++) {
    TagData td;
    MetadataTagIO::ReadTags(BPath(fFiles[i].String()), td);
    fileInfos[i].durationSec = td.lengthSec;

    if (td.track > 0) {
      fileInfos[i].trackNum = td.track;
    } else {
      /// Fallback: Try reading track number from filename
      BString fn = BPath(fFiles[i].String()).Leaf();
      const char *s = fn.String();
      while (*s && !isdigit(*s))
        s++;
      fileInfos[i].trackNum = (*s) ? atoi(s) : 0;
    }

    /// Heuristic: Clean filename for similarity check
    BString fn = BPath(fFiles[i].String()).Leaf();
    int lastDot = fn.FindLast('.');
    if (lastDot > 0)
      fn.Truncate(lastDot);

    const char *p = fn.String();
    while (*p &&
           (isdigit(*p) || isspace(*p) || *p == '-' || *p == '.' || *p == '_'))
      p++;
    fileInfos[i].cleanName = p;
  }

  /// Calculate scores matrix
  for (size_t i = 0; i < fFiles.size(); i++) {
    for (size_t k = 0; k < fTracks.size(); k++) {
      int score = 0;
      const MusicBrainzMatchTrackInfo &trk = fTracks[k];
      const FileInfo &file = fileInfos[i];

      /// 1. Duration Match
      int trkLen = ParseDuration(trk.duration);
      if (trkLen > 0 && file.durationSec > 0) {
        int diff = std::abs(trkLen - file.durationSec);
        if (diff <= 1)
          score += 50;
        else if (diff <= 3)
          score += 30;
        else if (diff <= 10)
          score -= 20;
        else
          score -= 50;
      }

      /// 2. Track Number Match
      if (file.trackNum > 0 && file.trackNum == trk.index) {
        score += 40;
      }

      /// 3. Name Similarity
      if (!file.cleanName.IsEmpty() && !trk.name.IsEmpty()) {
        if (file.cleanName.IFindFirst(trk.name) >= 0) {
          score += 25;
        } else {
          int dist =
              _LevenshteinDistance(file.cleanName.String(), trk.name.String());
          int maxLen = std::max(file.cleanName.Length(), trk.name.Length());
          if (maxLen > 0) {
            float sim = 1.0f - (float)dist / (float)maxLen;
            if (sim > 0.8f)
              score += 20;
            else if (sim > 0.5f)
              score += 10;
          }
        }
      }

      allScores.push_back({score, i, k});
    }
  }

  /// Greedy Assignment based on score
  std::sort(allScores.begin(), allScores.end(),
            [](const Score &a, const Score &b) { return a.score > b.score; });

  std::vector<MusicBrainzMatchTrackInfo *> assignments(fFiles.size(), nullptr);
  std::vector<bool> fileAssigned(fFiles.size(), false);
  std::vector<bool> trackUsed(fTracks.size(), false);

  for (const auto &s : allScores) {
    if (s.score < 0)
      break;

    if (!fileAssigned[s.fileIdx] && !trackUsed[s.trackIdx]) {
      assignments[s.fileIdx] =
          const_cast<MusicBrainzMatchTrackInfo *>(&fTracks[s.trackIdx]);
      fileAssigned[s.fileIdx] = true;
      trackUsed[s.trackIdx] = true;
    }
  }

  /// Fill gaps sequentially for unmatched files
  size_t trackIdx = 0;
  for (size_t i = 0; i < fFiles.size(); i++) {
    if (!assignments[i]) {
      while (trackIdx < fTracks.size() && trackUsed[trackIdx])
        trackIdx++;

      if (trackIdx < fTracks.size()) {
        assignments[i] = const_cast<MusicBrainzMatchTrackInfo *>(&fTracks[trackIdx]);
        trackUsed[trackIdx] = true;
      }
    }
  }

  /// Populate List View
  for (size_t i = 0; i < assignments.size(); i++) {
    if (assignments[i]) {
      BString label;
      label.SetToFormat("%" PRId32 ". %s (%s)", assignments[i]->index,
                        assignments[i]->name.String(),
                        assignments[i]->duration.String());
      fTrackListView->AddItem(
          new TrackListItem(label.String(), assignments[i]));
    } else {
      const char *label = "";
      if (fTracks.empty())
        label = B_TRANSLATE("Error: No data received");
      else
        label = B_TRANSLATE("<-- No Match -->");
      fTrackListView->AddItem(new TrackListItem(label, nullptr));
    }
  }

  /// Add remaining unused tracks at bottom
  for (size_t k = 0; k < fTracks.size(); k++) {
    if (!trackUsed[k]) {
      const auto &t = fTracks[k];
      BString label;
      label.SetToFormat("%" PRId32 ". %s (%s)", t.index, t.name.String(),
                        t.duration.String());
      fTrackListView->AddItem(new TrackListItem(
          label.String(), const_cast<MusicBrainzMatchTrackInfo *>(&t)));
    }
  }
}
