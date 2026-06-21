#ifndef BETON_LIBRARY_BROWSER_CONTROLLER_H
#define BETON_LIBRARY_BROWSER_CONTROLLER_H

#include "MediaTableView.h"
#include "MediaItem.h"
#include "SingleColumnListView.h"
#include <Message.h>
#include <Messenger.h>
#include <String.h>
#include <SupportDefs.h>
#include <set>
#include <unordered_map>
#include <vector>

/**
 * @class LibraryBrowserController
 * @brief Manages the "Column Browser" interface (Genre -> Artist -> Album ->
 * Tracks).
 *
 * This class handles the filtering logic, updating the cascading column views
 * based on selection, and maintaining the state of the "Active Playlist"
 * filtering (fActivePaths).
 *
 * It coordinates:
 * - `SingleColumnListView`s for Genre, Artist, Album.
 * - `MediaTableView` for the main track list.
 */
class LibraryBrowserController {
public:
  /**
   * @brief Constructs the manager.
   * @param target The BMessenger to receive selection change messages
   * (typically MainWindow).
   */
  LibraryBrowserController(BMessenger target);
  /**
   * @brief Destroys the controller.
   */
  ~LibraryBrowserController();

  /** @name View Accessors */
  ///@{
  /** @brief Returns the genre filter column view. */
  SingleColumnListView *GenreView() const;
  /** @brief Returns the artist/country filter column view. */
  SingleColumnListView *ArtistView() const;
  /** @brief Returns the album/language filter column view. */
  SingleColumnListView *AlbumView() const;
  /** @brief Returns the main media content table view. */
  MediaTableView *ContentView() const;

  /**
   * @brief Updates the filtered views based on the full database and current
   * selection.
   *
   * This is the heavy-lifting function that filters `allItems` down to the
   * lists displayed in each column using the current genre/artist/album
   * selection and search text.
   *
   * @param allItems Complete list of all media items in the cache.
   * @param isLibraryMode If true, shows everything. If false, filters by
   * `fActivePaths`.
   * @param currentContext Name of the current UI context (Library/Radio/DLNA/Playlist).
   * context if needed).
   * @param filterText Search filter string (default empty).
   * @param preserveScroll If true, keeps previous content scroll position.
   * @param updateContentList If false, updates filters only and keeps content list untouched.
   */
  void UpdateFilteredViews(const std::vector<MediaItem> &allItems,
                           bool isLibraryMode, const BString &currentContext,
                           const BString &filterText = "",
                           bool preserveScroll = false,
                           bool updateContentList = true);

  /**
   * @brief Incrementally adds a media item (used during live scanning).
   */
  void AddMediaItem(const MediaItem &item);

  /**
   * @brief Clears all filters and views.
   */
  void ResetFilters();

  ///@}

  /** @name Static Helper Methods */
  ///@{
  /**
   * @brief Returns the currently selected display text from a filter view.
   * @param v Filter view pointer.
   */
  static BString SelectedText(SingleColumnListView *v);
  /**
   * @brief Returns hidden data/path associated with selected item in a filter view.
   * @param v Filter view pointer.
   */
  static BString SelectedData(SingleColumnListView *v);

  ///@}

  /** @name Playlist / Active Scope Management */
  ///@{
  /** @brief Returns currently active path scope used in non-library mode. */
  const std::vector<BString> &ActivePaths() const;
  /**
   * @brief Sets active path scope for playlist-like filtering.
   * @param paths Allowed media paths.
   */
  void SetActivePaths(const std::vector<BString> &paths);
  /**
   * @brief Sets active item scope directly and synchronizes active paths.
   * @param items Allowed media items.
   */
  void SetActiveItems(const std::vector<MediaItem> &items);

  /**
   * @brief Replaces a path in the active scope after a file was moved.
   * @param from Old absolute path.
   * @param to New absolute path.
   */
  void RenameActivePath(const BString &from, const BString &to);

  /**
   * @brief Checks if a specific file path is allowed in the current view mode.
   */
  bool IsPathAllowed(const BString &filePath, bool isLibraryMode) const;

  /**
   * @brief Enables/disables radio filter mode.
   *
   * In radio mode the filter columns show Genre/Country/Language labels
   * instead of Genre/Artist/Album.
   */
  void SetRadioFilterMode(bool radio);

private:
  /**
   * @brief Internal helper to check path allowance against active paths.
   */
  bool _PathAllowedByMode(const BString &filePath, bool isLibraryMode,
                          const std::vector<BString> &activePaths) const;

private:
  /** @name State */
  ///@{
  BMessenger fTarget;

  SingleColumnListView *fGenreView;
  SingleColumnListView *fArtistView;
  SingleColumnListView *fAlbumView;
  MediaTableView *fContentView;

  std::vector<BString> fActivePaths;
  std::vector<MediaItem> fActiveItems;

  /// Cache last selection to avoid resetting downstream columns unnecessarily
  BString fLastSelectedGenre;
  BString fLastSelectedArtist;

  struct FilterState {
      BString genre;
      BString artist;
      BString album;
      BMessage sortState;
  };
  std::map<BString, FilterState> fSavedStates;
  BString fCurrentContext;

  bool fIsRadioFilterMode{false}; ///< Controls filter label text
  bool fFirstUpdate{true};        ///< Force filter restore on first update
  ///@}

public:
  /**
   * @brief Saves filter and sort state to a BMessage for persistence.
   * @param msg Target message to store state into.
   */
  void SaveSettings(BMessage *msg);

  /**
   * @brief Restores filter and sort state from a BMessage.
   * @param msg Source message containing saved state.
   */
  void LoadSettings(const BMessage *msg);
};

#endif // BETON_LIBRARY_BROWSER_CONTROLLER_H
