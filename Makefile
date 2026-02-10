NAME = BeTon
TYPE = APP
APP_MIME_SIG = application/x-vnd.BeTon

LINKER = $(CXX)
CC = gcc
CXX = g++

SRCS = \
    CacheManager.cpp \
    ContentColumnView.cpp \
    CoverView.cpp \
    DirectoryManagerWindow.cpp \
    InfoPanel.cpp \
    LibraryViewManager.cpp \
    Main.cpp \
    MainWindow.cpp \
    MatcherWindow.cpp \
    MediaPlaybackController.cpp \
    MediaScanner.cpp \
    MetadataHandler.cpp \
    MusicBrainzClient.cpp \
    MusicSource.cpp \
    NamePrompt.cpp \
    PlaylistGeneratorWindow.cpp \
    PlaylistListView.cpp \
    PlaylistManager.cpp \
    PlaylistUtils.cpp \
    PropertiesWindow.cpp \
    SeekBarView.cpp \
    SimpleColumnView.cpp \
    SyncConflictDialog.cpp \
    SyncSettingsDialog.cpp \
    TagSync.cpp

LIBS = be translation tag tracker media columnlistview musicbrainz5 network netservices bnetapi shared localestub stdc++

SYSTEM_INCLUDE_PATHS = \
    /boot/system/develop/headers/private/interface \
    /boot/system/develop/headers/private/netservices

RDEFS = BeTon.rdef
LOCALES = de

COMPILER_FLAGS = -Wall -std=c++17

include /boot/system/develop/etc/makefile-engine
