
#ifndef BETON_MUSICBRAINZ_MATCHER_WINDOW_H
#define BETON_MUSICBRAINZ_MATCHER_WINDOW_H

#include <Messenger.h>
#include <String.h>
#include <Window.h>
#include <vector>

class BListView;
class BButton;

/**
 * @struct MusicBrainzMatchTrackInfo
 * @brief Metadata for a single track from MusicBrainz used for matching.
 */
struct MusicBrainzMatchTrackInfo {
  BString name;
  BString duration;
  int index;
};

/**
 * @class MusicBrainzMatcherWindow
 * @brief A dialog window for manual and semi-automatic file-to-track matching.
 *
 * Allows the user to reorder a list of MusicBrainz tracks to align them
 * with a list of local files. Supports "smart" weighted matching and manual
 * drag-and-drop.
 */
class MusicBrainzMatcherWindow : public BWindow {
public:
  MusicBrainzMatcherWindow(
      const std::vector<BString> &files,
      const std::vector<MusicBrainzMatchTrackInfo> &tracks,
      const std::vector<int> &initialMapping, BMessenger target);
  virtual ~MusicBrainzMatcherWindow();

  void MessageReceived(BMessage *msg) override;

private:
  void _BuildUI();
  void _Apply();
  void _SmartMatch();

  /** @name Data */
  ///@{
  std::vector<BString> fFiles;
  std::vector<MusicBrainzMatchTrackInfo> fTracks;
  std::vector<int> fInitialMapping;
  BMessenger fTarget;
  ///@}

  /** @name UI Components */
  ///@{
  BListView *fFileListView;
  BListView *fTrackListView;

  BButton *fBtnApply;
  BButton *fBtnCancel;
  BButton *fBtnMoveUp;
  BButton *fBtnMoveDown;
  ///@}
};

#endif // BETON_MUSICBRAINZ_MATCHER_WINDOW_H
