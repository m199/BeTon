#ifndef BETON_METADATA_PROPERTIES_WINDOW_H
#define BETON_METADATA_PROPERTIES_WINDOW_H

#include <Entry.h>
#include <Messenger.h>
#include <Path.h>
#include <Rect.h>
#include <String.h>
#include <Window.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "MediaItem.h"

class BButton;
class BFilePanel;
class BListView;
class BStringView;
class BTabView;
class BTextControl;
class BView;
class ArtworkView;
class PropertiesArtworkView;

/**
 * @class MetadataPropertiesWindow
 * @brief Default window for viewing and editing file properties (tags) and
 * cover art.
 *
 * Supports both single-file and multi-file editing.
 * In multi-file mode, fields with mixed values are disabled or shown as mixed.
 * It provides tabs for basic tags, cover art management, and MusicBrainz
 * integration.
 */
class MetadataPropertiesWindow final : public BWindow {
  friend class PropertiesArtworkView;

public:
  /**
   * @brief Helper constructor for single file using path string.
   */
  MetadataPropertiesWindow(const BString &filePath, const BMessenger &target);

  /**
   * @brief Helper constructor for single file using BPath.
   */
  MetadataPropertiesWindow(const BPath &filePath, const BMessenger &target);

  /**
   * @brief Main constructor for single file.
   * @param frame The initial frame rect of the window.
   * @param filePath The path to the file to edit.
   * @param target The messenger to receive update notifications.
   */
  MetadataPropertiesWindow(BRect frame, const BPath &filePath,
                   const BMessenger &target);

  /**
   * @brief Helper constructor for multiple files.
   */
  MetadataPropertiesWindow(const std::vector<BPath> &filePaths,
                   const BMessenger &target);
  MetadataPropertiesWindow(const std::vector<BPath> &filePaths,
                   const std::vector<MediaItem> &preloadedItems,
                   const BMessenger &target);

  /**
   * @brief Main constructor for multiple files.
   * @param frame The initial frame rect of the window.
   * @param filePaths A list of file paths to edit simultaneously.
   * @param target The messenger to receive update notifications.
   */
  MetadataPropertiesWindow(BRect frame, const std::vector<BPath> &filePaths,
                   const BMessenger &target);

  /**
   * @brief Constructor for browsing multiple files individually (navigation
   * mode).
   * @param filePaths List of all files in the current context (e.g., playlist).
   * @param initialIndex Index of the file to start with.
   * @param target The messenger to receive update notifications.
   */
  MetadataPropertiesWindow(const std::vector<BPath> &filePaths, int32 initialIndex,
                   const BMessenger &target);

  ~MetadataPropertiesWindow() override;

  void MessageReceived(BMessage *msg) override;
  bool QuitRequested() override;

private:
  void _BuildUI();
  void _BuildTab_Tags(BView *parent);
  void _BuildTab_MB(BView *parent);
  void _ApplyColors();

  void _SendApply(bool saveToDisk);
  void _OpenCoverPanel();
  void _HandleCoverChosen(const entry_ref &ref);
  void _SendMessageToTarget(uint32 what, BMessage *payload = nullptr);
  void _LoadInitialData();
  void _LoadInitialDataMulti();
  void _UpdateHeaderFromFields();
  void _SetRating(int32 rating, bool markDirty);
  void _UpdateRatingStars();
  void _LoadFileAtIndex(int32 index);
  void _UpdateReadOnlyState();
  void _SaveWindowFrame() const;
  void _RememberInitialFieldValue(const char *name, BTextControl *field);
  bool _FieldValueChanged(const char *name, BTextControl *field) const;
  bool _FilesCanBeModified() const;
  void _ShowCoverContextMenu(BPoint screenWhere);

  enum class FieldState { AllSame, AllEmpty, Mixed };
  static FieldState _StateForStrings(const std::vector<BString> &vals,
                                     BString &outCommon);
  static FieldState _StateForInts(const std::vector<uint32> &vals,
                                  uint32 &outCommon);

private:
  BPath fFilePath;           ///< Current single file path (or first of multi)
  std::vector<BPath> fFiles; ///< List of all files being edited/browsed
  std::vector<MediaItem> fPreloadedItems; ///< Optional cache data for multi edit
  bool fIsMulti = false;     ///< True if editing multiple files at once
  int32 fCurrentIndex = 0;   ///< Index in fFiles (for navigation)

  BMessenger fTarget;

  BTabView *fTabs = nullptr;

  /** @name Cover Tab */
  ///@{
  ArtworkView *fArtworkView = nullptr;
  BStringView *fCoverStatus = nullptr;
  BFilePanel *fOpenPanel = nullptr;
  bool fCoverMixed = false;
  bool fCoverDirty = false;
  BString fCurrentCoverMime;
  std::vector<uint8_t> fCurrentCoverBytes;
  ///@}

  /** @name Header / Tags Tab */
  ///@{
  BStringView *fHdrTitle = nullptr;
  BStringView *fHdrSub1 = nullptr;
  BStringView *fHdrSub2 = nullptr;
  BStringView *fHdrRating = nullptr;
  int32 fCurrentRating = 0;
  bool fRatingDirty = false;
  bool fRatingMixed = false;
  bool fRatingReadOnly = false;

  BTextControl *fEdTitle = nullptr;
  BTextControl *fEdArtist = nullptr;
  BTextControl *fEdAlbum = nullptr;
  BTextControl *fEdAlbumArtist = nullptr;
  BTextControl *fEdComposer = nullptr;
  BTextControl *fEdGenre = nullptr;
  BTextControl *fEdYear = nullptr;
  BTextControl *fEdTrack = nullptr;
  BTextControl *fEdTrackTotal = nullptr;
  BTextControl *fEdDisc = nullptr;
  BTextControl *fEdDiscTotal = nullptr;
  BTextControl *fEdComment = nullptr;
  BTextControl *fEdMBTrackID = nullptr;
  BTextControl *fEdMBAlbumID = nullptr;
  std::map<std::string, BString> fInitialFieldValues;
  ///@}

  /** @name MusicBrainz Tab */
  ///@{
  BTextControl *fMbSearchArtist = nullptr;
  BTextControl *fMbSearchAlbum = nullptr;
  BTextControl *fMbSearchTitle = nullptr;
  BButton *fMbSearch = nullptr;
  BButton *fMbCancel = nullptr;
  BStringView *fMbStatusView = nullptr;
  BListView *fMbResults = nullptr;
  BButton *fMbApplyTrack = nullptr;
  BButton *fMbApplyAlbum = nullptr;
  ///@}

  /** @name Global Buttons */
  ///@{
  BButton *fBtnPrev = nullptr;
  BButton *fBtnNext = nullptr;
  BButton *fBtnApply = nullptr;
  BButton *fBtnSave = nullptr;
  BButton *fBtnCancel = nullptr;
  ///@}

  struct MbResultCache {
    BString recId;
    BString relId;
  };
  std::vector<MbResultCache> fMbCache;
  bool fMbAlbumResults = false;
};

#endif // BETON_METADATA_PROPERTIES_WINDOW_H
