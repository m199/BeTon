#include "PlaybackTransportController.h"

#include "ArtworkController.h"
#include "Config.h"
#include "MediaTableView.h"
#include "DLNAViewController.h"
#include "DLNAService.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "MediaItem.h"
#include "AudioPlaybackEngine.h"
#include "Messages.h"
#include "RadioStationController.h"
#include "PlaybackSeekBarView.h"
#include "ViewStateController.h"

#include <Button.h>
#include <Catalog.h>
#include <Message.h>
#include <MessageRunner.h>
#include <OS.h>
#include <Slider.h>
#include <String.h>
#include <StringView.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaybackTransportController"

/**
 * @brief Constructs playback transport controller.
 * @param window Owning main window context.
 */
PlaybackTransportController::PlaybackTransportController(MainWindow *window)
    : fWindow(window)
{
}

/**
 * @brief Toggles play/pause behavior based on current engine state.
 */
void
PlaybackTransportController::TogglePlayPause()
{
  if (!fWindow || !fWindow->fPlaybackEngine)
    return;

  if (fWindow->fPlaybackEngine->IsPlaying()) {
    if (fWindow->fIsRadioMode) {
      StopPlayback();
    } else {
      fWindow->fPlaybackEngine->Pause();
      _SetPlayIcon();
    }
  } else if (fWindow->fPlaybackEngine->IsPaused()) {
    fWindow->fPlaybackEngine->Resume();
    _SetPauseIcon();
  } else {
    fWindow->PostMessage(MSG_PLAY);
  }
  if (fWindow->fViewStateController) {
    fWindow->fViewStateController->UpdateTooltips();
  }
}

/**
 * @brief Toggles pause/resume without starting new playback.
 */
void
PlaybackTransportController::TogglePause()
{
  if (!fWindow || !fWindow->fPlaybackEngine)
    return;

  if (fWindow->fPlaybackEngine->IsPaused()) {
    fWindow->fPlaybackEngine->Resume();
  } else if (fWindow->fPlaybackEngine->IsPlaying()) {
    fWindow->fPlaybackEngine->Pause();
  }
  if (fWindow->fViewStateController) {
    fWindow->fViewStateController->UpdateTooltips();
  }
}

/**
 * @brief Stops playback and clears transport/UI now-playing state.
 */
void
PlaybackTransportController::StopPlayback()
{
  if (!fWindow)
    return;

  bool wasRadio = fWindow->fIsRadioMode;

  if (fWindow->fRadioStationController)
    fWindow->fRadioStationController->CancelQueuedPlay();

  AudioPlaybackEngine *engine = fWindow->fPlaybackEngine;
  if (engine)
    fWindow->LaunchThread("playback_stop", [engine]() { engine->Stop(); });

  fNowPlayingPath = "";
  fNowPlayingIsValid = false;
  if (fWindow->fRadioStationController) {
    fWindow->fRadioStationController->ClearActiveStation();
    fWindow->fRadioStationController->ClearActiveCover();
  }
  fWindow->UpdateFileInfo();
  if (fWindow->fUpdateRunner) {
    delete fWindow->fUpdateRunner;
    fWindow->fUpdateRunner = nullptr;
  }
  _SetPlayIcon();
  fWindow->fTitleView->SetText("");
  fWindow->fSeekBar->SetPosition(0);
  fWindow->fSeekBar->SetDuration(0);
  if (fWindow->fLibraryManager && fWindow->fLibraryManager->ContentView())
    fWindow->fLibraryManager->ContentView()->SetNowPlayingPath("");
  if (fWindow->fViewStateController) {
    fWindow->fViewStateController->UpdateTooltips();
  }
  if (wasRadio)
    fWindow->UpdateStatus(B_TRANSLATE("Radio stopped"));
}

/**
 * @brief Applies seek-bar request to playback engine.
 */
void
PlaybackTransportController::HandleSeekRequest(BMessage *msg)
{
  if (!fWindow || !fWindow->fPlaybackEngine || !msg)
    return;

#if ENABLE_MIDI_PLAYBACK
  BString lowerPath = fNowPlayingPath;
  lowerPath.ToLower();
  if (lowerPath.EndsWith(".mid") || lowerPath.EndsWith(".midi")) {
    fWindow->fSeekBar->SetPosition(fWindow->fPlaybackEngine->CurrentPosition());
    return;
  }
#endif

  bigtime_t newPos;
  if (msg->FindInt64("position", &newPos) == B_OK)
    fWindow->fPlaybackEngine->SeekTo(newPos);
}

/**
 * @brief Updates seek bar with current position and duration.
 */
void
PlaybackTransportController::UpdatePlaybackTime()
{
  if (!fWindow || !fWindow->fPlaybackEngine)
    return;

  bigtime_t dur = fWindow->fPlaybackEngine->Duration();
  if (dur <= 0) {
    fWindow->fSeekBar->SetDuration(0);
    fWindow->fSeekBar->SetPosition(0);
    return;
  }

  bigtime_t pos = fWindow->fPlaybackEngine->CurrentPosition();
  fWindow->fSeekBar->SetDuration(dur);
  fWindow->fSeekBar->SetPosition(pos);
}

/**
 * @brief Toggles mute state and synchronizes mute icon/slider.
 */
void
PlaybackTransportController::ToggleMute()
{
  if (!fWindow || !fWindow->fVolumeSlider)
    return;

  if (fWindow->fIsMuted) {
    fWindow->fIsMuted = false;
    fWindow->fBtnMute->SetIcon(fWindow->fIconMuteOn);
    fWindow->fVolumeSlider->SetValue(fWindow->fPreMuteVolume);
  } else {
    fWindow->fPreMuteVolume = fWindow->fVolumeSlider->Value();
    if (fWindow->fPreMuteVolume == 0) {
      fWindow->fPreMuteVolume = 100.0f;
    }
    fWindow->fIsMuted = true;
    fWindow->fBtnMute->SetIcon(fWindow->fIconMuteOff);
    fWindow->fVolumeSlider->SetValue(0);
  }
  fWindow->PostMessage(MSG_VOLUME_CHANGED);
  if (fWindow->fViewStateController) {
    fWindow->fViewStateController->UpdateTooltips();
  }
}

/**
 * @brief Applies current slider value to playback volume.
 */
void
PlaybackTransportController::ApplyVolumeChange()
{
  if (!fWindow)
    return;

  fWindow->fLastUserVolumeChange = system_time();
  if (fWindow->fPlaybackEngine && fWindow->fVolumeSlider) {
    if (fWindow->fIsMuted && fWindow->fVolumeSlider->Value() > 0) {
      fWindow->fIsMuted = false;
      fWindow->fBtnMute->SetIcon(fWindow->fIconMuteOn);
    }

    float linear = fWindow->fVolumeSlider->Value() / 100.0f;
    float vol = linear;
    if (!fWindow->fDlnaManager || !fWindow->fDlnaManager->IsRemoteOutput()) {
      vol = linear * linear;
    }
    fWindow->fPlaybackEngine->SetVolume(vol);
    if (fWindow->fViewStateController) {
      fWindow->fViewStateController->UpdateTooltips();
    }
  }
}

/**
 * @brief Forwards remote DLNA volume update to DLNA controller.
 */
void
PlaybackTransportController::ApplyDlnaVolumeUpdate(BMessage *msg)
{
  if (fWindow && fWindow->fDlnaController)
    fWindow->fDlnaController->ApplyVolumeUpdate(msg);
}

/**
 * @brief Handles now-playing notification and refreshes related UI.
 */
void
PlaybackTransportController::HandleNowPlaying(BMessage *msg)
{
  if (!fWindow || !msg)
    return;

  int32 index;
  BString path;
  if (msg->FindInt32("index", &index) != B_OK ||
      msg->FindString("path", &path) != B_OK) {
    return;
  }

  bool isStream = false;
  msg->FindBool("streaming", &isStream);

  _UpdateNowPlayingState(path, isStream, msg);

  BString tablePath = path;
  if (isStream && fWindow->fIsRadioMode && fWindow->fRadioStationController &&
      fWindow->fRadioStationController->HasActiveStation()) {
    tablePath = fWindow->fRadioStationController->ActiveStationUrl();
  }

  if (fWindow->fLibraryManager && fWindow->fLibraryManager->ContentView()) {
    fWindow->fLibraryManager->ContentView()->SetNowPlayingPath(tablePath);
  }

  fWindow->UpdateFileInfo();
  if (fWindow->fArtworkController)
    fWindow->fArtworkController->FetchNowPlayingCover(path, isStream);
}

/**
 * @brief Updates internal now-playing cache and title label.
 */
void
PlaybackTransportController::_UpdateNowPlayingState(const BString &path,
                                                  bool isStream,
                                                  BMessage *msg)
{
  fNowPlayingPath = path;
  fNowPlayingIsValid = false;

  BString artist;
  BString title;

  if (isStream) {
    if (fWindow->fDlnaController &&
        fWindow->fDlnaController->CurrentPlayItem()) {
      fNowPlayingItem = *fWindow->fDlnaController->CurrentPlayItem();
      fNowPlayingIsValid = true;
      artist = fNowPlayingItem.artist;
      title = fNowPlayingItem.title;
      if (fWindow->fRadioStationController) {
        fWindow->fRadioStationController->ClearActiveStation();
        fWindow->fRadioStationController->ClearActiveCover();
      }
    } else if (fWindow->fRadioStationController &&
               (!fWindow->fIsRadioMode ||
                !fWindow->fRadioStationController->HasActiveStation())) {
      fWindow->fRadioStationController->ClearActiveStation();
      fWindow->fRadioStationController->ClearActiveCover();
    }

    BString label;
    BString streamTitle;
    msg->FindString("title", &streamTitle);

#if ENABLE_DLNA_OUTPUT
    if (fWindow->fDlnaManager && fWindow->fDlnaManager->IsRemoteOutput()) {
      label = "Streaming file to Renderer";
    } else
#endif
    {
      if (!streamTitle.IsEmpty())
        label = streamTitle;
      else
        label = path;
    }
    fWindow->fTitleView->SetText(label);
    return;
  }

  if (fWindow->fRadioStationController) {
    fWindow->fRadioStationController->ClearActiveStation();
    fWindow->fRadioStationController->ClearActiveCover();
  }

  auto npIt = fWindow->fPathIndex.find(path);
  if (npIt != fWindow->fPathIndex.end()) {
    fNowPlayingItem = fWindow->fAllItems[npIt->second];
    fNowPlayingIsValid = true;
    artist = fNowPlayingItem.artist;
    title = fNowPlayingItem.title;
  }

  BString label;
  if (!artist.IsEmpty())
    label << artist << " - ";

  BString displayTitle = title.IsEmpty() ? path : title;
  label << displayTitle;

  fWindow->fTitleView->SetText(label);
}

bool
PlaybackTransportController::NowPlayingIsValid() const
{
  return fNowPlayingIsValid;
}

const MediaItem *
PlaybackTransportController::NowPlayingItem() const
{
  return fNowPlayingIsValid ? &fNowPlayingItem : nullptr;
}

const BString &
PlaybackTransportController::NowPlayingPath() const
{
  return fNowPlayingPath;
}

void
PlaybackTransportController::_SetPlayIcon()
{
  if (fWindow->fIconPlay) {
    fWindow->fBtnPlayPause->SetIcon(fWindow->fIconPlay, 0);
    fWindow->fBtnPlayPause->SetLabel("");
  }
}

void
PlaybackTransportController::_SetPauseIcon()
{
  if (fWindow->fIconPause) {
    fWindow->fBtnPlayPause->SetIcon(fWindow->fIconPause, 0);
    fWindow->fBtnPlayPause->SetLabel("");
  }
}

