#include "PlaybackQueueManager.h"

#include "MediaTableView.h"
#include "DLNAViewController.h"
#include "Debug.h"
#include "LibraryBrowserController.h"
#include "MainWindow.h"
#include "AudioPlaybackEngine.h"
#include "Messages.h"
#include "RadioStationController.h"
#include "ViewStateController.h"

#include <Alert.h>
#include <Button.h>
#include <Catalog.h>
#include <Entry.h>
#include <StringView.h>
#include <random>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PlaybackQueueManager"

namespace {

static bool
IsUrlPath(const BString &path)
{
  return path.IStartsWith("http://") || path.IStartsWith("https://")
         || path.IStartsWith("rtsp://");
}

static bool
IsMissingLocalFile(const MediaItem *item)
{
  if (!item || item->path.IsEmpty() || IsUrlPath(item->path))
    return false;

  if (item->missing)
    return true;

  BEntry entry(item->path.String(), true);
  return entry.InitCheck() != B_OK || !entry.Exists();
}

static void
ShowFileNotFoundAlert(const BString &path)
{
  BString text;
  text << B_TRANSLATE("The file could not be found:") << "\n\n" << path;

  (new BAlert(B_TRANSLATE("File not found"), text.String(), B_TRANSLATE("OK"),
              nullptr, nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT))->Go();
}

static int32
QueueIndexForContentRow(MediaTableView *view, int32 rowIndex)
{
  int32 queueIndex = 0;
  for (int32 i = 0; i < rowIndex; i++) {
    const MediaItem *item = view->ItemAt(i);
    if (item && !item->missing)
      queueIndex++;
  }
  return queueIndex;
}

} // namespace

PlaybackQueueManager::PlaybackQueueManager(MainWindow *window)
    : fWindow(window)
{
}

/**
 * @brief Builds queue from visible content and starts selected entry.
 */
void
PlaybackQueueManager::PlaySelectedQueue()
{
  if (!fWindow || !fWindow->fLibraryManager || !fWindow->fPlaybackEngine)
    return;

  if (fWindow->fRadioStationController)
    fWindow->fRadioStationController->CancelQueuedPlay();

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  BRow *row = cv->CurrentSelection();
  int32 sel = (row ? cv->IndexOf(row) : 0);

  const MediaItem *selectedItem = row ? cv->ItemAt(sel) : nullptr;
  if (IsMissingLocalFile(selectedItem)) {
    ShowFileNotFoundAlert(selectedItem->path);
    fWindow->UpdateStatus(B_TRANSLATE("File not found."), false);
    return;
  }

  std::vector<std::string> queue;
  queue.reserve(cv->CountRows());
  for (int32 i = 0; i < cv->CountRows(); ++i) {
    const MediaItem *mi = cv->ItemAt(i);
    if (!mi || mi->missing)
      continue;

    queue.push_back(mi->path.String());
  }

  if (queue.empty())
    return;

  int32 queueIdx = QueueIndexForContentRow(cv, sel);
  DEBUG_PRINT("MSG_PLAY_BTN: restart sel=%ld\n", (long)queueIdx);
  _ClearDlnaQueue();
  fActiveSource = fWindow->fIsLibraryMode ? SourceLibrary : SourcePlaylist;
  fWindow->fPlaybackEngine->Stop();
  fWindow->fPlaybackEngine->SetQueue(queue);
  fWindow->fPlaybackEngine->Play(queueIdx);
}

/**
 * @brief Plays next item based on source and repeat/shuffle mode.
 */
void
PlaybackQueueManager::PlayNext()
{
  if (_PlayAdjacentDlna(1) || _PlayAdjacentRadio(1)) {
    _SetPlayPauseIcon();
    return;
  }

  if (fWindow && fWindow->fPlaybackEngine) {
    if (fRepeatMode == RepeatOne) {
      fWindow->fPlaybackEngine->Play(fWindow->fPlaybackEngine->CurrentIndex());
    } else if (fShuffleEnabled) {
      int32 count = fWindow->fPlaybackEngine->QueueSize();
      if (count > 0) {
        fShuffleHistory.push_back(fWindow->fPlaybackEngine->CurrentIndex());
        fWindow->fPlaybackEngine->Play(_RandomIndex(count));
      }
    } else {
      fWindow->fPlaybackEngine->PlayNext();
    }
  }
  _SetPlayPauseIcon();
}

/**
 * @brief Plays previous item based on source and shuffle history.
 */
void
PlaybackQueueManager::PlayPrevious()
{
  if (_PlayAdjacentDlna(-1) || _PlayAdjacentRadio(-1)) {
    _SetPlayPauseIcon();
    return;
  }

  if (fWindow && fWindow->fPlaybackEngine) {
    if (fShuffleEnabled) {
      if (!fShuffleHistory.empty()) {
        int32 prev = fShuffleHistory.back();
        fShuffleHistory.pop_back();
        fWindow->fPlaybackEngine->Play(prev);
      }
    } else {
      fWindow->fPlaybackEngine->PlayPrev();
    }
  }
  _SetPlayPauseIcon();
}

/**
 * @brief Advances playback after natural track end.
 */
void
PlaybackQueueManager::HandleTrackEnded()
{
  if (!fWindow)
    return;

  DLNAViewController *dlna = fWindow->fDlnaController;
  if (fActiveSource == SourceDLNA && dlna &&
      dlna->HasPlayQueue() && dlna->CurrentPlayIndex() >= 0) {
    int32 dlnaIndex = dlna->CurrentPlayIndex();
    int32 dlnaSize = dlna->PlayQueueSize();
    if (fRepeatMode == RepeatOne) {
      dlna->PlayIndex(dlnaIndex);
    } else if (fShuffleEnabled) {
      if (dlnaSize > 0) {
        fShuffleHistory.push_back(dlnaIndex);
        dlna->PlayIndex(_RandomIndex(dlnaSize));
      }
    } else if (fRepeatMode == RepeatAll) {
      if (dlnaIndex + 1 < dlnaSize) {
        dlna->PlayIndex(dlnaIndex + 1);
      } else {
        dlna->PlayIndex(0);
      }
    } else {
      if (dlnaIndex + 1 < dlnaSize) {
        dlna->PlayIndex(dlnaIndex + 1);
      } else {
        _ClearDlnaQueue();
        fWindow->PostMessage(MSG_STOP);
      }
    }
  } else if ((fActiveSource == SourceLibrary ||
              fActiveSource == SourcePlaylist) && fWindow->fPlaybackEngine) {
    if (fRepeatMode == RepeatOne) {
      fWindow->fPlaybackEngine->Play(fWindow->fPlaybackEngine->CurrentIndex());
    } else if (fShuffleEnabled) {
      int32 count = fWindow->fPlaybackEngine->QueueSize();
      if (count > 0) {
        fShuffleHistory.push_back(fWindow->fPlaybackEngine->CurrentIndex());
        fWindow->fPlaybackEngine->Play(_RandomIndex(count));
      }
    } else if (fRepeatMode == RepeatAll) {
      if (fWindow->fPlaybackEngine->CurrentIndex() + 1 <
          fWindow->fPlaybackEngine->QueueSize())
        fWindow->fPlaybackEngine->PlayNext();
      else
        fWindow->fPlaybackEngine->Play(0);
    } else {
      if (fWindow->fPlaybackEngine->CurrentIndex() + 1 <
          fWindow->fPlaybackEngine->QueueSize())
        fWindow->fPlaybackEngine->PlayNext();
      else
        fWindow->PostMessage(MSG_STOP);
    }
  }
}

/**
 * @brief Toggles shuffle mode and updates related button state.
 */
void
PlaybackQueueManager::ToggleShuffle()
{
  if (!fWindow)
    return;

  fShuffleEnabled = !fShuffleEnabled;
  fShuffleHistory.clear();
  _UpdateShuffleIcon();
  if (fWindow->fViewStateController) {
    fWindow->fViewStateController->UpdateTooltips();
  }
}

/**
 * @brief Cycles repeat mode and updates repeat icon.
 */
void
PlaybackQueueManager::ToggleRepeat()
{
  if (!fWindow)
    return;

  if (fRepeatMode == RepeatOff) {
    fRepeatMode = RepeatAll;
  } else if (fRepeatMode == RepeatAll) {
    fRepeatMode = RepeatOne;
  } else {
    fRepeatMode = RepeatOff;
  }
  _UpdateRepeatIcon();
  if (fWindow->fViewStateController) {
    fWindow->fViewStateController->UpdateTooltips();
  }
}

bool
PlaybackQueueManager::ShuffleEnabled() const
{
  return fShuffleEnabled;
}

int32
PlaybackQueueManager::RepeatModeValue() const
{
  return static_cast<int32>(fRepeatMode);
}

void
PlaybackQueueManager::SetShuffleEnabled(bool enabled)
{
  if (!fWindow)
    return;

  fShuffleEnabled = enabled;
  fShuffleHistory.clear();
  _UpdateShuffleIcon();
}

void
PlaybackQueueManager::SetRepeatModeValue(int32 mode)
{
  if (!fWindow)
    return;

  if (mode == static_cast<int32>(RepeatAll))
    fRepeatMode = RepeatAll;
  else if (mode == static_cast<int32>(RepeatOne))
    fRepeatMode = RepeatOne;
  else
    fRepeatMode = RepeatOff;
  _UpdateRepeatIcon();
}

void
PlaybackQueueManager::SetActiveSource(PlaybackSource source)
{
  fActiveSource = source;
}

void
PlaybackQueueManager::PlayCurrentSelection()
{
  if (!fWindow || !fWindow->fLibraryManager)
    return;

  MediaTableView *cv = fWindow->fLibraryManager->ContentView();
  BRow *selRow = cv->CurrentSelection();
  int32 rowIdx = (selRow ? cv->IndexOf(selRow) : -1);

  if (rowIdx < 0) {
    DEBUG_PRINT("MSG_PLAY: no selection\n");
    return;
  }

  if (fWindow->fIsRadioMode) {
    const MediaItem *mi = cv->ItemAt(rowIdx);
    if (mi && fWindow->fRadioStationController)
      fWindow->fRadioStationController->PlayStation(*mi);
  } else if (fWindow->fIsDlnaMode) {
    if (fWindow->fDlnaController)
      fWindow->fDlnaController->PlaySelection(cv, rowIdx);
  } else {
    PlayLocalSelection(cv, rowIdx);
  }
}

void
PlaybackQueueManager::PlayLocalSelection(MediaTableView *view,
                                            int32 rowIndex)
{
  if (!fWindow || !view || !fWindow->fPlaybackEngine)
    return;

  if (fWindow->fRadioStationController)
    fWindow->fRadioStationController->CancelQueuedPlay();
  const MediaItem *selectedItem = view->ItemAt(rowIndex);
  if (IsMissingLocalFile(selectedItem)) {
    ShowFileNotFoundAlert(selectedItem->path);
    fWindow->UpdateStatus(B_TRANSLATE("File not found."), false);
    return;
  }

  std::vector<std::string> queue;
  view->BuildQueue(queue);

  if (queue.empty())
    return;

  int32 queueIdx = QueueIndexForContentRow(view, rowIndex);
  DEBUG_PRINT("MSG_PLAY: start index=%ld (queue=%zu)\n",
              (long)queueIdx, queue.size());
  _ClearDlnaQueue();
  fActiveSource = fWindow->fIsLibraryMode ? SourceLibrary : SourcePlaylist;
  fWindow->fPlaybackEngine->Stop();
  fWindow->fPlaybackEngine->SetQueue(queue);
  fWindow->fPlaybackEngine->Play(queueIdx);
  _SetPlayPauseIcon();
}

int32
PlaybackQueueManager::_RandomIndex(int32 count)
{
  return std::uniform_int_distribution<int32>(0, count - 1)(fRng);
}

void
PlaybackQueueManager::_UpdateShuffleIcon()
{
  if (fShuffleEnabled && fWindow->fIconShuffleOn) {
    fWindow->fBtnShuffle->SetIcon(fWindow->fIconShuffleOn, 0);
  } else if (!fShuffleEnabled && fWindow->fIconShuffleOff) {
    fWindow->fBtnShuffle->SetIcon(fWindow->fIconShuffleOff, 0);
  }
}

void
PlaybackQueueManager::_UpdateRepeatIcon()
{
  switch (fRepeatMode) {
  case RepeatAll:
    if (fWindow->fIconRepeatAll)
      fWindow->fBtnRepeat->SetIcon(fWindow->fIconRepeatAll, 0);
    break;
  case RepeatOne:
    if (fWindow->fIconRepeatOne)
      fWindow->fBtnRepeat->SetIcon(fWindow->fIconRepeatOne, 0);
    break;
  default:
    if (fWindow->fIconRepeatOff)
      fWindow->fBtnRepeat->SetIcon(fWindow->fIconRepeatOff, 0);
    break;
  }
}

void
PlaybackQueueManager::_SetPlayPauseIcon()
{
  if (fWindow && fWindow->fPlaybackEngine && fWindow->fPlaybackEngine->IsPlaying() &&
      fWindow->fIconPause) {
    fWindow->fBtnPlayPause->SetIcon(fWindow->fIconPause, 0);
    fWindow->fBtnPlayPause->SetLabel("");
  }
}

void
PlaybackQueueManager::_ClearDlnaQueue()
{
  if (fWindow->fDlnaController)
    fWindow->fDlnaController->ClearPlayQueue();
}

bool
PlaybackQueueManager::_PlayAdjacentDlna(int32 direction)
{
  if (!fWindow || !fWindow->fDlnaController)
    return false;

  DLNAViewController *dlna = fWindow->fDlnaController;
  if (fActiveSource != SourceDLNA || !dlna->HasPlayQueue() ||
      dlna->CurrentPlayIndex() < 0)
    return false;

  int32 current = dlna->CurrentPlayIndex();
  int32 count = dlna->PlayQueueSize();
  if (count <= 0)
    return true;

  int32 target = current;
  if (fRepeatMode == RepeatOne) {
    target = current;
  } else if (fShuffleEnabled && direction > 0) {
    fShuffleHistory.push_back(current);
    target = _RandomIndex(count);
  } else if (fShuffleEnabled && direction < 0) {
    if (!fShuffleHistory.empty()) {
      target = fShuffleHistory.back();
      fShuffleHistory.pop_back();
    }
  } else if (direction > 0) {
    target = (current + 1) % count;
  } else {
    target = (current + count - 1) % count;
  }

  dlna->PlayIndex(target);
  return true;
}

bool
PlaybackQueueManager::_PlayAdjacentRadio(int32 direction)
{
  if (!fWindow || !fWindow->fRadioStationController ||
      fActiveSource != SourceRadio || !fWindow->fLibraryManager)
    return false;

  MediaTableView *view = fWindow->fLibraryManager->ContentView();
  if (!view)
    return false;

  int32 count = view->CountRows();
  if (count <= 0)
    return true;

  int32 current = -1;
  const BString &activeUrl =
      fWindow->fRadioStationController->ActiveStationUrl();
  if (!activeUrl.IsEmpty()) {
    for (int32 i = 0; i < count; ++i) {
      const MediaItem *item = view->ItemAt(i);
      if (item && item->path == activeUrl) {
        current = i;
        break;
      }
    }
  }

  if (current < 0) {
    if (BRow *row = view->CurrentSelection())
      current = view->IndexOf(row);
  }
  if (current < 0)
    current = 0;

  int32 target = current;
  if (fRepeatMode == RepeatOne) {
    target = current;
  } else if (fShuffleEnabled && direction > 0) {
    fShuffleHistory.push_back(current);
    target = _RandomIndex(count);
  } else if (fShuffleEnabled && direction < 0) {
    if (!fShuffleHistory.empty()) {
      target = fShuffleHistory.back();
      fShuffleHistory.pop_back();
    }
  } else if (direction > 0) {
    target = (current + 1) % count;
  } else {
    target = (current + count - 1) % count;
  }

  const MediaItem *station = view->ItemAt(target);
  if (!station)
    return true;

  view->DeselectAll();
  if (BRow *row = view->RowAt(target)) {
    view->AddToSelection(row);
    view->ScrollTo(row);
  }
  fWindow->fRadioStationController->PlayStation(*station);
  return true;
}
