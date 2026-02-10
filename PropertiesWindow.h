#ifndef PROPERTIES_WINDOW_H
#define PROPERTIES_WINDOW_H

#include <Entry.h>
#include <Messenger.h>
#include <Path.h>
#include <Rect.h>
#include <String.h>
#include <Window.h>

#include <cstdint>
#include <vector>

class BButton;
class BFilePanel;
class BListView;
class BStringView;
class BTabView;
class BTextControl;
class BView;
class CoverView;

/**
 * @class PropertiesWindow
 * @brief Default window for viewing and editing file properties (tags) and
 * cover art.
 *
 * Supports both single-file and multi-file editing.
 * In multi-file mode, fields with mixed values are disabled or shown as mixed.
 * It provides tabs for basic tags, cover art management, and MusicBrainz
 * integration.
 */
class PropertiesWindow final : public BWindow {
public:
  /**
   * @brief Helper constructor for single file using path string.
   */
  PropertiesWindow(const BString &filePath, const BMessenger &target);

  /**
   * @brief Helper constructor for single file using BPath.
   */
  PropertiesWindow(const BPath &filePath, const BMessenger &target);

  /**
   * @brief Main constructor for single file.
   * @param frame The initial frame rect of the window.
   * @param filePath The path to the file to edit.
   * @param target The messenger to receive update notifications.
   */
  PropertiesWindow(BRect frame, const BPath &filePath,
                   const BMessenger &target);

  /**
   * @brief Helper constructor for multiple files.
   */
  PropertiesWindow(const std::vector<BPath> &filePaths,
                   const BMessenger &target);

  /**
   * @brief Main constructor for multiple files.
   * @param frame The initial frame rect of the window.
   * @param filePaths A list of file paths to edit simultaneously.
   * @param target The messenger to receive update notifications.
   */
  PropertiesWindow(BRect frame, const std::vector<BPath> &filePaths,
                   const BMessenger &target);

  /**
   * @brief Constructor for browsing multiple files individually (navigation
   * mode).
   * @param filePaths List of all files in the current context (e.g., playlist).
   * @param initialIndex Index of the file to start with.
   * @param target The messenger to receive update notifications.
   */
  PropertiesWindow(const std::vector<BPath> &filePaths, int32 initialIndex,
                   const BMessenger &target);

  ~PropertiesWindow() override;

  void MessageReceived(BMessage *msg) override;

private:
  void _BuildUI();
  void _BuildTab_Tags(BView *parent);
  void _BuildTab_Cover(BView *parent);
  void _BuildTab_MB(BView *parent);

  void _SendApply(bool saveToDisk);
  void _OpenCoverPanel();
  void _HandleCoverChosen(const entry_ref &ref);
  void _SendMessageToTarget(uint32 what, BMessage *payload = nullptr);
  void _LoadInitialData();
  void _LoadInitialDataMulti();
  void _UpdateHeaderFromFields();
  void _LoadFileAtIndex(int32 index);

  enum class FieldState { AllSame, AllEmpty, Mixed };
  static FieldState _StateForStrings(const std::vector<BString> &vals,
                                     BString &outCommon);
  static FieldState _StateForInts(const std::vector<uint32> &vals,
                                  uint32 &outCommon);

private:
  BPath fFilePath;           // Current single file path (or first of multi)
  std::vector<BPath> fFiles; // List of all files being edited/browsed
  bool fIsMulti = false;     // True if editing multiple files at once
  int32 fCurrentIndex = 0;   // Index in fFiles (for navigation)

  BMessenger fTarget;

  BTabView *fTabs = nullptr;

  /** @name Cover Tab */
  ///@{
  CoverView *fCoverView = nullptr;
  BFilePanel *fOpenPanel = nullptr;
  bool fCoverMixed = false;
  BButton *fBtnCoverLoad = nullptr;
  BButton *fBtnCoverClr = nullptr;
  BButton *fBtnCoverApplyAlbum = nullptr;
  BButton *fBtnCoverClearAlbum = nullptr;
  BButton *fBtnCoverFromMB = nullptr;
  std::vector<uint8_t> fCurrentCoverBytes;
  ///@}

  /** @name Header / Tags Tab */
  ///@{
  BStringView *fHdrTitle = nullptr;
  BStringView *fHdrSub1 = nullptr;
  BStringView *fHdrSub2 = nullptr;
  BStringView *fHdrRating = nullptr;
  int32 fCurrentRating = 0;

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
};

#endif // PROPERTIES_WINDOW_H
