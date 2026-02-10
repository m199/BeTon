#include "LibraryViewManager.h"
#include "ContentColumnView.h"
#include "Debug.h"
#include "MediaItem.h"
#include "Messages.h"
#include "SimpleColumnView.h"
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <Entry.h>
#include <Path.h>
#include <ScrollView.h>
#include <String.h>
#include <Window.h>
#include <algorithm>
#include <cstdio>
#include <set>

#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LibraryViewManager"

static const BString kLabelAllGenre = B_TRANSLATE("Show All Genre");
static const BString kLabelAllArtist = B_TRANSLATE("Show All Artist");
static const BString kLabelAllAlbum = B_TRANSLATE("Show All Album");
static const BString kLabelNoGenre = B_TRANSLATE("No Genre");
static const BString kLabelNoArtist = B_TRANSLATE("No Artist");
static const BString kLabelNoAlbum = B_TRANSLATE("No Album");

/**
 * @brief Constructs the LibraryViewManager.
 *
 * Initializes the four main column views:
 * - Genre
 * - Artist
 * - Album
 * - Content (Tracks)
 *
 * Sets up message targets for selection changes.
 *
 * @param target The messenger (typically MainWindow) to receive selection
 * messages.
 */
LibraryViewManager::LibraryViewManager(BMessenger target) : fTarget(target) {

  fGenreView = new SimpleColumnView("genre");
  fGenreView->SetSelectionMessage(MSG_SELECTION_CHANGED_GENRE);
  fGenreView->SetTarget(fTarget);

  fArtistView = new SimpleColumnView("artist");
  fArtistView->SetSelectionMessage(MSG_SELECTION_CHANGED_ARTIST);
  fArtistView->SetTarget(fTarget);

  fAlbumView = new SimpleColumnView("album");
  fAlbumView->SetSelectionMessage(MSG_SELECTION_CHANGED_ALBUM);
  fAlbumView->SetTarget(fTarget);

  fContentView = new ContentColumnView("content");
}

LibraryViewManager::~LibraryViewManager() {
  // Views are usually owned by the window's view hierarchy,
  // so we don't strictly need to delete them if they are attached.
}

SimpleColumnView *LibraryViewManager::GenreView() const { return fGenreView; }
SimpleColumnView *LibraryViewManager::ArtistView() const { return fArtistView; }
SimpleColumnView *LibraryViewManager::AlbumView() const { return fAlbumView; }
ContentColumnView *LibraryViewManager::ContentView() const {
  return fContentView;
}

const std::vector<BString> &LibraryViewManager::ActivePaths() const {
  return fActivePaths;
}

void LibraryViewManager::SetActivePaths(const std::vector<BString> &paths) {
  fActivePaths = paths;
}

BString LibraryViewManager::SelectedText(SimpleColumnView *v) {
  if (!v)
    return "";
  int32 sel = v->CurrentSelection();
  if (sel >= 0) {
    return v->ItemAt(sel);
  }
  return "";
}

BString LibraryViewManager::SelectedData(SimpleColumnView *v) {
  if (!v)
    return "";
  int32 sel = v->CurrentSelection();
  if (sel >= 0) {
    return v->PathAt(sel);
  }
  return "";
}

/**
 * @brief Resets all filters and clears the content view.
 */
void LibraryViewManager::ResetFilters() {
  fGenreView->Clear();
  fArtistView->Clear();
  fAlbumView->Clear();
  fContentView->ClearEntries();
  fActivePaths.clear();
}

/**
 * @brief Checks if a file path is allowed based on the current mode (Library vs
 * Playlist).
 */
bool LibraryViewManager::IsPathAllowed(const BString &filePath,
                                       bool isLibraryMode) const {
  return _PathAllowedByMode(filePath, isLibraryMode, fActivePaths);
}

bool LibraryViewManager::_PathAllowedByMode(
    const BString &filePath, bool isLibraryMode,
    const std::vector<BString> &activePaths) const {
  if (isLibraryMode)
    return true;

  for (const auto &p : activePaths) {
    if (p == filePath)
      return true;
  }
  return false;
}

/**
 * @brief The core filtering logic.
 *
 * Updates Genre -> Artist -> Album -> Content views based on the current
 * selection. Also handles "smart updates" to avoid flicker if list contents
 * haven't changed.
 *
 * Filtering Process:
 * 1. Filter Source Items based on Library/Playlist Mode.
 * 2. Build Filter Sets (Genre, Artist, Album -> Years).
 * 3. Populate Filter Lists (Genre, Artist, Album).
 * 4. Build Final Content List.
 * 5. Notify Target (Main Window) about totals.
 * 6. Update Content View.
 * 7. Prepare Display Items (handling "Alles anzeigen", "Kein..." and
 * Disambiguation).
 * 8. Smart Update of List Views.
 *
 * @param allItems The full database of media items.
 * @param isLibraryMode True if showing full library, False if showing a
 * specific playlist (ActivePaths).
 * @param currentPlaylist Name of the current playlist (for UI if needed).
 * @param filterText Search filter text.
 */
void LibraryViewManager::UpdateFilteredViews(
    const std::vector<MediaItem> &allItems, bool isLibraryMode,
    const BString &currentPlaylist, const BString &filterText) {

  BString selGenre = SelectedText(fGenreView);
  BString selArtist = SelectedText(fArtistView);
  BString selAlbum = SelectedText(fAlbumView);

  // Reset downstream selections if upstream selection changed
  if (selGenre != fLastSelectedGenre) {
    selArtist = "";
    selAlbum = "";
  } else if (selArtist != fLastSelectedArtist) {
    selAlbum = "";
  }

  fLastSelectedGenre = selGenre;
  fLastSelectedArtist = selArtist;

  // 1. Filter Source Items based on Library/Playlist Mode
  std::vector<MediaItem> sourceItems;

  if (isLibraryMode) {
    sourceItems = allItems;
  } else {
    sourceItems.reserve(fActivePaths.size());
    for (const auto &p : fActivePaths) {
      auto it = std::find_if(allItems.begin(), allItems.end(),
                             [&](const MediaItem &mi) { return mi.path == p; });
      if (it != allItems.end()) {
        sourceItems.push_back(*it);
      } else {
        // Create dummy item for missing files in playlist
        MediaItem mi;
        mi.path = p;

        BPath bp(p.String());
        mi.title = bp.Leaf() ? bp.Leaf() : p.String();

        BEntry e(bp.Path());
        mi.missing = !e.Exists();
        sourceItems.push_back(mi);
      }
    }
  }

  fContentView->ClearEntries();

  // 2. Build Filter Sets
  std::set<BString> allGenres;
  bool hasUntaggedGenreSrc = false;

  std::set<BString> artistsForGenre;
  bool hasUntaggedArtistForGenre = false;

  // Map AlbumName -> Set of Years (for disambiguation)
  std::map<BString, std::set<int32>> albumsForGA;
  bool hasUntaggedAlbumForGA = false;

  // -- Filter Lambdas --

  auto genreOK = [&](const MediaItem &i) {
    if (selGenre.IsEmpty() || selGenre == kLabelAllGenre)
      return true;
    if (selGenre == kLabelNoGenre)
      return i.genre.IsEmpty();
    return i.genre == selGenre;
  };

  auto artistOK = [&](const MediaItem &i) {
    if (selArtist.IsEmpty() || selArtist == kLabelAllArtist)
      return true;
    if (selArtist == kLabelNoArtist)
      return i.artist.IsEmpty();
    return i.artist == selArtist;
  };

  BString selAlbumData = SelectedData(fAlbumView);

  auto albumOK = [&](const MediaItem &i) {
    if (selAlbum.IsEmpty() || selAlbum == kLabelAllAlbum)
      return true;
    if (selAlbum == kLabelNoAlbum)
      return i.album.IsEmpty();

    // Check for Year disambiguation in hidden data column
    if (!selAlbumData.IsEmpty()) {
      int32 sep = selAlbumData.FindLast("|");
      if (sep > 0) {
        BString yearStr = selAlbumData.String() + sep + 1;
        int32 targetYear = atoi(yearStr.String());

        BString targetName;
        selAlbumData.CopyInto(targetName, 0, sep);

        if (i.album != targetName)
          return false;
        if (i.year != targetYear)
          return false;

        return true;
      }
    }

    // Standard album name match
    if (i.album != selAlbum)
      return false;

    return true;
  };

  auto textOK = [&](const MediaItem &i) {
    if (filterText.IsEmpty())
      return true;
    if (i.title.IFindFirst(filterText) >= 0)
      return true;
    if (i.artist.IFindFirst(filterText) >= 0)
      return true;
    if (i.album.IFindFirst(filterText) >= 0)
      return true;
    return false;
  };

  // 3. Populate Filter Lists (Genre, Artist, Album)
  for (const auto &it : sourceItems) {
    if (!textOK(it))
      continue;

    if (it.genre.IsEmpty())
      hasUntaggedGenreSrc = true;
    else
      allGenres.insert(it.genre);

    if (genreOK(it)) {
      if (it.artist.IsEmpty())
        hasUntaggedArtistForGenre = true;
      else
        artistsForGenre.insert(it.artist);

      if (artistOK(it)) {
        if (it.album.IsEmpty())
          hasUntaggedAlbumForGA = true;
        else {
          albumsForGA[it.album].insert(it.year);
        }
      }
    }
  }

  // 4. Build Final Content List
  std::vector<MediaItem> finalItems;
  finalItems.reserve(sourceItems.size());

  for (const auto &it : sourceItems) {
    if (!(genreOK(it) && artistOK(it) && albumOK(it)))
      continue;
    if (!textOK(it))
      continue;
    finalItems.push_back(it);
  }

  // 5. Notify Target (Main Window) about totals
  int32 totalCount = finalItems.size();
  int64 totalDuration = 0;
  for (const auto &it : finalItems) {
    totalDuration += it.duration;
  }

  if (fTarget.IsValid()) {
    BMessage previewMsg(MSG_LIBRARY_PREVIEW);
    previewMsg.AddInt32("count", totalCount);
    previewMsg.AddInt64("duration", totalDuration);
    fTarget.SendMessage(&previewMsg);
  }

  // 6. Update Content View
  fContentView->AddEntries(finalItems);

  // 7. Prepare Display Items (handling "All", "No...", and
  // Disambiguation)
  struct DisplayItem {
    BString text;
    BString data; // Hidden data (e.g. "AlbumName|2023")
  };

  std::vector<BString> genreItems;
  genreItems.push_back(kLabelAllGenre);
  if (hasUntaggedGenreSrc)
    genreItems.push_back(kLabelNoGenre);
  for (const auto &g : allGenres)
    genreItems.push_back(g);

  std::vector<BString> artistItems;
  artistItems.push_back(kLabelAllArtist);
  if (hasUntaggedArtistForGenre)
    artistItems.push_back(kLabelNoArtist);
  for (const auto &a : artistsForGenre)
    artistItems.push_back(a);

  std::vector<DisplayItem> albumDisplayItems;
  albumDisplayItems.push_back({kLabelAllAlbum, ""});
  if (hasUntaggedAlbumForGA)
    albumDisplayItems.push_back({kLabelNoAlbum, ""});

  for (auto &[name, years] : albumsForGA) {
    if (years.empty())
      continue;

    std::vector<int32> sortedYears(years.begin(), years.end());
    std::sort(sortedYears.begin(),
              sortedYears.end()); // Ensure years are sorted

    if (sortedYears.size() == 1) {
      // Single year, no visual disambiguation needed, but store data just in
      // case
      int32 y = sortedYears[0];
      BString data = name;
      data << "|" << y;
      albumDisplayItems.push_back({name, data});
    } else {
      // Multiple years for same album name -> Disambiguate
      for (int32 y : sortedYears) {
        BString displayName = name;
        if (y > 0) {
          displayName << " [" << y << "]";
        } else {
          displayName << " [?]";
        }

        BString data = name;
        data << "|" << y;
        albumDisplayItems.push_back({displayName, data});
      }
    }
  }

  // 8. Smart Update of List Views (Prevent flickering/scrolling reset if
  // unchanged)
  auto smartUpdateWithData =
      [&](SimpleColumnView *view, const std::vector<DisplayItem> &newItems,
          const BString &currentSelText, const BString &currentSelData) {
        bool changed = false;
        if (view->CountItems() != (int32)newItems.size()) {
          changed = true;
        } else {
          for (int32 i = 0; i < (int32)newItems.size(); i++) {
            if (view->ItemAt(i) != newItems[i].text ||
                view->PathAt(i) != newItems[i].data) {
              changed = true;
              break;
            }
          }
        }

        if (!changed)
          return;

        view->Clear();
        for (const auto &item : newItems) {
          view->AddItem(item.text, item.data);
        }

        // Restore Selection
        if (!currentSelText.IsEmpty()) {
          bool found = false;

          // Try matching by data first (more precise)
          if (!currentSelData.IsEmpty()) {
            for (int32 i = 0; i < view->CountItems(); i++) {
              if (view->PathAt(i) == currentSelData) {
                view->Select(i);
                view->ScrollToSelection();
                found = true;
                break;
              }
            }
          }

          // Fallback to text match
          if (!found) {
            for (int32 i = 0; i < view->CountItems(); i++) {
              if (view->ItemAt(i) == currentSelText) {
                view->Select(i);
                view->ScrollToSelection();
                break;
              }
            }
          }
        }
      };

  auto toDisplay = [](const std::vector<BString> &strs) {
    std::vector<DisplayItem> out;
    for (const auto &s : strs)
      out.push_back({s, ""});
    return out;
  };

  smartUpdateWithData(fGenreView, toDisplay(genreItems), selGenre, "");
  smartUpdateWithData(fArtistView, toDisplay(artistItems), selArtist, "");
  smartUpdateWithData(fAlbumView, albumDisplayItems, selAlbum, selAlbumData);
}

/**
 * @brief Adds a single item to the views incrementally.
 * Note: Only used for real-time updates (e.g. during scan).
 */
void LibraryViewManager::AddMediaItem(const MediaItem &item) {

  fContentView->AddEntry(item);

  auto addUnique = [](SimpleColumnView *v, const BString &val,
                      const char *emptyLabel) {
    BString text = val.IsEmpty() ? BString(emptyLabel) : val;

    for (int32 i = 0; i < v->CountItems(); i++) {
      if (v->ItemAt(i) == text)
        return;
    }
    v->AddItem(text);
  };

  addUnique(fGenreView, item.genre, kLabelNoGenre.String());

  BString selGenre = SelectedText(fGenreView);
  bool genreMatch = (selGenre.IsEmpty() || selGenre == kLabelAllGenre ||
                     (selGenre == kLabelNoGenre && item.genre.IsEmpty()) ||
                     selGenre == item.genre);

  if (genreMatch) {
    addUnique(fArtistView, item.artist, kLabelNoArtist.String());

    BString selArtist = SelectedText(fArtistView);
    bool artistMatch =
        (selArtist.IsEmpty() || selArtist == kLabelAllArtist ||
         (selArtist == kLabelNoArtist && item.artist.IsEmpty()) ||
         selArtist == item.artist);

    if (artistMatch) {
      addUnique(fAlbumView, item.album, kLabelNoAlbum.String());
    }
  }
}
