NAME = BeTon
TYPE = APP
APP_MIME_SIG = application/x-vnd.BeTon

LINKER = $(CXX)
CC = gcc
CXX = g++

SRCS = \
    DirectoryManagerWindow.cpp \
    Main.cpp \
    MainWindow.cpp \
    MediaScanner.cpp \
    MediaPlaybackController.cpp \
    NamePrompt.cpp \
    PlaylistListView.cpp \
    PlaylistManager.cpp \
    SeekBarView.cpp \
    LibraryViewManager.cpp \
    CacheManager.cpp \
    ContentColumnView.cpp \
    SimpleColumnView.cpp \
    MetadataHandler.cpp \
    PlaylistUtils.cpp \
    InfoPanel.cpp \
    TagSync.cpp \
    MusicBrainzClient.cpp \
    PropertiesWindow.cpp \
    MatcherWindow.cpp \
    PlaylistGeneratorWindow.cpp \
    CoverView.cpp

LIBS = be translation tag tracker media columnlistview musicbrainz5 network netservices bnetapi shared localestub stdc++

SYSTEM_INCLUDE_PATHS = \
    /boot/system/develop/headers/private/interface \
    /boot/system/develop/headers/private/netservices

RDEFS = BeTon.rdef
LOCALES = de

COMPILER_FLAGS = -Wall -std=c++17

include /boot/system/develop/etc/makefile-engine
