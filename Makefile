NAME = BeTon
TYPE = APP
APP_MIME_SIG = application/x-vnd.BeTon

LINKER = $(CXX)
CC = gcc
CXX = g++

SRCS = \
    app/Main.cpp \
    app/MainWindow.cpp \
    artwork/ArtworkController.cpp \
    dlna/DLNAMessageHandler.cpp \
    dlna/DLNAViewController.cpp \
    dlna/DLNAService.cpp \
    library/MediaLibraryCache.cpp \
    library/LibraryMessageHandler.cpp \
    library/LibraryController.cpp \
    library/LibraryBrowserController.cpp \
    library/MediaLibraryScanner.cpp \
    library/MusicSourceSettings.cpp \
    metadata/MetadataMessageHandler.cpp \
    metadata/MetadataService.cpp \
    metadata/PropertiesController.cpp \
    metadata/MetadataPropertiesWindow.cpp \
    metadata/MetadataTagIO.cpp \
    musicbrainz/MusicBrainzMatcherWindow.cpp \
    musicbrainz/MusicBrainzApiClient.cpp \
    musicbrainz/MusicBrainzLookupController.cpp \
    network/LocalFileHttpServer.cpp \
    network/NetworkAudioStreamIO.cpp \
    playback/AudioPlaybackEngine.cpp \
    playback/PlaybackMessageHandler.cpp \
    playback/PlaybackTransportController.cpp \
    playback/PlaybackQueueManager.cpp \
    playback/PlaybackSeekBarView.cpp \
    playlist/PlaylistMessageHandler.cpp \
    playlist/SmartPlaylistGeneratorWindow.cpp \
    playlist/PlaylistSidebarView.cpp \
    playlist/PlaylistLibrary.cpp \
    playlist/PlaylistEditController.cpp \
    playlist/PlaylistSelectionController.cpp \
    radio/RadioMessageHandler.cpp \
    radio/RadioStationController.cpp \
    radio/RadioStationLibrary.cpp \
    radio/RadioStationEditorDialog.cpp \
    settings/SettingsController.cpp \
    sync/SyncMessageHandler.cpp \
    sync/MetadataSyncConflictDialog.cpp \
    sync/MetadataSyncController.cpp \
    sync/MusicSourceSyncSettingsDialog.cpp \
    ui/IconButtonView.cpp \
    ui/MediaTableView.cpp \
    ui/ArtworkView.cpp \
    ui/MusicSourceManagerWindow.cpp \
    ui/NowPlayingInfoPanel.cpp \
    ui/MarqueeTextView.cpp \
    ui/PlaylistNameDialog.cpp \
    ui/SingleColumnListView.cpp \
    ui/StatusBarController.cpp \
    ui/ViewMessageHandler.cpp \
    ui/ViewStateController.cpp

LIBS = be translation tag tracker media midi columnlistview musicbrainz5 network netservices bnetapi shared localestub stdc++ avformat avcodec avutil swresample

LOCAL_INCLUDE_PATHS = \
    . \
    app \
    artwork \
    dlna \
    library \
    metadata \
    musicbrainz \
    network \
    playback \
    playlist \
    radio \
    settings \
    sync \
    ui

SYSTEM_INCLUDE_PATHS = \
    /boot/system/develop/headers/private/interface \
    /boot/system/develop/headers/private/netservices \
    /boot/system/develop/headers/private/media/experimental \
    /boot/system/develop/headers/private/shared

RDEFS = BeTon.rdef
LOCALES = de

COMPILER_FLAGS = -Wall -std=c++17

include /boot/system/develop/etc/makefile-engine
