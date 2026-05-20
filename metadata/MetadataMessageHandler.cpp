#include "MetadataMessageHandler.h"

#include "ArtworkController.h"
#include "MainWindow.h"
#include "Messages.h"
#include "MusicBrainzLookupController.h"
#include "PropertiesController.h"

#include <Message.h>

/**
 * @brief Constructs metadata message handler and MusicBrainz sub-controller.
 * @param window Owning main window context.
 */
MetadataMessageHandler::MetadataMessageHandler(MainWindow *window)
    : fWindow(window),
      fMusicBrainzLookupController(new MusicBrainzLookupController(window)) {}

/**
 * @brief Destroys metadata message handler.
 */
MetadataMessageHandler::~MetadataMessageHandler() {
  delete fMusicBrainzLookupController;
}

/**
 * @brief Dispatches metadata messages to properties/artwork/MusicBrainz logic.
 * @param msg Incoming message.
 * @return `true` if handled by this router.
 */
bool MetadataMessageHandler::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_COVER_APPLY_ALBUM: {
    if (fWindow->fArtworkController)
      fWindow->fArtworkController->ApplyAlbumCover(msg);
    break;
  }

  case MSG_COVER_CLEAR_ALBUM: {
    if (fWindow->fArtworkController)
      fWindow->fArtworkController->ClearAlbumCover(msg);
    break;
  }

  case MSG_COVER_DROPPED_APPLY_ALL: {
    if (fWindow->fArtworkController)
      fWindow->fArtworkController->ApplyDroppedCoverToAll(msg);
    break;
  }

  case MSG_PROP_APPLY:
  case MSG_PROP_SAVE: {
    fWindow->fPropertiesController->SavePropertyTags(msg);
    break;
  }

  case MSG_PROP_REQUEST_COVER: {
    if (fWindow->fArtworkController)
      fWindow->fArtworkController->RequestEmbeddedCover(msg);
    break;
  }

  case MSG_PROP_CLOSED: {
    fWindow->fMetadataPropertiesWindow = nullptr;
    break;
  }

  case MSG_PROPERTIES: {
    fWindow->fPropertiesController->OpenMetadataPropertiesWindow(msg);
    break;
  }

  case MSG_MB_SEARCH:
  case MSG_MB_CANCEL:
  case MSG_MATCH_RESULT:
  case MSG_MB_SEARCH_COMPLETE:
  case MSG_MB_APPLY:
  case MSG_MB_APPLY_ALBUM:
  case MSG_COVER_FETCH_MB: {
    return fMusicBrainzLookupController &&
           fMusicBrainzLookupController->HandleMessage(msg);
  }

  case MSG_SET_RATING: {
    fWindow->fPropertiesController->SetRating(msg);
    break;
  }

  default:
    return false;
  }

  return true;
}
