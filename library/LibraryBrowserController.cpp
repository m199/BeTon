#include "LibraryBrowserController.h"
#include "MediaTableView.h"
#include "Debug.h"
#include "MediaItem.h"
#include "Messages.h"
#include "SingleColumnListView.h"
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <Entry.h>
#include <OS.h>
#include <Path.h>
#include <ScrollView.h>
#include <String.h>
#include <Window.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <string>


#include <Catalog.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "LibraryBrowserController"

static BString kLabelAllGenre = B_TRANSLATE("Show all Genre");
static BString kLabelAllArtist = B_TRANSLATE("Show all Artist");
static BString kLabelAllAlbum = B_TRANSLATE("Show all Album");
static BString kLabelNoGenre = B_TRANSLATE("No Genre");
static BString kLabelNoArtist = B_TRANSLATE("No Artist");
static BString kLabelNoAlbum = B_TRANSLATE("No Album");

/**
 * @brief Normalizes album/artist key parts for stable deduplication.
 *
 * Trims, lowercases, and collapses whitespace to single spaces.
 */
static BString
NormalizeAlbumKeyPart(const BString &value)
{
  std::string normalized;
  normalized.reserve(value.Length());

  bool previousWasSpace = true;
  const char *raw = value.String();
  for (int32 i = 0; i < value.Length(); ++i) {
    unsigned char c = (unsigned char)raw[i];
    if (std::isspace(c)) {
      if (!previousWasSpace) {
        normalized.push_back(' ');
        previousWasSpace = true;
      }
      continue;
    }
    normalized.push_back((char)std::tolower(c));
    previousWasSpace = false;
  }

  if (!normalized.empty() && normalized.back() == ' ')
    normalized.pop_back();

  return BString(normalized.c_str());
}

/**
 * @brief Builds a stable album identity key for filtering/disambiguation.
 * @param item Source media item.
 * @param radioMode True when radio-specific album identity should be used.
 */
static BString
AlbumIdentityForItem(const MediaItem &item, bool radioMode)
{
  if (radioMode)
    return item.album;

  if (!item.mbAlbumId.IsEmpty()) {
    BString key("mb:");
    key << item.mbAlbumId;
    return key;
  }

  BString albumArtist = item.albumArtist;
  if (albumArtist.IsEmpty())
    albumArtist = item.artist;

  BString key("meta:");
  key << NormalizeAlbumKeyPart(albumArtist) << "|"
      << NormalizeAlbumKeyPart(item.album) << "|" << item.year;
  return key;
}

/**
 * @brief Constructs the LibraryBrowserController.
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
LibraryBrowserController::LibraryBrowserController(BMessenger target) : fTarget(target) {

  fGenreView = new SingleColumnListView("genre");
  fGenreView->SetSelectionMessage(MSG_SELECTION_CHANGED_GENRE);
  fGenreView->SetTarget(fTarget);

  fArtistView = new SingleColumnListView("artist");
  fArtistView->SetSelectionMessage(MSG_SELECTION_CHANGED_ARTIST);
  fArtistView->SetTarget(fTarget);

  fAlbumView = new SingleColumnListView("album");
  fAlbumView->SetSelectionMessage(MSG_SELECTION_CHANGED_ALBUM);
  fAlbumView->SetTarget(fTarget);

  fContentView = new MediaTableView("content");
}

LibraryBrowserController::~LibraryBrowserController() {
  /// Views are usually owned by the window's view hierarchy,
  /// so we don't strictly need to delete them if they are attached.
}

/**
 * @brief Returns the genre filter column view.
 */
SingleColumnListView *LibraryBrowserController::GenreView() const { return fGenreView; }
/**
 * @brief Returns the artist/country filter column view.
 */
SingleColumnListView *LibraryBrowserController::ArtistView() const { return fArtistView; }
/**
 * @brief Returns the album/language filter column view.
 */
SingleColumnListView *LibraryBrowserController::AlbumView() const { return fAlbumView; }
/**
 * @brief Returns the main content table view.
 */
MediaTableView *LibraryBrowserController::ContentView() const {
  return fContentView;
}

/**
 * @brief Returns current active path scope.
 */
const std::vector<BString> &LibraryBrowserController::ActivePaths() const {
  return fActivePaths;
}

/**
 * @brief Sets active path scope and clears active item scope.
 */
void LibraryBrowserController::SetActivePaths(const std::vector<BString> &paths) {
  fActivePaths = paths;
  fActiveItems.clear(); ///< Paths take precedence, clear objects
}

/**
 * @brief Sets active item scope and synchronizes path scope.
 */
void LibraryBrowserController::SetActiveItems(const std::vector<MediaItem> &items) {
  fActiveItems = items;
  fActivePaths.clear();
  /// Sync paths for IsPathAllowed compatibility
  fActivePaths.reserve(items.size());
  for (const auto &it : items)
    fActivePaths.push_back(it.path);
}

/**
 * @brief Returns selected display text from a filter view.
 */
BString LibraryBrowserController::SelectedText(SingleColumnListView *v) {
  if (!v)
    return "";
  int32 sel = v->CurrentSelection();
  if (sel >= 0) {
    return v->ItemAt(sel);
  }
  return "";
}

/**
 * @brief Returns selected hidden data/path from a filter view.
 */
BString LibraryBrowserController::SelectedData(SingleColumnListView *v) {
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
void LibraryBrowserController::ResetFilters() {
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
bool LibraryBrowserController::IsPathAllowed(const BString &filePath,
                                       bool isLibraryMode) const {
  return _PathAllowedByMode(filePath, isLibraryMode, fActivePaths);
}

bool LibraryBrowserController::_PathAllowedByMode(
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

void LibraryBrowserController::SetRadioFilterMode(bool radio) {
  fIsRadioFilterMode = radio;
  if (radio) {
    kLabelAllGenre = B_TRANSLATE("Show all Genre");
    kLabelAllArtist = B_TRANSLATE("Show all Country");
    kLabelAllAlbum = B_TRANSLATE("Show all Language");
    kLabelNoGenre = B_TRANSLATE("No Genre");
    kLabelNoArtist = B_TRANSLATE("No Country");
    kLabelNoAlbum = B_TRANSLATE("No Language");
  } else {
    kLabelAllGenre = B_TRANSLATE("Show all Genre");
    kLabelAllArtist = B_TRANSLATE("Show all Artist");
    kLabelAllAlbum = B_TRANSLATE("Show all Album");
    kLabelNoGenre = B_TRANSLATE("No Genre");
    kLabelNoArtist = B_TRANSLATE("No Artist");
    kLabelNoAlbum = B_TRANSLATE("No Album");
  }
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
 * 7. Prepare Display Items (handling "Show all", "No" and
 * Disambiguation).
 * 8. Smart Update of List Views.
 *
 * @param allItems The full database of media items.
 * @param isLibraryMode True if showing full library, False if showing a
 * specific playlist (ActivePaths).
 * @param currentContext Name of current view context (Library/Radio/DLNA/Playlist).
 * @param filterText Search filter text.
 * @param preserveScroll True to preserve content scroll position.
 * @param updateContentList True to rebuild content list, false to update filters only.
 */
void LibraryBrowserController::UpdateFilteredViews(
    const std::vector<MediaItem> &allItems, bool isLibraryMode,
    const BString &currentContext, const BString &filterText,
    bool preserveScroll, bool updateContentList) {

  bigtime_t tStart = system_time();

  /// 1. Save state for previous context if changed
  if (!fCurrentContext.IsEmpty() && fCurrentContext != currentContext) {
    FilterState &state = fSavedStates[fCurrentContext];
    state.genre = SelectedText(fGenreView);
    state.artist = SelectedText(fArtistView);
    state.album = SelectedText(fAlbumView);
    state.sortState.MakeEmpty();
    if (fContentView)
      fContentView->SaveSortState(&state.sortState);
  }

  BString selGenre = SelectedText(fGenreView);
  BString selArtist = SelectedText(fArtistView);
  BString selAlbum = SelectedText(fAlbumView);

  /// 2. Restore state for new context if changing contexts or on first update
  bool contextChanged = (fCurrentContext != currentContext);
  bool restoreFilters = (fFirstUpdate || contextChanged);

  if (restoreFilters) {
    if (fSavedStates.count(currentContext) > 0) {
      FilterState &state = fSavedStates[currentContext];
      selGenre = state.genre;
      selArtist = state.artist;
      selAlbum = state.album;

      if (fContentView && !state.sortState.IsEmpty())
        fContentView->RestoreSortState(&state.sortState);
    } else {
      /// No saved state for this context, default to empty/all
      selGenre = "";
      selArtist = "";
      selAlbum = "";
    }

    fLastSelectedGenre = selGenre;
    fLastSelectedArtist = selArtist;

    /// Only mark as restored if we actually had something to work with
    if (!allItems.empty() || !fActivePaths.empty())
      fFirstUpdate = false;
  }

  fCurrentContext = currentContext;

  /// Reset downstream selections if upstream selection changed
  if (selGenre != fLastSelectedGenre) {
    selArtist = "";
    selAlbum = "";
  } else if (selArtist != fLastSelectedArtist) {
    selAlbum = "";
  }

  fLastSelectedGenre = selGenre;
  fLastSelectedArtist = selArtist;

  /// 1. Filter Source Items based on Library/Playlist Mode
  std::vector<MediaItem> playlistItems;

  if (!isLibraryMode) {
    if (!fActiveItems.empty()) {
      playlistItems = fActiveItems;
    } else {
      /// Fallback: Build items from paths (e.g. from a loaded .m3u)
      playlistItems.reserve(fActivePaths.size());

      /// Optimization: Build a map for O(1) lookup in allItems
      std::unordered_map<std::string, size_t> pathMap;
      pathMap.reserve(allItems.size());
      for (size_t i = 0; i < allItems.size(); ++i) {
        pathMap[allItems[i].path.String()] = i;
      }

      for (const auto &p : fActivePaths) {
        auto it = pathMap.find(p.String());
        if (it != pathMap.end()) {
          playlistItems.push_back(allItems[it->second]);
        } else {
          MediaItem mi;
          mi.path = p;

          BPath bp(p.String());
          mi.title = bp.Leaf() ? bp.Leaf() : p.String();

          BEntry e(bp.Path());
          mi.missing = !e.Exists();
          playlistItems.push_back(mi);
        }
      }
    }
  }

  const std::vector<MediaItem> &sourceItems =
      isLibraryMode ? allItems : playlistItems;

  if (updateContentList) {
    if (preserveScroll) {
      fContentView->SaveScrollState();
    }
    fContentView->ClearEntries();
  }

  /// 2. Build Filter Sets
  std::set<BString> allGenres;
  bool hasUntaggedGenreSrc = false;

  std::set<BString> artistsForGenre;
  bool hasUntaggedArtistForGenre = false;

  struct AlbumKey {
    int32 year;
    BString identity;
    bool operator<(const AlbumKey &o) const {
      if (identity == o.identity)
        return false;
      if (year != o.year)
        return year < o.year;
      return identity.Compare(o.identity) < 0;
    }
  };

  /// Map AlbumName -> Set of AlbumKeys (for disambiguation)
  std::map<BString, std::set<AlbumKey>> albumsForGA;
  bool hasUntaggedAlbumForGA = false;

  /// -- Filter Lambdas --

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

    /// Check against hidden data if available
    if (!selAlbumData.IsEmpty()) {
      if (AlbumIdentityForItem(i, fIsRadioFilterMode) == selAlbumData)
        return true;

      BString legacyData = i.album;
      legacyData << "|" << i.year << "|" << i.base;
      return legacyData == selAlbumData;
    }

    /// Standard album name match
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

  /// 3. Populate Filter Lists (Genre, Artist, Album)
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
          albumsForGA[it.album].insert(
              {it.year, AlbumIdentityForItem(it, fIsRadioFilterMode)});
        }
      }
    }
  }

  /// 4. Build Final Content List
  std::vector<MediaItem> finalItems;
  finalItems.reserve(sourceItems.size());

  for (const auto &it : sourceItems) {
    if (!(genreOK(it) && artistOK(it) && albumOK(it)))
      continue;
    if (!textOK(it))
      continue;
    finalItems.push_back(it);
  }

  /// 5. Notify Target (Main Window) about totals
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

  bigtime_t tFilter = system_time();

  /// 6. Update Content View
  size_t finalCount = finalItems.size();
  if (updateContentList) {
    if (restoreFilters && fSavedStates.count(currentContext) > 0) {
      FilterState &state = fSavedStates[currentContext];
      if (!state.sortState.IsEmpty())
        fContentView->RestoreSortState(&state.sortState);
    }
    fContentView->SetPlaylistMode(!isLibraryMode);
    fContentView->AddEntries(std::move(finalItems));
  }

  bigtime_t tContent = system_time();

  DEBUG_PRINT("UpdateFilteredViews: source=%zu, "
              "filtered=%zu, filterBuild=%lld us, addEntries=%lld us\n",
              sourceItems.size(), finalCount, (long long)(tFilter - tStart),
              (long long)(tContent - tFilter));

  /// 7. Prepare Display Items (handling "All", "No...", and
  /// Disambiguation)
  struct DisplayItem {
    BString text;
    BString data; ///< Hidden data (e.g. "AlbumName|2023")
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

  for (auto &[name, keys] : albumsForGA) {
    if (keys.empty())
      continue;

    std::vector<AlbumKey> sortedKeys(keys.begin(), keys.end());

    if (sortedKeys.size() == 1) {
      /// Single item, no visual disambiguation needed, but store data just in
      /// case
      BString data = sortedKeys[0].identity;
      albumDisplayItems.push_back({name, data});
    } else {
      /// Multiple items for same album name -> Disambiguate
      int32 lastYear = -1;
      char letter = 'A';

      for (const auto &k : sortedKeys) {
        int32 y = k.year;

        BString displayName = name;

        /// Check if there are other keys with the SAME year
        bool sameYearExists = false;
        for (const auto &other : sortedKeys) {
          if (&other != &k && other.year == y) {
            sameYearExists = true;
            break;
          }
        }

        if (sameYearExists) {
          if (y != lastYear) {
            lastYear = y;
            letter = 'A';
          }
          if (y > 0) {
            displayName << " [" << y << ", " << letter << "]";
          } else {
            displayName << " [" << letter << "]";
          }
          letter++;
        } else {
          if (y > 0) {
            displayName << " [" << y << "]";
          } else {
            displayName << " [?]";
          }
        }

        BString data = k.identity;
        albumDisplayItems.push_back({displayName, data});
      }
    }
  }

  /// 8. Smart Update of List Views (Prevent flickering/scrolling reset if
  /// unchanged)
  bigtime_t tSmartStart = system_time();

  auto smartUpdateWithData =
      [&](SingleColumnListView *view, const std::vector<DisplayItem> &newItems,
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

        std::vector<std::pair<BString, BString>> bulk;
        bulk.reserve(newItems.size());
        for (const auto &item : newItems) {
          bulk.push_back({item.text, item.data});
        }
        view->SetItems(bulk);

        /// Restore Selection
        if (!currentSelText.IsEmpty()) {
          bool found = false;

          /// Try matching by data first (more precise)
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

          /// Fallback to text match
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

  bigtime_t tSmartEnd = system_time();
  DEBUG_PRINT("smartUpdate: genre=%ld, artist=%ld, "
              "album=%ld, time=%lld us\n",
              (long)genreItems.size(), (long)artistItems.size(),
              (long)albumDisplayItems.size(),
              (long long)(tSmartEnd - tSmartStart));
}

/**
 * @brief Adds a single item to the views incrementally.
 * Note: Only used for real-time updates (e.g. during scan).
 */
void LibraryBrowserController::AddMediaItem(const MediaItem &item) {

  fContentView->AddEntry(item);

  auto addUnique = [](SingleColumnListView *v, const BString &val,
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

/**
 * @brief Saves the current filter and sort state to a BMessage.
 *
 * Persists the active genre/artist/album filter selections and
 * the sort column state so they survive application restarts.
 *
 * @param msg Target message to store state into.
 */
void LibraryBrowserController::SaveSettings(BMessage *msg) {
  if (!msg)
    return;

  /// Sync current UI into map before saving
  if (!fCurrentContext.IsEmpty()) {
    FilterState &state = fSavedStates[fCurrentContext];
    state.genre = SelectedText(fGenreView);
    state.artist = SelectedText(fArtistView);
    state.album = SelectedText(fAlbumView);
    state.sortState.MakeEmpty();
    if (fContentView)
      fContentView->SaveSortState(&state.sortState);
  }

  for (const auto &pair : fSavedStates) {
    BMessage stateMsg;
    stateMsg.AddString("context", pair.first);
    if (!pair.second.genre.IsEmpty())
      stateMsg.AddString("genre", pair.second.genre);
    if (!pair.second.artist.IsEmpty())
      stateMsg.AddString("artist", pair.second.artist);
    if (!pair.second.album.IsEmpty())
      stateMsg.AddString("album", pair.second.album);
    if (!pair.second.sortState.IsEmpty())
      stateMsg.AddMessage("sort", &pair.second.sortState);
    msg->AddMessage("filter_state", &stateMsg);
  }
}

/**
 * @brief Restores filter and sort state from a BMessage.
 *
 * Populates the saved state variables so the next call to
 * UpdateFilteredViews() (with restoreFilters=true) will apply them.
 *
 * @param msg Source message containing saved state.
 */
void LibraryBrowserController::LoadSettings(const BMessage *msg) {
  if (!msg)
    return;

  BMessage stateMsg;
  int i = 0;
  while (msg->FindMessage("filter_state", i++, &stateMsg) == B_OK) {
    BString ctx;
    if (stateMsg.FindString("context", &ctx) == B_OK) {
      FilterState state;
      stateMsg.FindString("genre", &state.genre);
      stateMsg.FindString("artist", &state.artist);
      stateMsg.FindString("album", &state.album);
      stateMsg.FindMessage("sort", &state.sortState);
      fSavedStates[ctx] = state;
    }
  }

  /// Backward compatibility for old format
  if (msg->HasString("lib_genre") && fSavedStates.count("Library") == 0) {
    FilterState &libState = fSavedStates["Library"];
    msg->FindString("lib_genre", &libState.genre);
    msg->FindString("lib_artist", &libState.artist);
    msg->FindString("lib_album", &libState.album);
    msg->FindMessage("lib_sort", &libState.sortState);
  }
}
