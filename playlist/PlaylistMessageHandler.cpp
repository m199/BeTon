#include "PlaylistMessageHandler.h"

#include "MainWindow.h"
#include "PlaylistEditController.h"
#include "Messages.h"

#include <Message.h>

PlaylistMessageHandler::PlaylistMessageHandler(MainWindow *window)
    : fWindow(window) {}

bool PlaylistMessageHandler::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_NEW_SMART_PLAYLIST: {
    fWindow->fPlaylistEditController->ShowSmartPlaylistGenerator();
    break;
  }

  case MSG_GENERATE_PLAYLIST: {
    fWindow->fPlaylistEditController->GenerateSmartPlaylist(msg);
    break;
  }

  case MSG_MOVE_UP:
  case MSG_MOVE_DOWN: {
    fWindow->fPlaylistEditController->MovePlaylistItem(msg);
    break;
  }

  case MSG_REORDER_PLAYLIST: {
    fWindow->fPlaylistEditController->ReorderPlaylist(msg);
    break;
  }

  case B_SIMPLE_DATA: {
    fWindow->fPlaylistEditController->HandlePlaylistDrop(msg);
    break;
  }

  case MSG_ADD_TO_PLAYLIST: {
    fWindow->fPlaylistEditController->AddSelectedItemsToPlaylist(msg);
    break;
  }

  case MSG_NEW_PLAYLIST: {
    fWindow->fPlaylistEditController->PromptNewPlaylist(msg);
    break;
  }

  case MSG_SAVE_PLAYLIST_SELECTION: {
    fWindow->fPlaylistEditController->SaveSelectedPlaylist();
    break;
  }

  case MSG_SET_PLAYLIST_FOLDER: {
    fWindow->fPlaylistEditController->SelectPlaylistFolder();
    break;
  }

  case MSG_PLAYLIST_CREATED: {
    fWindow->fPlaylistEditController->CreatePlaylistFromPrompt(msg);
    break;
  }

  case MSG_NAME_PROMPT_RENAME: {
    fWindow->fPlaylistEditController->RenamePlaylistFromPrompt(msg);
    break;
  }

  case MSG_LIST_PLAYLIST: {
    fWindow->fPlaylistEditController->ReplyWithPlaylistNames(msg);
    break;
  }

  case MSG_PLAYLIST_FOLDER_SELECTED: {
    fWindow->fPlaylistEditController->HandlePlaylistFolderSelected(msg);
    break;
  }

  default:
    return false;
  }

  return true;
}
