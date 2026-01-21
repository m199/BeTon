#include "MainWindow.h"
#include "ContentColumnView.h"
#include "Debug.h"
#include "DirectoryManagerWindow.h"
#include "InfoPanel.h"
#include "MatcherWindow.h"
#include "MatchingUtils.h"
#include "NamePrompt.h"
#include "PlaylistGeneratorWindow.h"
#include "PlaylistListView.h"
#include "PlaylistManager.h"
#include "PlaylistUtils.h"
#include "PropertiesWindow.h"
#include "SeekBarView.h"
#include "TagSync.h"

#include <AboutWindow.h>
#include <Button.h>
#include <ColumnTypes.h>
#include <DataIO.h>
#include <Directory.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <OS.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Roster.h>
#include <ScrollView.h>
#include <Slider.h>
#include <StatusBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TranslationUtils.h>
#include <View.h>
#include <algorithm>
#include <cinttypes>
#include <random>
#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tag.h>
#include <vector>

#include <Catalog.h>

#include <Application.h>
#include <IconUtils.h>
#include <Resources.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"

/** @name Player Button Icon Resource IDs */
///@{
static constexpr int32 ICON_PLAY_GRAY = 2001;
static constexpr int32 ICON_PAUSE_GRAY = 2003;
static constexpr int32 ICON_PREV = 2005;
static constexpr int32 ICON_NEXT = 2006;
static constexpr int32 ICON_SHUFFLE_GRAY = 2007;
static constexpr int32 ICON_SHUFFLE_COLOR = 2008;
static constexpr int32 ICON_STOP = 2009;
static constexpr int32 ICON_REPEAT_GRAY = 2010;
static constexpr int32 ICON_REPEAT_GREEN = 2011;
static constexpr int32 ICON_REPEAT_ORANGE = 2012;
///@}

/**
 * @brief Loads a vector icon from application resources and renders it to a
 * bitmap.
 * @param id The resource ID of the icon.
 * @param size The desired size in pixels.
 * @return A new BBitmap containing the rendered icon, or nullptr on failure.
 */
static BBitmap *LoadIconFromResource(int32 id, float size) {
  if (!be_app || !be_app->AppResources())
    return nullptr;

  size_t len = 0;
  const void *data =
      be_app->AppResources()->LoadResource(B_VECTOR_ICON_TYPE, id, &len);
  if (!data || len == 0) {
    fprintf(stderr, "[MainWindow] Icon-ID %ld nicht gefunden\n", (long)id);
    return nullptr;
  }

  BRect r(0, 0, size - 1, size - 1);
  auto *bmp = new BBitmap(r, 0, B_RGBA32);
  if (BIconUtils::GetVectorIcon(static_cast<const uint8 *>(data), len, bmp) !=
      B_OK) {
    delete bmp;
    fprintf(stderr, "[MainWindow] Icon-ID %ld: Dekodierung fehlgeschlagen\n",
            (long)id);
    return nullptr;
  }
  return bmp;
}

MainWindow *gMainWindow = nullptr;

extern void AddItemToPlaylist(const BString &playlist, const BString &path);

static void CollectPathsFromMessage(const BMessage *msg,
                                    std::vector<BPath> &out) {
  out.clear();
  if (!msg)
    return;

  BString s;
  int32 i = 0;
  while (msg->FindString("file", i++, &s) == B_OK)
    if (!s.IsEmpty())
      out.emplace_back(s.String());
  if (!out.empty())
    return;

  entry_ref ref;
  int32 r = 0;
  while (msg->FindRef("refs", r++, &ref) == B_OK) {
    BEntry e(&ref, true);
    if (e.InitCheck() == B_OK && e.Exists()) {
      BPath p;
      if (e.GetPath(&p) == B_OK)
        out.push_back(p);
    }
  }
}

/**
 * @brief Constructs the Main Window of the application.
 *
 * Initializes the UI, managers (Playlist, Library, Cache), and playback
 * controller. Starts the initial cache load and status updates.
 */
MainWindow::MainWindow()
    : BWindow(BRect(100, 100, 400, 300), "BeTon", B_DOCUMENT_WINDOW,
              B_QUIT_ON_WINDOW_CLOSE),
      fNewFilesCount(0), fSongDuration(0), fPropertiesWindow(nullptr),
      fController(nullptr), fUpdateRunner(nullptr) {

  fController = new MediaPlaybackController();
  fController->SetTarget(BMessenger(this));

  fController->SetVolume(1.0f);

  fPlaylistManager = new PlaylistManager(BMessenger(this));

  fCacheManager = new CacheManager(BMessenger(this));
  fCacheManager->Run();

  fLibraryManager = new LibraryViewManager(BMessenger(this));
  fMetadataHandler = new MetadataHandler(BMessenger(fCacheManager));

  fInfoPanel = new InfoPanel();
  fStatusLabel = new BStringView("status", B_TRANSLATE("Loading..."));

  fSeekBarColor = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
  fSelectionColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

  _BuildUI();

  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;
  float windowWidth = fontHeight * 70;       // ~1008px at default font
  float windowHeight = windowWidth / 1.618f; // Golden ratio
  ResizeTo(windowWidth, windowHeight);
  CenterOnScreen();
  fPlaylistManager->LoadAvailablePlaylists();

  BMessenger(fCacheManager).SendMessage(MSG_LOAD_CACHE);

  fMbClient = new MusicBrainzClient("beton-app@outlook.com");

  fStatusLabel->SetText(B_TRANSLATE("Loading Music Library..."));

  fPendingItems = fAllItems;
  fCurrentIndex = 0;

  fBatchRunner = new BMessageRunner(BMessenger(this),
                                    new BMessage(MSG_BATCH_TIMER), 50000);

  RegisterWithCacheManager();

  BMessage msg(MSG_INIT_LIBRARY);
  PostMessage(&msg);

  LoadSettings();
}

/**
 * @brief Destructor.
 *
 * Cleans up all allocated managers, runners, and the playback controller.
 * Saves current settings before exit.
 */
MainWindow::~MainWindow() {
  SaveSettings();
  if (fController) {
    fController->Shutdown();
    delete fController;
    fController = nullptr;
  }
  if (fCacheManager) {
    fCacheManager->Lock();
    fCacheManager->Quit();
    fCacheManager = nullptr;
  }
  delete fUpdateRunner;
  delete fBatchRunner;
  delete fLibraryManager;
  delete fPlaylistManager;
  delete fMetadataHandler;
  delete fMbClient;
  delete fSearchRunner;

  delete fIconPlay;
  delete fIconPause;
  delete fIconStop;
  delete fIconNext;
  delete fIconPrev;
  delete fIconShuffleOff;
  delete fIconShuffleOn;
  delete fIconRepeatOff;
  delete fIconRepeatAll;
  delete fIconRepeatOne;
}

/**
 * @brief Builds the User Interface.
 *
 * Creates the menu bar, toolbar buttons, status bar, and the main split view
 * containing the sidebar (playlists/info) and the library browser.
 */
void MainWindow::_BuildUI() {
  const float kItemSpacing = 3.0f;
  const float kGroupSpacing = 8.0f;

  fMenuBar = new BMenuBar("menuBar");
  fMenuBar->SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));

  BMenu *fileMenu = new BMenu(B_TRANSLATE("File"));
  fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Manage Music Folders"),
                                  new BMessage(MSG_MANAGE_DIRECTORIES)));
  fileMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Rescan"), new BMessage(MSG_RESCAN_FULL)));
  fileMenu->AddSeparatorItem();
  fileMenu->AddItem(
      new BMenuItem(B_TRANSLATE("Quit"), new BMessage(B_QUIT_REQUESTED), 'q'));
  fMenuBar->AddItem(fileMenu);

  BMenu *playlistMenu = new BMenu(B_TRANSLATE("Playlists"));
  playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("New Playlist"),
                                      new BMessage(MSG_NEW_PLAYLIST)));
  playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Generate New Playlist"),
                                      new BMessage(MSG_NEW_SMART_PLAYLIST)));
  playlistMenu->AddSeparatorItem();
  playlistMenu->AddItem(new BMenuItem(B_TRANSLATE("Set Playlist Folder"),
                                      new BMessage(MSG_SET_PLAYLIST_FOLDER)));
  fMenuBar->AddItem(playlistMenu);

  BMenu *appearanceMenu = new BMenu(B_TRANSLATE("Appearance"));

  BMenu *artworkMenu = new BMenu(B_TRANSLATE("Artwork"));
  fViewCoverItem =
      new BMenuItem(B_TRANSLATE("On"), new BMessage(MSG_ARTWORK_ON));
  fViewInfoItem =
      new BMenuItem(B_TRANSLATE("Off"), new BMessage(MSG_ARTWORK_OFF));
  fShowCoverArt = true;
  fViewCoverItem->SetMarked(true);
  fViewInfoItem->SetMarked(false);
  artworkMenu->AddItem(fViewCoverItem);
  artworkMenu->AddItem(fViewInfoItem);
  appearanceMenu->AddItem(artworkMenu);

  BMenu *selColorMenu = new BMenu(B_TRANSLATE("Selection Color"));
  fSelColorSystemItem = new BMenuItem(B_TRANSLATE("System Default"),
                                      new BMessage(MSG_SELECTION_COLOR_SYSTEM));
  fSelColorMatchItem = new BMenuItem(B_TRANSLATE("Match SeekBar"),
                                     new BMessage(MSG_SELECTION_COLOR_MATCH));
  fSelColorSystemItem->SetMarked(!fUseSeekBarColorForSelection);
  fSelColorMatchItem->SetMarked(fUseSeekBarColorForSelection);
  selColorMenu->AddItem(fSelColorSystemItem);
  selColorMenu->AddItem(fSelColorMatchItem);
  appearanceMenu->AddItem(selColorMenu);

  fMenuBar->AddItem(appearanceMenu);

  BMenu *helpMenu = new BMenu(B_TRANSLATE("Help"));
  helpMenu->AddItem(new BMenuItem(B_TRANSLATE("About BeTon" B_UTF8_ELLIPSIS),
                                  new BMessage(B_ABOUT_REQUESTED)));
  fMenuBar->AddItem(helpMenu);

  fSeekBar = new SeekBarView("seekbar");

  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;
  float barHeight = std::max(17.0f, fontHeight * 1.3f);

  fVisualBar = new BStatusBar("visual");
  fVisualBar->SetExplicitMinSize(BSize(fontHeight * 20, barHeight));
  fVisualBar->SetExplicitMaxSize(BSize(fontHeight * 20, barHeight));
  fVisualBar->SetBarColor(make_color(100, 180, 255));

  fTitleView = new BStringView("titleView", B_TRANSLATE("No Title"));
  fTitleView->SetExplicitMaxSize(BSize(fontHeight * 55, barHeight));

  fBtnPrev = new BButton("", new BMessage(MSG_PREV_BTN));
  fBtnPlayPause = new BButton("", new BMessage(MSG_PLAYPAUSE));
  fBtnStop = new BButton("", new BMessage(MSG_STOP));
  fBtnNext = new BButton("", new BMessage(MSG_PLAY_NEXT));
  fBtnShuffle = new BButton("", new BMessage(MSG_SHUFFLE_TOGGLE));
  fBtnRepeat = new BButton("", new BMessage(MSG_REPEAT_TOGGLE));

  float size = fontHeight * 1.8f;
  if (size < 24.0f)
    size = 24.0f;
  BSize buttonSize(size, size);

  float iconSize = size * 0.65f; // Icon is 65% of button size
  fIconPlay = LoadIconFromResource(ICON_PLAY_GRAY, iconSize);
  fIconPause = LoadIconFromResource(ICON_PAUSE_GRAY, iconSize);
  fIconStop = LoadIconFromResource(ICON_STOP, iconSize);
  fIconNext = LoadIconFromResource(ICON_NEXT, iconSize);
  fIconPrev = LoadIconFromResource(ICON_PREV, iconSize);
  fIconShuffleOff = LoadIconFromResource(ICON_SHUFFLE_GRAY, iconSize);
  fIconShuffleOn = LoadIconFromResource(ICON_SHUFFLE_COLOR, iconSize);
  fIconRepeatOff = LoadIconFromResource(ICON_REPEAT_GRAY, iconSize);
  fIconRepeatAll = LoadIconFromResource(ICON_REPEAT_GREEN, iconSize);
  fIconRepeatOne = LoadIconFromResource(ICON_REPEAT_ORANGE, iconSize);

  if (fIconPrev)
    fBtnPrev->SetIcon(fIconPrev, 0);
  if (fIconPlay)
    fBtnPlayPause->SetIcon(fIconPlay, 0);
  if (fIconStop)
    fBtnStop->SetIcon(fIconStop, 0);
  if (fIconNext)
    fBtnNext->SetIcon(fIconNext, 0);
  if (fIconShuffleOff)
    fBtnShuffle->SetIcon(fIconShuffleOff, 0);
  if (fIconRepeatOff)
    fBtnRepeat->SetIcon(fIconRepeatOff, 0);

  fBtnPrev->SetExplicitSize(buttonSize);
  fBtnPlayPause->SetExplicitSize(buttonSize);
  fBtnShuffle->SetExplicitSize(buttonSize);
  fBtnRepeat->SetExplicitSize(buttonSize);
  fBtnStop->SetExplicitSize(buttonSize);
  fBtnNext->SetExplicitSize(buttonSize);

  fVolumeSlider = new BSlider("volume", nullptr, nullptr, 0, 100, B_HORIZONTAL);
  fVolumeSlider->SetModificationMessage(new BMessage(MSG_VOLUME_CHANGED));
  fVolumeSlider->SetValue(100);
  fVolumeSlider->SetExplicitMinSize(BSize(fontHeight * 6, B_SIZE_UNSET));
  fVolumeSlider->SetExplicitMaxSize(BSize(fontHeight * 8, B_SIZE_UNSET));

  fSearchField =
      new BTextControl("search", "", "", new BMessage(MSG_SEARCH_MODIFY));
  fSearchField->SetModificationMessage(new BMessage(MSG_SEARCH_MODIFY));
  fSearchField->SetTarget(this);

  BScrollView *playlistScroll = new BScrollView(
      "playlist_scroll", fPlaylistManager->View(), B_WILL_DRAW, false, true);

  BScrollView *genreScroll = new BScrollView(
      "genre_scroll", fLibraryManager->GenreView(), B_WILL_DRAW, false, true);
  BScrollView *artistScroll = new BScrollView(
      "artist_scroll", fLibraryManager->ArtistView(), B_WILL_DRAW, false, true);
  BScrollView *albumScroll = new BScrollView(
      "album_scroll", fLibraryManager->AlbumView(), B_WILL_DRAW, false, true);
  BScrollView *contentScroll =
      new BScrollView("content_scroll", fLibraryManager->ContentView(),
                      B_WILL_DRAW, false, false);
  contentScroll->SetBorder(B_NO_BORDER);

  BGroupView *sidebarGroup = new BGroupView(B_VERTICAL, 0);
  sidebarGroup->SetExplicitMinSize(BSize(fontHeight * 14, B_SIZE_UNSET));
  sidebarGroup->SetExplicitMaxSize(BSize(fontHeight * 14, B_SIZE_UNSET));

  BLayoutBuilder::Group<>(sidebarGroup)
      .Add(playlistScroll, 1.0f)
      .AddStrut(kItemSpacing)
      .Add(fInfoPanel, 0.0f);

  BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
      .Add(fMenuBar)
      .AddGroup(B_VERTICAL, kGroupSpacing)
      .SetInsets(kGroupSpacing)

      .AddGroup(B_HORIZONTAL, kItemSpacing)
      .Add(fSeekBar)
      .Add(new BView("spacer", B_WILL_DRAW), 0.0f)
      .Add(fTitleView)
      .End()

      .AddGroup(B_HORIZONTAL, kItemSpacing)
      .Add(fBtnPrev)
      .Add(fBtnPlayPause)
      .Add(fBtnStop)
      .Add(fBtnNext)
      .Add(fBtnShuffle)
      .Add(fBtnRepeat)
      .AddStrut(kItemSpacing)
      .Add(fVolumeSlider)
      .AddGlue()
      .Add(fSearchField)
      .End()

      .AddSplit(B_HORIZONTAL, kGroupSpacing)

      .Add(sidebarGroup, 0.25f)

      .AddGroup(B_VERTICAL, kItemSpacing, 0.75f)

      .AddGroup(B_HORIZONTAL, kItemSpacing)
      .Add(genreScroll, 1.0f)
      .Add(artistScroll, 1.0f)
      .Add(albumScroll, 1.0f)
      .End()

      // .Add(contentScroll, 2.0f)
      .Add(contentScroll, 2.0f)
      .End()
      .End()

      .AddGroup(B_HORIZONTAL, 0)
      .Add(fStatusLabel)
      .AddGlue()
      .End()
      .End();
}

void UpdateScrollbars(BListView *listView) {
  if (!listView || listView->CountItems() == 0)
    return;

  float itemHeight = listView->ItemAt(0)->Height();
  float totalHeight = listView->CountItems() * itemHeight;

  BScrollBar *vBar = listView->ScrollBar(B_VERTICAL);
  if (vBar) {
    float max = std::max(0.0f, totalHeight - listView->Bounds().Height());
    vBar->SetRange(0, max);
    vBar->SetProportion(listView->Bounds().Height() / totalHeight);
  }

  BScrollBar *hBar = listView->ScrollBar(B_HORIZONTAL);
  if (hBar) {
    hBar->SetRange(0, 0);
  }
}

/**
 * @brief Main message loop handler.
 *
 * Handles all application messages, including:
 * - Playback control (Play, Pause, stop, Next, Prev)
 * - Library updates & scanning
 * - Playlist management
 * - Metadata updates & MusicBrainz integration
 * - UI selection changes
 *
 * @param msg The received message.
 */
void MainWindow::MessageReceived(BMessage *msg) {
  /*   DEBUG_PRINT("Message empfangen: what = '%c%c%c%c'\\n",
                 (msg->what >> 24) & 0xff, (msg->what >> 16) & 0xff,
                 (msg->what >> 8) & 0xff, msg->what & 0xff);*/

  switch (msg->what) {

  case B_ABOUT_REQUESTED: {
    BAboutWindow* about =
        new BAboutWindow("BeTon", "application/x-vnd.BeTon");
    about->AddCopyright(2025, "Daniel Weber");
    about->AddDescription(
                   "A music library manager and player for Haiku.\n\n"
                   "Solid grey and cold\nYet it vibrates with the "
                   "sound\nConcrete sings today\n\n"
                   "Icons by zuMi\n"
                   "https://hvif-store.art/\n\n"
                   "Licensed under the MIT License."
                   );
    about->Show();
    break;
  }

  case MSG_TEST_MODE: {
    std::vector<BString> files = {"File1.mp3", "File2.mp3", "File3.mp3",
                                  "File4.mp3"};
    std::vector<MatcherTrackInfo> tracks;
    MatcherTrackInfo t1;
    t1.index = 1;
    t1.name = "Test Track 1";
    t1.duration = "3:30";
    tracks.push_back(t1);

    MatcherTrackInfo t2;
    t2.index = 2;
    t2.name = "Test Track 2";
    t2.duration = "4:45";
    tracks.push_back(t2);

    MatcherTrackInfo t3;
    t3.index = 3;
    t3.name = "Test Track 3";
    t3.duration = "2:20";
    tracks.push_back(t3);

    std::vector<int> map(files.size(), -1);

    new MatcherWindow(files, tracks, map, BMessenger(this));

    break;
  }

  case MSG_ARTWORK_ON:
    fShowCoverArt = true;
    if (fViewCoverItem)
      fViewCoverItem->SetMarked(true);
    if (fViewInfoItem)
      fViewInfoItem->SetMarked(false);

    {
      BMessage selMsg(MSG_SELECTION_CHANGED_CONTENT);
      PostMessage(&selMsg);
    }
    break;

  case MSG_ARTWORK_OFF:
    fShowCoverArt = false;
    if (fViewCoverItem)
      fViewCoverItem->SetMarked(false);
    if (fViewInfoItem)
      fViewInfoItem->SetMarked(true);

    if (fInfoPanel)
      fInfoPanel->Switch(InfoPanel::Info);
    break;

  case MSG_VIEW_INFO:

    if (fInfoPanel)
      fInfoPanel->Switch(InfoPanel::Info);
    break;

  case MSG_VIEW_COVER:
    if (fInfoPanel && fShowCoverArt)
      fInfoPanel->Switch(InfoPanel::Cover);
    break;

  case MSG_PLAY: {
    ContentColumnView *cv = fLibraryManager->ContentView();
    BRow *selRow = cv->CurrentSelection();
    int32 index = (selRow ? cv->IndexOf(selRow) : -1);

    if (index >= 0) {
      std::vector<std::string> queue;
      queue.reserve(cv->CountRows());

      for (int32 i = 0; i < cv->CountRows(); ++i) {
        const MediaItem *mi = cv->ItemAt(i);
        if (!mi)
          continue;

        if (mi->missing)
          continue;

        queue.push_back(mi->path.String());
      }

      if (!queue.empty()) {
        DEBUG_PRINT("[Window] MSG_PLAY: start index=%ld (queue=%zu)\\n",
                    (long)index, queue.size());
        fController->Stop();
        fController->SetQueue(queue);
        fController->Play(index);
        fSongDuration = fController->Duration();
        if (fIconPause)
          fBtnPlayPause->SetIcon(fIconPause, 0);
      }
    } else {
      DEBUG_PRINT("[Window] MSG_PLAY: no selection\\n");
    }
    break;
  }

  case MSG_COVER_APPLY_ALBUM: {
    const void *data = nullptr;
    ssize_t size = 0;
    BString filePath;
    if (msg->FindString("file", &filePath) == B_OK &&
        msg->FindData("bytes", B_RAW_TYPE, &data, &size) == B_OK && size > 0) {
      fMetadataHandler->ApplyAlbumCover(filePath, data, size);
      UpdateFileInfo();
    }
    break;
  }

  case MSG_COVER_CLEAR_ALBUM: {
    BString filePath;
    if (msg->FindString("file", &filePath) == B_OK) {
      fMetadataHandler->ClearAlbumCover(filePath);
      UpdateFileInfo();
    }
    break;
  }

  case MSG_COVER_DROPPED_APPLY_ALL: {
    fMetadataHandler->ApplyCoverToAll(msg);
    break;
  }

  case MSG_SEEKBAR_COLOR_DROPPED: {
    rgb_color *color;
    ssize_t size;
    if (msg->FindData("color", B_RGB_COLOR_TYPE, (const void **)&color,
                      &size) == B_OK &&
        size == sizeof(rgb_color)) {
      fSeekBarColor = *color;
      fUseCustomSeekBarColor = true;
      ApplyColors();
      SaveSettings();
    }
    break;
  }

  case MSG_SELECTION_COLOR_SYSTEM: {
    fUseSeekBarColorForSelection = false;
    fUseCustomSeekBarColor = false; // Also reset SeekBar to default
    if (fSelColorSystemItem)
      fSelColorSystemItem->SetMarked(true);
    if (fSelColorMatchItem)
      fSelColorMatchItem->SetMarked(false);
    ApplyColors();
    SaveSettings();
    break;
  }

  case MSG_SELECTION_COLOR_MATCH: {
    fUseSeekBarColorForSelection = true;
    if (fSelColorSystemItem)
      fSelColorSystemItem->SetMarked(false);
    if (fSelColorMatchItem)
      fSelColorMatchItem->SetMarked(true);
    ApplyColors();
    SaveSettings();
    break;
  }

  case B_COLORS_UPDATED: {
    if (!fUseCustomSeekBarColor) {
      fSeekBarColor = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
    }
    if (!fUseSeekBarColorForSelection) {
      fSelectionColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
    }
    ApplyColors();
    break;
  }

  case MSG_PROP_APPLY:
  case MSG_PROP_SAVE: {
    BString tmp;
    if (msg->FindString("mbAlbumID", &tmp) == B_OK)
      DEBUG_PRINT("[MainWindow] PROP_SAVE: mbAlbumID='%s'\\n", tmp.String());
    if (msg->FindString("mbTrackID", &tmp) == B_OK)
      DEBUG_PRINT("[MainWindow] PROP_SAVE: mbTrackID='%s'\\n", tmp.String());
    if (msg->FindString("disc", &tmp) == B_OK)
      DEBUG_PRINT("[MainWindow] PROP_SAVE: disc='%s'\\n", tmp.String());

    fMetadataHandler->SaveTags(msg);
    break;
  }

  case MSG_PROP_REQUEST_COVER: {
    BString file;
    if (msg->FindString("file", &file) == B_OK && !file.IsEmpty()) {

      CoverBlob cover;
      if (TagSync::ExtractEmbeddedCover(BPath(file.String()), cover)) {
        BMessage reply(MSG_PROP_SET_COVER_DATA);
        reply.AddData("bytes", B_RAW_TYPE, cover.data(), (ssize_t)cover.size());

        BMessenger sender = msg->ReturnAddress();
        sender.SendMessage(&reply);
      }
    }
    break;
  }

  case MSG_PLAYPAUSE: {
    if (fController) {
      if (fController->IsPlaying()) {
        fController->Pause();
        if (fIconPlay)
          fBtnPlayPause->SetIcon(fIconPlay, 0);
      } else if (fController->IsPaused()) {
        fController->Resume();
        if (fIconPause)
          fBtnPlayPause->SetIcon(fIconPause, 0);

      } else {
        ContentColumnView *cv = fLibraryManager->ContentView();
        BRow *selRow = cv->CurrentSelection();
        int32 index = (selRow ? cv->IndexOf(selRow) : -1);

        if (index >= 0) {
          std::vector<std::string> queue;
          queue.reserve(cv->CountRows());

          for (int32 i = 0; i < cv->CountRows(); ++i) {
            const MediaItem *mi = cv->ItemAt(i);
            if (!mi || mi->missing)
              continue;

            queue.push_back(mi->path.String());
          }

          if (!queue.empty()) {
            DEBUG_PRINT("[Window] MSG_PLAYPAUSE: start index=%ld"
                        " (queue=%zu)\\n",
                        (long)index, queue.size());
            fController->Stop();
            fController->SetQueue(queue);
            fController->Play(index);
            fSongDuration = fController->Duration();
            if (fIconPause)
              fBtnPlayPause->SetIcon(fIconPause, 0);
          }
        } else {
          DEBUG_PRINT("[Window] MSG_PLAYPAUSE: no selection\\n");
        }
      }
    }
    break;
  }

  case MSG_VOLUME_CHANGED: {
    if (fController && fVolumeSlider) {
      float linear = fVolumeSlider->Value() / 100.0f;
      float vol = linear * linear;
      DEBUG_PRINT("[MainWindow] Volume slider: %ld"
                  " -> linear %.2f -> exp %.2f\n",
                  (long)fVolumeSlider->Value(), linear, vol);
      fController->SetVolume(vol);
    }
    break;
  }

  case MSG_CACHE_LOADED: {
    DEBUG_PRINT("[MainWindow] MSG_CACHE_LOADED received\\n");
    fCacheLoaded = true;
    if (fCacheManager) {
      auto entries = fCacheManager->AllEntries();
      fAllItems = std::move(entries);

      fKnownPaths.clear();
      for (const auto &item : fAllItems) {
        fKnownPaths.insert(item.path);
      }

      DEBUG_PRINT("[MainWindow] Cache populated: %zu items\\n",
                  fAllItems.size());

      UpdateFilteredViews();
      _UpdateStatusLibrary();
    }
    break;
  }

  case MSG_DELETE_ITEM: {

    std::vector<BString> removedPaths;

    BRow *row = nullptr;
    ContentColumnView *cv = fLibraryManager->ContentView();
    while ((row = cv->CurrentSelection(row)) != nullptr) {
      const MediaItem *mi = cv->ItemAt(cv->IndexOf(row));
      if (mi) {
        removedPaths.push_back(mi->path);
      }
    }

    if (!removedPaths.empty() && fCurrentPlaylistName.Length() > 0) {

      for (const auto &path : removedPaths) {
        for (int32 i = 0; i < cv->CountRows(); i++) {
          const MediaItem *mi = cv->ItemAt(i);
          if (mi && mi->path == path) {
            BRow *r = cv->RowAt(i);
            cv->RemoveRow(r);
            delete r;
            break;
          }
        }
      }

      std::vector<BString> remainingPaths;
      for (int32 i = 0; i < cv->CountRows(); i++) {
        const MediaItem *mi = cv->ItemAt(i);
        if (mi) {
          remainingPaths.push_back(mi->path);
        }
      }
      fPlaylistManager->SavePlaylist(fCurrentPlaylistName, remainingPaths);
    }
    break;
  }

  case MSG_RESCAN_FULL: {
    DEBUG_PRINT("[MainWindow] Rescan triggered\\n");

    fLibraryManager->ContentView()->Clear();
    fLibraryManager->GenreView()->Clear();
    fLibraryManager->ArtistView()->Clear();
    fLibraryManager->AlbumView()->Clear();
    fAllItems.clear();

    if (fCacheManager) {
      BMessenger(fCacheManager).SendMessage(MSG_RESCAN);
    }

    fStatusLabel->SetText(B_TRANSLATE("Rescan started..."));
    break;
  }

  case MSG_SCAN_PROGRESS: {
    int32 dirs = 0;
    int32 files = 0;
    int64 elapsedSec = 0;
    if (msg->FindInt32("dirs", &dirs) == B_OK &&
        msg->FindInt32("files", &files) == B_OK) {

      msg->FindInt64("elapsed_sec", &elapsedSec);

      BString status;
      if (elapsedSec > 0) {
        int32 min = elapsedSec / 60;
        int32 sec = elapsedSec % 60;
        status.SetToFormat(B_TRANSLATE("Scanning: %ld folders, %ld"
                                       " files (%02d:%02d)"),
                           (long)dirs, (long)files, (int)min, (int)sec);
      } else {
        status.SetToFormat(B_TRANSLATE("Scanning: %ld folders, %ld files"),
                           (long)dirs, (long)files);
      }
      fStatusLabel->SetText(status.String());
    }
    break;
  }

  case MSG_SCAN_DONE: {
    DEBUG_PRINT("[MainWindow] MSG_SCAN_DONE received\\n");

    BString status;
    int64 elapsedSec = 0;
    msg->FindInt64("elapsed_sec", &elapsedSec);

    int32 min = elapsedSec / 60;
    int32 sec = elapsedSec % 60;

    status.SetToFormat(
        B_TRANSLATE("Scan completed in %02d:%02d, %ld new files"), (int)min,
        (int)sec, (long)fNewFilesCount);
    UpdateStatus(status.String(), false);

    if (fCacheManager) {

      auto entries = fCacheManager->AllEntries();
      fAllItems = std::move(entries);

      fKnownPaths.clear();
      for (const auto &item : fAllItems) {
        fKnownPaths.insert(item.path);
      }
    }

    UpdateFilteredViews();

    fNewFilesCount = 0;
    break;
  }

  case MSG_BATCH_TIMER: {
    if (fCurrentIndex >= (int32)fPendingItems.size()) {
      if (fBatchRunner) {
        delete fBatchRunner;
        fBatchRunner = nullptr;
      }
      DEBUG_PRINT("[MainWindow] Cache load finished (%zu items)\\n",
                  fPendingItems.size());
      _UpdateStatusLibrary();
      break;
    }

    const int BATCH_SIZE = 200;
    int count = 0;

    while (fCurrentIndex < (int32)fPendingItems.size() && count < BATCH_SIZE) {
      fLibraryManager->ContentView()->AddEntry(fPendingItems[fCurrentIndex]);
      fCurrentIndex++;
      count++;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), B_TRANSLATE("Loading cache... %ld/%zu"),
             (long)fCurrentIndex, fPendingItems.size());
    fStatusLabel->SetText(buf);
    break;
  }

  case MSG_SHUFFLE_TOGGLE: {
    fShuffleEnabled = !fShuffleEnabled;
    if (fShuffleEnabled && fIconShuffleOn) {
      fBtnShuffle->SetIcon(fIconShuffleOn, 0);
    } else if (!fShuffleEnabled && fIconShuffleOff) {
      fBtnShuffle->SetIcon(fIconShuffleOff, 0);
    }
    break;
  }

  case MSG_REPEAT_TOGGLE: {
    if (fRepeatMode == RepeatOff) {
      fRepeatMode = RepeatAll;
      if (fIconRepeatAll)
        fBtnRepeat->SetIcon(fIconRepeatAll, 0);
    } else if (fRepeatMode == RepeatAll) {
      fRepeatMode = RepeatOne;
      if (fIconRepeatOne)
        fBtnRepeat->SetIcon(fIconRepeatOne, 0);
    } else {
      fRepeatMode = RepeatOff;
      if (fIconRepeatOff)
        fBtnRepeat->SetIcon(fIconRepeatOff, 0);
    }
    break;
  }

  case MSG_MOVE_UP:
  case MSG_MOVE_DOWN: {
    int32 index = -1;
    if (msg->FindInt32("index", &index) != B_OK || index < 0)
      break;

    int32 playlistIdx = fPlaylistManager->View()->CurrentSelection();
    if (playlistIdx <= 0)
      break;

    BString playlistName = fPlaylistManager->View()->ItemAt(playlistIdx);
    if (playlistName.IsEmpty())
      break;

    int32 newIndex = (msg->what == MSG_MOVE_UP) ? index - 1 : index + 1;
    ContentColumnView *cv = fLibraryManager->ContentView();
    if (newIndex < 0 || newIndex >= cv->CountRows())
      break;

    fPlaylistManager->ReorderPlaylistItem(playlistName, index, newIndex);

    std::vector<MediaItem> items;
    items.reserve(cv->CountRows());
    for (int32 i = 0; i < cv->CountRows(); ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi)
        items.push_back(*mi);
    }

    if (index >= 0 && index < (int32)items.size() && newIndex >= 0 &&
        newIndex < (int32)items.size()) {
      MediaItem temp = items[index];
      items.erase(items.begin() + index);
      items.insert(items.begin() + newIndex, temp);
    }

    cv->ClearEntries();
    for (const auto &mi : items) {
      cv->AddEntry(mi);
    }

    if (BRow *row = cv->RowAt(newIndex)) {
      cv->DeselectAll();
      cv->AddToSelection(row);
      cv->ScrollTo(row);
    }
    break;
  }

  case MSG_REORDER_PLAYLIST: {
    int32 fromIndex = -1, toIndex = -1;
    if (msg->FindInt32("from_index", &fromIndex) != B_OK ||
        msg->FindInt32("to_index", &toIndex) != B_OK)
      break;

    if (fromIndex == toIndex || fromIndex < 0 || toIndex < 0)
      break;

    // Get current playlist name from sidebar selection
    int32 playlistIdx = fPlaylistManager->View()->CurrentSelection();
    if (playlistIdx <= 0)
      break;

    BString playlistName = fPlaylistManager->View()->ItemAt(playlistIdx);
    if (playlistName.IsEmpty())
      break;

    ContentColumnView *cv = fLibraryManager->ContentView();
    if (toIndex >= cv->CountRows())
      toIndex = cv->CountRows() - 1;

    fPlaylistManager->ReorderPlaylistItem(playlistName, fromIndex, toIndex);

    std::vector<MediaItem> items;
    items.reserve(cv->CountRows());
    for (int32 i = 0; i < cv->CountRows(); ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi)
        items.push_back(*mi);
    }

    if (fromIndex < (int32)items.size() && toIndex < (int32)items.size()) {
      MediaItem temp = items[fromIndex];
      items.erase(items.begin() + fromIndex);
      items.insert(items.begin() + toIndex, temp);
    }

    cv->DeselectAll();

    cv->ClearEntries();
    for (const auto &mi : items) {
      cv->AddEntry(mi);
    }

    cv->Invalidate();
    if (BView *scrollView = cv->ScrollView()) {
      scrollView->Invalidate();
    }

    BRow *newRow = cv->RowAt(toIndex);
    if (newRow) {
      cv->SetFocusRow(newRow);
      cv->AddToSelection(newRow);
      cv->ScrollTo(newRow);
    }

    cv->Sync();
    UpdateIfNeeded();
    break;
  }

  case B_SIMPLE_DATA: {
    printf("[MainWindow] B_SIMPLE_DATA received!\\n");
    fflush(stdout);

    int32 sourceIndex = -1;
    if (msg->FindInt32("source_index", &sourceIndex) != B_OK) {
      printf("[MainWindow] No source_index\\n");
      fflush(stdout);
      break; // Not an internal drag
    }
    printf("[MainWindow] source_index=%ld\\n", (long)sourceIndex);
    fflush(stdout);

    int32 playlistIdx = fPlaylistManager->View()->CurrentSelection();
    if (playlistIdx <= 0)
      break;

    BString playlistName = fPlaylistManager->View()->ItemAt(playlistIdx);
    if (playlistName.IsEmpty())
      break;

    ContentColumnView *cv = fLibraryManager->ContentView();
    BPoint dropPoint;
    if (msg->FindPoint("_drop_point_", &dropPoint) == B_OK ||
        msg->FindPoint("be:view_where", &dropPoint) == B_OK) {
      cv->ConvertFromScreen(&dropPoint);
    } else {
      cv->GetMouse(&dropPoint, nullptr);
    }

    BRow *targetRow = cv->RowAt(dropPoint);
    int32 targetIndex =
        targetRow ? cv->IndexOf(targetRow) : cv->CountRows() - 1;

    if (sourceIndex == targetIndex || sourceIndex < 0 || targetIndex < 0)
      break;

    fPlaylistManager->ReorderPlaylistItem(playlistName, sourceIndex,
                                          targetIndex);

    std::vector<MediaItem> items;
    items.reserve(cv->CountRows());
    for (int32 i = 0; i < cv->CountRows(); ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi)
        items.push_back(*mi);
    }

    if (sourceIndex < (int32)items.size() &&
        targetIndex < (int32)items.size()) {
      MediaItem temp = items[sourceIndex];
      items.erase(items.begin() + sourceIndex);
      items.insert(items.begin() + targetIndex, temp);
    }

    cv->ClearEntries();
    for (const auto &mi : items) {
      cv->AddEntry(mi);
    }

    if (BRow *row = cv->RowAt(targetIndex)) {
      cv->DeselectAll();
      cv->AddToSelection(row);
      cv->ScrollTo(row);
    }
    break;
  }

  case MSG_PLAY_BTN: {
    ContentColumnView *cv = fLibraryManager->ContentView();
    BRow *row = cv->CurrentSelection();
    int32 sel = (row ? cv->IndexOf(row) : 0);

    std::vector<std::string> queue;
    queue.reserve(cv->CountRows());
    for (int32 i = 0; i < cv->CountRows(); ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (!mi || mi->missing)
        continue;

      queue.push_back(mi->path.String());
    }

    if (!queue.empty()) {
      DEBUG_PRINT("[Window] MSG_PLAY_BTN: restart sel=%ld\\n", (long)sel);
      fController->Stop();
      fController->SetQueue(queue);
      fController->Play(sel);
      fSongDuration = fController->Duration();
    }
    break;
  }
  case MSG_MEDIA_BATCH: {
    type_code type;
    int32 count = 0;
    if (msg->GetInfo("path", &type, &count) != B_OK)
      break;

    bool needsUpdate = false;
    for (int32 i = 0; i < count; i++) {
      BString pathStr;
      if (msg->FindString("path", i, &pathStr) != B_OK)
        continue;

      BPath normPath(pathStr.String());
      BString path;
      if (normPath.InitCheck() == B_OK)
        path = normPath.Path();
      else
        path = pathStr;

      auto it =
          std::find_if(fAllItems.begin(), fAllItems.end(),
                       [&](const MediaItem &mi) { return mi.path == path; });

      MediaItem *itemToUpdate = nullptr;
      if (it != fAllItems.end()) {
        itemToUpdate = &(*it);
      } else {
        MediaItem newItem;
        newItem.path = path;
        fAllItems.push_back(newItem);
        itemToUpdate = &fAllItems.back();
      }

      if (itemToUpdate) {
        BString tmp;
        if (msg->FindString("title", i, &tmp) == B_OK)
          itemToUpdate->title = tmp;
        if (msg->FindString("artist", i, &tmp) == B_OK)
          itemToUpdate->artist = tmp;
        if (msg->FindString("album", i, &tmp) == B_OK)
          itemToUpdate->album = tmp;
        if (msg->FindString("genre", i, &tmp) == B_OK)
          itemToUpdate->genre = tmp;

        int32 val;
        if (msg->FindInt32("year", i, &val) == B_OK)
          itemToUpdate->year = val;
        if (msg->FindInt32("track", i, &val) == B_OK)
          itemToUpdate->track = val;
        if (msg->FindInt32("disc", i, &val) == B_OK)
          itemToUpdate->disc = val;
        if (msg->FindInt32("duration", i, &val) == B_OK)
          itemToUpdate->duration = val;

        needsUpdate = true;
      }
    }

    if (needsUpdate) {
      DEBUG_PRINT(
          "[MainWindow] Batch update processed (%d items). Refreshing views.\n",
          (int)count);
      UpdateFilteredViews();
    }
    break;
  }
  case MSG_MEDIA_ITEM_FOUND: {
    BString pathStr;
    if (msg->FindString("path", &pathStr) == B_OK) {
      BPath normPath(pathStr.String());
      BString path;
      if (normPath.InitCheck() == B_OK)
        path = normPath.Path();
      else
        path = pathStr;

      DEBUG_PRINT(
          "[MainWindow] Item update path: '%s' (Normalized from '%s')\n",
          path.String(), pathStr.String());

      auto it =
          std::find_if(fAllItems.begin(), fAllItems.end(),
                       [&](const MediaItem &mi) { return mi.path == path; });

      MediaItem *itemToUpdate = nullptr;

      if (it != fAllItems.end()) {
        itemToUpdate = &(*it);
      } else {
        MediaItem newItem;
        newItem.path = path;
        fAllItems.push_back(newItem);
        itemToUpdate = &fAllItems.back();
      }

      if (itemToUpdate) {
        BString tmp;
        if (msg->FindString("title", &tmp) == B_OK)
          itemToUpdate->title = tmp;
        if (msg->FindString("artist", &tmp) == B_OK)
          itemToUpdate->artist = tmp;
        if (msg->FindString("album", &tmp) == B_OK) {
          DEBUG_PRINT("[MainWindow] Updating Album to: %s\n", tmp.String());
          itemToUpdate->album = tmp;
        }
        if (msg->FindString("genre", &tmp) == B_OK)
          itemToUpdate->genre = tmp;
        if (msg->FindString("comment", &tmp) == B_OK)
          itemToUpdate->comment = tmp;

        int32 val;
        if (msg->FindInt32("year", &val) == B_OK)
          itemToUpdate->year = val;
        if (msg->FindInt32("track", &val) == B_OK)
          itemToUpdate->track = val;
        if (msg->FindInt32("trackTotal", &val) == B_OK)
          itemToUpdate->trackTotal = val;
        if (msg->FindInt32("disc", &val) == B_OK)
          itemToUpdate->disc = val;
        if (msg->FindInt32("discTotal", &val) == B_OK)
          itemToUpdate->discTotal = val;
        if (msg->FindInt32("duration", &val) == B_OK)
          itemToUpdate->duration = val;

        DEBUG_PRINT("[MainWindow] Calling UpdateFilteredViews...\n");
        UpdateFilteredViews();
      }
    }
    break;
  }

  case MSG_MEDIA_ITEM_REMOVED: {
    BString path;
    if (msg->FindString("path", &path) == B_OK) {
      DEBUG_PRINT("[MainWindow] remove item: %s\\n", path.String());

      ContentColumnView *cv = fLibraryManager->ContentView();
      for (int32 i = 0; i < cv->CountRows(); ++i) {
        const MediaItem *mi = cv->ItemAt(i);
        if (mi && mi->path == path) {
          BRow *r = cv->RowAt(i);
          cv->RemoveRow(r);
          delete r;
          break;
        }
      }

      auto it =
          std::remove_if(fAllItems.begin(), fAllItems.end(),
                         [&](const MediaItem &mi) { return mi.path == path; });
      if (it != fAllItems.end()) {
        fAllItems.erase(it, fAllItems.end());
      }
    }
    break;
  }

  case MSG_NOW_PLAYING: {
    int32 index;
    BString path;
    if (msg->FindInt32("index", &index) == B_OK &&
        msg->FindString("path", &path) == B_OK) {

      BString artist, title, album, genre;
      int32 year = 0;
      int32 bitrate = 0;
      for (const auto &media : fAllItems) {
        if (media.path == path) {
          artist = media.artist;
          title = media.title;
          album = media.album;
          genre = media.genre;
          year = media.year;
          bitrate = media.bitrate;
          break;
        }
      }

      BString label;
      if (!artist.IsEmpty())
        label << artist << " - ";

      BString displayTitle = title.IsEmpty() ? path : title;
      label << displayTitle;

      fTitleView->SetText(label);

      // Update now-playing indicator in content view
      if (fLibraryManager && fLibraryManager->ContentView()) {
        fLibraryManager->ContentView()->SetNowPlayingPath(path);
      }

      // Update InfoPanel with current track info after the latest change ;)
      if (fInfoPanel) {
        BString info;
        info << B_TRANSLATE("Artist: ") << (artist.IsEmpty() ? "-" : artist)
             << "\n";
        info << B_TRANSLATE("Album: ") << (album.IsEmpty() ? "-" : album)
             << "\n";
        info << B_TRANSLATE("Title: ") << (title.IsEmpty() ? "-" : title)
             << "\n";
        info << B_TRANSLATE("Year: ") << year << "\n";
        info << B_TRANSLATE("Genre: ") << (genre.IsEmpty() ? "-" : genre)
             << "\n\n";
        info << B_TRANSLATE("Bitrate: ") << bitrate << " kbps\n";
        fInfoPanel->SetFileInfo(info);
      }
    }
    break;
  }

  case MSG_MANAGE_DIRECTORIES: {
    DirectoryManagerWindow *win = new DirectoryManagerWindow(fCacheManager);
    win->Show();
    break;
  }

  case B_CONTROL_INVOKED: {
    void *source = nullptr;
    ContentColumnView *cv = fLibraryManager->ContentView();
    if (msg->FindPointer("source", &source) == B_OK && source == cv) {
      BRow *row = cv->CurrentSelection();
      int32 index = (row ? cv->IndexOf(row) : -1);

      if (index >= 0 && fController) {
        std::vector<std::string> queue;
        for (int32 i = 0; i < cv->CountRows(); ++i) {
          const MediaItem *mi = cv->ItemAt(i);
          if (mi)
            queue.push_back(mi->path.String());
        }

        if (!queue.empty()) {
          fController->Stop();
          fController->SetQueue(queue);
          fController->Play(index);
          fSongDuration = fController->Duration();
        }
      }
    }
    break;
  }

  case MSG_PLAY_NEXT:
    if (fController) {
      if (fRepeatMode == RepeatOne) {
        fController->Play(fController->CurrentIndex());
      } else if (fShuffleEnabled) {
        int32 count = fController->QueueSize();
        if (count > 0) {
          int32 next = rand() % count;
          fController->Play(next);
        }
      } else {
        fController->PlayNext();
      }
    }
    if (fController->IsPlaying())
      fBtnPlayPause->SetLabel("⏸");
    break;

  case MSG_PREV_BTN:
    if (fController) {
      if (fShuffleEnabled) {
        int32 count = fController->QueueSize();
        if (count > 0) {
          int32 prev = rand() % count;
          fController->Play(prev);
        }
      } else {
        fController->PlayPrev();
      }
    }
    if (fController->IsPlaying())
      fBtnPlayPause->SetLabel("⏸");
    break;

  case MSG_PAUSE:
    if (fController) {
      if (fController->IsPaused()) {
        fController->Resume();
      } else if (fController->IsPlaying()) {
        fController->Pause();
      }
    }
    break;

  case MSG_STOP:
    if (fController)
      fController->Stop();
    if (fUpdateRunner) {
      delete fUpdateRunner;
      fUpdateRunner = nullptr;
    }
    break;

  case MSG_SEEK_REQUEST:
    if (fController) {
      bigtime_t newPos;
      if (msg->FindInt64("position", &newPos) == B_OK)
        fController->SeekTo(newPos);
    }
    break;

  case MSG_TIME_UPDATE: {
    if (!fController)
      break;
    bigtime_t dur = fController->Duration();
    if (dur <= 0)
      break;

    bigtime_t pos = fController->CurrentPosition();
    fSeekBar->SetDuration(dur);
    fSeekBar->SetPosition(pos);
    break;
  }

  case MSG_TRACK_ENDED: {
    if (fController) {
      if (fRepeatMode == RepeatOne) {
        fController->Play(fController->CurrentIndex());
      } else if (fShuffleEnabled) {
        int32 count = fController->QueueSize();
        if (count > 0) {
          int32 next = rand() % count;
          fController->Play(next);
        }
      } else if (fRepeatMode == RepeatAll) {
        if (fController->CurrentIndex() + 1 < fController->QueueSize()) {
          fController->PlayNext();
        } else {
          fController->Play(0);
        }
      } else {
        fController->PlayNext();
      }
    }
    break;
  }

  case MSG_SEARCH_MODIFY: {

    delete fSearchRunner;
    BMessage exec(MSG_SEARCH_EXECUTE);
    fSearchRunner = new BMessageRunner(BMessenger(this), &exec, 300000, 1);
    break;
  }

  case MSG_SEARCH_EXECUTE: {
    UpdateFilteredViews();
    break;
  }
  case MSG_SELECTION_CHANGED_GENRE: {
    UpdateFilteredViews();
    break;
  }
  case MSG_SELECTION_CHANGED_ALBUM: {
    UpdateFilteredViews();
    break;
  }
  case MSG_SELECTION_CHANGED_ARTIST: {
    UpdateFilteredViews();
    break;
  }

  case MSG_SELECTION_CHANGED_CONTENT: {

    ContentColumnView *cv = fLibraryManager->ContentView();
    int32 rowIndex = cv->IndexOf(cv->CurrentSelection());
    if (rowIndex < 0)
      break;

    const MediaItem *mi = cv->SelectedItem();
    if (!mi)
      break;

    UpdateFileInfo();

    if (mi->path.IsEmpty()) {
      if (fInfoPanel) {
        fInfoPanel->ClearCover();
        fInfoPanel->Switch(InfoPanel::Info);
      }
      break;
    }

    if (fLastSelectedPath == mi->path) {

      break;
    }
    fLastSelectedPath = mi->path;

    if (fInfoPanel) {
      fInfoPanel->ClearCover();
      fInfoPanel->Switch(InfoPanel::Info);
    }

    BMessenger target(this);
    BString pathStr = mi->path;
    LaunchThread("CoverFetch", [target, pathStr]() {
      BPath p(pathStr.String());
      CoverBlob cb;
      BBitmap *bmp = nullptr;

      if (TagSync::ExtractEmbeddedCover(p, cb) && cb.data() && cb.size() > 0) {
        BMemoryIO io(cb.data(), cb.size());
        bmp = BTranslationUtils::GetBitmap(&io);
      }

      if (target.IsValid()) {
        BMessage reply(MSG_COVER_BITMAP_READY);
        reply.AddString("path", pathStr);
        if (bmp) {
          reply.AddPointer("bitmap", bmp);
        }
        target.SendMessage(&reply);
      } else {
        delete bmp;
      }
    });

    break;
  }

  case MSG_COVER_BITMAP_READY: {
    BString path;
    if (msg->FindString("path", &path) != B_OK)
      break;

    BBitmap *bmp = nullptr;
    msg->FindPointer("bitmap", (void **)&bmp);

    ContentColumnView *cv = fLibraryManager->ContentView();
    BRow *row = cv->CurrentSelection();
    bool match = false;
    if (row) {
      int32 idx = cv->IndexOf(row);
      const MediaItem *mi = cv->ItemAt(idx);
      if (mi && path == mi->path) {
        match = true;
      }
    }

    if (match && bmp && fInfoPanel) {
      if (fShowCoverArt) {
        fInfoPanel->SetCover(bmp);
      }
    }

    delete bmp;
    break;
  }

  case MSG_ADD_TO_PLAYLIST: {
    BString playlist;
    if (msg->FindString("playlist", &playlist) != B_OK)
      break;

    if (!fPlaylistManager->IsPlaylistWritable(playlist)) {
      DEBUG_PRINT("[MainWindow] addp abgelehnt: Playlist '%s' ist nicht "
                  "beschreibbar\\n",
                  playlist.String());
      break;
    }

    int32 index;
    bool hadAny = false;
    for (int32 i = 0; msg->FindInt32("index", i, &index) == B_OK; ++i) {
      BString path = GetPathForContentItem(index);
      if (path.IsEmpty())
        continue;

      DEBUG_PRINT("[MainWindow] addp: Index=%ld"
                  ", Playlist=%s, Pfad=%s\\n",
                  (long)index, playlist.String(), path.String());

      AddItemToPlaylist(path, playlist);
      hadAny = true;
    }

    if (!hadAny && msg->FindInt32("index", &index) == B_OK) {
      BString path = GetPathForContentItem(index);
      if (path.IsEmpty())
        break;

      DEBUG_PRINT("[MainWindow] addp(single): Index=%ld"
                  ", Playlist=%s, Pfad=%s\\n",
                  (long)index, playlist.String(), path.String());

      AddItemToPlaylist(path, playlist);
    }
    break;
  }

  case MSG_PLAYLIST_SELECTION:
  case MSG_INIT_LIBRARY: {
    int32 selected = fPlaylistManager->View()->CurrentSelection();
    if (selected < 0)
      break;

    BString name = fPlaylistManager->View()->ItemAt(selected);
    if (name.IsEmpty())
      break;

    fCurrentPlaylistName = name;

    int32 kindInt = 0;
    PlaylistItemKind kind = PlaylistItemKind::Playlist;

    if (msg->what == MSG_PLAYLIST_SELECTION &&
        msg->FindInt32("kind", &kindInt) == B_OK) {
      kind = (PlaylistItemKind)kindInt;
    } else {

      if (name == "Library") {
        kind = PlaylistItemKind::Library;
      } else {
        kind = PlaylistItemKind::Playlist;
      }
    }

    fIsLibraryMode = (kind == PlaylistItemKind::Library);
    fIsLibraryMode = (kind == PlaylistItemKind::Library);

    if (fIsLibraryMode) {
      fLibraryManager->SetActivePaths({});
    } else {
      std::vector<BString> paths = fPlaylistManager->LoadPlaylist(name);
      fLibraryManager->SetActivePaths(paths);
    }

    UpdateFilteredViews();
    break;
  }

  case MSG_PROPERTIES: {
    std::vector<BPath> files;
    CollectPathsFromMessage(msg, files);

    if (files.empty()) {
      BRow *row = nullptr;
      ContentColumnView *cv = fLibraryManager->ContentView();
      while ((row = cv->CurrentSelection(row)) != nullptr) {
        int32 idx = cv->IndexOf(row);
        BString path = GetPathForContentItem(idx);
        if (!path.IsEmpty())
          files.emplace_back(path.String());
      }
    }

    if (files.empty()) {
      DEBUG_PRINT(
          "[Properties] Keine Pfade in MSG_PROPERTIES (file/refs + Auswahl "
          "leer)\\n");
      break;
    }

    if (files.size() == 1) {

      std::vector<BPath> contextFiles;
      int32 selectionIndex = 0;

      ContentColumnView *cv = fLibraryManager->ContentView();
      int32 count = cv->CountRows();

      contextFiles.reserve(count);

      BString targetPath = files[0].Path();

      for (int32 i = 0; i < count; ++i) {
        const MediaItem *mi = cv->ItemAt(i);
        if (mi) {
          contextFiles.emplace_back(mi->path.String());
          if (mi->path == targetPath) {
            selectionIndex = (int32)contextFiles.size() - 1;
          }
        }
      }

      fPropertiesWindow =
          new PropertiesWindow(contextFiles, selectionIndex, BMessenger(this));
    } else {
      fPropertiesWindow = new PropertiesWindow(files, BMessenger(this));
    }
    fPropertiesWindow->Show();
    break;
  }

  case MSG_NEW_PLAYLIST: {

    fPendingPlaylistFiles.MakeEmpty();

    BMessage filesMsg;
    if (msg->FindMessage("files", &filesMsg) == B_OK) {
      fPendingPlaylistFiles = filesMsg;
      DEBUG_PRINT("[MainWindow] %ld"
                  " Dateien für neue Playlist gepuffert\\n",
                  (long)filesMsg.CountNames(B_REF_TYPE));
    }

    NamePrompt *prompt = new NamePrompt(BMessenger(this));
    prompt->Show();
    break;
  }
  case MSG_SAVE_PLAYLIST_SELECTION: {
    int32 selected = fPlaylistManager->View()->CurrentSelection();
    if (selected < 0)
      break;

    BString item = fPlaylistManager->View()->ItemAt(selected);
    if (!item)
      break;

    BString name = item;
    std::vector<BString> paths;

    ContentColumnView *cv = fLibraryManager->ContentView();
    for (int32 i = 0; i < cv->CountRows(); ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi)
        paths.push_back(mi->path);
    }

    fPlaylistManager->SavePlaylist(name, paths);
    break;
  }

  case MSG_SET_PLAYLIST_FOLDER: {
    _SelectPlaylistFolder();
    break;
  }

  case MSG_PLAYLIST_CREATED: {
    BString name;
    if (msg->FindString("name", &name) == B_OK && !name.IsEmpty()) {

      CreatePlaylist(name);

      fPlaylistManager->CreateNewPlaylist(name);

      entry_ref ref;
      int32 i = 0;
      while (fPendingPlaylistFiles.FindRef("refs", i++, &ref) == B_OK) {
        BPath path(&ref);
        AddItemToPlaylist(path.Path(), name);
        DEBUG_PRINT(
            "[MainWindow] Datei '%s' zu neuer Playlist '%s' hinzugefügt\\n",
            path.Path(), name.String());
      }
      fPendingPlaylistFiles.MakeEmpty();
    }
    break;
  }

  case MSG_REVEAL_IN_TRACKER: {
    std::vector<entry_ref> refs;
    BMessage files;
    if (msg->FindMessage("files", &files) == B_OK) {
      entry_ref r;
      for (int32 i = 0; files.FindRef("refs", i, &r) == B_OK; ++i)
        refs.push_back(r);
    } else {
      entry_ref r;
      for (int32 i = 0; msg->FindRef("refs", i, &r) == B_OK; ++i)
        refs.push_back(r);
    }
    if (refs.empty())
      break;

    std::set<BString> openedDirs;
    for (const auto &r : refs) {
      BEntry e(&r, true);
      BPath filePath;
      if (e.GetPath(&filePath) != B_OK)
        continue;

      BPath dirPath(filePath);
      if (dirPath.GetParent(&dirPath) != B_OK)
        continue;

      BString d = dirPath.Path();
      if (openedDirs.insert(d).second) {
        entry_ref dirRef;
        if (get_ref_for_path(d.String(), &dirRef) == B_OK) {
          BRoster roster;
          status_t st = roster.Launch(&dirRef);
          if (st != B_OK && st != B_ALREADY_RUNNING) {
            DEBUG_PRINT("[MainWindow] Tracker Launch dir failed: %s\\n",
                        strerror(st));
          }
        }
      }
    }

    break;
  }

  case MSG_NAME_PROMPT_RENAME: {
    BString oldName, newName;
    if (msg->FindString("old", &oldName) == B_OK &&
        msg->FindString("name", &newName) == B_OK && !newName.IsEmpty()) {

      BPath dirPath;
      if (find_directory(B_USER_SETTINGS_DIRECTORY, &dirPath) == B_OK) {
        dirPath.Append("BeTon/Playlists");

        BString oldFile = oldName;
        oldFile << ".m3u";
        BString newFile = newName;
        newFile << ".m3u";

        BPath oldPath(dirPath.Path(), oldFile.String());
        BPath newPath(dirPath.Path(), newFile.String());

        BEntry entry(oldPath.Path());
        if (entry.Exists() && entry.Rename(newPath.Path()) == B_OK) {
          DEBUG_PRINT("[MainWindow] Playlist '%s' → '%s' umbenannt\\n",
                      oldName.String(), newName.String());

          fPlaylistManager->RenamePlaylist(oldName, newName);
        }
      }
    }
    break;
  }

  case MSG_LIST_PLAYLIST: {
    BMessage reply;
    fPlaylistManager->GetPlaylistNames(reply, false);
    msg->SendReply(&reply);
    break;
  }

  case MSG_MB_SEARCH: {
    BString artist, title, album;
    msg->FindString("artist", &artist);
    msg->FindString("title", &title);
    msg->FindString("album", &album);

    DEBUG_PRINT(
        "[MainWindow] MSG_MB_SEARCH received: A='%s', T='%s', Alb='%s'\\n",
        artist.String(), title.String(), album.String());

    fPendingFiles.clear();
    BString fpath;
    for (int32 i = 0; msg->FindString("file", i, &fpath) == B_OK; i++) {
      fPendingFiles.push_back(fpath);
    }
    DEBUG_PRINT("[MainWindow] MSG_MB_SEARCH context: %zu files\\n",
                fPendingFiles.size());

    int32 gen = ++fMbSearchGeneration;
    BMessenger replyTo = msg->ReturnAddress();
    UpdateStatus(B_TRANSLATE("Searching on MusicBrainz..."));

    LaunchThread("MBSearch", [this, artist, title, album, replyTo, gen]() {
      if (!fMbClient) {
        DEBUG_PRINT("[MainWindow] Search thread abort: fMbClient is null\\n");
        return;
      }

      DEBUG_PRINT("[MainWindow] Thread running SearchRecording... Gen=%ld"
                  "\\n",
                  (long)gen);
      auto abortCheck = [this, gen]() { return fMbSearchGeneration != gen; };
      std::vector<MBHit> hits =
          fMbClient->SearchRecording(artist, title, album, abortCheck);
      DEBUG_PRINT("[MainWindow] SearchRecording returned %zu hits\\n",
                  hits.size());

      BMessage completion(MSG_MB_SEARCH_COMPLETE);
      completion.AddPointer("hits", new std::vector<MBHit>(hits));
      completion.AddMessenger("replyTo", replyTo);
      completion.AddInt32("generation", gen);
      BMessenger(this).SendMessage(&completion);
    });
    break;
  }

  case MSG_RESET_STATUS:
    _UpdateStatusLibrary();
    break;

  case MSG_MB_CANCEL: {
    DEBUG_PRINT(
        "[MainWindow] MSG_MB_CANCEL received. Aborting current operations.\\n");
    fMbSearchGeneration++;
    UpdateStatus(B_TRANSLATE("Cancelled by user."));

    BMessage msg(MSG_RESET_STATUS);
    new BMessageRunner(BMessenger(this), &msg, 3000000, 1);
    break;
  }

  case MSG_STATUS_UPDATE: {
    BString text;
    if (msg->FindString("text", &text) == B_OK) {
      UpdateStatus(text);
    }
    break;
  }

  case MSG_MATCH_RESULT: {

    DEBUG_PRINT("[MainWindow] Matcher Applied. Processing...\\n");

    int32 trackIdx;
    int32 i = 0;
    BString itemPath;

    while (msg->FindInt32("track_idx", i, &trackIdx) == B_OK) {

      if (msg->FindString("file_path", i, &itemPath) != B_OK) {

        if (i < (int32)fPendingFiles.size())
          itemPath = fPendingFiles[i];
        else
          break;
      }

      if (trackIdx >= 0 && trackIdx < (int32)fPendingRelease.tracks.size()) {
        const MBTrack &trk = fPendingRelease.tracks[trackIdx];
        BString filePath = itemPath;

        TagData td;
        TagSync::ReadTags(BPath(filePath.String()), td);

        td.artist = fPendingRelease.albumArtist;
        td.album = fPendingRelease.album;
        td.title = trk.title;
        td.year = fPendingRelease.year;
        td.track = trk.track;
        td.trackTotal = (uint32)fPendingRelease.tracks.size();
        td.disc = trk.disc;
        td.albumArtist = fPendingRelease.albumArtist;
        td.mbAlbumID = fPendingRelease.releaseId;
        td.mbTrackID = trk.recordingId;

        DEBUG_PRINT("[MainWindow] Applying Tags to '%s':\\n",
                    filePath.String());
        DEBUG_PRINT("    Title: %s\\n", td.title.String());
        DEBUG_PRINT("    MB Track ID: %s\\n", td.mbTrackID.String());
        DEBUG_PRINT("    MB Album ID: %s\\n", td.mbAlbumID.String());

        TagSync::WriteTags(BPath(filePath.String()), td);
        if (fPendingCoverBlob.size() > 0) {
          TagSync::WriteEmbeddedCover(BPath(filePath.String()),
                                      fPendingCoverBlob);
        }
        TagSync::WriteBfsAttributes(BPath(filePath.String()), td, nullptr);

        BMessage update(MSG_MEDIA_ITEM_FOUND);
        update.AddString("path", filePath);
        update.AddString("title", td.title);
        update.AddString("artist", td.artist);

        BMessenger(this).SendMessage(&update);
        if (fCacheManager)
          BMessenger(fCacheManager).SendMessage(&update);
      }
      i++;
    }
    UpdateStatus(B_TRANSLATE("Metadata applied successfully (Manual)."));

    fPendingFiles.clear();
    fPendingCoverBlob.clear();
    break;
  }

  case MSG_MB_SEARCH_COMPLETE: {
    int32 gen = 0;
    msg->FindInt32("generation", &gen);
    if (gen != fMbSearchGeneration) {

      std::vector<MBHit> *hits = nullptr;
      if (msg->FindPointer("hits", (void **)&hits) == B_OK && hits)
        delete hits;
      break;
    }

    std::vector<MBHit> *hits = nullptr;
    if (msg->FindPointer("hits", (void **)&hits) == B_OK && hits) {

      if (hits->empty()) {
        UpdateStatus(B_TRANSLATE("MusicBrainz: Nothing found."));
      } else {
        BString s;
        s.SetToFormat(B_TRANSLATE("MusicBrainz: %zu hits."), hits->size());
        UpdateStatus(s);
      }

      BMessenger replyTo;
      msg->FindMessenger("replyTo", &replyTo);

      DEBUG_PRINT(
          "[MainWindow] MB Search Complete. Hits: %zu. ReplyTo Valid: %ld"
          "\\n",
          hits->size(), (long)(int32)replyTo.IsValid());

      if (replyTo.IsValid()) {

        int targetCount = (int)fPendingFiles.size();
        if (targetCount > 0) {
          std::sort(hits->begin(), hits->end(),
                    [targetCount](const MBHit &a, const MBHit &b) {
                      int diffA = std::abs(a.trackCount - targetCount);
                      int diffB = std::abs(b.trackCount - targetCount);
                      if (diffA != diffB)
                        return diffA < diffB;

                      return a.year > b.year;
                    });
        }

        BMessage resp(MSG_MB_RESULTS);
        for (const auto &h : *hits) {
          BString item;

          BString extra;
          extra << h.releaseTitle;
          if (h.year > 0)
            extra << ", " << h.year;
          if (!h.country.IsEmpty())
            extra << ", " << h.country;
          if (h.trackCount > 0)
            extra << ", " << h.trackCount << " Tracks";

          item.SetToFormat("%s - %s (%s)", h.artist.String(), h.title.String(),
                           extra.String());

          resp.AddString("item", item);
          resp.AddString("id", h.recordingId);
          resp.AddString("releaseId", h.releaseId);
        }
        status_t err = replyTo.SendMessage(&resp);
        DEBUG_PRINT(
            "[MainWindow] Sent MB Results to PropertiesWindow. Error: %ld"
            "\\n",
            (long)err);
      }
      delete hits;
    }
    break;
  }

  case MSG_MB_APPLY:
  case MSG_MB_APPLY_ALBUM: {
    BString recId, relId;
    if (msg->FindString("id", &recId) != B_OK)
      break;
    msg->FindString("releaseId", &relId);

    UpdateStatus("Hole Metadaten von MusicBrainz...");

    bool albumMode = (msg->what == MSG_MB_APPLY_ALBUM);

    std::vector<BString> files;
    BString f;
    for (int32 i = 0; msg->FindString("file", i, &f) == B_OK; i++) {
      files.push_back(f);
    }
    if (files.empty())
      break;

    BMessenger replyTo = msg->ReturnAddress();

    DEBUG_PRINT("[MainWindow] MSG_MB_APPLY received. IDs: rec='%s', rel='%s'. "
                "Files: %zu\\n",
                recId.String(), relId.String(), files.size());

    int32 gen = fMbSearchGeneration;
    LaunchThread("MBApply", [this, recId, relId, files, albumMode, replyTo,
                             gen]() mutable {
      if (!fMbClient) {
        DEBUG_PRINT("[MainWindow] Apply thread abort: fMbClient is null\\n");
        return;
      }

      auto abortStatus = [this]() {
        BMessage statusDone(MSG_STATUS_UPDATE);
        statusDone.AddString("text", B_TRANSLATE("Cancelled."));
        BMessenger(this).SendMessage(&statusDone);
      };

      if (fMbSearchGeneration != gen) {
        abortStatus();
        return;
      }

      auto abortCheck = [this, gen]() { return fMbSearchGeneration != gen; };

      BString effectiveRelId = relId;
      if (effectiveRelId.IsEmpty()) {
        DEBUG_PRINT("[MainWindow] Resolving release for recording: %s\\n",
                    recId.String());
        effectiveRelId = fMbClient->BestReleaseForRecording(recId, abortCheck);
      }

      if (fMbSearchGeneration != gen) {
        abortStatus();
        return;
      }

      if (effectiveRelId.IsEmpty()) {
        DEBUG_PRINT("[MainWindow] Error: Could not resolve release ID.\\n");
        BMessage statusDone(MSG_STATUS_UPDATE);
        statusDone.AddString("text",
                             B_TRANSLATE("Error: Release ID not found."));
        BMessenger(this).SendMessage(&statusDone);
        return;
      }

      DEBUG_PRINT("[MainWindow] Fetching details for release: %s\\n",
                  effectiveRelId.String());
      MBRelease rel = fMbClient->GetReleaseDetails(effectiveRelId, abortCheck);

      if (fMbSearchGeneration != gen) {
        abortStatus();
        return;
      }

      DEBUG_PRINT("[MainWindow] Release fetched: '%s' (%zu tracks)\\n",
                  rel.album.String(), rel.tracks.size());

      CoverBlob coverBlob;
      std::vector<uint8_t> coverData;
      BString coverMime;

      bool hasCover = false;

      if (!rel.releaseGroupId.IsEmpty()) {
        DEBUG_PRINT(
            "[MainWindow] Trying to fetch cover for Release Group: %s\\n",
            rel.releaseGroupId.String());
        hasCover = fMbClient->FetchCover(rel.releaseGroupId, coverData,
                                         &coverMime, 500, true, abortCheck);
      }

      if (fMbSearchGeneration != gen) {
        abortStatus();
        return;
      }

      if (!hasCover && !effectiveRelId.IsEmpty()) {
        DEBUG_PRINT("[MainWindow] No Group cover, trying Release: %s\\n",
                    effectiveRelId.String());
        hasCover = fMbClient->FetchCover(effectiveRelId, coverData, &coverMime,
                                         500, true, abortCheck);
      }

      if (fMbSearchGeneration != gen) {
        abortStatus();
        return;
      }

      if (hasCover) {
        DEBUG_PRINT("[MainWindow] Cover fetched: %zu bytes (%s)\\n",
                    coverData.size(), coverMime.String());
        coverBlob.assign(coverData.data(), coverData.size());
      } else {
        DEBUG_PRINT("[MainWindow] No cover found for release/group.\\n");
      }

      if (albumMode && files.size() == 1) {
        BPath p(files[0].String());
        BPath parent;
        if (p.GetParent(&parent) == B_OK) {
          DEBUG_PRINT("[MainWindow] Single file selected in Album Mode. "
                      "Scanning parent: %s\\n",
                      parent.Path());
          BDirectory dir(parent.Path());
          BEntry entry;
          std::vector<BString> dirFiles;
          while (dir.GetNextEntry(&entry) == B_OK) {
            BPath ep;
            if (entry.GetPath(&ep) == B_OK && !entry.IsDirectory()) {
              BString pathStr = ep.Path();
              if (pathStr.EndsWith(".mp3") || pathStr.EndsWith(".flac") ||
                  pathStr.EndsWith(".wav") || pathStr.EndsWith(".m4a") ||
                  pathStr.EndsWith(".ogg")) {
                dirFiles.push_back(pathStr);
              }
            }
          }
          if (!dirFiles.empty()) {
            DEBUG_PRINT(
                "[MainWindow] Expanded single file to %zu files in %s\\n",
                dirFiles.size(), parent.Path());
            files = dirFiles;
          }
        }
      }

      DEBUG_PRINT(
          "[MainWindow] Starting processing loop for %zu files. Mode: %s\\n",
          files.size(), albumMode ? "Album" : "Track");

      if (albumMode) {

        std::sort(files.begin(), files.end());

        std::vector<int> fileToTrackMap(files.size(), -1);
        std::vector<bool> trackUsed(rel.tracks.size(), false);
        int filesMatched = 0;
        bool durationMismatch = false;

        for (size_t i = 0; i < files.size(); i++) {
          BPath bp(files[i].String());
          TagData td;
          TagSync::ReadTags(bp, td);

          const MBTrack *bestMatch = nullptr;
          int bestTrackIdx = -1;

          if (td.track > 0) {
            for (size_t k = 0; k < rel.tracks.size(); k++) {
              if (!trackUsed[k] && rel.tracks[k].track == td.track) {

                int durDiff =
                    abs((int)rel.tracks[k].length - (int)td.lengthSec);
                if (durDiff < 15) {
                  bestMatch = &rel.tracks[k];
                  bestTrackIdx = k;
                } else {
                  durationMismatch = true;
                }
                break;
              }
            }
          }

          if (!bestMatch) {
            int fnTrack = MatchingUtils::ExtractTrackNumber(bp.Leaf());
            if (fnTrack > 0) {
              for (size_t k = 0; k < rel.tracks.size(); k++) {
                if (!trackUsed[k] && rel.tracks[k].track == (uint32)fnTrack) {
                  int durDiff =
                      abs((int)rel.tracks[k].length - (int)td.lengthSec);
                  if (durDiff < 15) {
                    bestMatch = &rel.tracks[k];
                    bestTrackIdx = k;
                  }
                  break;
                }
              }
            }
          }

          if (bestMatch) {
            fileToTrackMap[i] = bestTrackIdx;
            trackUsed[bestTrackIdx] = true;
            filesMatched++;
          }
        }

        int nextTrackIdx = 0;
        for (size_t i = 0; i < files.size(); i++) {
          if (fileToTrackMap[i] == -1) {

            while (nextTrackIdx < (int)rel.tracks.size() &&
                   trackUsed[nextTrackIdx]) {
              nextTrackIdx++;
            }
            if (nextTrackIdx < (int)rel.tracks.size()) {

              fileToTrackMap[i] = nextTrackIdx;
              trackUsed[nextTrackIdx] = true;
            }
          }
        }

        bool allMapped = true;
        for (int idx : fileToTrackMap)
          if (idx == -1)
            allMapped = false;

        bool confident = allMapped && !durationMismatch &&
                         (filesMatched >= (int)files.size() / 2);

        if (confident) {
          DEBUG_PRINT(
              "[MainWindow] Auto-Match confident. Applying tags directly.\n");

          for (size_t i = 0; i < files.size(); i++) {
            int tIdx = fileToTrackMap[i];
            if (tIdx < 0)
              continue;

            const MBTrack &trk = rel.tracks[tIdx];
            TagData td;
            TagSync::ReadTags(BPath(files[i].String()), td);

            td.artist = rel.albumArtist;
            td.album = rel.album;
            td.title = trk.title;
            td.year = rel.year;
            td.track = trk.track;
            td.trackTotal = (uint32)rel.tracks.size();
            td.disc = trk.disc;
            td.albumArtist = rel.albumArtist;
            td.mbAlbumID = rel.releaseId;
            td.mbTrackID = trk.recordingId;

            TagSync::WriteTags(BPath(files[i].String()), td);
            if (coverBlob.size() > 0) {
              TagSync::WriteEmbeddedCover(BPath(files[i].String()), coverBlob);
            }

            TagSync::WriteBfsAttributes(BPath(files[i].String()), td, nullptr);
            BMessage update(MSG_MEDIA_ITEM_FOUND);
            update.AddString("path", files[i]);

            BMessenger(this).SendMessage(&update);
            if (fCacheManager)
              BMessenger(fCacheManager).SendMessage(&update);
          }
          BMessage statusMsg(MSG_STATUS_UPDATE);
          statusMsg.AddString(
              "text",
              B_TRANSLATE("Metadata applied successfully (Auto-Match)."));
          BMessenger(this).SendMessage(&statusMsg);

        } else {
          DEBUG_PRINT("[MainWindow] Auto-Match NOT confident (Mismatch=%" PRId32
                      ", "
                      "Matched=%" PRId32 "/%zu). Opening MatcherWindow.\n",
                      durationMismatch, filesMatched, files.size());

          std::vector<MatcherTrackInfo> trackInfos;
          for (const auto &t : rel.tracks) {
            BString dur;
            dur.SetToFormat("%d:%02d", (int)(t.length / 60),
                            (int)(t.length % 60));
            trackInfos.push_back({t.title, dur, (int)t.track});
          }

          try {
            new MatcherWindow(files, trackInfos, fileToTrackMap,
                              BMessenger(this));
          } catch (...) {
            DEBUG_PRINT("[MainWindow] Failed to create MatcherWindow!\n");
          }

          if (Lock()) {
            fPendingRelease = rel;
            fPendingCoverBlob = coverBlob;
            Unlock();
          }
        }

      } else {

        for (const auto &path : files) {
          TagData td;
          TagSync::ReadTags(BPath(path.String()), td);

          const MBTrack *trkMatch = nullptr;
          for (const auto &t : rel.tracks) {
            if (t.recordingId == recId) {
              trkMatch = &t;
              break;
            }
          }

          if (trkMatch) {
            DEBUG_PRINT("[MainWindow] Applying Track Mode: File '%s' -> Track "
                        "Match '%s'\\n",
                        path.String(), trkMatch->title.String());
          } else {
            DEBUG_PRINT("[MainWindow] Warning: Track Mode, but bad recID match "
                        "for file '%s'\\n",
                        path.String());
          }

          td.artist = rel.albumArtist;
          td.album = rel.album;
          td.year = rel.year;
          td.mbAlbumID = rel.releaseId;
          td.mbTrackID = recId;

          if (trkMatch) {
            td.title = trkMatch->title;
            td.track = trkMatch->track;
            td.disc = trkMatch->disc;
          }

          TagSync::WriteTags(BPath(path.String()), td);
          if (coverBlob.size() > 0) {
            TagSync::WriteEmbeddedCover(BPath(path.String()), coverBlob);
          }
          TagSync::WriteBfsAttributes(BPath(path.String()), td, nullptr);

          BMessage update(MSG_MEDIA_ITEM_FOUND);
          update.AddString("path", path);
          update.AddString("title", td.title);
          update.AddString("artist", td.artist);
          update.AddString("album", td.album);
          update.AddString("genre", td.genre);
          update.AddInt32("year", td.year);

          DEBUG_PRINT("[MainWindow] MSG_MEDIA_ITEM_FOUND sending (Path=%s, "
                      "Year=%lu)\n",
                      path.String(), (unsigned long)td.year);

          BMessenger(this).SendMessage(&update);
          if (fCacheManager) {
            BMessenger(fCacheManager).SendMessage(&update);
          }
        }
      }

      BMessage doneMsg(MSG_STATUS_UPDATE);
      doneMsg.AddString("text",
                        B_TRANSLATE("Metadaten erfolgreich gespeichert."));
      BMessenger(this).SendMessage(&doneMsg);

      if (!files.empty()) {
        BMessage coverMsg(MSG_COVER_FETCH_MB);
        coverMsg.AddString("file", files[0]);
        if (replyTo.IsValid()) {
          coverMsg.AddMessenger("original_reply_to", replyTo);
        }
        BMessenger(this).SendMessage(&coverMsg);
      }
    });

    break;
  }

  case MSG_COVER_FETCH_MB: {
    BString path;
    if (msg->FindString("file", &path) != B_OK) {
      DEBUG_PRINT("[MainWindow] MSG_COVER_FETCH_MB: Could not find 'file' in "
                  "message.\\n");
      break;
    }
    DEBUG_PRINT("[MainWindow] MSG_COVER_FETCH_MB: File = %s\\n", path.String());

    BMessenger replyTo;
    if (msg->FindMessenger("original_reply_to", &replyTo) != B_OK) {
      replyTo = msg->ReturnAddress();
    }

    int32 gen = fMbSearchGeneration;
    LaunchThread("CoverFetchMB", [this, path, replyTo, gen]() {
      DEBUG_PRINT("[MainWindow] MB Thread started for %s (Gen=%ld)\\n",
                  path.String(), (long)gen);
      if (!fMbClient) {
        DEBUG_PRINT("[MainWindow] fMbClient is NULL!\\n");
        return;
      }

      if (fMbSearchGeneration != gen) {
        DEBUG_PRINT("[MainWindow] Aborted (Gen mismatch start)\\n");
        return;
      }

      auto abortCheck = [this, gen]() { return fMbSearchGeneration != gen; };

      TagData td;
      if (!TagSync::ReadTags(BPath(path.String()), td)) {
        DEBUG_PRINT("[MainWindow] Could not read tags from %s\\n",
                    path.String());
        return;
      }

      if (fMbSearchGeneration != gen) {
        DEBUG_PRINT("[MainWindow] Aborted (Gen mismatch post-read)\\n");
        return;
      }

      BString relId = td.mbAlbumID;
      DEBUG_PRINT("[MainWindow] MB Album ID from tags: '%s'\\n",
                  relId.String());

      if (relId == "MusicBrainz Album Id" || relId.Length() < 30) {
        DEBUG_PRINT("[MainWindow] ID '%s' seems invalid. Ignoring.\\n",
                    relId.String());
        relId = "";
      }

      if (relId.IsEmpty()) {
        DEBUG_PRINT("[MainWindow] No ID, trying search...\\n");
        std::vector<MBHit> hits = fMbClient->SearchRecording(
            td.artist, td.title, td.album, abortCheck);

        if (fMbSearchGeneration != gen) {
          DEBUG_PRINT("[MainWindow] Aborted (Gen mismatch post-search)\\n");
          return;
        }

        if (!hits.empty()) {
          relId = hits[0].releaseId;
          DEBUG_PRINT("[MainWindow] Search found release ID: %s\\n",
                      relId.String());
        } else {
          DEBUG_PRINT("[MainWindow] Search returned 0 hits.\\n");
        }
      }

      if (relId.IsEmpty()) {
        DEBUG_PRINT("[MainWindow] resolving relId failed -> abort.\\n");
        return;
      }

      if (fMbSearchGeneration != gen) {
        DEBUG_PRINT("[MainWindow] Aborted (Gen mismatch pre-fetch)\\n");
        return;
      }

      std::vector<uint8_t> data;
      BString mime;
      DEBUG_PRINT("[MainWindow] Fetching cover for %s...\\n", relId.String());
      if (fMbClient->FetchCover(relId, data, &mime, 500, false, abortCheck)) {
        if (fMbSearchGeneration != gen)
          return;
        DEBUG_PRINT("[MainWindow] FetchCover success! %zu bytes, mime=%s\\n",
                    data.size(), mime.String());
        BMessage reply(MSG_PROP_SET_COVER_DATA);
        reply.AddData("bytes", B_RAW_TYPE, data.data(), data.size());
        replyTo.SendMessage(&reply);
      } else {
        if (fMbSearchGeneration != gen)
          return;

        DEBUG_PRINT(
            "[MainWindow] FetchCover failed for Release ID. Trying Release "
            "Group...\\n");
        MBRelease mbRel = fMbClient->GetReleaseDetails(relId, abortCheck);

        if (fMbSearchGeneration != gen)
          return;

        if (!mbRel.releaseGroupId.IsEmpty()) {
          DEBUG_PRINT("[MainWindow] Found Release Group ID: %s. Fetching...\\n",
                      mbRel.releaseGroupId.String());
          if (fMbClient->FetchCover(mbRel.releaseGroupId, data, &mime, 500,
                                    true, abortCheck)) {
            if (fMbSearchGeneration != gen)
              return;
            DEBUG_PRINT("[MainWindow] FetchCover (Group) success! %zu bytes, "
                        "mime=%s\\n",
                        data.size(), mime.String());
            BMessage reply(MSG_PROP_SET_COVER_DATA);
            reply.AddData("bytes", B_RAW_TYPE, data.data(), data.size());
            replyTo.SendMessage(&reply);
          } else {
            DEBUG_PRINT("[MainWindow] FetchCover (Group) failed.\\n");
          }
        } else {
          DEBUG_PRINT(
              "[MainWindow] No Release Group found for this release.\\n");
        }
      }

      BMessage statusDone(MSG_STATUS_UPDATE);
      statusDone.AddString("text", B_TRANSLATE("Ready."));
      BMessenger(this).SendMessage(&statusDone);
    });
    break;
  }

  case MSG_PLAYLIST_FOLDER_SELECTED: {

    entry_ref ref;
    if (msg->FindRef("refs", &ref) == B_OK) {
      BEntry entry(&ref, true);
      BPath path;
      if (entry.GetPath(&path) == B_OK) {
        fPlaylistPath = path.Path();
        fPlaylistManager->SetPlaylistFolderPath(fPlaylistPath);
        fPlaylistManager->LoadAvailablePlaylists();
        SaveSettings();
        BString statusMsg;
        statusMsg.SetToFormat(B_TRANSLATE("Playlist-Ordner gesetzt: %s"),
                              fPlaylistPath.String());
        UpdateStatus(statusMsg);
      }
    }
    break;
  }

  case MSG_NEW_SMART_PLAYLIST: {
    std::set<BString> uniqueGenres;
    for (const auto &item : fAllItems) {
      if (!item.genre.IsEmpty())
        uniqueGenres.insert(item.genre);
    }
    std::vector<BString> genres(uniqueGenres.begin(), uniqueGenres.end());

    PlaylistGeneratorWindow *win =
        new PlaylistGeneratorWindow(BMessenger(this), genres);
    win->Show();
    break;
  }

  case MSG_GENERATE_PLAYLIST: {
    BString name;
    if (msg->FindString("name", &name) != B_OK || name.IsEmpty()) {
      name = B_TRANSLATE("Generated Playlist");
    }

    bool shuffle = false;
    msg->FindBool("shuffle", &shuffle);

    std::vector<BMessage> rules;
    BMessage ruleMsg;
    int32 i = 0;
    while (msg->FindMessage("rule", i++, &ruleMsg) == B_OK) {
      rules.push_back(ruleMsg);
    }

    int32 limitMode = 0;
    msg->FindInt32("limit_mode", &limitMode);
    int32 limitValue = 0;
    msg->FindInt32("limit_value", &limitValue);

    std::vector<MediaItem> matches;
    matches.reserve(fAllItems.size());

    for (const auto &item : fAllItems) {
      bool allRulesMatch = true;

      for (const auto &r : rules) {
        int32 type = 0;
        r.FindInt32("type", &type);
        BString val1;
        r.FindString("val1", &val1);
        BString val2;
        r.FindString("val2", &val2);
        bool exclude = false;
        r.FindBool("exclude", &exclude);

        bool currentRuleMatch = false;

        if (type == 0) {
          if (!val1.IsEmpty()) {
            currentRuleMatch = (item.genre.ICompare(val1) == 0);
          }
        } else if (type == 1) {
          if (!val1.IsEmpty()) {
            currentRuleMatch = (item.artist.IFindFirst(val1) >= 0);
          }
        } else if (type == 2) {
          int32 y1 = atoi(val1.String());
          int32 y2 = atoi(val2.String());

          if (y1 > 0 && item.year < y1)
            currentRuleMatch = false;
          else if (y2 > 0 && item.year > y2)
            currentRuleMatch = false;
          else
            currentRuleMatch = true;

          bool inRange = true;
          if (y1 > 0 && item.year < y1)
            inRange = false;
          if (y2 > 0 && item.year > y2)
            inRange = false;
          currentRuleMatch = inRange;
        }

        if (exclude) {
          if (currentRuleMatch) {
            allRulesMatch = false;
            break;
          }
        } else {
          if (!currentRuleMatch) {
            allRulesMatch = false;
            break;
          }
        }
      }

      if (allRulesMatch) {
        matches.push_back(item);
      }
    }

    if (shuffle) {
      std::random_device rd;
      std::mt19937 g(rd());
      std::shuffle(matches.begin(), matches.end(), g);
    } else {
    }

    if (limitMode > 0 && matches.size() > 0) {
      if (limitMode == 1) {
        if ((int32)matches.size() > limitValue) {
          matches.resize(limitValue);
        }
      } else if (limitMode == 2) {
        int64 maxSeconds = (int64)limitValue * 60;
        int64 currentSeconds = 0;
        size_t cutIndex = matches.size();
        for (size_t k = 0; k < matches.size(); ++k) {
          currentSeconds += matches[k].duration;
          if (currentSeconds > maxSeconds) {
            cutIndex = k;
            break;
          }
        }
        if (cutIndex < matches.size()) {
          matches.resize(cutIndex);
        }
      }
    }

    std::vector<BString> paths;
    paths.reserve(matches.size());
    for (const auto &m : matches)
      paths.push_back(m.path);

    fPlaylistManager->SavePlaylist(name, paths);

    BString statusMsg;
    statusMsg.SetToFormat(B_TRANSLATE("Playlist '%s' erstellt"), name.String());
    if (shuffle)
      statusMsg << " " << B_TRANSLATE("(Gemischt)");
    if (limitMode > 0)
      statusMsg << " " << B_TRANSLATE("(Limitiert)");

    BString countStr;
    countStr.SetToFormat(B_TRANSLATE(": %zu Titel."), matches.size());
    statusMsg << countStr;

    UpdateStatus(statusMsg);
    break;
  }

  case MSG_LIBRARY_PREVIEW: {
    int32 count = 0;
    int64 duration = 0;
    if (msg->FindInt32("count", &count) == B_OK) {
      if (msg->FindInt64("duration", &duration) != B_OK)
        duration = 0;

      BString text;
      if (duration > 0) {
        int32 h = duration / 3600;
        int32 m = (duration % 3600) / 60;
        int32 s = duration % 60;
        if (h > 0)
          text.SetToFormat(B_TRANSLATE("%ld Titel. Gesamtdauer %02d:%02d:%02d"),
                           (long)count, (int)h, (int)m, (int)s);
        else
          text.SetToFormat(B_TRANSLATE("%ld Titel. Gesamtdauer %02d:%02d"),
                           (long)count, (int)m, (int)s);
      } else {
        text.SetToFormat(B_TRANSLATE("%ld tracks"), (long)count);
      }
      fStatusLabel->SetText(text.String());
    }
    break;
  }

  case MSG_COUNT_UPDATED:
    _UpdateStatusLibrary();
    break;

  default:
    BWindow::MessageReceived(msg);
    break;
  }
}

/**
 * @brief Helper to spawn and resume a background thread.
 *
 * @param name Name of the thread (for system monitor).
 * @param func Lambda function to execute in the thread.
 * @return thread_id The ID of the spawned thread, or error code.
 */
thread_id MainWindow::LaunchThread(const char *name,
                                   std::function<void()> &&func) {
  auto *funcPtr = new std::function<void()>(std::move(func));
  thread_id thread =
      spawn_thread(_ThreadEntry, name, B_NORMAL_PRIORITY, funcPtr);
  if (thread >= 0) {
    resume_thread(thread);
  } else {
    delete funcPtr;
  }
  return thread;
}

/**
 * @brief Static entry point for spawned C++ threads.
 */
status_t MainWindow::_ThreadEntry(void *data) {
  auto *func = static_cast<std::function<void()> *>(data);
  if (func) {
    (*func)();
    delete func;
  }
  return B_OK;
}

/**
 * @brief Triggers a refresh of the library views based on current filters.
 */
void MainWindow::UpdateFilteredViews() {
  if (fLibraryManager) {
    fLibraryManager->UpdateFilteredViews(
        fAllItems, fIsLibraryMode, fCurrentPlaylistName, fSearchField->Text());
    _UpdateStatusLibrary();
  }
}

/**
 * @brief Registers this window as a listener for CacheManager updates.
 */
void MainWindow::RegisterWithCacheManager() {
  BMessage reg(MSG_REGISTER_TARGET);
  reg.AddMessenger("target", BMessenger(this));
  BMessenger(fCacheManager).SendMessage(&reg);

  DEBUG_PRINT("[MainWindow] registered as UI target at CacheManager\\n");
}

/**
 * @brief Updates the "Info" side panel with details of the selected item.
 *
 * Reads tags via TagLib on demand if the item is valid.
 */
void MainWindow::UpdateFileInfo() {
  const MediaItem *mi = fLibraryManager->ContentView()->SelectedItem();
  if (!mi) {

    if (fInfoPanel)
      fInfoPanel->SetFileInfo(
          B_TRANSLATE("Artist:\nAlbum:\nTitle:\nYear:\nGenre:"
                      "\n\nBitrate:\nSample Rate:\nChannels:"));
    return;
  }

  if (mi->path.IsEmpty()) {
    BString info;
    info << B_TRANSLATE("Artist: ") << mi->artist << "\n";
    info << B_TRANSLATE("Album: ") << mi->album << "\n";
    info << B_TRANSLATE("Title: ") << mi->title << "\n";
    info << B_TRANSLATE("Year: ") << mi->year << "\n";
    info << B_TRANSLATE("Genre: ") << mi->genre << "\n\n";
    info << B_TRANSLATE("Bitrate: ") << mi->bitrate << " kbps\n";

    if (fInfoPanel)
      fInfoPanel->SetFileInfo(info);
    return;
  }

  TagLib::FileRef f(mi->path.String());
  if (!f.isNull() && f.tag()) {
    TagLib::Tag *tag = f.tag();
    TagLib::AudioProperties *prop = f.audioProperties();

    BString info;
    info << B_TRANSLATE("Artist: ")
         << (tag->artist().isEmpty() ? "-" : tag->artist().toCString(true))
         << "\n";
    info << B_TRANSLATE("Album: ")
         << (tag->album().isEmpty() ? "-" : tag->album().toCString(true))
         << "\n";
    info << B_TRANSLATE("Title: ")
         << (tag->title().isEmpty() ? "-" : tag->title().toCString(true))
         << "\n";
    info << B_TRANSLATE("Year: ") << (tag->year() ? tag->year() : 0) << "\n";
    info << B_TRANSLATE("Genre: ")
         << (tag->genre().isEmpty() ? "-" : tag->genre().toCString(true))
         << "\n\n";

    if (prop) {
      info << B_TRANSLATE("Bitrate: ") << prop->bitrate() << " kbps\n";
      info << B_TRANSLATE("Sample Rate: ") << prop->sampleRate() << " Hz\n";
      info << B_TRANSLATE("Channels: ") << prop->channels();
    }

    if (fInfoPanel)
      fInfoPanel->SetFileInfo(info);
  } else {
    BString info;
    info << B_TRANSLATE("Artist: ") << mi->artist << "\n";
    info << B_TRANSLATE("Album: ") << mi->album << "\n";
    info << B_TRANSLATE("Title: ") << mi->title << "\n";
    info << B_TRANSLATE("Year: ") << mi->year << "\n";
    info << B_TRANSLATE("Genre: ") << mi->genre << "\n\n";
    info << B_TRANSLATE("Bitrate: ") << mi->bitrate << " kbps\n";

    if (fInfoPanel)
      fInfoPanel->SetFileInfo(info);
  }
}

/**
 * @brief Helper to get the full path of an item in the Content View.
 */
BString MainWindow::GetPathForContentItem(int index) {
  ContentColumnView *cv = fLibraryManager->ContentView();
  if (!cv)
    return "";

  const MediaItem *item = cv->ItemAt(index);
  if (!item)
    return "";

  return item->path;
}

/**
 * @brief Retrieves list of playlist names from PlaylistManager.
 */
void MainWindow::GetPlaylistNames(BMessage &out, bool onlyWritable) const {
  if (fPlaylistManager) {
    fPlaylistManager->GetPlaylistNames(out, onlyWritable);
  }
}

/**
 * @brief Adds an entry to a playlist.
 */
void MainWindow::AddPlaylistEntry(const BString &playlistName,
                                  const BString &label,
                                  const BString &fullPath) {
  if (fPlaylistManager) {

    fPlaylistManager->AddPlaylistEntry(label, fullPath);
  }
}
/**
 * @brief Updates the status bar text.
 *
 * @param text The message to display.
 * @param isPermanent If false, the message will be reset after a timeout.
 */
void MainWindow::UpdateStatus(const BString &text, bool isPermanent) {
  if (fStatusLabel) {
    fStatusLabel->SetText(text.String());
  }

  delete fStatusRunner;
  fStatusRunner = nullptr;

  if (!isPermanent) {

    BMessage msg(MSG_RESET_STATUS);
    fStatusRunner = new BMessageRunner(BMessenger(this), &msg, 5000000, 1);
  }
}

/**
 * @brief Updates status bar with library statistics (Count, Duration).
 */
void MainWindow::_UpdateStatusLibrary() {
  if (!fCacheLoaded)
    return;

  int32 count = 0;
  int64 totalSeconds = 0;

  if (fLibraryManager && fLibraryManager->ContentView()) {
    ContentColumnView *cv = fLibraryManager->ContentView();
    count = cv->CountRows();
    for (int32 i = 0; i < count; ++i) {
      const MediaItem *mi = cv->ItemAt(i);
      if (mi)
        totalSeconds += mi->duration;
    }
  } else {
    count = fAllItems.size();
    for (const auto &mi : fAllItems) {
      totalSeconds += mi.duration;
    }
  }

  int32 hours = totalSeconds / 3600;
  int32 mins = (totalSeconds % 3600) / 60;
  int32 secs = totalSeconds % 60;

  BString s;
  if (hours > 0)
    s.SetToFormat(B_TRANSLATE("%ld tracks. Total duration %02d:%02d:%02d"),
                  (long)count, (int)hours, (int)mins, (int)secs);
  else
    s.SetToFormat(B_TRANSLATE("%ld tracks. Total duration %02d:%02d"),
                  (long)count, (int)mins, (int)secs);

  UpdateStatus(s.String(), true);
}

/**
 * @brief Saves current UI state (columns, playlist path, etc.) to settings
 * file.
 */
void MainWindow::SaveSettings() {
  if (!fLibraryManager || !fLibraryManager->ContentView())
    return;

  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
    settingsPath.Append("BeTon/settings");
    BFile file(settingsPath.Path(),
               B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
    if (file.InitCheck() == B_OK) {
      BMessage state;
      fLibraryManager->ContentView()->SaveState(&state);

      state.AddBool("show_cover_art", fShowCoverArt);
      if (!fPlaylistPath.IsEmpty()) {
        state.AddString("playlist_path", fPlaylistPath);
      }

      state.AddBool("use_custom_seekbar_color", fUseCustomSeekBarColor);
      state.AddBool("use_seekbar_color_for_selection",
                    fUseSeekBarColorForSelection);
      state.AddData("seekbar_color", B_RGB_COLOR_TYPE, &fSeekBarColor,
                    sizeof(rgb_color));
      state.AddData("selection_color", B_RGB_COLOR_TYPE, &fSelectionColor,
                    sizeof(rgb_color));

      state.Flatten(&file);
    }
  }
}

/**
 * @brief Loads UI state from settings file.
 */
void MainWindow::LoadSettings() {
  if (!fLibraryManager || !fLibraryManager->ContentView())
    return;

  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) == B_OK) {
    settingsPath.Append("BeTon/settings");
    BFile file(settingsPath.Path(), B_READ_ONLY);
    if (file.InitCheck() == B_OK) {
      BMessage state;
      if (state.Unflatten(&file) == B_OK) {
        fLibraryManager->ContentView()->LoadState(&state);

        if (state.FindBool("show_cover_art", &fShowCoverArt) == B_OK) {
          if (fViewCoverItem)
            fViewCoverItem->SetMarked(fShowCoverArt);
          if (fViewInfoItem)
            fViewInfoItem->SetMarked(!fShowCoverArt);

          if (!fShowCoverArt && fInfoPanel) {
            fInfoPanel->Switch(InfoPanel::Info);
          }
        }

        if (state.FindString("playlist_path", &fPlaylistPath) != B_OK) {
          fPlaylistPath = "";
        }

        state.FindBool("use_custom_seekbar_color", &fUseCustomSeekBarColor);
        state.FindBool("use_seekbar_color_for_selection",
                       &fUseSeekBarColorForSelection);

        rgb_color *color;
        ssize_t size;
        if (state.FindData("seekbar_color", B_RGB_COLOR_TYPE,
                           (const void **)&color, &size) == B_OK &&
            size == sizeof(rgb_color)) {
          fSeekBarColor = *color;
        } else {
          fSeekBarColor = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
        }
        if (state.FindData("selection_color", B_RGB_COLOR_TYPE,
                           (const void **)&color, &size) == B_OK &&
            size == sizeof(rgb_color)) {
          fSelectionColor = *color;
        } else {
          fSelectionColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
        }

        if (fSelColorSystemItem)
          fSelColorSystemItem->SetMarked(!fUseSeekBarColorForSelection);
        if (fSelColorMatchItem)
          fSelColorMatchItem->SetMarked(fUseSeekBarColorForSelection);

        ApplyColors();
      }
    }
  }

  if (fPlaylistPath.IsEmpty()) {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
      path.Append("BeTon/Playlists");
      fPlaylistPath = path.Path();
    }
  }

  if (fPlaylistManager && !fPlaylistPath.IsEmpty()) {
    fPlaylistManager->SetPlaylistFolderPath(fPlaylistPath);
    fPlaylistManager->LoadAvailablePlaylists();
  }
}

/**
 * @brief Opens a file panel to select the playlist storage directory.
 */
void MainWindow::_SelectPlaylistFolder() {
  BFilePanel *panel = new BFilePanel(
      B_OPEN_PANEL, new BMessenger(this), nullptr, B_DIRECTORY_NODE, false,
      new BMessage(MSG_PLAYLIST_FOLDER_SELECTED));
  panel->Show();
}

/**
 * @brief Calculates the luminance of a color (0.0 - 1.0).
 */
static float CalculateLuminance(rgb_color color) {
  return (0.299f * color.red + 0.587f * color.green + 0.114f * color.blue) /
         255.0f;
}

/**
 * @brief Applies custom colors to SeekBar and selection.
 */
void MainWindow::ApplyColors() {
  if (fSeekBar) {
    rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);
    float bgLuminance = CalculateLuminance(panelBg);

    rgb_color bg;
    rgb_color border;

    if (bgLuminance < 0.3f) {
      bg = tint_color(panelBg, 0.85f); // Slightly lighter than black, but dark
      border = tint_color(bg, B_LIGHTEN_1_TINT);
    } else {
      bg = tint_color(panelBg, B_DARKEN_1_TINT);
      border = tint_color(bg, B_DARKEN_2_TINT);
    }

    if (fUseCustomSeekBarColor) {
      fSeekBar->SetColors(bg, fSeekBarColor, border);
    } else {
      fSeekBar->SetColors(bg, ui_color(B_CONTROL_HIGHLIGHT_COLOR), border);
    }

    if (fSearchField) {
      if (bgLuminance < 0.5f) {
        fSearchField->TextView()->SetViewColor(tint_color(panelBg, 0.80f));
        fSearchField->TextView()->SetLowColor(tint_color(panelBg, 0.80f));
        fSearchField->TextView()->SetHighColor((rgb_color){220, 220, 220, 255});
      } else {
        fSearchField->TextView()->SetViewColor(
            ui_color(B_DOCUMENT_BACKGROUND_COLOR));
        fSearchField->TextView()->SetLowColor(
            ui_color(B_DOCUMENT_BACKGROUND_COLOR));
        fSearchField->TextView()->SetHighColor(ui_color(B_DOCUMENT_TEXT_COLOR));
      }
      fSearchField->TextView()->Invalidate();
    }
  }

  rgb_color selColor =
      fUseSeekBarColorForSelection
          ? (fUseCustomSeekBarColor ? fSeekBarColor
                                    : ui_color(B_CONTROL_HIGHLIGHT_COLOR))
          : ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

  selColor.alpha = 255;

  if (fLibraryManager && fLibraryManager->ContentView()) {
    ContentColumnView *cv = fLibraryManager->ContentView();
    cv->SetColor(B_COLOR_SELECTION, selColor);

    float luminance = CalculateLuminance(selColor);
    rgb_color selTextColor =
        luminance > 0.5f
            ? (rgb_color){0, 0, 0, 255}        // Dark text on light background
            : (rgb_color){255, 255, 255, 255}; // Light text on dark background
    cv->SetColor(B_COLOR_SELECTION_TEXT, selTextColor);
  }

  if (fLibraryManager) {
    if (fLibraryManager->GenreView())
      fLibraryManager->GenreView()->SetSelectionColor(selColor);
    if (fLibraryManager->ArtistView())
      fLibraryManager->ArtistView()->SetSelectionColor(selColor);
    if (fLibraryManager->AlbumView())
      fLibraryManager->AlbumView()->SetSelectionColor(selColor);
  }

  if (fPlaylistManager && fPlaylistManager->View()) {
    fPlaylistManager->View()->SetSelectionColor(selColor);
  }
}
