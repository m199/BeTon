#include "MainWindow.h"
#include "ArtworkController.h"
#include "MediaTableView.h"
#include "DLNAMessageHandler.h"
#include "DLNAViewController.h"
#include "DLNAService.h"
#include "Debug.h"
#include "MusicSourceManagerWindow.h"
#include "NowPlayingInfoPanel.h"
#include "LibraryMessageHandler.h"
#include "LibraryController.h"
#include "MusicBrainzMatcherWindow.h"
#include "MetadataMessageHandler.h"
#include "MusicSourceSettings.h"
#include "PlaylistNameDialog.h"
#include "PlaybackMessageHandler.h"
#include "PlaybackTransportController.h"
#include "PlaybackQueueManager.h"
#include "PlaylistMessageHandler.h"
#include "SmartPlaylistGeneratorWindow.h"
#include "PlaylistSidebarView.h"
#include "PlaylistLibrary.h"
#include "PlaylistEditController.h"
#include "PlaylistSelectionController.h"

#include "PropertiesController.h"
#include "MetadataPropertiesWindow.h"
#include "RadioMessageHandler.h"
#include "RadioStationController.h"
#include "PlaybackSeekBarView.h"
#include "SettingsController.h"
#include "StatusBarController.h"
#include "SyncMessageHandler.h"
#include "MetadataSyncConflictDialog.h"
#include "MetadataSyncController.h"
#include "MetadataTagIO.h"
#include "ViewMessageHandler.h"
#include "ViewStateController.h"

#include <AboutWindow.h>
#include <Autolock.h>
#include <Button.h>
#include <ColumnTypes.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <FindDirectory.h>
#include <GroupLayout.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
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
#include <View.h>
#include <algorithm>
#include <random>
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
static constexpr int32 ICON_MUTE_ON = 2013;
static constexpr int32 ICON_MUTE_OFF = 2014;
///@}

bool MainWindow::_HandleViewMessage(BMessage *msg) {
  return fViewMessageHandler && fViewMessageHandler->HandleMessage(msg);
}

bool MainWindow::_HandlePlaybackMessage(BMessage *msg) {
  return fPlaybackMessageHandler && fPlaybackMessageHandler->HandleMessage(msg);
}

bool MainWindow::_HandlePlaylistMessage(BMessage *msg) {
  return fPlaylistMessageHandler && fPlaylistMessageHandler->HandleMessage(msg);
}

bool MainWindow::_HandleRadioMessage(BMessage *msg) {
  return fRadioMessageHandler && fRadioMessageHandler->HandleMessage(msg);
}

bool MainWindow::_HandleDlnaMessage(BMessage *msg) {
  return fDlnaCommandHandler && fDlnaCommandHandler->HandleMessage(msg);
}

bool MainWindow::_HandleStatusAndSearchMessage(BMessage *msg) {
  return fStatusBarController && fStatusBarController->HandleMessage(msg);
}

bool MainWindow::_HandleSyncMessage(BMessage *msg) {
  return fSyncMessageHandler && fSyncMessageHandler->HandleMessage(msg);
}

bool MainWindow::_HandleMetadataMessage(BMessage *msg) {
  return fMetadataMessageHandler && fMetadataMessageHandler->HandleMessage(msg);
}

void MainWindow::_ShowAboutWindow() {
  BAboutWindow *about = new BAboutWindow("BeTon", "application/x-vnd.BeTon");
  about->AddCopyright(2025, "Daniel Weber");
  about->AddDescription("A music library manager and player for Haiku.\n\n"
                        "Solid grey and cold\nYet it vibrates with the "
                        "sound\nConcrete sings today\n\n"
                        "Thanks to:\n"
                        "andimachovec\n"
                        "Begasus\n"
                        "Humdinger\n"
                        "niFinx\n"
                        "zuMi\n\n"
                        "Icons by zuMi\n"
                        "https://hvif-store.art/\n\n"
                        "Licensed under the MIT License.");
  about->Show();
}

void MainWindow::_ShowMatcherTestWindow() {
  std::vector<BString> files = {"File1.mp3", "File2.mp3", "File3.mp3",
                                "File4.mp3"};
  std::vector<MusicBrainzMatchTrackInfo> tracks;
  MusicBrainzMatchTrackInfo t1;
  t1.index = 1;
  t1.name = "Test Track 1";
  t1.duration = "3:30";
  tracks.push_back(t1);

  MusicBrainzMatchTrackInfo t2;
  t2.index = 2;
  t2.name = "Test Track 2";
  t2.duration = "4:45";
  tracks.push_back(t2);

  MusicBrainzMatchTrackInfo t3;
  t3.index = 3;
  t3.name = "Test Track 3";
  t3.duration = "2:20";
  tracks.push_back(t3);

  std::vector<int> map(files.size(), -1);

  new MusicBrainzMatcherWindow(files, tracks, map, BMessenger(this));
}

bool MainWindow::_HandleAppCommandMessage(BMessage *msg) {
  switch (msg->what) {
  case B_ABOUT_REQUESTED: {
    _ShowAboutWindow();
    break;
  }

  case MSG_TEST_MODE: {
    _ShowMatcherTestWindow();
    break;
  }

  case MSG_MANAGE_DIRECTORIES: {
    fLibraryController->ShowDirectoryManager();
    break;
  }

  case B_CONTROL_INVOKED: {
    _HandleControlInvoked(msg);
    break;
  }

  case MSG_REVEAL_IN_TRACKER: {
    fLibraryController->RevealInTracker(msg);
    break;
  }

  default:
    return false;
  }
  return true;
}

void MainWindow::_HandleControlInvoked(BMessage *msg) {
  void *source = nullptr;
  MediaTableView *cv = fLibraryManager->ContentView();
  if (msg->FindPointer("source", &source) == B_OK && source == cv)
    PostMessage(MSG_PLAY);
}

bool MainWindow::_HandleLibraryDataMessage(BMessage *msg) {
  return fLibraryMessageHandler && fLibraryMessageHandler->HandleMessage(msg);
}

#if ENABLE_DLNA_OUTPUT
void MainWindow::_SetOutputMenuVisible(bool visible) {
  if (!fSettingsMenu || !fOutputMenuItem)
    return;

  if (fBtnRenderer) {
    bool showRendererButton = visible && fShowRendererBtn;
    bool isRendererButtonHidden = fBtnRenderer->IsHidden();
    if (showRendererButton && isRendererButtonHidden)
      fBtnRenderer->Show();
    else if (!showRendererButton && !isRendererButtonHidden)
      fBtnRenderer->Hide();

    if (fBtnRenderer->Parent()) {
      fBtnRenderer->Parent()->InvalidateLayout(true);
      fBtnRenderer->Parent()->Relayout();
    }
    InvalidateLayout(true);
    Layout(true);
  }

  bool isVisible = fOutputMenuItem->Menu() == fSettingsMenu;
  if (visible == isVisible)
    return;

  if (visible) {
    int32 index = fSettingsMenu->CountItems();
    if (fToggleDlnaItem) {
      int32 dlnaIndex = fSettingsMenu->IndexOf(fToggleDlnaItem);
      if (dlnaIndex >= 0)
        index = dlnaIndex + 1;
    }
    fSettingsMenu->AddItem(fOutputMenuItem, index);
  } else {
    fSettingsMenu->RemoveItem(fOutputMenuItem);
  }
}
#endif

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
    DEBUG_PRINT("Icon-ID %ld not found\n", (long)id);
    return nullptr;
  }

  BRect r(0, 0, size - 1, size - 1);
  auto *bmp = new BBitmap(r, 0, B_RGBA32);
  if (BIconUtils::GetVectorIcon(static_cast<const uint8 *>(data), len, bmp) !=
      B_OK) {
    delete bmp;
    DEBUG_PRINT("Icon-ID %ld: Decoding failed\n", (long)id);
    return nullptr;
  }
  return bmp;
}

MainWindow *gMainWindow = nullptr;

extern void AddItemToPlaylist(const BString &playlist, const BString &path);

class WindowClickFilter : public BMessageFilter {
public:
  explicit WindowClickFilter(MainWindow *window)
      : BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, B_MOUSE_DOWN),
        fWindow(window) {}

  filter_result Filter(BMessage *msg, BHandler **target) override {
    if (fWindow && msg->what == B_MOUSE_DOWN) {
      if (fWindow->fLibraryManager && fWindow->fLibraryManager->ContentView()) {
        MediaTableView *view = fWindow->fLibraryManager->ContentView();
        if (view->HasActiveEditor()) {
          BView *v = dynamic_cast<BView *>(*target);
          bool clickOnEditor = false;
          for (BView *p = v; p; p = p->Parent()) {
            if (p == view->ActiveEditor()) {
              clickOnEditor = true;
              break;
            }
          }
          if (!clickOnEditor) {
            view->CommitCellEdit();
          }
        }
      }
    }
    return B_DISPATCH_MESSAGE;
  }

private:
  MainWindow *fWindow;
};

/**
 * @brief Constructs the Main Window of the application.
 *
 * Initializes the UI, managers (Playlist, Library, Cache), and playback
 * controller. Starts the initial cache load and status updates.
 */
MainWindow::MainWindow()
    : BWindow(BRect(100, 100, 400, 300), "BeTon", B_DOCUMENT_WINDOW,
              B_QUIT_ON_WINDOW_CLOSE),
      fNewFilesCount(0), fMetadataPropertiesWindow(nullptr), fPlaybackEngine(nullptr),
      fUpdateRunner(nullptr) {

  MusicSourceSettings::InitCache();

  fPlaybackEngine = new AudioPlaybackEngine();
  fPlaybackEngine->SetTarget(BMessenger(this));

  fPlaybackEngine->SetVolume(1.0f);

  fPlaylistLibrary = new PlaylistLibrary(BMessenger(this));
  fRadioStationLibrary = new RadioStationLibrary(BMessenger(this));

  fDlnaManager = new DLNAService(BMessenger(this));
#if ENABLE_DLNA_OUTPUT
  fPlaybackEngine->SetRemoteOutputManagers(fDlnaManager, &fLocalServer);
#endif

  fMediaLibraryCache = new MediaLibraryCache(BMessenger(this));
  fMediaLibraryCache->Run();

  fLibraryManager = new LibraryBrowserController(BMessenger(this));
  fMetadataService = new MetadataService(BMessenger(this));
  fArtworkController = new ArtworkController(this);
  fMetadataMessageHandler = new MetadataMessageHandler(this);
  fPlaybackMessageHandler = new PlaybackMessageHandler(this);
  fPlaybackTransportController = new PlaybackTransportController(this);
  fPlaybackQueueManager = new PlaybackQueueManager(this);
  fPlaylistMessageHandler = new PlaylistMessageHandler(this);
  fPlaylistSelectionController = new PlaylistSelectionController(this);
  fPlaylistEditController = new PlaylistEditController(this);
  fPropertiesController = new PropertiesController(this);
  fLibraryController = new LibraryController(this);
  fRadioStationController = new RadioStationController(this);
  fRadioMessageHandler = new RadioMessageHandler(this);
  fDlnaController = new DLNAViewController(this);
  fDlnaCommandHandler = new DLNAMessageHandler(this);
  fSettingsController = new SettingsController(this);
  fStatusBarController = new StatusBarController(this);
  fLibraryMessageHandler = new LibraryMessageHandler(this);
  fViewStateController = new ViewStateController(this);
  fViewMessageHandler = new ViewMessageHandler(this);
  fSyncMessageHandler = new SyncMessageHandler(this);
  fMetadataSyncController = new MetadataSyncController(this);

  fNowPlayingInfoPanel = new NowPlayingInfoPanel();
  fStatusLabel = new BStringView("status", B_TRANSLATE("Loading..."));

  fSeekBarColor = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
  fSelectionColor = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);

  _BuildUI();

  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;
  float windowWidth = fontHeight * 70;
  float windowHeight = windowWidth / 1.618f;
  ResizeTo(windowWidth, windowHeight);
  CenterOnScreen();

  float minW = std::max(580.0f, fontHeight * 41.0f);
  float minH = std::max(510.0f, fontHeight * 36.0f);
  SetSizeLimits(minW, 32768, minH, 32768);

  BMessenger(fMediaLibraryCache).SendMessage(MSG_LOAD_CACHE);

  fMbClient = new MusicBrainzApiClient("beton-app@outlook.com");

  fStatusLabel->SetText(B_TRANSLATE("Loading Music Library..."));

  RegisterWithMediaLibraryCache();

  LoadSettings();

  BMessage msg(MSG_INIT_LIBRARY);
  PostMessage(&msg);

#if ENABLE_DLNA_OUTPUT
  if (fDlnaController)
    fDlnaController->RebuildRendererMenu();
  fLocalServer.Start();
#endif

  AddCommonFilter(new WindowClickFilter(this));
}

/**
 * @brief Destructor.
 *
 * Cleans up all allocated managers, runners, and the playback controller.
 * Saves current settings before exit.
 */
MainWindow::~MainWindow() {
  fShuttingDown.store(true, std::memory_order_relaxed);
  if (fRadioStationController)
    fRadioStationController->CancelQueuedPlay();
  if (fPlaybackEngine)
    fPlaybackEngine->Stop();

  if (fRadioStationController)
    fRadioStationController->WaitForPlayThread();

  _WaitForLaunchedThreads();

  SaveSettings();
  if (fPlaybackEngine) {
    fPlaybackEngine->Shutdown();
    delete fPlaybackEngine;
    fPlaybackEngine = nullptr;
  }
  if (fMediaLibraryCache) {
    fMediaLibraryCache->Lock();
    fMediaLibraryCache->Quit();
    fMediaLibraryCache = nullptr;
  }
  delete fUpdateRunner;
  delete fBatchRunner;
  delete fSettingsController;
  delete fStatusBarController;
  delete fLibraryManager;
  delete fPlaylistLibrary;
  delete fArtworkController;
  delete fMetadataMessageHandler;
  delete fPlaybackMessageHandler;
  delete fPlaybackTransportController;
  delete fPlaybackQueueManager;
  delete fPlaylistMessageHandler;
  delete fPlaylistSelectionController;
  delete fPlaylistEditController;
  delete fPropertiesController;
  delete fLibraryController;
  delete fViewStateController;
  delete fFilePanel;
  delete fRadioMessageHandler;
  delete fRadioStationController;
  delete fRadioStationLibrary;
  delete fDlnaCommandHandler;
  delete fDlnaController;
  delete fDlnaManager;
  delete fLibraryMessageHandler;
  delete fViewMessageHandler;
  delete fSyncMessageHandler;
  delete fMetadataSyncController;
  delete fMetadataService;
  delete fMbClient;
  delete fSearchRunner;
  delete fViewsRefreshRunner;

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
#if ENABLE_DLNA_OUTPUT
  if (fOutputMenuItem && fOutputMenuItem->Menu() == nullptr)
    delete fOutputMenuItem;
  delete fIconRenderer;
#endif
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

  fileMenu->AddItem(new BMenuItem(B_TRANSLATE("Synchronize Metadata"),
                                  new BMessage(MSG_SYNC_SMART)));

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

  fSettingsMenu = new BMenu(B_TRANSLATE("Settings"));

#if ENABLE_DLNA_OUTPUT
  fOutputMenu = new BMenu(B_TRANSLATE("Output"));
  fOutputMenuItem = new BMenuItem(fOutputMenu);
  fShowRendererBtnItem = new BMenuItem(B_TRANSLATE("Show Button"),
                                       new BMessage(MSG_TOGGLE_RENDERER_BTN));
  fShowRendererBtnItem->SetMarked(fShowRendererBtn);
  fOutputMenu->AddItem(fShowRendererBtnItem);
  fOutputMenu->AddSeparatorItem();
#endif

  fToggleRadioItem = new BMenuItem(B_TRANSLATE("Enable Radio"),
                                   new BMessage(MSG_TOGGLE_RADIO));
  fToggleRadioItem->SetMarked(fRadioEnabled);
  fToggleDlnaItem =
      new BMenuItem(B_TRANSLATE("Enable DLNA"), new BMessage(MSG_TOGGLE_DLNA));
  fToggleDlnaItem->SetMarked(fDlnaEnabled);

  fSettingsMenu->AddItem(fToggleRadioItem);
  fSettingsMenu->AddItem(fToggleDlnaItem);
#if ENABLE_DLNA_OUTPUT
  _SetOutputMenuVisible(fDlnaEnabled);
#endif
  fSettingsMenu->AddSeparatorItem();

  BMenu *sidebarMenu = new BMenu(B_TRANSLATE("Sidebar"));
  fViewCoverItem =
      new BMenuItem(B_TRANSLATE("Album Cover"), new BMessage(MSG_ARTWORK_ON));
  fViewInfoItem = new BMenuItem(B_TRANSLATE("File Information"),
                                new BMessage(MSG_FILEINFO_ON));
  fViewFiltersItem =
      new BMenuItem(B_TRANSLATE("Filters"), new BMessage(MSG_TOGGLE_FILTERS));
  fViewCoverItem->SetMarked(fShowCoverArt);
  fViewInfoItem->SetMarked(fShowFileInfo);

  bool currentFilters = false;
  if (fIsLibraryMode)
    currentFilters = fShowFiltersLibrary;
  else if (fIsRadioMode)
    currentFilters = fShowFiltersRadio;
  else if (fIsDlnaMode)
    currentFilters = fShowFiltersDlna;
  else
    currentFilters = fShowFiltersPlaylist;
  fViewFiltersItem->SetMarked(currentFilters);

  fSettingsMenu->AddItem(fViewFiltersItem);

  sidebarMenu->AddItem(fViewCoverItem);
  sidebarMenu->AddItem(fViewInfoItem);
  fSettingsMenu->AddItem(sidebarMenu);

  BMenu *selColorMenu = new BMenu(B_TRANSLATE("Selection Color"));
  fSelColorSystemItem = new BMenuItem(B_TRANSLATE("System Default"),
                                      new BMessage(MSG_SELECTION_COLOR_SYSTEM));
  fSelColorMatchItem = new BMenuItem(B_TRANSLATE("Match SeekBar"),
                                     new BMessage(MSG_SELECTION_COLOR_MATCH));
  fSelColorSystemItem->SetMarked(!fUseSeekBarColorForSelection);
  fSelColorMatchItem->SetMarked(fUseSeekBarColorForSelection);
  selColorMenu->AddItem(fSelColorSystemItem);
  selColorMenu->AddItem(fSelColorMatchItem);
  fSettingsMenu->AddItem(selColorMenu);

  BMenu *tooltipsMenu = new BMenu(B_TRANSLATE("Tooltips"));
  fTooltipsOnItem =
      new BMenuItem(B_TRANSLATE("On"), new BMessage(MSG_TOOLTIPS_ON));
  fTooltipsOffItem =
      new BMenuItem(B_TRANSLATE("Off"), new BMessage(MSG_TOOLTIPS_OFF));
  fTooltipsOffItem->SetMarked(true);
  tooltipsMenu->AddItem(fTooltipsOnItem);
  tooltipsMenu->AddItem(fTooltipsOffItem);
  fSettingsMenu->AddItem(tooltipsMenu);

  fFastEditItem = new BMenuItem(B_TRANSLATE("Enable Fast Edit"),
                                new BMessage(MSG_TOGGLE_FAST_EDIT));
  fFastEditItem->SetMarked(fFastEditEnabled);
  fSettingsMenu->AddItem(fFastEditItem);

  fMenuBar->AddItem(fSettingsMenu);

  BMenu *helpMenu = new BMenu(B_TRANSLATE("Help"));
  helpMenu->AddItem(new BMenuItem(B_TRANSLATE("About BeTon" B_UTF8_ELLIPSIS),
                                  new BMessage(B_ABOUT_REQUESTED)));
  fMenuBar->AddItem(helpMenu);

  fSeekBar = new PlaybackSeekBarView("seekbar");

  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;
  float barHeight = std::max(22.0f, fontHeight * 1.5f);

  fSeekBar->SetExplicitMinSize(BSize(fontHeight * 14, barHeight));
  fSeekBar->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, barHeight));

  fTitleView = new MarqueeTextView("titleView");
  fTitleView->SetText(B_TRANSLATE("No Title"));
  fTitleView->SetExplicitMinSize(BSize(10, barHeight));
  fTitleView->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, barHeight));

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
  fIconMuteOn = LoadIconFromResource(ICON_MUTE_ON, iconSize);
  fIconMuteOff = LoadIconFromResource(ICON_MUTE_OFF, iconSize);

  fBtnMute = new IconButtonView("mute_icon", fIconMuteOn, new BMessage(MSG_MUTE_TOGGLE));
#if ENABLE_DLNA_OUTPUT
  fIconRenderer = LoadIconFromResource(1005, iconSize);
#endif

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
  fBtnPlayPause->SetLabel("");

  fBtnPrev->SetExplicitSize(buttonSize);
  fBtnPlayPause->SetExplicitSize(buttonSize);
  fBtnShuffle->SetExplicitSize(buttonSize);
  fBtnRepeat->SetExplicitSize(buttonSize);
  fBtnStop->SetExplicitSize(buttonSize);
  fBtnNext->SetExplicitSize(buttonSize);
  fBtnMute->SetExplicitSize(BSize(iconSize * 1.2f, size));

  if (fShowTooltips && fViewStateController) {
    fViewStateController->UpdateTooltips();
  }

  fVolumeSlider = new BSlider("volume", nullptr, nullptr, 0, 100, B_HORIZONTAL);
  fVolumeSlider->SetModificationMessage(new BMessage(MSG_VOLUME_CHANGED));
  fVolumeSlider->SetValue(100);
  fVolumeSlider->SetExplicitMinSize(BSize(fontHeight * 6, B_SIZE_UNSET));
  fVolumeSlider->SetExplicitMaxSize(BSize(fontHeight * 8, B_SIZE_UNSET));

  fSearchField =
      new BTextControl("search", "", "", new BMessage(MSG_SEARCH_MODIFY));
  fSearchField->SetModificationMessage(new BMessage(MSG_SEARCH_MODIFY));
  fSearchField->SetTarget(this);
  fSearchField->SetExplicitSize(BSize(fontHeight * 15, B_SIZE_UNSET));

  fDlnaServerMenu = new BPopUpMenu("");
  fDlnaServerField = new BMenuField("dlna_server", nullptr, fDlnaServerMenu);
  fDlnaServerField->SetExplicitMinSize(BSize(fontHeight * 10, B_SIZE_UNSET));
  fDlnaServerField->SetExplicitMaxSize(BSize(fontHeight * 16, B_SIZE_UNSET));
  fDlnaServerField->Hide();
#if ENABLE_DLNA_OUTPUT
  fRendererMenu = new BPopUpMenu("");
  BMessage *localeMsg = new BMessage(MSG_RENDERER_SELECTED);
  localeMsg->AddString("uuid", "");
  BMenuItem *localeItem = new BMenuItem(B_TRANSLATE("Locale"), localeMsg);
  localeItem->SetMarked(true);
  fRendererMenu->AddItem(localeItem);
  fRendererMenu->SetTargetForItems(this);

  fBtnRenderer = new BButton("", new BMessage(MSG_SHOW_RENDERER_MENU));
  if (fIconRenderer)
    fBtnRenderer->SetIcon(fIconRenderer, 0);
  fBtnRenderer->SetExplicitSize(buttonSize);
#endif

  BScrollView *playlistScroll = new BScrollView(
      "playlist_scroll", fPlaylistLibrary->View(), B_WILL_DRAW, false, true);
  playlistScroll->SetExplicitMinSize(BSize(0, 0));

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
  contentScroll->SetExplicitMinSize(BSize(0, 0));

  genreScroll->SetExplicitMinSize(BSize(0, 0));
  artistScroll->SetExplicitMinSize(BSize(0, 0));
  albumScroll->SetExplicitMinSize(BSize(0, 0));

  BGroupView *sidebarGroup = new BGroupView(B_VERTICAL, 0);
  sidebarGroup->SetExplicitMinSize(BSize(fontHeight * 14, B_SIZE_UNSET));
  sidebarGroup->SetExplicitMaxSize(BSize(fontHeight * 14, B_SIZE_UNSET));

  BLayoutBuilder::Group<>(sidebarGroup)
      .Add(playlistScroll, 1.0f)
      .Add(fNowPlayingInfoPanel, 0.0f);

  fFilterGroup = new BGroupView(B_HORIZONTAL, kItemSpacing);
  BLayoutBuilder::Group<>(fFilterGroup)
      .Add(genreScroll, 1.0f)
      .Add(artistScroll, 1.0f)
      .Add(albumScroll, 1.0f)
      .End();

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
      .Add(fBtnMute)
      .Add(fVolumeSlider)
#if ENABLE_DLNA_OUTPUT
      .AddStrut(kItemSpacing)
      .Add(fBtnRenderer)
#endif
      .AddStrut(kItemSpacing)
      .Add(fDlnaServerField)
      .AddGlue()
      .Add(fSearchField)
      .End()

      .AddSplit(B_HORIZONTAL, kGroupSpacing)

      .Add(sidebarGroup, 0.25f)

      .AddGroup(B_VERTICAL, kItemSpacing, 0.75f)

      .Add(fFilterGroup, 1.0f)
      .Add(contentScroll, 2.0f)
      .End()
      .End()

      .AddGroup(B_HORIZONTAL, 0)
      .Add(fStatusLabel)
      .AddGlue()
      .End()
      .End();
}

void MainWindow::WindowActivated(bool active) {
  BWindow::WindowActivated(active);
  if (!active) {
    if (fLibraryManager && fLibraryManager->ContentView()) {
      fLibraryManager->ContentView()->CommitCellEdit();
    }
  }
}

/**
 * @brief Main message loop handler.
 *
 * Handles all application messages, including:
 * Playback control (Play, Pause, stop, Next, Prev)
 * Library updates & scanning
 * Playlist management
 * Metadata updates & MusicBrainz integration
 * UI selection changes
 *
 * @param msg The received message.
 */
void MainWindow::MessageReceived(BMessage *msg) {
  if (_HandleViewMessage(msg))
    return;
  if (_HandlePlaybackMessage(msg))
    return;

  if (_HandlePlaylistMessage(msg))
    return;

  if (_HandleRadioMessage(msg))
    return;

  if (_HandleDlnaMessage(msg))
    return;

  if (_HandleStatusAndSearchMessage(msg))
    return;

  if (_HandleSyncMessage(msg))
    return;

  if (_HandleMetadataMessage(msg))
    return;

  if (_HandleAppCommandMessage(msg))
    return;

  if (_HandleLibraryDataMessage(msg))
    return;

  BWindow::MessageReceived(msg);
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

  BAutolock lock(&fWorkerThreadsLock);
  if (fShuttingDown.load(std::memory_order_relaxed)) {
    delete funcPtr;
    return B_NOT_ALLOWED;
  }

  thread_id thread = spawn_thread(_ThreadEntry, name, B_NORMAL_PRIORITY, funcPtr);
  if (thread >= 0) {
    fWorkerThreads.push_back(thread);
    resume_thread(thread);
  } else {
    delete funcPtr;
  }
  return thread;
}

void MainWindow::_WaitForLaunchedThreads() {
  std::vector<thread_id> threads;
  {
    BAutolock lock(&fWorkerThreadsLock);
    threads.swap(fWorkerThreads);
  }

  thread_id current = find_thread(nullptr);
  for (thread_id thread : threads) {
    if (thread < 0 || thread == current)
      continue;
    status_t exit;
    wait_for_thread(thread, &exit);
  }
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
void MainWindow::UpdateFilteredViews(bool preserveScroll) {
  if (fLibraryManager) {
    const auto &items = (fIsRadioMode || fIsDlnaMode) ? fRadioItems : fAllItems;
    BString context = "Library";
    if (fIsRadioMode)
      context = "Radio";
    else if (fIsDlnaMode)
      context = "DLNA";
    else if (!fIsLibraryMode)
      context = fCurrentPlaylistName;

    fLibraryManager->UpdateFilteredViews(
        items, fIsLibraryMode || fIsRadioMode || fIsDlnaMode, context,
        fSearchField->Text(), preserveScroll);
    if (!fIsRadioMode && !fIsDlnaMode) {
      if (fStatusBarController)
        fStatusBarController->UpdateLibraryStatus();
    }
  }
}

/**
 * @brief Registers this window as a listener for MediaLibraryCache updates.
 */
void MainWindow::RegisterWithMediaLibraryCache() {
  BMessage reg(MSG_REGISTER_TARGET);
  reg.AddMessenger("target", BMessenger(this));
  BMessenger(fMediaLibraryCache).SendMessage(&reg);

  DEBUG_PRINT("registered as UI target at MediaLibraryCache\n");
}

/**
 * @brief Updates the "Info" side panel with details of the selected item.
 *
 * Prioritizes data from the MediaItem (which includes BFS attributes and tags).
 * Only reads technical audio properties from file if missing in
 * MediaItem.
 */
void MainWindow::UpdateFileInfo() {
  const MediaItem *mi = nullptr;
  bool isPlaying =
      fPlaybackEngine && (fPlaybackEngine->IsPlaying() || fPlaybackEngine->IsPaused());

  bool isRadio =
      isPlaying && fRadioStationController && fRadioStationController->HasActiveStation();

  if (isPlaying && !isRadio && fPlaybackTransportController &&
      fPlaybackTransportController->NowPlayingIsValid()) {
    mi = fPlaybackTransportController->NowPlayingItem();
  }

  if (!mi && (!isPlaying || !isRadio)) {
    if (fLibraryManager && fLibraryManager->ContentView()) {
      mi = fLibraryManager->ContentView()->SelectedItem();
    }
  }

  if (!mi && !isRadio) {
    if (fNowPlayingInfoPanel) {
      fNowPlayingInfoPanel->SetFileInfo("");
      fNowPlayingInfoPanel->SetTechInfo("");
    }
    return;
  }

  BString techStr;
  int32 bitrate = mi ? mi->bitrate : 0;
  int32 sampleRate = mi ? mi->sampleRate : 0;
  int32 channels = mi ? mi->channels : 0;

  if (bitrate == 0 && fPlaybackEngine) {
    bitrate = fPlaybackEngine->CurrentBitrate();
  }
  if (sampleRate == 0 && fPlaybackEngine) {
    sampleRate = fPlaybackEngine->CurrentSampleRate();
  }
  if (channels == 0 && fPlaybackEngine) {
    channels = fPlaybackEngine->CurrentChannels();
  }

  if (bitrate > 0) {
    techStr << bitrate << " kbps";
  }
  if (sampleRate > 0) {
    if (!techStr.IsEmpty()) techStr << " | ";
    techStr << (sampleRate / 1000.0f) << " kHz";
  }
  if (channels > 0) {
    if (!techStr.IsEmpty()) techStr << " | ";
    techStr << (channels == 1 ? "Mono" : (channels == 2 ? "Stereo" : "Surround"));
  }

  BString ext;
  if (mi && !mi->path.IsEmpty()) {
    int32 extIdx = mi->path.FindLast('.');
    if (extIdx != B_ERROR && extIdx < mi->path.Length() - 1) {
      mi->path.CopyInto(ext, extIdx + 1, mi->path.Length() - extIdx - 1);
      int32 queryIdx = ext.FindFirst('?');
      if (queryIdx != B_ERROR)
        ext.Truncate(queryIdx);
      ext.ToUpper();
    }
  } else if (isRadio) {
    ext = "STREAM";
  }

  if (!ext.IsEmpty()) {
    if (!techStr.IsEmpty()) techStr << " | ";
    techStr << ext;
  }

  if (fNowPlayingInfoPanel) {
    if (isRadio) {
      fNowPlayingInfoPanel->SetBoxLabel(B_TRANSLATE("Now Playing (Radio)"));
      BString artist = fRadioStationController->ActiveStreamArtist();
      BString title = fRadioStationController->ActiveStreamTrackTitle();
      BString album = fRadioStationController->ActiveStreamAlbum();

      if (artist.IsEmpty() && title.IsEmpty()) {
        artist =
            fRadioStationController->ActiveStreamTitle() == "(Fetching metadata...)"
                ? ""
                : fRadioStationController->ActiveStreamTitle();
        title = fRadioStationController->ActiveStationName();
      }
      if (artist.IsEmpty() && !title.IsEmpty())
        artist = fRadioStationController->ActiveStationName();
      if (album.IsEmpty())
        album = fRadioStationController->ActiveStationGenre();

      fNowPlayingInfoPanel->SetTags(artist, title, album);
    } else if (mi) {
      if (isPlaying) {
        if (fDlnaController && fDlnaController->CurrentPlayIndex() >= 0) {
          fNowPlayingInfoPanel->SetBoxLabel(B_TRANSLATE("Now Playing (DLNA)"));
        } else {
          fNowPlayingInfoPanel->SetBoxLabel(B_TRANSLATE("Now Playing"));
        }
      } else {
        fNowPlayingInfoPanel->SetBoxLabel(B_TRANSLATE("File Information"));
      }

      BString artist = mi->artist.IsEmpty() ? "-" : mi->artist;
      BString album = mi->album.IsEmpty() ? "-" : mi->album;
      BString title = mi->title.IsEmpty() ? "-" : mi->title;
      int32 year = mi->year;

      if (year > 0)
        album << " (" << year << ")";

      fNowPlayingInfoPanel->SetTags(artist, title, album);
    }

    fNowPlayingInfoPanel->SetTechInfo(techStr);
  }
}

/**
 * @brief Helper to get the full path of an item in the Content View.
 */
BString MainWindow::GetPathForContentItem(int index) {
  MediaTableView *cv = fLibraryManager->ContentView();
  if (!cv)
    return "";

  const MediaItem *item = cv->ItemAt(index);
  if (!item)
    return "";

  return item->path;
}

/**
 * @brief Retrieves list of playlist names from PlaylistLibrary.
 */
void MainWindow::GetPlaylistNames(BMessage &out, bool onlyWritable) const {
  if (fPlaylistLibrary) {
    fPlaylistLibrary->GetPlaylistNames(out, onlyWritable);
  }
}

/**
 * @brief Adds an entry to a playlist.
 */
void MainWindow::AddPlaylistEntry(const BString &playlistName,
                                  const BString &label,
                                  const BString &fullPath) {
  if (fPlaylistLibrary) {
    fPlaylistLibrary->AddPlaylistEntry(label, fullPath);
  }
}
/**
 * @brief Updates the status bar text.
 *
 * @param text The message to display.
 * @param isPermanent If false, the message will be reset after a timeout.
 */
void MainWindow::UpdateStatus(const BString &text, bool isPermanent) {
  if (fStatusBarController)
    fStatusBarController->UpdateStatus(text, isPermanent);
}

/**
 * @brief Saves current UI state (columns, playlist path, etc.) to settings
 * file.
 */
void MainWindow::SaveSettings() {
  if (fSettingsController)
    fSettingsController->SaveSettings();
}

/**
 * @brief Loads UI state from settings file.
 */
void MainWindow::LoadSettings() {
  if (fSettingsController)
    fSettingsController->LoadSettings();
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
  rgb_color panelBg = ui_color(B_PANEL_BACKGROUND_COLOR);

  if (fMenuBar) {
    fMenuBar->SetViewColor(panelBg);
    fMenuBar->SetLowColor(panelBg);
    fMenuBar->Invalidate();
  }

  if (fTitleView) {
    fTitleView->UpdateSystemColors();
  }

  if (fBtnMute) {
    fBtnMute->UpdateSystemColors();
  }

  if (fSeekBar) {
    float bgLuminance = CalculateLuminance(panelBg);

    rgb_color bg;
    rgb_color border;

    if (bgLuminance < 0.3f) {
      bg = tint_color(panelBg, 0.85f); // Slightly lighter than black, but dark enough ;)
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
    MediaTableView *cv = fLibraryManager->ContentView();
    cv->SetColor(B_COLOR_SELECTION, selColor);

    float luminance = CalculateLuminance(selColor);
    rgb_color selTextColor = luminance > 0.5f ? (rgb_color){0, 0, 0, 255}
                                              : (rgb_color){255, 255, 255, 255};
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

  if (fPlaylistLibrary && fPlaylistLibrary->View()) {
    fPlaylistLibrary->View()->SetSelectionColor(selColor);
  }
}
