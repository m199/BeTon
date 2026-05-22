#include "AudioPlaybackEngine.h"
#include "DLNAService.h"
#include "Debug.h"
#include "LocalFileHttpServer.h"
#include "Messages.h"
#include "NetworkAudioStreamIO.h"

#include <Entry.h>
#include <File.h>
#include <Message.h>
#include <MidiSynthFile.h>
#include <OS.h>
#include <Path.h>
#include <Url.h>
#include <algorithm>
#include <cstring>
#include <stdio.h>
#include <vector>

using namespace BPrivate::Network;

static BLocker sMidiHookLock("midi hook");
static AudioPlaybackEngine *sMidiHookController = nullptr;

static void EmptyMidiFileHook(int32) {}

#if ENABLE_MIDI_PLAYBACK
struct MidiTempoEvent {
  uint32 tick;
  uint32 tempo;
};

static uint16 ReadBE16(const uint8 *data) {
  return ((uint16)data[0] << 8) | data[1];
}

static uint32 ReadBE32(const uint8 *data) {
  return ((uint32)data[0] << 24) | ((uint32)data[1] << 16) |
         ((uint32)data[2] << 8) | data[3];
}

static bool ReadMidiVar(const uint8 *data, size_t size, size_t &offset,
                        uint32 &value) {
  value = 0;
  for (int i = 0; i < 4; i++) {
    if (offset >= size)
      return false;
    uint8 byte = data[offset++];
    value = (value << 7) | (byte & 0x7f);
    if ((byte & 0x80) == 0)
      return true;
  }
  return true;
}

static bool SkipMidiData(const uint8 *data, size_t size, size_t &offset,
                         size_t count) {
  if (offset + count > size)
    return false;
  offset += count;
  return true;
}

static bool ComputeMidiDuration(const entry_ref &ref, bigtime_t &duration,
                                int32 &tickDuration) {
  duration = 0;
  tickDuration = 0;

  BFile file(&ref, B_READ_ONLY);
  if (file.InitCheck() != B_OK)
    return false;

  off_t fileSize = 0;
  if (file.GetSize(&fileSize) != B_OK || fileSize < 14 ||
      fileSize > 16 * 1024 * 1024) {
    return false;
  }

  std::vector<uint8> bytes((size_t)fileSize);
  if (file.Read(bytes.data(), bytes.size()) != (ssize_t)bytes.size())
    return false;

  const uint8 *data = bytes.data();
  size_t size = bytes.size();
  size_t offset = 0;
  if (memcmp(data, "MThd", 4) != 0)
    return false;
  offset += 4;

  uint32 headerSize = ReadBE32(data + offset);
  offset += 4;
  if (headerSize < 6 || offset + headerSize > size)
    return false;

  uint16 tracks = ReadBE16(data + offset + 2);
  int16 division = (int16)ReadBE16(data + offset + 4);
  offset += headerSize;

  uint32 maxTick = 0;
  std::vector<MidiTempoEvent> tempos;
  tempos.push_back({0, 500000});

  for (uint16 track = 0; track < tracks && offset + 8 <= size; track++) {
    if (memcmp(data + offset, "MTrk", 4) != 0)
      break;
    offset += 4;
    uint32 trackSize = ReadBE32(data + offset);
    offset += 4;
    if (offset + trackSize > size)
      return false;

    size_t trackEnd = offset + trackSize;
    uint32 tick = 0;
    uint8 runningStatus = 0;
    while (offset < trackEnd) {
      uint32 delta = 0;
      if (!ReadMidiVar(data, trackEnd, offset, delta))
        return false;
      tick += delta;
      maxTick = std::max(maxTick, tick);
      if (offset >= trackEnd)
        break;

      uint8 status = data[offset++];
      if (status < 0x80) {
        if (runningStatus == 0)
          return false;
        offset--;
        status = runningStatus;
      } else if (status < 0xf0) {
        runningStatus = status;
      }

      if (status == 0xff) {
        if (offset >= trackEnd)
          return false;
        uint8 metaType = data[offset++];
        uint32 length = 0;
        if (!ReadMidiVar(data, trackEnd, offset, length))
          return false;
        if (offset + length > trackEnd)
          return false;
        if (metaType == 0x51 && length == 3) {
          uint32 tempo = ((uint32)data[offset] << 16) |
                         ((uint32)data[offset + 1] << 8) | data[offset + 2];
          if (tempo > 0)
            tempos.push_back({tick, tempo});
        }
        offset += length;
      } else if (status == 0xf0 || status == 0xf7) {
        uint32 length = 0;
        if (!ReadMidiVar(data, trackEnd, offset, length) ||
            !SkipMidiData(data, trackEnd, offset, length)) {
          return false;
        }
      } else {
        uint8 type = status & 0xf0;
        size_t dataBytes = (type == 0xc0 || type == 0xd0) ? 1 : 2;
        if (!SkipMidiData(data, trackEnd, offset, dataBytes))
          return false;
      }
    }
    offset = trackEnd;
  }

  if (maxTick == 0)
    return false;
  tickDuration = (int32)std::min(maxTick, (uint32)0x7fffffff);

  if (division < 0) {
    int8 fpsByte = (int8)((division >> 8) & 0xff);
    int fps = -fpsByte;
    int subframes = division & 0xff;
    if (fps <= 0 || subframes <= 0)
      return false;
    duration = ((bigtime_t)maxTick * 1000000LL) / (fps * subframes);
    return duration > 0;
  }

  uint16 ticksPerQuarter = (uint16)division;
  if (ticksPerQuarter == 0)
    return false;

  std::sort(tempos.begin(), tempos.end(),
            [](const MidiTempoEvent &a, const MidiTempoEvent &b) {
              return a.tick < b.tick;
            });

  uint32 lastTick = 0;
  uint32 currentTempo = 500000;
  bigtime_t total = 0;
  for (const auto &event : tempos) {
    if (event.tick > maxTick)
      break;
    if (event.tick > lastTick) {
      total += ((bigtime_t)(event.tick - lastTick) * currentTempo) /
               ticksPerQuarter;
      lastTick = event.tick;
    }
    currentTempo = event.tempo;
  }
  if (maxTick > lastTick) {
    total += ((bigtime_t)(maxTick - lastTick) * currentTempo) /
             ticksPerQuarter;
  }

  duration = total;
  return duration > 0;
}
#endif

AudioPlaybackEngine::AudioPlaybackEngine() {}

AudioPlaybackEngine::~AudioPlaybackEngine() { Stop(); }

/**
 * @brief Sets the messenger for notifying the UI about playback events.
 *
 * @param target The messenger (usually the MainWindow).
 */
void AudioPlaybackEngine::SetTarget(BMessenger target) { fTarget = target; }

#if ENABLE_DLNA_OUTPUT
void AudioPlaybackEngine::SetRemoteOutputManagers(
    DLNAService *dlna, LocalFileHttpServer *localServer) {
  fDlnaManager = dlna;
  fLocalFileHttpServer = localServer;
}
#endif

/**
 * @brief Starts the BMessageRunner that sends periodic time updates to the UI.
 */
void AudioPlaybackEngine::_StartTimeUpdates() {
  if (fUpdateRunner == nullptr && fTarget.IsValid()) {
    fUpdateRunner =
        new BMessageRunner(fTarget, new BMessage(MSG_TIME_UPDATE), 500000);
  }
}

/**
 * @brief Stops the periodic time updates.
 */
void AudioPlaybackEngine::_StopTimeUpdates() {
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
void AudioPlaybackEngine::_CleanupMedia() {

  _StopMidi(true);

  if (fNetworkStream) {
    fNetworkStream->Stop();
    snooze(50000);
  }

  if (fPlayer) {
    snooze(20000);
    delete fPlayer;
    fPlayer = nullptr;
  }

  bigtime_t callbackDeadline = system_time() + 500000;
  while (fInCallback.load(std::memory_order_relaxed) &&
         system_time() < callbackDeadline) {
    snooze(1000);
  }

  if (fTrack) {
    fMediaFile->ReleaseTrack(fTrack);
    fTrack = nullptr;
  }
  if (fMediaFile) {
    delete fMediaFile;
    fMediaFile = nullptr;
  }
  if (fNetworkStream) {
    delete fNetworkStream;
    fNetworkStream = nullptr;
  }
}

/**
 * @brief Sets the playback volume.
 *
 * @param percent Volume level between 0.0 and 1.0.
 */
void AudioPlaybackEngine::SetVolume(float vol) {
  if (vol < 0.0f)
    vol = 0.0f;
  if (vol > 1.0f)
    vol = 1.0f;
  fVolume = vol;

#if ENABLE_DLNA_OUTPUT
  if (fDlnaManager && fDlnaManager->IsRemoteOutput()) {
    static std::atomic<bool> sIsSettingVolume(false);
    if (sIsSettingVolume.exchange(true))
      return; ///< Prevent thread explosion while dragging volume slider

    int32 percent = (int32)(vol * 100);
    struct VolData {
      DLNAService *mgr;
      int32 vol;
    };
    VolData *data = new VolData{fDlnaManager, percent};
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          VolData *d = (VolData *)arg;
          d->mgr->SetRendererVolume(d->vol);
          delete d;
          sIsSettingVolume = false;
          return 0;
        },
        "dlna_vol", B_NORMAL_PRIORITY, data);
    resume_thread(tid);
    return;
  }
#endif

  if (fPlayer) {
    fPlayer->SetVolume(fVolume);
  }
  if (fMidiSynth) {
    fMidiSynth->SetVolume(fVolume);
  }
}

status_t AudioPlaybackEngine::_StartMidiAt(int32 position) {
  if (!fMidiSynth)
    return B_ERROR;

  fSuppressMidiHook.store(false, std::memory_order_relaxed);
  fMidiSynth->SetFileHook(&AudioPlaybackEngine::_MidiFileHook, 0);
  fMidiSynth->SetVolume(fVolume);
  fMidiSynth->Position(position);
  status_t st = fMidiSynth->Start();
  if (st == B_OK) {
    bigtime_t basePos = 0;
    int32 duration = fMidiTickDuration.load(std::memory_order_relaxed);
    if (duration <= 0)
      duration = fMidiSynth->Duration();
    if (duration > 0 && fDuration > 0)
      basePos = ((bigtime_t)position * fDuration) / duration;
    fMidiPosition.store(position, std::memory_order_relaxed);
    fMidiBasePos.store(basePos, std::memory_order_relaxed);
    fMidiStartTime.store(system_time(), std::memory_order_relaxed);
    fIsMidiPlaying.store(true, std::memory_order_relaxed);
    fMidiRunning.store(true, std::memory_order_relaxed);
  }
  return st;
}

void AudioPlaybackEngine::_StopMidi(bool unload) {
  BAutolock midiLock(&fMidiLock);
  if (!fMidiSynth)
    return;

  fMidiSeekSerial.fetch_add(1, std::memory_order_relaxed);
  fSuppressMidiHook.store(true, std::memory_order_relaxed);
  fMidiPosition.store(fMidiSynth->Seek(), std::memory_order_relaxed);
  _SilenceMidi();
  fMidiSynth->SetFileHook(&EmptyMidiFileHook, 0);
  if (fMidiRunning.exchange(false, std::memory_order_relaxed)) {
    fMidiSynth->Stop();
    snooze(20000);
  }

  if (!unload)
    return;

  fMidiSynth->UnloadFile();
  delete fMidiSynth;
  fMidiSynth = nullptr;
  {
    BAutolock hookLock(&sMidiHookLock);
    if (sMidiHookController == this)
      sMidiHookController = nullptr;
  }
  fIsMidiPlaying.store(false, std::memory_order_relaxed);
  fMidiRunning.store(false, std::memory_order_relaxed);
  fSuppressMidiHook.store(false, std::memory_order_relaxed);
  fMidiPosition.store(0, std::memory_order_relaxed);
  fMidiTickDuration.store(0, std::memory_order_relaxed);
  fMidiBasePos.store(0, std::memory_order_relaxed);
  fMidiStartTime.store(0, std::memory_order_relaxed);
}

void AudioPlaybackEngine::_SilenceMidi() {
  if (!fMidiSynth)
    return;

  fMidiSynth->SetVolume(0.0);
}

/**
 * @brief Plays the track at the specified index in the queue.
 *
 * Stops current playback, initializes BMediaFile and BMediaTrack,
 * sets up audio format, and starts the BSoundPlayer.
 *
 * @param trackIndex Index of the track in fQueue to play.
 */
void AudioPlaybackEngine::Play(size_t trackIndex) {
  DEBUG_PRINT("Play(%zu) called\n", trackIndex);

  fCurrentBitrate.store(0);
  fCurrentSampleRate.store(0);
  fCurrentChannels.store(0);

  Stop(true);
  snooze(10000);

  if (trackIndex >= fQueue.size()) {
    DEBUG_PRINT("index %zu out of range (queue size %zu)\n", trackIndex,
                fQueue.size());
    return;
  }

  fCurrentIdx = trackIndex;
  const char *path = fQueue[trackIndex].c_str();
  DEBUG_PRINT("opening: %s\n", path);

#if ENABLE_DLNA_OUTPUT || ENABLE_MIDI_PLAYBACK
  BString lowerPath = path;
  lowerPath.ToLower();
  const bool isMidiFile =
      lowerPath.EndsWith(".mid") || lowerPath.EndsWith(".midi");
#endif

#if ENABLE_DLNA_OUTPUT
  if (fDlnaManager && fDlnaManager->IsRemoteOutput() && !isMidiFile) {
    BString targetUrl;
    if (fLocalFileHttpServer) {
      fLocalFileHttpServer->ServeFile(BString(path), targetUrl);
    }

    entry_ref ref;
    if (get_ref_for_path(path, &ref) == B_OK) {
      BMediaFile tempFile(&ref);
      if (tempFile.InitCheck() == B_OK) {
        BMediaTrack *tempTrack = tempFile.TrackAt(0);
        if (tempTrack) {
          fDuration = tempTrack->Duration();
          tempFile.ReleaseTrack(tempTrack);
        }
      }
    }

    struct PlayData {
      DLNAService *mgr;
      BString url;
    };
    PlayData *data = new PlayData{fDlnaManager, targetUrl};
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          PlayData *d = (PlayData *)arg;
          d->mgr->SetAVTransportURI(d->url, "");
          d->mgr->RendererPlay();
          delete d;
          return 0;
        },
        "dlna_play", B_NORMAL_PRIORITY, data);
    resume_thread(tid);

    fPlaying = true;
    fIsRemotePlaying = true;
    fIsStreaming = true; ///< Prevents local duration parsing

    if (fTarget.IsValid()) {
      BMessage m(MSG_NOW_PLAYING);
      m.AddInt32("index", fCurrentIdx);
      m.AddBool("streaming", true);
      m.AddString("path", targetUrl);
      fTarget.SendMessage(&m);
    }

    _StartTimeUpdates();
    return;
  }
#endif

  entry_ref ref;
  status_t st = get_ref_for_path(path, &ref);
  if (st != B_OK) {
    DEBUG_PRINT("get_ref_for_path failed: %s (%ld)\n", strerror(st),
                (long)st);
    return;
  }

#if ENABLE_MIDI_PLAYBACK
  if (isMidiFile) {
    fMidiSynth = new BMidiSynthFile();
    st = fMidiSynth->LoadFile(&ref);
    if (st != B_OK) {
      DEBUG_PRINT("BMidiSynthFile::LoadFile failed: %s (%ld)\n",
                  strerror(st), (long)st);
      _CleanupMedia();
      return;
    }

    {
      BAutolock hookLock(&sMidiHookLock);
      sMidiHookController = this;
    }
    fIsMidiPlaying = true;
    fPlaying = true;
    fPaused = false;
    fAtEnd = false;
    fCurrentPos = 0;
    int32 midiTickDuration = 0;
    if (ComputeMidiDuration(ref, fDuration, midiTickDuration))
      fMidiTickDuration.store(midiTickDuration, std::memory_order_relaxed);
    else
      fDuration = (bigtime_t)fMidiSynth->Duration() * 1000LL;

    st = _StartMidiAt(0);
    if (st != B_OK) {
      DEBUG_PRINT("BMidiSynthFile::Start failed: %s (%ld)\n",
                  strerror(st), (long)st);
      _CleanupMedia();
      return;
    }

    if (fTarget.IsValid()) {
      BMessage m(MSG_NOW_PLAYING);
      m.AddInt32("index", (int32)trackIndex);
      m.AddString("path", path);
      fTarget.SendMessage(&m);
    }

    _StartTimeUpdates();
    return;
  }
#endif

  fMediaFile = new BMediaFile(&ref);
  st = fMediaFile->InitCheck();
  if (st != B_OK) {
    DEBUG_PRINT("BMediaFile::InitCheck failed: %s (%ld)\n",
                strerror(st), (long)st);
    _CleanupMedia();
    return;
  }

  fTrack = fMediaFile->TrackAt(0);
  if (!fTrack) {
    DEBUG_PRINT("TrackAt(0) returned nullptr\n");
    _CleanupMedia();
    return;
  }

  fDuration = fTrack->Duration();
  DEBUG_PRINT("duration: %lld us (%.2f s)\n", (long long)fDuration,
              fDuration / 1e6);

  media_format mf{};
  st = fTrack->DecodedFormat(&mf);
    
    media_format encFmt;
    fTrack->EncodedFormat(&encFmt);
    if (encFmt.type == B_MEDIA_ENCODED_AUDIO) {
        fCurrentBitrate.store(encFmt.u.encoded_audio.bit_rate / 1000);
    } else {
        fCurrentBitrate.store(0);
    }
    
    if (st == B_OK && mf.type == B_MEDIA_RAW_AUDIO) {
        fCurrentSampleRate.store(mf.u.raw_audio.frame_rate);
        fCurrentChannels.store(mf.u.raw_audio.channel_count);
    } else {
        fCurrentSampleRate.store(0);
        fCurrentChannels.store(0);
    }
    
    if (st != B_OK) {
    DEBUG_PRINT("DecodedFormat failed: %s (%ld)\n", strerror(st),
                (long)st);
    _CleanupMedia();
    return;
  }

  const media_raw_audio_format &raf = mf.u.raw_audio;
  DEBUG_PRINT("decoded: rate=%.0f Hz, channels=%d, format=0x%lx, "
              "byte_order=%s, buffer=%ld\n",
              raf.frame_rate, (int)raf.channel_count, (unsigned long)raf.format,
              raf.byte_order == B_MEDIA_BIG_ENDIAN ? "BE" : "LE",
              (long)raf.buffer_size);

  fPlayer = new BSoundPlayer(&raf, "Orchester", &_PlayBuffer, NULL, this);
  if (!fPlayer) {
    DEBUG_PRINT("BSoundPlayer new failed\n");
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

  DEBUG_PRINT("started OK\n");
}

/**
 * @brief Plays an internet radio stream from a URL.
 *
 * Uses NetworkAudioStreamIO's FFmpeg backend for HTTP/HTTPS, ICY and HLS.
 * Disables seeking and sets duration to 0 (live stream).
 *
 * @param url The stream URL to play.
 */
void AudioPlaybackEngine::PlayUrl(const BUrl &url, const char *title,
                                      int32 durationSeconds,
                                      BUrlContext *context) {
  fCurrentBitrate.store(0);
  fCurrentSampleRate.store(0);
  fCurrentChannels.store(0);

  BAutolock lock(fPlayLock);
  DEBUG_PRINT("PlayUrl(%s, duration=%ld) called\n",
              url.UrlString().String(), (long)durationSeconds);

  _StopLocked(true);
  snooze(10000);

#if ENABLE_DLNA_OUTPUT
  BString targetUrl = url.UrlString();
  BString lowerUrl = targetUrl;
  lowerUrl.ToLower();
  const bool isMidiUrl =
      lowerUrl.EndsWith(".mid") || lowerUrl.EndsWith(".midi");

  if (fDlnaManager && fDlnaManager->IsRemoteOutput() && !isMidiUrl) {
    if (!targetUrl.StartsWith("http")) {
      if (fLocalFileHttpServer) {
        fLocalFileHttpServer->ServeFile(targetUrl, targetUrl);
      }
    }
    struct PlayData {
      DLNAService *mgr;
      BString url;
      BString title;
    };
    PlayData *data = new PlayData{fDlnaManager, targetUrl, title ? title : ""};
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          PlayData *d = (PlayData *)arg;
          d->mgr->SetAVTransportURI(d->url, d->title);
          d->mgr->RendererPlay();
          delete d;
          return 0;
        },
        "dlna_play", B_NORMAL_PRIORITY, data);
    resume_thread(tid);

    fPlaying = true;
    fIsRemotePlaying = true;
    fIsStreaming = true; ///< Prevents local duration parsing
    fDuration = (bigtime_t)durationSeconds * 1000000;

    if (fTarget.IsValid()) {
      BMessage m(MSG_NOW_PLAYING);
      m.AddInt32("index", fCurrentIdx);
      m.AddBool("streaming", true);
      m.AddString("path", targetUrl);
      if (title && strlen(title) > 0)
        m.AddString("title", title);
      fTarget.SendMessage(&m);
    }

    _StartTimeUpdates();
    return;
  }

  fIsRemotePlaying = false;
#endif

  fNetworkStream =
      new NetworkAudioStreamIO(fTarget, context ? context : &fUrlContext);

  NetworkAudioStreamIO::Mode mode = NetworkAudioStreamIO::MODE_ICY;
  if (url.UrlString().IEndsWith(".m3u8"))
    mode = NetworkAudioStreamIO::MODE_HLS;
  else if (context != nullptr)
    mode = NetworkAudioStreamIO::MODE_DLNA;

  status_t st = fNetworkStream->Open(url.UrlString(), mode);
  if (st != B_OK) {
    DEBUG_PRINT("NetworkAudioStreamIO::Open failed\n");
    if (mode == NetworkAudioStreamIO::MODE_DLNA && fTarget.IsValid()) {
      BMessage m(MSG_DLNA_RESOURCE_UNAVAILABLE);
      m.AddString("path", url.UrlString().String());
      if (title && strlen(title) > 0)
        m.AddString("title", title);
      fTarget.SendMessage(&m);
    }
    _CleanupMedia();
    return;
  }

  fIsStreaming.store(durationSeconds == 0, std::memory_order_relaxed);
  fDuration = (bigtime_t)durationSeconds * 1000000;

  media_raw_audio_format raf{};
  st = fNetworkStream->WaitForFormat(&raf, 30000000);
  if (st != B_OK) {
    DEBUG_PRINT("WaitForFormat failed/timed out\n");
    if (mode == NetworkAudioStreamIO::MODE_DLNA && fTarget.IsValid()) {
      BMessage m(MSG_DLNA_RESOURCE_UNAVAILABLE);
      m.AddString("path", url.UrlString().String());
      if (title && strlen(title) > 0)
        m.AddString("title", title);
      fTarget.SendMessage(&m);
    }
    _CleanupMedia();
    fIsStreaming.store(false, std::memory_order_relaxed);
    return;
  }
  fCurrentSampleRate.store(raf.frame_rate);
  fCurrentChannels.store(raf.channel_count);
  DEBUG_PRINT("FFmpeg stream format: rate=%.0f ch=%ld\n",
              raf.frame_rate, (long)raf.channel_count);

  if (mode == NetworkAudioStreamIO::MODE_HLS) {
    size_t prebufferBytes =
        (size_t)raf.frame_rate * raf.channel_count * sizeof(float) * 2;
    status_t prebufferStatus = fNetworkStream->WaitForData(prebufferBytes,
                                                           15000000);
    DEBUG_PRINT("HLS PCM prebuffer: requested=%zu status=%ld\n",
                prebufferBytes, (long)prebufferStatus);
  }

  fPlayer = new BSoundPlayer(&raf, "Orchester", &_PlayBuffer, NULL, this);
  if (!fPlayer) {
    _CleanupMedia();
    fIsStreaming.store(false, std::memory_order_relaxed);
    return;
  }

  fPlayer->SetVolume(fVolume);
  fPlayer->Start();
  fPlayer->SetHasData(true);

  if (fTarget.IsValid()) {
    BMessage m(MSG_NOW_PLAYING);
    m.AddInt32("index", 0);
    m.AddString("path", url.UrlString().String());
    if (title && strlen(title) > 0)
      m.AddString("title", title);
    m.AddBool("streaming", true);
    fTarget.SendMessage(&m);
  }

  fAtEnd.store(false, std::memory_order_relaxed);
  fPlaying.store(true, std::memory_order_relaxed);
  fPaused.store(false, std::memory_order_relaxed);
  fCurrentPos = 0;

  _StartTimeUpdates();

  DEBUG_PRINT("stream started via NetworkAudioStreamIO\n");
}

/**
 * @brief Pauses playback.
 */
void AudioPlaybackEngine::Pause() {
#if ENABLE_DLNA_OUTPUT
  if (fIsRemotePlaying && fDlnaManager) {
    DLNAService *mgr = fDlnaManager;
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          ((DLNAService *)arg)->RendererPause();
          return 0;
        },
        "dlna_pause", B_NORMAL_PRIORITY, mgr);
    resume_thread(tid);
    fPaused.store(true, std::memory_order_relaxed);
    fPlaying.store(false, std::memory_order_relaxed);
    return;
  }
#endif

  if (fPlayer && fPlaying.load(std::memory_order_relaxed)) {
    fPlayer->Stop();
    fPaused.store(true, std::memory_order_relaxed);
    fPlaying.store(false, std::memory_order_relaxed);
  }
  if (fMidiSynth && fPlaying.load(std::memory_order_relaxed)) {
    BAutolock midiLock(&fMidiLock);
    if (!fMidiSynth || !fPlaying.load(std::memory_order_relaxed))
      return;
    bigtime_t pos = CurrentPosition();
    fCurrentPos = pos;
    fMidiPosition.store(fMidiSynth->Seek(), std::memory_order_relaxed);
    fMidiBasePos.store(pos, std::memory_order_relaxed);
    fMidiStartTime.store(0, std::memory_order_relaxed);
    _SilenceMidi();
    fMidiSynth->Pause();
    fPaused.store(true, std::memory_order_relaxed);
    fPlaying.store(false, std::memory_order_relaxed);
  }
}

/**
 * @brief Resumes paused playback.
 */
void AudioPlaybackEngine::Resume() {
#if ENABLE_DLNA_OUTPUT
  if (fIsRemotePlaying && fDlnaManager) {
    DLNAService *mgr = fDlnaManager;
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          ((DLNAService *)arg)->RendererPlay();
          return 0;
        },
        "dlna_resume", B_NORMAL_PRIORITY, mgr);
    resume_thread(tid);
    fPaused.store(false, std::memory_order_relaxed);
    fPlaying.store(true, std::memory_order_relaxed);
    return;
  }
#endif

  if (fPlayer && fPaused.load(std::memory_order_relaxed)) {
    fPlayer->Start();
    fPlayer->SetHasData(true);
    fPaused.store(false, std::memory_order_relaxed);
    fPlaying.store(true, std::memory_order_relaxed);
  }
  if (fMidiSynth && fPaused.load(std::memory_order_relaxed)) {
    BAutolock midiLock(&fMidiLock);
    if (!fMidiSynth || !fPaused.load(std::memory_order_relaxed))
      return;
    fMidiSynth->SetVolume(fVolume);
    fMidiBasePos.store(fCurrentPos.load(std::memory_order_relaxed),
                       std::memory_order_relaxed);
    fMidiStartTime.store(system_time(), std::memory_order_relaxed);
    fMidiSynth->Resume();
    fPaused.store(false, std::memory_order_relaxed);
    fPlaying.store(true, std::memory_order_relaxed);
  }
}

/**
 * @brief Stops playback completely and resets state.
 */
void AudioPlaybackEngine::Stop(bool switching) {
  BAutolock lock(fPlayLock);
  _StopLocked(switching);
}

void AudioPlaybackEngine::_StopLocked(bool switching) {
  DEBUG_PRINT("Stop(switching=%s) called\n",
              switching ? "true" : "false");

#if ENABLE_DLNA_OUTPUT
  if (fIsRemotePlaying && fDlnaManager && !switching) {
    DLNAService *mgr = fDlnaManager;
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          ((DLNAService *)arg)->RendererStop();
          return 0;
        },
        "dlna_stop", B_NORMAL_PRIORITY, mgr);
    resume_thread(tid);
    fIsRemotePlaying = false;
  }
#endif

  _StopTimeUpdates();
  fAtEnd = true;
  fPlaying.store(false, std::memory_order_relaxed);
  fPaused.store(false, std::memory_order_relaxed);
  fIsStreaming.store(false, std::memory_order_relaxed);

  if (fNetworkStream) {
      DEBUG_PRINT("stopping NetworkAudioStreamIO to unblock audio callback...\n");
      fNetworkStream->Stop();
  }

  if (fPlayer) {
    DEBUG_PRINT("stopping BSoundPlayer...\n");
    fPlayer->SetHasData(false);
    fPlayer->Stop();

    snooze(20000);
  }
  _CleanupMedia();

  fCurrentPos = 0;
  fDuration = 0;
  fCurrentIdx = 0;

  DEBUG_PRINT("Stop() finished\n");
}

/**
 * @brief Plays the next track in the queue, if available.
 */
void AudioPlaybackEngine::PlayNext() {
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
void AudioPlaybackEngine::PlayPrev() {
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
void AudioPlaybackEngine::SeekTo(bigtime_t pos) {
#if ENABLE_DLNA_OUTPUT
  if (fDlnaManager &&
      (fIsRemotePlaying.load(std::memory_order_relaxed) ||
       fDlnaManager->IsRemoteOutput())) {
    static std::atomic<bool> sIsSeeking(false);
    static std::atomic<bigtime_t> sPendingSeek(-1);
    if (sIsSeeking.exchange(true)) {
      sPendingSeek.store(pos, std::memory_order_relaxed);
      return;
    }

    struct SeekData {
      DLNAService *mgr;
      bigtime_t pos;
    };
    SeekData *data = new SeekData{fDlnaManager, pos};
    thread_id tid = spawn_thread(
        [](void *arg) -> int32 {
          SeekData *d = (SeekData *)arg;
          for (;;) {
            status_t err = d->mgr->RendererSeek(d->pos);
            DEBUG_PRINT("DLNA seek to %lld returned %ld\n",
                        (long long)d->pos, (long)err);

            bigtime_t pending =
                sPendingSeek.exchange(-1, std::memory_order_relaxed);
            if (pending < 0 || pending == d->pos)
              break;
            d->pos = pending;
          }
          delete d;
          sIsSeeking = false;
          return 0;
        },
        "dlna_seek", B_NORMAL_PRIORITY, data);
    if (tid >= 0) {
      resume_thread(tid);
    } else {
      delete data;
      sPendingSeek.store(-1, std::memory_order_relaxed);
      sIsSeeking = false;
    }
    return;
  }
#endif

  if (fMidiSynth && fIsMidiPlaying.load(std::memory_order_relaxed)) {
    return;
  }

  if (fNetworkStream &&
      fNetworkStream->GetMode() == NetworkAudioStreamIO::MODE_DLNA) {
    status_t ret = fNetworkStream->SeekToTime(pos);
    DEBUG_PRINT("DLNA local stream seek to %lld returned %ld\n",
                (long long)pos, (long)ret);
    if (ret == B_OK)
      fCurrentPos = pos;
    return;
  }

  if (!fTrack || fIsStreaming.load(std::memory_order_relaxed) || fIsMidiPlaying)
    return;

  bigtime_t newTime = pos;
  status_t ret = fTrack->SeekToTime(&newTime, B_MEDIA_SEEK_CLOSEST_BACKWARD);
  if (ret == B_OK) {
    fCurrentPos = newTime;
  }
}

bool AudioPlaybackEngine::IsPlaying() const {
  return fPlaying.load(std::memory_order_relaxed) &&
         !fPaused.load(std::memory_order_relaxed);
}

/**
 * @brief Shuts down the controller, stopping playback and cleaning up.
 */
void AudioPlaybackEngine::Shutdown() {
  BAutolock lock(fPlayLock);
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

bool AudioPlaybackEngine::IsPaused() const {
  return fPaused.load(std::memory_order_relaxed);
}

int32 AudioPlaybackEngine::CurrentIndex() const { return fCurrentIdx; }

void AudioPlaybackEngine::SetQueue(const std::vector<std::string> &queue) {
  fQueue = queue;
  fCurrentIdx = 0;
}

bigtime_t AudioPlaybackEngine::CurrentPosition() const {
#if ENABLE_DLNA_OUTPUT
  if (fDlnaManager && fDlnaManager->IsRemoteOutput())
    return fDlnaManager->GetCurrentPosition();
#endif
  if (fMidiSynth && fIsMidiPlaying.load(std::memory_order_relaxed)) {
    bigtime_t pos = fCurrentPos.load(std::memory_order_relaxed);
    if (!fPaused.load(std::memory_order_relaxed) &&
        fPlaying.load(std::memory_order_relaxed)) {
      bigtime_t start = fMidiStartTime.load(std::memory_order_relaxed);
      if (start > 0)
        pos = fMidiBasePos.load(std::memory_order_relaxed) +
              (system_time() - start);
    }
    if (fDuration > 0 && pos > fDuration)
      pos = fDuration;
    return pos;
  }
  return fCurrentPos;
}

void AudioPlaybackEngine::_MidiFileHook(int32) {
  AudioPlaybackEngine *controller = nullptr;
  {
    BAutolock hookLock(&sMidiHookLock);
    controller = sMidiHookController;
  }

  if (!controller ||
      controller->fShuttingDown.load(std::memory_order_relaxed) ||
      controller->fAtEnd.load(std::memory_order_relaxed) ||
      controller->fSuppressMidiHook.load(std::memory_order_relaxed) ||
      !controller->fIsMidiPlaying.load(std::memory_order_relaxed)) {
    return;
  }

  bool expected = false;
  if (!controller->fAtEnd.compare_exchange_strong(expected, true))
    return;

  controller->fPlaying.store(false, std::memory_order_relaxed);
  controller->fPaused.store(false, std::memory_order_relaxed);
  controller->fMidiRunning.store(false, std::memory_order_relaxed);

  if (controller->fTarget.IsValid()) {
    BMessage m(MSG_TRACK_ENDED);
    controller->fTarget.SendMessage(&m);
  }
}

bigtime_t AudioPlaybackEngine::Duration() const {
#if ENABLE_DLNA_OUTPUT
  if (fDlnaManager && fDlnaManager->IsRemoteOutput()) {
    bigtime_t dlnaDur = fDlnaManager->GetCurrentDuration();
    if (dlnaDur > 0)
      return dlnaDur;
  }
#endif
  return fDuration;
}

/**
 * @brief Static audio buffer callback for BSoundPlayer.
 *
 * Reads decoded frames from BMediaTrack and fills the audio buffer.
 * Handles end-of-track detection and notification.
 */
void AudioPlaybackEngine::_PlayBuffer(
    void *cookie, void *buffer, size_t size,
    const media_raw_audio_format &format) {
  auto *self = static_cast<AudioPlaybackEngine *>(cookie);
  if (!self) {
    memset(buffer, 0, size);
    return;
  }

  self->fInCallback.store(true, std::memory_order_relaxed);

  if (self->fShuttingDown.load(std::memory_order_relaxed) ||
      self->fAtEnd.load(std::memory_order_relaxed)) {
    memset(buffer, 0, size);
    self->fInCallback.store(false, std::memory_order_relaxed);
    return;
  }

  if (self->fNetworkStream) {
    ssize_t read = self->fNetworkStream->ReadPcm(buffer, size);
    if (read < 0) {
      memset(buffer, 0, size);
      if (self->fIsStreaming.load(std::memory_order_relaxed)) {
        /// Live stream (HLS/ICY): only signal end-of-track when the
        /// FFmpeg decode loop has truly finished. During HLS retries
        /// the loop is still running, so we just output silence.
        bool isFinished = !self->fNetworkStream->IsRequestRunning();
        DEBUG_PRINT("ReadPcm returned error (streaming), "
                    "isFinished=%d\n", (int)isFinished);
        if (isFinished) {
          bool expected = false;
          if (self->fAtEnd.compare_exchange_strong(expected, true) &&
              self->fTarget.IsValid()) {
            BMessage m(MSG_TRACK_ENDED);
            self->fTarget.SendMessage(&m);
          }
        }
      } else {
        /// Finite-length stream (DLNA file, etc.): end immediately
        bool expected = false;
        if (self->fAtEnd.compare_exchange_strong(expected, true) &&
            self->fTarget.IsValid()) {
          BMessage m(MSG_TRACK_ENDED);
          self->fTarget.SendMessage(&m);
        }
      }
    } else if (read > 0) {
      const int bytesPerSample = (format.format & 0xF);
      const int frameSize = bytesPerSample * format.channel_count;
      if (frameSize > 0) {
        int64 frames = (int64)size / frameSize;
        self->fCurrentPos +=
            (bigtime_t)((frames * 1000000LL) / (int)format.frame_rate);
      }
    }
    self->fInCallback.store(false, std::memory_order_relaxed);
    return;
  }

  if (self->fTrack == nullptr) {
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
  } else if (self->fIsStreaming.load(std::memory_order_relaxed)) {
    /// If the network request is finished, this is a real EOF
    bool isFinished =
        (self->fNetworkStream && !self->fNetworkStream->IsRequestRunning());
    bool expected = false;

    if (isFinished && self->fAtEnd.compare_exchange_strong(expected, true)) {
      if (self->fTarget.IsValid()) {
        BMessage m(MSG_TRACK_ENDED);
        self->fTarget.SendMessage(&m);
      }
    }

    memset(buffer, 0, size);
    if (!isFinished)
      snooze(10000); ///< Prevent tight loop if network is just slow
  } else {
    /// End of stream or error (local files only)
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
