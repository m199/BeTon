#include "MediaPlaybackController.h"
#include "Debug.h"

#include <Entry.h>
#include <Message.h>
#include <OS.h>
#include <Path.h>
#include <cstring>
#include <stdio.h>

MediaPlaybackController::MediaPlaybackController() {}

MediaPlaybackController::~MediaPlaybackController() { Stop(); }

/**
 * @brief Sets the messenger for notifying the UI about playback events.
 *
 * @param target The messenger (usually the MainWindow).
 */
void MediaPlaybackController::SetTarget(BMessenger target) { fTarget = target; }

/**
 * @brief Starts the BMessageRunner that sends periodic time updates to the UI.
 */
void MediaPlaybackController::_StartTimeUpdates() {
  if (fUpdateRunner == nullptr && fTarget.IsValid()) {
    fUpdateRunner =
        new BMessageRunner(fTarget, new BMessage(MSG_TIME_UPDATE), 500000);
  }
}

/**
 * @brief Stops the periodic time updates.
 */
void MediaPlaybackController::_StopTimeUpdates() {
  if (fUpdateRunner) {
    delete fUpdateRunner;
    fUpdateRunner = nullptr;
  }
}

/**
 * @brief Cleans up media resources (BSoundPlayer, BMediaTrack, BMediaFile).
 *
 * Ensures thread safety and proper resource deallocation order.
 */
void MediaPlaybackController::_CleanupMedia() {

  if (fPlayer) {
    // Wait briefly to allow the sound player thread to exit its callback
    snooze(20000);
    delete fPlayer;
    fPlayer = nullptr;
  }
  if (fTrack) {
    fMediaFile->ReleaseTrack(fTrack);
    fTrack = nullptr;
  }
  if (fMediaFile) {
    delete fMediaFile;
    fMediaFile = nullptr;
  }
}

/**
 * @brief Sets the playback volume.
 *
 * @param percent Volume level between 0.0 and 1.0.
 */
void MediaPlaybackController::SetVolume(float percent) {
  if (percent < 0.0f)
    percent = 0.0f;
  if (percent > 1.0f)
    percent = 1.0f;
  fVolume = percent;
  if (fPlayer) {
    fPlayer->SetVolume(fVolume);
  }
}

/**
 * @brief Plays the track at the specified index in the queue.
 *
 * Stops current playback, initializes BMediaFile and BMediaTrack,
 * sets up audio format, and starts the BSoundPlayer.
 *
 * @param trackIndex Index of the track in fQueue to play.
 */
void MediaPlaybackController::Play(size_t trackIndex) {
  DEBUG_PRINT("[Controller] Play(%zu) called\n", trackIndex);

  Stop();
  snooze(10000);

  if (trackIndex >= fQueue.size()) {
    DEBUG_PRINT("[Play2] index %zu out of range (queue size %zu)\n", trackIndex,
                fQueue.size());
    return;
  }

  fCurrentIdx = trackIndex;
  const char *path = fQueue[trackIndex].c_str();
  DEBUG_PRINT("[Play2] opening: %s\n", path);

  entry_ref ref;
  status_t st = get_ref_for_path(path, &ref);
  if (st != B_OK) {
    DEBUG_PRINT("[Play2] get_ref_for_path failed: %s (%ld)\n", strerror(st),
                (long)st);
    return;
  }

  fMediaFile = new BMediaFile(&ref);
  st = fMediaFile->InitCheck();
  if (st != B_OK) {
    DEBUG_PRINT("[Play2] BMediaFile::InitCheck failed: %s (%ld)\n",
                strerror(st), (long)st);
    _CleanupMedia();
    return;
  }

  fTrack = fMediaFile->TrackAt(0);
  if (!fTrack) {
    DEBUG_PRINT("[Play2] TrackAt(0) returned nullptr\n");
    _CleanupMedia();
    return;
  }

  fDuration = fTrack->Duration();
  DEBUG_PRINT("[Play2] duration: %lld us (%.2f s)\n", (long long)fDuration,
              fDuration / 1e6);

  media_format mf{};
  st = fTrack->DecodedFormat(&mf);
  if (st != B_OK) {
    DEBUG_PRINT("[Play2] DecodedFormat failed: %s (%ld)\n", strerror(st),
                (long)st);
    _CleanupMedia();
    return;
  }

  const media_raw_audio_format &raf = mf.u.raw_audio;
  DEBUG_PRINT("[Play2] decoded: rate=%.0f Hz, channels=%d, format=0x%lx, "
              "byte_order=%s, buffer=%ld\n",
              raf.frame_rate, (int)raf.channel_count, (unsigned long)raf.format,
              raf.byte_order == B_MEDIA_BIG_ENDIAN ? "BE" : "LE",
              (long)raf.buffer_size);

  fPlayer = new BSoundPlayer(&raf, "Orchester", &_PlayBuffer, NULL, this);
  if (!fPlayer) {
    DEBUG_PRINT("[Play2] BSoundPlayer new failed\n");
    _CleanupMedia();
    return;
  }

  fPlayer->SetVolume(fVolume);

  fPlayer->Start();
  fPlayer->SetHasData(true);

  if (fTarget.IsValid()) {
    BMessage m(MSG_NOW_PLAYING);
    m.AddInt32("index", (int32)trackIndex);
    m.AddString("path", path);
    fTarget.SendMessage(&m);
  }

  fAtEnd.store(false, std::memory_order_relaxed);
  fPlaying.store(true, std::memory_order_relaxed);
  fPaused.store(false, std::memory_order_relaxed);
  fCurrentPos = 0;

  _StartTimeUpdates();

  DEBUG_PRINT("[Play2] started OK\n");
}

/**
 * @brief Pauses playback.
 */
void MediaPlaybackController::Pause() {
  if (fPlayer && fPlaying.load(std::memory_order_relaxed)) {
    fPlayer->Stop();
    fPaused.store(true, std::memory_order_relaxed);
    fPlaying.store(false, std::memory_order_relaxed);
  }
}

/**
 * @brief Resumes paused playback.
 */
void MediaPlaybackController::Resume() {
  if (fPlayer && fPaused.load(std::memory_order_relaxed)) {
    fPlayer->Start();
    fPlayer->SetHasData(true);
    fPaused.store(false, std::memory_order_relaxed);
    fPlaying.store(true, std::memory_order_relaxed);
  }
}

/**
 * @brief Stops playback completely and resets state.
 */
void MediaPlaybackController::Stop() {
  DEBUG_PRINT("[Controller] Stop() called\n");

  _StopTimeUpdates();
  fAtEnd = true;

  if (fPlayer) {
    DEBUG_PRINT("[Controller] stopping BSoundPlayer...\n");
    fPlayer->SetHasData(false);
    fPlayer->Stop();

    snooze(20000);
  }

  _CleanupMedia();

  fPlaying.store(false, std::memory_order_relaxed);
  fPaused.store(false, std::memory_order_relaxed);
  fCurrentPos = 0;
  fDuration = 0;
  fCurrentIdx = 0;

  DEBUG_PRINT("[Controller] Stop() finished\n");
}

/**
 * @brief Plays the next track in the queue, if available.
 */
void MediaPlaybackController::PlayNext() {
  if (!fQueue.empty()) {
    if (fCurrentIdx + 1 < fQueue.size()) {
      fCurrentIdx++;
      Play(fCurrentIdx);
    } else {
      Stop();
    }
  }
}

/**
 * @brief Plays the previous track in the queue, if available.
 */
void MediaPlaybackController::PlayPrev() {
  if (!fQueue.empty()) {
    if (fCurrentIdx > 0) {
      fCurrentIdx--;
      Play(fCurrentIdx);
    } else {
      Stop();
    }
  }
}

/**
 * @brief Seeks to a specific position in the current track.
 *
 * @param pos Position in microseconds.
 */
void MediaPlaybackController::SeekTo(bigtime_t pos) {
  if (!fTrack)
    return;

  bigtime_t newTime = pos;
  status_t ret = fTrack->SeekToTime(&newTime, B_MEDIA_SEEK_CLOSEST_BACKWARD);
  if (ret == B_OK) {
    fCurrentPos = newTime;
  }
}

bool MediaPlaybackController::IsPlaying() const {
  return fPlaying.load(std::memory_order_relaxed) &&
         !fPaused.load(std::memory_order_relaxed);
}

/**
 * @brief Shuts down the controller, stopping playback and cleaning up.
 */
void MediaPlaybackController::Shutdown() {
  fShuttingDown = true;
  fAtEnd = true;
  _StopTimeUpdates();

  if (fPlayer) {
    fPlayer->SetHasData(false);
    fPlayer->Stop();
  }

  _CleanupMedia();
  fTarget = BMessenger();
  fPlaying.store(false, std::memory_order_relaxed);
  fPaused.store(false, std::memory_order_relaxed);
}

bool MediaPlaybackController::IsPaused() const {
  return fPaused.load(std::memory_order_relaxed);
}

int32 MediaPlaybackController::CurrentIndex() const { return fCurrentIdx; }

void MediaPlaybackController::SetQueue(const std::vector<std::string> &queue) {
  fQueue = queue;
  fCurrentIdx = 0;
}

bigtime_t MediaPlaybackController::CurrentPosition() const {
  return fCurrentPos;
}

bigtime_t MediaPlaybackController::Duration() const { return fDuration; }

/**
 * @brief Static audio buffer callback for BSoundPlayer.
 *
 * Reads decoded frames from BMediaTrack and fills the audio buffer.
 * Handles end-of-track detection and notification.
 */
void MediaPlaybackController::_PlayBuffer(
    void *cookie, void *buffer, size_t size,
    const media_raw_audio_format &format) {
  auto *self = static_cast<MediaPlaybackController *>(cookie);
  if (!self) {
    memset(buffer, 0, size);
    return;
  }

  self->fInCallback.store(true, std::memory_order_relaxed);

  if (self->fShuttingDown.load(std::memory_order_relaxed) ||
      self->fAtEnd.load(std::memory_order_relaxed) || self->fTrack == nullptr) {
    memset(buffer, 0, size);
    self->fInCallback.store(false, std::memory_order_relaxed);
    return;
  }

  const int bytesPerSample = (format.format & 0xF);
  const int frameSize = bytesPerSample * format.channel_count;
  int64 frames = frameSize > 0 ? (int64)(size / frameSize) : 0;

  status_t ret = B_ERROR;
  if (self->fTrack && frames > 0)
    ret = self->fTrack->ReadFrames(buffer, &frames);

  if (ret == B_OK && frames > 0) {
    self->fCurrentPos +=
        (bigtime_t)((frames * 1000000LL) / (int)format.frame_rate);
    size_t produced = (size_t)frames * frameSize;
    if (produced < size)
      memset((uint8 *)buffer + produced, 0, size - produced);
  } else {
    // End of stream or error
    bool expected = false;
    if (!self->fShuttingDown.load(std::memory_order_relaxed) &&
        !self->fStopping.load(std::memory_order_relaxed) &&
        self->fAtEnd.compare_exchange_strong(expected, true)) {

      memset(buffer, 0, size);
      if (self->fTarget.IsValid()) {
        BMessage m(MSG_TRACK_ENDED);
        self->fTarget.SendMessage(&m);
      }
    } else {
      memset(buffer, 0, size);
    }
  }

  self->fInCallback.store(false, std::memory_order_relaxed);
}
