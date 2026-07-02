#include "NetworkAudioStreamIO.h"
#include "Debug.h"
#include "Messages.h"

#include <Locker.h>
#include <MediaFile.h>
#include <MediaIO.h>
#include <MediaTrack.h>
#include <Message.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace BPrivate::Network;

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace {

std::atomic<bool> sFfmpegNetworkInitialized(false);

void EnsureFfmpegNetworkInitialized() {
  bool expected = false;
  if (sFfmpegNetworkInitialized.compare_exchange_strong(expected, true))
    avformat_network_init();
}

void MaybeDeinitFfmpegNetwork() {
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
  // Deprecated after Haiku R1/beta6: beta5 can crash in stream threads when
  // FFmpeg's global network state is repeatedly init/deinit'ed.
  // Keep it initialized for the process lifetime while beta5 is supported.
#else
  bool expected = true;
  if (sFfmpegNetworkInitialized.compare_exchange_strong(expected, false))
    avformat_network_deinit();
#endif
}

uint32 ReadBE32(const uint8 *data) {
  return ((uint32)data[0] << 24) | ((uint32)data[1] << 16) |
         ((uint32)data[2] << 8) | data[3];
}

uint64 ReadBE64(const uint8 *data) {
  return ((uint64)ReadBE32(data) << 32) | ReadBE32(data + 4);
}

uint32 ReadSyncSafe32(const uint8 *data) {
  return ((uint32)(data[0] & 0x7f) << 21) |
         ((uint32)(data[1] & 0x7f) << 14) |
         ((uint32)(data[2] & 0x7f) << 7) | (uint32)(data[3] & 0x7f);
}

bool IsLikelyId3(const uint8 *data, size_t size) {
  return data && size >= 10 && memcmp(data, "ID3", 3) == 0 && data[3] < 5 &&
         data[4] == 0 && (data[6] & 0x80) == 0 && (data[7] & 0x80) == 0 &&
         (data[8] & 0x80) == 0 && (data[9] & 0x80) == 0;
}

size_t Id3TagTotalSize(const uint8 *data, size_t size) {
  if (!IsLikelyId3(data, size))
    return 0;

  size_t total = 10 + ReadSyncSafe32(data + 6);
  if (data[5] & 0x10)
    total += 10;
  return total <= size ? total : 0;
}

void Unsynchronise(std::vector<uint8> &data) {
  size_t out = 0;
  for (size_t in = 0; in < data.size(); ++in) {
    data[out++] = data[in];
    if (data[in] == 0xff && in + 1 < data.size() && data[in + 1] == 0x00)
      ++in;
  }
  data.resize(out);
}

void AppendUtf8Codepoint(BString &out, uint32 cp) {
  if (cp == 0)
    return;
  if (cp < 0x80) {
    char byte = (char)cp;
    out.Append(&byte, 1);
  } else if (cp < 0x800) {
    char bytes[2] = {(char)(0xc0 | (cp >> 6)), (char)(0x80 | (cp & 0x3f))};
    out.Append(bytes, 2);
  } else if (cp < 0x10000) {
    char bytes[3] = {(char)(0xe0 | (cp >> 12)),
                     (char)(0x80 | ((cp >> 6) & 0x3f)),
                     (char)(0x80 | (cp & 0x3f))};
    out.Append(bytes, 3);
  } else if (cp <= 0x10ffff) {
    char bytes[4] = {(char)(0xf0 | (cp >> 18)),
                     (char)(0x80 | ((cp >> 12) & 0x3f)),
                     (char)(0x80 | ((cp >> 6) & 0x3f)),
                     (char)(0x80 | (cp & 0x3f))};
    out.Append(bytes, 4);
  }
}

BString DecodeId3Text(const uint8 *data, size_t size) {
  BString text;
  if (!data || size == 0)
    return text;

  uint8 encoding = data[0];
  data++;
  size--;

  if (encoding == 3 || encoding == 0) {
    while (size > 0 && data[size - 1] == 0)
      size--;
    text.SetTo((const char *)data, size);
    return text;
  }

  bool littleEndian = false;
  if (encoding == 1 && size >= 2) {
    if (data[0] == 0xff && data[1] == 0xfe) {
      littleEndian = true;
      data += 2;
      size -= 2;
    } else if (data[0] == 0xfe && data[1] == 0xff) {
      data += 2;
      size -= 2;
    }
  }

  for (size_t i = 0; i + 1 < size; i += 2) {
    uint16 unit = littleEndian ? ((uint16)data[i + 1] << 8) | data[i]
                               : ((uint16)data[i] << 8) | data[i + 1];
    if (unit == 0)
      break;
    if (unit >= 0xd800 && unit <= 0xdbff && i + 3 < size) {
      uint16 low =
          littleEndian ? ((uint16)data[i + 3] << 8) | data[i + 2]
                       : ((uint16)data[i + 2] << 8) | data[i + 3];
      if (low >= 0xdc00 && low <= 0xdfff) {
        uint32 cp = 0x10000 + (((uint32)unit - 0xd800) << 10) +
                    ((uint32)low - 0xdc00);
        AppendUtf8Codepoint(text, cp);
        i += 2;
        continue;
      }
    }
    AppendUtf8Codepoint(text, unit);
  }
  return text;
}

size_t FindId3TextTerminator(const uint8 *data, size_t size, uint8 encoding) {
  if (encoding == 1 || encoding == 2) {
    for (size_t i = 0; i + 1 < size; i += 2) {
      if (data[i] == 0 && data[i + 1] == 0)
        return i + 2;
    }
    return size;
  }

  for (size_t i = 0; i < size; ++i) {
    if (data[i] == 0)
      return i + 1;
  }
  return size;
}

BString DecodeId3TextValue(uint8 encoding, const uint8 *data, size_t size) {
  std::vector<uint8> encoded;
  encoded.reserve(size + 1);
  encoded.push_back(encoding);
  encoded.insert(encoded.end(), data, data + size);
  return DecodeId3Text(encoded.data(), encoded.size());
}

BString Trimmed(BString value) {
  value.Trim();
  return value;
}

bool StartsWithHttp(const BString &value) {
  return value.IStartsWith("http://") || value.IStartsWith("https://");
}

bool IsLikelyArtworkUrl(BString value) {
  value.Trim();
  if (!StartsWithHttp(value))
    return false;

  value.ToLower();
  int32 query = value.FindFirst('?');
  if (query >= 0)
    value.Truncate(query);

  if (value.EndsWith(".jpg") || value.EndsWith(".jpeg") ||
      value.EndsWith(".png") || value.EndsWith(".webp") ||
      value.EndsWith(".gif")) {
    return true;
  }

  return value.FindFirst("/cover") >= 0 || value.FindFirst("/covers/") >= 0 ||
         value.FindFirst("/artwork") >= 0 || value.FindFirst("/image") >= 0 ||
         value.FindFirst("/images/") >= 0;
}

bool ContainsI(const char *text, const char *needle) {
  if (!text || !needle)
    return false;

  BString haystack(text);
  BString pattern(needle);
  haystack.ToLower();
  pattern.ToLower();
  return haystack.FindFirst(pattern.String()) >= 0;
}

float PcmPeakFloat(const uint8 *data, size_t size) {
  if (!data || size < sizeof(float))
    return 0.0f;

  size_t sampleCount = size / sizeof(float);
  float peak = 0.0f;
  for (size_t i = 0; i < sampleCount; ++i) {
    float sample;
    memcpy(&sample, data + i * sizeof(float), sizeof(sample));
    if (sample < 0.0f)
      sample = -sample;
    if (sample > peak)
      peak = sample;
  }
  return peak;
}

const char *SampleFmtName(AVSampleFormat format) {
  const char *name = av_get_sample_fmt_name(format);
  return name ? name : "?";
}

bool StartsWithString(const std::string &text, const char *prefix) {
  return text.rfind(prefix, 0) == 0;
}

bool EndsWithString(const std::string &text, const char *suffix) {
  size_t suffixLength = strlen(suffix);
  return text.size() >= suffixLength &&
         text.compare(text.size() - suffixLength, suffixLength, suffix) == 0;
}

std::string TrimString(const std::string &value) {
  size_t first = 0;
  while (first < value.size() &&
         (value[first] == ' ' || value[first] == '\t' ||
          value[first] == '\r' || value[first] == '\n')) {
    first++;
  }

  size_t last = value.size();
  while (last > first &&
         (value[last - 1] == ' ' || value[last - 1] == '\t' ||
          value[last - 1] == '\r' || value[last - 1] == '\n')) {
    last--;
  }

  return value.substr(first, last - first);
}

std::string ResolveHlsUrl(const std::string &playlistUrl,
                          const std::string &segmentUrl) {
  if (StartsWithString(segmentUrl, "http://") ||
      StartsWithString(segmentUrl, "https://")) {
    return segmentUrl;
  }

  if (!segmentUrl.empty() && segmentUrl[0] == '/') {
    size_t scheme = playlistUrl.find("://");
    if (scheme != std::string::npos) {
      size_t hostEnd = playlistUrl.find('/', scheme + 3);
      if (hostEnd != std::string::npos)
        return playlistUrl.substr(0, hostEnd) + segmentUrl;
    }
  }

  size_t slash = playlistUrl.rfind('/');
  if (slash == std::string::npos)
    return segmentUrl;

  return playlistUrl.substr(0, slash + 1) + segmentUrl;
}

bool ReadUrlBytes(const std::string &url, std::vector<uint8> &out,
                  size_t maxBytes) {
  out.clear();

  AVDictionary *options = nullptr;
  av_dict_set(&options, "user_agent",
              "Mozilla/5.0 (Haiku; x86_64) Beton/1.0", 0);
  av_dict_set(&options, "rw_timeout", "3000000", 0);

  AVIOContext *io = nullptr;
  int result = avio_open2(&io, url.c_str(), AVIO_FLAG_READ, nullptr, &options);
  av_dict_free(&options);
  if (result < 0)
    return false;

  std::vector<uint8> buffer(8192);
  while (out.size() < maxBytes) {
    int toRead = (int)std::min(buffer.size(), maxBytes - out.size());
    result = avio_read(io, buffer.data(), toRead);
    if (result <= 0)
      break;
    out.insert(out.end(), buffer.begin(), buffer.begin() + result);
  }

  avio_closep(&io);
  return !out.empty();
}

size_t HlsBufferedBytes(off_t totalWritten, off_t readPos) {
  return totalWritten > readPos ? (size_t)(totalWritten - readPos) : 0;
}

bool FindLatestHlsMediaSegment(const std::string &playlistUrl,
                               const std::string &playlist,
                               std::string &segmentUrl) {
  segmentUrl.clear();

  size_t lineStart = 0;
  while (lineStart <= playlist.size()) {
    size_t lineEnd = playlist.find('\n', lineStart);
    if (lineEnd == std::string::npos)
      lineEnd = playlist.size();

    std::string line = TrimString(playlist.substr(lineStart,
        lineEnd - lineStart));
    if (!line.empty() && line[0] != '#') {
      segmentUrl = ResolveHlsUrl(playlistUrl, line);
    }

    if (lineEnd == playlist.size())
      break;
    lineStart = lineEnd + 1;
  }

  return !segmentUrl.empty();
}

bool ReadNullTerminated(const uint8 *data, size_t size, size_t &offset,
                        BString &out) {
  if (offset >= size)
    return false;
  size_t start = offset;
  while (offset < size && data[offset] != 0)
    offset++;
  if (offset >= size)
    return false;
  out.SetTo((const char *)data + start, offset - start);
  offset++;
  return true;
}

struct EmsgBoxInfo {
  size_t payloadOffset;
  bigtime_t presentationOffset;
  bool hasPresentationOffset;
};

bool ParseEmsgBox(const uint8 *data, size_t size, EmsgBoxInfo &info) {
  if (size < 12 || memcmp(data + 4, "emsg", 4) != 0)
    return false;

  info.payloadOffset = 0;
  info.presentationOffset = 0;
  info.hasPresentationOffset = false;

  size_t offset = 8;
  uint64 boxSize = ReadBE32(data);
  if (boxSize == 1) {
    if (size < 16)
      return false;
    boxSize = ReadBE64(data + 8);
    offset = 16;
  } else if (boxSize == 0) {
    boxSize = size;
  }
  if (boxSize > size || boxSize < offset + 4)
    return false;

  uint8 version = data[offset];
  offset += 4; ///< version + flags

  if (version == 0) {
    BString scheme;
    BString value;
    if (!ReadNullTerminated(data, (size_t)boxSize, offset, scheme) ||
        !ReadNullTerminated(data, (size_t)boxSize, offset, value) ||
        offset + 16 > boxSize)
      return false;

    uint32 timescale = ReadBE32(data + offset);
    uint32 presentationDelta = ReadBE32(data + offset + 4);
    if (timescale > 0) {
      info.presentationOffset =
          (bigtime_t)(((uint64)presentationDelta * 1000000ULL) / timescale);
      info.hasPresentationOffset = true;
    }
    offset += 16;
  } else if (version == 1) {
    if (offset + 20 > boxSize)
      return false;
    uint32 timescale = ReadBE32(data + offset);
    uint64 presentationTime = ReadBE64(data + offset + 4);
    if (timescale > 0) {
      info.presentationOffset =
          (bigtime_t)((presentationTime * 1000000ULL) / timescale);
      info.hasPresentationOffset = true;
    }
    offset += 20;
    BString scheme;
    BString value;
    if (!ReadNullTerminated(data, (size_t)boxSize, offset, scheme) ||
        !ReadNullTerminated(data, (size_t)boxSize, offset, value))
      return false;
  } else {
    return false;
  }

  info.payloadOffset = offset;
  return info.payloadOffset < boxSize;
}

struct TimedId3Fields {
  BString artist;
  BString title;
  BString album;
  BString url;
};

bool ParseId3Tag(const uint8 *data, size_t size, TimedId3Fields &fields) {
  if (!IsLikelyId3(data, size))
    return false;

  size_t totalSize = Id3TagTotalSize(data, size);
  if (totalSize == 0)
    return false;

  uint8 major = data[3];
  uint8 flags = data[5];
  uint32 tagSize = ReadSyncSafe32(data + 6);
  if (tagSize == 0)
    return false;

  size_t payloadSize = std::min<size_t>(tagSize, totalSize - 10);
  std::vector<uint8> payload(data + 10, data + 10 + payloadSize);
  if (flags & 0x80)
    Unsynchronise(payload);

  size_t offset = 0;
  if (flags & 0x40) {
    if (payload.size() < 4)
      return false;
    uint32 extSize =
        major == 4 ? ReadSyncSafe32(payload.data()) : ReadBE32(payload.data());
    size_t skip = major == 4 ? extSize : extSize + 4;
    if (skip > payload.size())
      return false;
    offset = skip;
  }

  while (offset + 10 <= payload.size()) {
    const uint8 *frame = payload.data() + offset;
    if (frame[0] == 0)
      break;

    char id[5] = {(char)frame[0], (char)frame[1], (char)frame[2],
                  (char)frame[3], 0};
    uint32 frameSize = major == 4 ? ReadSyncSafe32(frame + 4)
                                  : ReadBE32(frame + 4);
    offset += 10;
    if (frameSize == 0 || offset + frameSize > payload.size())
      break;

    const uint8 *body = payload.data() + offset;
    if (id[0] == 'T' && strcmp(id, "TXXX") != 0) {
      BString text = Trimmed(DecodeId3Text(body, frameSize));
      if (strcmp(id, "TIT2") == 0)
        fields.title = text;
      else if (strcmp(id, "TPE1") == 0 || strcmp(id, "TPE2") == 0)
        fields.artist = text;
      else if (strcmp(id, "TALB") == 0)
        fields.album = text;
      else if (strcmp(id, "TRSN") == 0 && fields.album.IsEmpty())
        fields.album = text;
    } else if (strcmp(id, "TXXX") == 0) {
      BString text;
      if (frameSize > 1) {
        uint8 encoding = body[0];
        size_t valueOffset =
            1 + FindId3TextTerminator(body + 1, frameSize - 1, encoding);
        if (valueOffset < frameSize)
          text = Trimmed(DecodeId3TextValue(encoding, body + valueOffset,
                                            frameSize - valueOffset));
      }
      if (StartsWithHttp(text) && fields.url.IsEmpty())
        fields.url = text;
      else if (fields.title.IsEmpty())
        fields.title = text;
    } else if (strcmp(id, "WXXX") == 0) {
      BString url;
      if (frameSize > 1) {
        uint8 encoding = body[0];
        size_t valueOffset =
            1 + FindId3TextTerminator(body + 1, frameSize - 1, encoding);
        size_t urlLen = frameSize - std::min(valueOffset, (size_t)frameSize);
        while (urlLen > 0 && body[valueOffset + urlLen - 1] == 0)
          urlLen--;
        url.SetTo((const char *)body + valueOffset, urlLen);
        url = Trimmed(url);
      }
      if (StartsWithHttp(url) && fields.url.IsEmpty())
        fields.url = url;
    } else if (id[0] == 'W') {
      size_t urlLen = frameSize;
      while (urlLen > 0 && body[urlLen - 1] == 0)
        urlLen--;
      BString url((const char *)body, urlLen);
      url = Trimmed(url);
      if (StartsWithHttp(url) && fields.url.IsEmpty())
        fields.url = url;
    }

    offset += frameSize;
  }

  return !fields.title.IsEmpty() || !fields.artist.IsEmpty() ||
         !fields.album.IsEmpty() || !fields.url.IsEmpty();
}

bool ParseId3Range(const uint8 *data, size_t size, TimedId3Fields &fields) {
  if (!data || size == 0)
    return false;

  bool parsed = false;
  for (size_t offset = 0; offset + 10 <= size;) {
    if (!IsLikelyId3(data + offset, size - offset)) {
      ++offset;
      continue;
    }

    size_t tagSize = Id3TagTotalSize(data + offset, size - offset);
    if (tagSize == 0) {
      ++offset;
      continue;
    }

    if (ParseId3Tag(data + offset, tagSize, fields))
      parsed = true;
    offset += tagSize;
  }

  return parsed;
}

void MergeFfmpegMetadata(AVDictionary *dict, TimedId3Fields &fields) {
  AVDictionaryEntry *entry = nullptr;
  while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
    if (!entry->key || !entry->value || entry->value[0] == '\0')
      continue;

    BString value(entry->value);
    value.Trim();
    if (value.IsEmpty())
      continue;

    const char *key = entry->key;
    if ((strcasecmp(key, "title") == 0 ||
         strcasecmp(key, "StreamTitle") == 0 ||
         strcasecmp(key, "TIT2") == 0) &&
        fields.title.IsEmpty()) {
      fields.title = value;
    } else if ((strcasecmp(key, "artist") == 0 ||
                strcasecmp(key, "album_artist") == 0 ||
                strcasecmp(key, "TPE1") == 0 ||
                strcasecmp(key, "TPE2") == 0) &&
               fields.artist.IsEmpty()) {
      fields.artist = value;
    } else if ((strcasecmp(key, "album") == 0 ||
                strcasecmp(key, "TALB") == 0 ||
                strcasecmp(key, "TRSN") == 0) &&
               fields.album.IsEmpty()) {
      fields.album = value;
    } else if (StartsWithHttp(value) &&
               (strcasecmp(key, "url") == 0 ||
                strcasecmp(key, "StreamUrl") == 0 ||
                strcasecmp(key, "WXXX") == 0 ||
                ContainsI(key, "url") ||
                ContainsI(key, "cover") ||
                ContainsI(key, "image") ||
                ContainsI(key, "artwork"))) {
      if (fields.url.IsEmpty() && IsLikelyArtworkUrl(value))
        fields.url = value;
    }
  }
}

} /// namespace

/**
 * @brief Constructs NetworkAudioStreamIO with an 8 MB ring buffer.
 * @param target The BMessenger to send metadata updates to.
 */
NetworkAudioStreamIO::NetworkAudioStreamIO(BMessenger target, BUrlContext *context)
    : fBuffer(new uint8[kBufferCapacity]), fWritePos(0), fTotalWritten(0),
      fReadPos(0), fValidStart(0), fTarget(target), fMode(MODE_ICY),
      fContext(context), fRunning(false), fRequestRunning(false),
      fExpectedSize(0), fFfmpegThread(-1), fHlsFormatKnown(false),
      fLastHlsMetadataPoll(0), fLastPcmReadDebugLog(0),
      fFfmpegReadDeadline(0), fPendingSeekTime(-1) {
  fDataReady = create_sem(0, "stream_data_ready");
  fHlsFormatReady = create_sem(0, "hls_format_ready");
  memset(&fHlsFormat, 0, sizeof(fHlsFormat));
}

NetworkAudioStreamIO::~NetworkAudioStreamIO() {
  Stop();
  if (fDataReady >= 0)
    delete_sem(fDataReady);
  if (fHlsFormatReady >= 0)
    delete_sem(fHlsFormatReady);
  delete[] fBuffer;
}

ssize_t NetworkAudioStreamIO::Read(void *buffer, size_t size) {
  if (!buffer || size == 0)
    return 0;

  size_t totalRead = 0;
  uint8 *dst = (uint8 *)buffer;

  off_t readPos = fReadPos.load(std::memory_order_relaxed);

  if (readPos == 0 || (readPos % (512 * 1024) == 0)) {
    off_t totalWritten = fTotalWritten.load(std::memory_order_acquire);
    DEBUG_PRINT("Read(%zu) @ %lld, available=%zu\n", size,
                (long long)readPos, (size_t)(totalWritten - readPos));
  }

  while (totalRead < size) {
    off_t totalWrittenBytes = fTotalWritten.load(std::memory_order_acquire);
    off_t available = totalWrittenBytes - readPos;

    if (available <= 0) {
      if (!fRequestRunning.load(std::memory_order_relaxed))
        return (ssize_t)totalRead; ///< Return what we have, 0 means EOF

      acquire_sem_etc(fDataReady, 1, B_RELATIVE_TIMEOUT, 500000);
      continue;
    }

    size_t toRead = std::min(size - totalRead, (size_t)available);
    size_t bufOffset = (size_t)(readPos % kBufferCapacity);
    size_t chunk = std::min(toRead, kBufferCapacity - bufOffset);

    memcpy(dst + totalRead, fBuffer + bufOffset, chunk);
    totalRead += chunk;
    readPos += chunk;
    fReadPos.store(readPos, std::memory_order_relaxed);

    if (chunk < toRead) {
      size_t rest = toRead - chunk;
      memcpy(dst + totalRead, fBuffer, rest);
      totalRead += rest;
      readPos += rest;
      fReadPos.store(readPos, std::memory_order_relaxed);
    }
  }

  return (ssize_t)totalRead;
}

ssize_t NetworkAudioStreamIO::Write(const void *buffer, size_t size) {
  if (!buffer || size == 0)
    return 0;

  if (fTotalWritten == 0) {
  }

  const uint8 *src = (const uint8 *)buffer;
  size_t totalWritten = 0;

  while (totalWritten < size) {
    if (!fRunning)
      break;

    off_t writePos = fWritePos.load(std::memory_order_relaxed);
    off_t totalWrittenBytes = fTotalWritten.load(std::memory_order_relaxed);
    off_t readPos = fReadPos.load(std::memory_order_relaxed);

    off_t unread = totalWrittenBytes - readPos;
    /// Reserve 1MB (1048576 bytes) of history space in the buffer.
    /// This allows BMediaFile to seek backwards by up to 1MB without triggering
    /// a network restart!
    off_t maxFuture = (off_t)kBufferCapacity - 1048576;

    if (unread >= maxFuture) {
      snooze(10000); ///< 10ms backpressure
      continue;
    }

    size_t chunkAllowed = unread < 0 ? maxFuture : maxFuture - (size_t)unread;
    size_t bufOffset = (size_t)(writePos % kBufferCapacity);
    size_t toWrite = std::min(size - totalWritten, kBufferCapacity - bufOffset);
    toWrite = std::min(toWrite, chunkAllowed);

    memcpy(fBuffer + bufOffset, src + totalWritten, toWrite);

    fWritePos.store(writePos + toWrite, std::memory_order_relaxed);
    fTotalWritten.store(totalWrittenBytes + toWrite, std::memory_order_release);

    totalWritten += toWrite;
  }

  release_sem(fDataReady);
  return (ssize_t)totalWritten;
}

ssize_t NetworkAudioStreamIO::ReadAt(off_t position, void *buffer, size_t size) {
  Seek(position, SEEK_SET);
  return Read(buffer, size);
}

ssize_t NetworkAudioStreamIO::WriteAt(off_t position, const void *buffer,
                                 size_t size) {
  return Write(buffer, size);
}

off_t NetworkAudioStreamIO::Seek(off_t position, uint32 seekMode) {
  off_t readPos = fReadPos.load(std::memory_order_relaxed);
  switch (seekMode) {
  case SEEK_SET:
    readPos = position;
    break;
  case SEEK_CUR:
    readPos += position;
    break;
  case SEEK_END:
    if (fExpectedSize <= 0 && fRequestRunning.load(std::memory_order_relaxed)) {
      /// Prevent sniffers from seeking to the end of an unbounded live stream
      return B_ERROR;
    }
    readPos =
        (fExpectedSize > 0 ? fExpectedSize
                           : fTotalWritten.load(std::memory_order_acquire)) +
        position;
    break;
  }

  if (readPos < 0)
    readPos = 0;

  fReadPos.store(readPos, std::memory_order_relaxed);

  return readPos;
}

off_t NetworkAudioStreamIO::Position() const {
  return fReadPos.load(std::memory_order_relaxed);
}

status_t NetworkAudioStreamIO::GetSize(off_t *size) const {
  if (!size)
    return B_BAD_VALUE;

  if (fExpectedSize > 0) {
    *size = fExpectedSize;
    return B_OK;
  }

  if (fRequestRunning.load(std::memory_order_relaxed)) {
    /// For live streams, we don't know the size. Returning a fake large size
    /// causes sniffers (like Ogg) to SEEK_END and hang the Read loop forever.
    return B_ERROR;
  }
  *size = fTotalWritten.load(std::memory_order_acquire);
  return B_OK;
}

status_t NetworkAudioStreamIO::WaitForData(size_t minBytes,
                                      bigtime_t timeoutUs) const {
  bigtime_t deadline = system_time() + timeoutUs;
  while (fTotalWritten.load(std::memory_order_acquire) < (off_t)minBytes) {
    if (!fRunning)
      return B_ERROR;
    if (system_time() >= deadline) {
      if (fTotalWritten.load(std::memory_order_acquire) > 32768) {
        DEBUG_PRINT("WaitForData timed out, but proceeding "
                    "with %zu bytes\n",
                    (size_t)fTotalWritten.load(std::memory_order_acquire));
        return B_OK;
      }
      return B_TIMED_OUT;
    }
    snooze(50000);
  }
  DEBUG_PRINT("WaitForData: %zu bytes buffered\n",
              (size_t)fTotalWritten.load(std::memory_order_acquire));
  return B_OK;
}

status_t NetworkAudioStreamIO::SeekToTime(bigtime_t position) {
  if (fMode != MODE_DLNA || position < 0 || !fRunning)
    return B_NOT_ALLOWED;

  fPendingSeekTime.store(position, std::memory_order_release);
  release_sem(fDataReady);
  return B_OK;
}

/**
 * @brief Waits until the HLS decoder has determined the audio format.
 */
status_t NetworkAudioStreamIO::WaitForFormat(media_raw_audio_format *format,
                                        bigtime_t timeoutUs) const {
  if (fHlsFormatKnown) {
    *format = fHlsFormat;
    return B_OK;
  }
  status_t err =
      acquire_sem_etc(fHlsFormatReady, 1, B_RELATIVE_TIMEOUT, timeoutUs);
  if (err == B_OK && fHlsFormatKnown) {
    *format = fHlsFormat;
    return B_OK;
  }
  return B_TIMED_OUT;
}

/**
 * @brief Reads decoded PCM from the ring buffer.
 *
 * Called from the BSoundPlayer callback. Returns available data,
 * filling silence if the buffer underruns.
 */
ssize_t NetworkAudioStreamIO::ReadPcm(void *buffer, size_t size) {
  if (!buffer || size == 0)
    return 0;

  uint8 *dst = (uint8 *)buffer;
  size_t totalRead = 0;

  off_t readPos = fReadPos.load(std::memory_order_relaxed);
  off_t totalWritten = fTotalWritten.load(std::memory_order_acquire);
  size_t available = totalWritten > readPos ? (size_t)(totalWritten - readPos) : 0;

  if (available == 0) {
    if (!fRunning) {
      memset(buffer, 0, size);
      return B_ERROR;
    }
    acquire_sem_etc(fDataReady, 1, B_RELATIVE_TIMEOUT, 200000);
    readPos = fReadPos.load(std::memory_order_relaxed);
    totalWritten = fTotalWritten.load(std::memory_order_acquire);
    available = totalWritten > readPos ? (size_t)(totalWritten - readPos) : 0;
    if (available == 0) {
#if 0
      bigtime_t now = system_time();
      bigtime_t last =
          fLastPcmReadDebugLog.load(std::memory_order_relaxed);
      if (now - last >= 1000000 &&
          fLastPcmReadDebugLog.compare_exchange_strong(last, now)) {
        DEBUG_PRINT("ReadPcm underrun: requested=%zu totalWritten=%lld "
                    "readPos=%lld running=%d requestRunning=%d\n",
                    size, (long long)totalWritten, (long long)readPos,
                    (int)fRunning.load(std::memory_order_relaxed),
                    (int)fRequestRunning.load(std::memory_order_relaxed));
      }
#endif
      memset(buffer, 0, size);
      return (ssize_t)size;
    }
  }

  size_t toRead = std::min(size, available);
  size_t bufOffset = (size_t)readPos % kBufferCapacity;
  size_t chunk = std::min(toRead, kBufferCapacity - bufOffset);

  memcpy(dst, fBuffer + bufOffset, chunk);
  totalRead += chunk;
  readPos += chunk;

  if (chunk < toRead) {
    size_t rest = toRead - chunk;
    memcpy(dst + totalRead, fBuffer, rest);
    totalRead += rest;
    readPos += rest;
  }
  fReadPos.store(readPos, std::memory_order_relaxed);

  if (totalRead < size) {
    memset(dst + totalRead, 0, size - totalRead);
  }

#if 0
  bigtime_t now = system_time();
  bigtime_t last = fLastPcmReadDebugLog.load(std::memory_order_relaxed);
  if (now - last >= 1000000 &&
      fLastPcmReadDebugLog.compare_exchange_strong(last, now)) {
    DEBUG_PRINT("ReadPcm: requested=%zu read=%zu silenceFill=%zu peak=%.6f "
                "totalWritten=%lld readPos=%lld availableBefore=%zu\n",
                size, totalRead, size - totalRead, PcmPeakFloat(dst, totalRead),
                (long long)totalWritten, (long long)readPos, available);
  }
#endif

  return (ssize_t)size;
}

/// --- Stream Control ---

/**
 * @brief Opens a stream in the specified mode.
 */
status_t NetworkAudioStreamIO::Open(const BString &url, Mode mode,
                               BUrlContext *context) {
  Stop();
  if (context)
    fContext = context;
  fRunning = true;
  fRequestRunning = true;

  if (fDataReady >= 0)
    delete_sem(fDataReady);
  fDataReady = create_sem(0, "stream_data_ready");

  if (fHlsFormatReady >= 0)
    delete_sem(fHlsFormatReady);
  fHlsFormatReady = create_sem(0, "hls_format_ready");

  fUrl = url;
  fMode = mode;
  fRunning = true;
  fWritePos = 0;
  fTotalWritten = 0;
  fReadPos = 0;
  fValidStart = 0;
  fExpectedSize = 0;
  fLastStreamTitle = "";
  fLastStreamUrl = "";
  fLastIcyPacket = "";
  fLastHlsMetadataSegment = "";
  fPendingHlsMetadata.clear();
  fHlsFormatKnown = false;
  fLastHlsMetadataPoll = 0;
  fLastPcmReadDebugLog = 0;
  fFfmpegReadDeadline = 0;
  fPendingSeekTime = -1;
  memset(&fHlsFormat, 0, sizeof(fHlsFormat));

  EnsureFfmpegNetworkInitialized();

  fFfmpegThread =
      spawn_thread(_FfmpegThreadEntry, "ffmpeg_stream", B_NORMAL_PRIORITY, this);
  if (fFfmpegThread < 0) {
    fRunning = false;
    fRequestRunning = false;
    MaybeDeinitFfmpegNetwork();
    return B_ERROR;
  }
  resume_thread(fFfmpegThread);
  return B_OK;
}

/**
 * @brief Stops all streaming activities and waits for threads.
 */
void NetworkAudioStreamIO::Stop() {
  fRunning = false;
  fRequestRunning = false;

  if (fDataReady >= 0)
    release_sem(fDataReady);
  if (fHlsFormatReady >= 0)
    release_sem(fHlsFormatReady);

  if (fFfmpegThread >= 0) {
    status_t exit;
    wait_for_thread(fFfmpegThread, &exit);
    fFfmpegThread = -1;
  }
}

/// --- ICY Metadata Parsing ---

void NetworkAudioStreamIO::_ParseIcyMeta(const BString &raw) {
  if (raw.IsEmpty())
    return;

  BString title, url;
  int32 pos = raw.FindFirst("StreamTitle='");
  if (pos >= 0) {
    int32 start = pos + 13;
    int32 end = raw.FindFirst("';", start);
    if (end > start)
      raw.CopyInto(title, start, end - start);
  }

  pos = raw.FindFirst("StreamUrl='");
  if (pos >= 0) {
    int32 start = pos + 11;
    int32 end = raw.FindFirst("';", start);
    if (end > start)
      raw.CopyInto(url, start, end - start);
  }
  if (!url.IsEmpty() && !IsLikelyArtworkUrl(url))
    url = "";

  if (!title.IsEmpty() || !url.IsEmpty())
    _NotifyMetadata(title, url);
}

void NetworkAudioStreamIO::_NotifyMetadata(const BString &title,
                                      const BString &url) {
  if (title == fLastStreamTitle && url == fLastStreamUrl)
    return;

  fLastStreamTitle = title;
  fLastStreamUrl = url;

  if (fTarget.IsValid()) {
    BMessage m(MSG_STREAM_METADATA);
    if (!title.IsEmpty())
      m.AddString("stream_title", title);
    if (!url.IsEmpty())
      m.AddString("stream_url", url);
    fTarget.SendMessage(&m);
  }
}

void NetworkAudioStreamIO::_NotifyMetadata(const BString &artist,
                                      const BString &title,
                                      const BString &album,
                                      const BString &url) {
  BString streamTitle = title;
  if (!artist.IsEmpty() && !title.IsEmpty()) {
    streamTitle = artist;
    streamTitle << " - " << title;
  } else if (streamTitle.IsEmpty()) {
    streamTitle = artist;
  }

  if (streamTitle == fLastStreamTitle && url == fLastStreamUrl)
    return;

  fLastStreamTitle = streamTitle;
  fLastStreamUrl = url;

  if (fTarget.IsValid()) {
    BMessage m(MSG_STREAM_METADATA);
    if (!streamTitle.IsEmpty())
      m.AddString("stream_title", streamTitle);
    if (!artist.IsEmpty())
      m.AddString("stream_artist", artist);
    if (!title.IsEmpty())
      m.AddString("stream_track_title", title);
    if (!album.IsEmpty())
      m.AddString("stream_album", album);
    if (!url.IsEmpty())
      m.AddString("stream_url", url);
    fTarget.SendMessage(&m);
  }
}

void NetworkAudioStreamIO::_ProcessFfmpegMetadata(AVDictionary *dict) {
  if (!dict)
    return;

  TimedId3Fields fields;
  MergeFfmpegMetadata(dict, fields);
  if (!fields.title.IsEmpty() || !fields.artist.IsEmpty() ||
      !fields.album.IsEmpty() || !fields.url.IsEmpty()) {
    _NotifyMetadata(fields.artist, fields.title, fields.album, fields.url);
  }
}

int32 NetworkAudioStreamIO::_FfmpegThreadEntry(void *arg) {
  static_cast<NetworkAudioStreamIO *>(arg)->_FfmpegLoop();
  return 0;
}

void NetworkAudioStreamIO::_ProcessHlsTimedMetadata(const uint8 *data,
                                                    size_t size) {
  if (!data || size == 0)
    return;

  TimedId3Fields fields;
  if (ParseId3Range(data, size, fields)) {
    DEBUG_PRINT("HLS ID3 metadata: artist='%s' title='%s' album='%s'\n",
                fields.artist.String(), fields.title.String(),
                fields.album.String());
    _NotifyMetadata(fields.artist, fields.title, fields.album, fields.url);
  }

  for (size_t offset = 0; offset + 12 <= size;) {
    uint64 boxSize = ReadBE32(data + offset);
    size_t headerSize = 8;
    if (boxSize == 1) {
      if (offset + 16 > size)
        break;
      boxSize = ReadBE64(data + offset + 8);
      headerSize = 16;
    } else if (boxSize == 0) {
      boxSize = size - offset;
    }

    if (boxSize < headerSize || offset + boxSize > size)
      break;

    if (memcmp(data + offset + 4, "emsg", 4) == 0) {
      EmsgBoxInfo emsgInfo;
      if (ParseEmsgBox(data + offset, (size_t)boxSize, emsgInfo)) {
        TimedId3Fields emsgFields;
        const uint8 *payload = data + offset + emsgInfo.payloadOffset;
        size_t payloadSize = (size_t)boxSize - emsgInfo.payloadOffset;
        if (ParseId3Range(payload, payloadSize, emsgFields)) {
          DEBUG_PRINT("HLS emsg ID3 metadata: artist='%s' title='%s' album='%s'\n",
                      emsgFields.artist.String(), emsgFields.title.String(),
                      emsgFields.album.String());
          _NotifyMetadata(emsgFields.artist, emsgFields.title,
                          emsgFields.album, emsgFields.url);
        } else {
          DEBUG_PRINT("HLS emsg box without parsed ID3 payload (%zu bytes)\n",
                      payloadSize);
        }
      }
    }

    offset += (size_t)boxSize;
  }
}

void NetworkAudioStreamIO::_QueueHlsSegmentMetadata(const uint8 *data,
                                                    size_t size) {
  if (!data || size == 0)
    return;

  std::vector<HlsTimedMetadataEvent> segmentEvents;

  for (size_t offset = 0; offset + 12 <= size;) {
    uint64 boxSize = ReadBE32(data + offset);
    size_t headerSize = 8;
    if (boxSize == 1) {
      if (offset + 16 > size)
        break;
      boxSize = ReadBE64(data + offset + 8);
      headerSize = 16;
    } else if (boxSize == 0) {
      boxSize = size - offset;
    }

    if (boxSize < headerSize || offset + boxSize > size)
      break;

    if (memcmp(data + offset + 4, "emsg", 4) == 0) {
      EmsgBoxInfo emsgInfo;
      if (ParseEmsgBox(data + offset, (size_t)boxSize, emsgInfo)) {
        TimedId3Fields fields;
        const uint8 *payload = data + offset + emsgInfo.payloadOffset;
        size_t payloadSize = (size_t)boxSize - emsgInfo.payloadOffset;
        if (ParseId3Range(payload, payloadSize, fields)) {
          HlsTimedMetadataEvent event;
          event.dueTime = emsgInfo.hasPresentationOffset
                              ? emsgInfo.presentationOffset
                              : 0;
          event.artist = fields.artist;
          event.title = fields.title;
          event.album = fields.album;
          event.url = fields.url;
          segmentEvents.push_back(event);
        }
      }
    }

    offset += (size_t)boxSize;
  }

  if (segmentEvents.empty())
    return;

  bigtime_t firstOffset = segmentEvents[0].dueTime;
  for (const auto &event : segmentEvents)
    firstOffset = std::min(firstOffset, event.dueTime);

  bigtime_t now = system_time();
  for (auto &event : segmentEvents) {
    event.dueTime = now + std::max<bigtime_t>(0, event.dueTime - firstOffset);
    fPendingHlsMetadata.push_back(event);
  }

  std::sort(fPendingHlsMetadata.begin(), fPendingHlsMetadata.end(),
            [](const HlsTimedMetadataEvent &a,
               const HlsTimedMetadataEvent &b) {
              return a.dueTime < b.dueTime;
            });
}

void NetworkAudioStreamIO::_PollFfmpegIcyMetadata(void *formatContext) {
  AVFormatContext *fmtCtx = static_cast<AVFormatContext *>(formatContext);
  if (!fmtCtx)
    return;

  uint8_t *value = nullptr;
  int ret = av_opt_get(fmtCtx, "icy_metadata_packet", AV_OPT_SEARCH_CHILDREN,
                       &value);
  if ((ret < 0 || !value || value[0] == '\0') && fmtCtx->pb) {
    if (value) {
      av_free(value);
      value = nullptr;
    }
    ret = av_opt_get(fmtCtx->pb, "icy_metadata_packet",
                     AV_OPT_SEARCH_CHILDREN, &value);
  }

  if (ret < 0 || !value || value[0] == '\0') {
    if (value)
      av_free(value);
    return;
  }

  BString packet((const char *)value);
  av_free(value);

  if (packet == fLastIcyPacket)
    return;

  fLastIcyPacket = packet;
  _ParseIcyMeta(packet);
}

void NetworkAudioStreamIO::_PollHlsPlaylistMetadata() {
  if (!fUrl.IEndsWith(".m3u8"))
    return;

  std::vector<uint8> playlistBytes;
  if (!ReadUrlBytes(fUrl.String(), playlistBytes, 512 * 1024)) {
    DEBUG_PRINT("HLS metadata playlist read failed: %s\n",
                fUrl.String());
    return;
  }

  std::string playlist((const char *)playlistBytes.data(),
                       playlistBytes.size());
  std::string segmentUrl;
  if (!FindLatestHlsMediaSegment(fUrl.String(), playlist, segmentUrl))
    return;

  if (EndsWithString(segmentUrl, ".m3u8")) {
    std::vector<uint8> nestedPlaylistBytes;
    if (!ReadUrlBytes(segmentUrl, nestedPlaylistBytes, 512 * 1024))
      return;

    std::string nestedPlaylist((const char *)nestedPlaylistBytes.data(),
                               nestedPlaylistBytes.size());
    std::string nestedSegmentUrl;
    if (!FindLatestHlsMediaSegment(segmentUrl, nestedPlaylist,
                                   nestedSegmentUrl)) {
      return;
    }
    segmentUrl = nestedSegmentUrl;
  }

  if (fLastHlsMetadataSegment == segmentUrl.c_str() &&
      !fPendingHlsMetadata.empty()) {
    return;
  }

  fLastHlsMetadataSegment = segmentUrl.c_str();

  std::vector<uint8> segmentBytes;
  if (!ReadUrlBytes(segmentUrl, segmentBytes, 128 * 1024)) {
    DEBUG_PRINT("HLS metadata segment read failed: %s\n",
                segmentUrl.c_str());
    return;
  }

  DEBUG_PRINT("Scanning HLS segment metadata: %s (%zu bytes)\n",
              segmentUrl.c_str(), segmentBytes.size());
  _QueueHlsSegmentMetadata(segmentBytes.data(), segmentBytes.size());
}

void NetworkAudioStreamIO::_DispatchDueHlsMetadata() {
  bigtime_t now = system_time();
  while (!fPendingHlsMetadata.empty() &&
         fPendingHlsMetadata.front().dueTime <= now) {
    HlsTimedMetadataEvent event = fPendingHlsMetadata.front();
    fPendingHlsMetadata.erase(fPendingHlsMetadata.begin());

    DEBUG_PRINT("HLS timed metadata due: artist='%s' title='%s' album='%s'\n",
                event.artist.String(), event.title.String(),
                event.album.String());
    _NotifyMetadata(event.artist, event.title, event.album, event.url);
  }
}

int NetworkAudioStreamIO::_FfmpegInterruptCallback(void *arg) {
  NetworkAudioStreamIO *self = static_cast<NetworkAudioStreamIO *>(arg);
  if (!self || !self->fRunning.load(std::memory_order_relaxed))
    return 1;

  bigtime_t deadline =
      self->fFfmpegReadDeadline.load(std::memory_order_relaxed);
  if (deadline > 0 && system_time() > deadline) {
    DEBUG_PRINT("Interrupting stalled FFmpeg read\n");
    return 1;
  }

  return 0;
}

/**
 * @brief Main streaming loop using direct FFmpeg API.
 *
 * FFmpeg handles HTTP/HTTPS, ICY metadata, codecs and HLS segmenting. Decoded
 * float PCM is written into the ring buffer for BSoundPlayer.
 */
void NetworkAudioStreamIO::_FfmpegLoop() {
  DEBUG_PRINT("FFmpeg stream loop started for %s\n", fUrl.String());

  AVFormatContext *fmtCtx = avformat_alloc_context();
  if (!fmtCtx) {
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }
  fmtCtx->interrupt_callback.callback = _FfmpegInterruptCallback;
  fmtCtx->interrupt_callback.opaque = this;

  AVDictionary *opts = nullptr;

  bool isHls = fUrl.IEndsWith(".m3u8");
  av_dict_set(&opts, "icy", "1", 0);
  av_dict_set(&opts, "user_agent",
              "Mozilla/5.0 (Haiku; x86_64) AppleWebKit/605.1.15 (KHTML, like Gecko) WebPositive/1.0", 0);
  av_dict_set(&opts, "reconnect", "1", 0);
  if (!isHls) {
    av_dict_set(&opts, "reconnect_at_eof", "1", 0);
  } else {
    av_dict_set(&opts, "max_reload", "8", 0);
    av_dict_set(&opts, "m3u8_hold_counters", "8", 0);
  }
  av_dict_set(&opts, "reconnect_streamed", "1", 0);
  av_dict_set(&opts, "reconnect_on_network_error", "1", 0);
  av_dict_set(&opts, "reconnect_delay_max", "5", 0);
  av_dict_set(&opts, "rw_timeout", "15000000", 0);

  DEBUG_PRINT("FFmpeg opening input: %s\n", fUrl.String());
  fFfmpegReadDeadline = system_time() + 15000000;
  if (avformat_open_input(&fmtCtx, fUrl.String(), nullptr, &opts) < 0) {
    DEBUG_PRINT("avformat_open_input failed for %s\n", fUrl.String());
    av_dict_free(&opts);
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }
  DEBUG_PRINT("FFmpeg input opened: %s\n", fUrl.String());
  fFfmpegReadDeadline = 0;
  av_dict_free(&opts);

  fFfmpegReadDeadline = system_time() + 15000000;
  if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
    DEBUG_PRINT("avformat_find_stream_info failed\n");
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }
  fFfmpegReadDeadline = 0;

  _ProcessFfmpegMetadata(fmtCtx->metadata);
  _PollFfmpegIcyMetadata(fmtCtx);
  for (unsigned int i = 0; i < fmtCtx->nb_streams; ++i)
    _ProcessFfmpegMetadata(fmtCtx->streams[i]->metadata);

  int audioIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
  if (audioIdx < 0) {
    DEBUG_PRINT("No audio stream found\n");
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }

  AVStream *stream = fmtCtx->streams[audioIdx];
  const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
  if (!codec) {
    DEBUG_PRINT("No decoder found for codec id %d\n", stream->codecpar->codec_id);
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }

  AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
  if (!codecCtx) {
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }

  if (avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0) {
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }

  if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
    DEBUG_PRINT("avcodec_open2 failed\n");
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }

  if (codecCtx->ch_layout.nb_channels <= 0) {
    int channels = stream->codecpar->ch_layout.nb_channels > 0
                       ? stream->codecpar->ch_layout.nb_channels
                       : 2;
    av_channel_layout_default(&codecCtx->ch_layout, channels);
  }

  SwrContext *swr = nullptr;
  AVChannelLayout outLayout;
  av_channel_layout_copy(&outLayout, &codecCtx->ch_layout);

  int ret = swr_alloc_set_opts2(&swr,
                                &outLayout,
                                AV_SAMPLE_FMT_FLT,
                                codecCtx->sample_rate,
                                &codecCtx->ch_layout,
                                codecCtx->sample_fmt,
                                codecCtx->sample_rate,
                                0, nullptr);
  if (ret < 0 || !swr || swr_init(swr) < 0) {
    DEBUG_PRINT("swr_alloc_set_opts2 / swr_init failed\n");
    if (swr) swr_free(&swr);
    av_channel_layout_uninit(&outLayout);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fHlsFormatReady);
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }

  fHlsFormat.frame_rate = codecCtx->sample_rate;
  fHlsFormat.channel_count = codecCtx->ch_layout.nb_channels;
  fHlsFormat.format = media_raw_audio_format::B_AUDIO_FLOAT;
  fHlsFormat.byte_order = B_MEDIA_HOST_ENDIAN;
  fHlsFormat.buffer_size = 8192;
  fHlsFormatKnown = true;
  release_sem(fHlsFormatReady);

  DEBUG_PRINT("FFmpeg setup OK: input=%s codec=%s sample_fmt=%s rate=%d "
              "channels=%d hls=%d\n",
              fmtCtx->iformat && fmtCtx->iformat->name
                  ? fmtCtx->iformat->name : "?",
              codec->name ? codec->name : "?",
              SampleFmtName(codecCtx->sample_fmt),
              codecCtx->sample_rate, codecCtx->ch_layout.nb_channels,
              (int)isHls);

  const int channels = codecCtx->ch_layout.nb_channels;
  const size_t hlsMetadataMinBuffer =
      (size_t)codecCtx->sample_rate * channels * sizeof(float) * 4;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *frame = av_frame_alloc();
  if (!pkt || !frame) {
    DEBUG_PRINT("Could not allocate FFmpeg packet/frame\n");
    av_frame_free(&frame);
    av_packet_free(&pkt);
    swr_free(&swr);
    av_channel_layout_uninit(&outLayout);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);
    fRequestRunning = false;
    release_sem(fDataReady);
    MaybeDeinitFfmpegNetwork();
    return;
  }

  /// Maximum number of consecutive av_read_frame() failures before giving up
  /// on an HLS stream. Each retry waits 1 second, giving the network ~10s to
  /// recover from transient errors or segment-boundary hiccups.
  static const int kMaxHlsRetries = 10;
  int hlsRetries = 0;
#if 0
  bigtime_t lastDecodeDebugLog = 0;
  size_t decodedFramesSinceLog = 0;
  size_t convertedBytesSinceLog = 0;
  size_t writtenBytesSinceLog = 0;
  float decodePeakSinceLog = 0.0f;
  int lastSwrResult = 0;
#endif

  while (fRunning) {
    _DispatchDueHlsMetadata();

    bigtime_t seekTime =
        fPendingSeekTime.exchange(-1, std::memory_order_acq_rel);
    if (seekTime >= 0) {
      int64_t seekTarget = av_rescale_q(
          seekTime, AVRational{1, 1000000}, stream->time_base);
      ret = av_seek_frame(fmtCtx, audioIdx, seekTarget, AVSEEK_FLAG_BACKWARD);
      DEBUG_PRINT("FFmpeg seek to %lld us returned %d\n",
                  (long long)seekTime, ret);
      if (ret >= 0) {
        avcodec_flush_buffers(codecCtx);
        swr_close(swr);
        swr_init(swr);
        av_packet_unref(pkt);
        fWritePos.store(0, std::memory_order_relaxed);
        fTotalWritten.store(0, std::memory_order_release);
        fReadPos.store(0, std::memory_order_relaxed);
        fValidStart.store(0, std::memory_order_relaxed);
        release_sem(fDataReady);
      }
    }

    bigtime_t hlsMetadataPollInterval = 10000000;
    size_t hlsBuffered = HlsBufferedBytes(
        fTotalWritten.load(std::memory_order_acquire),
        fReadPos.load(std::memory_order_relaxed));
    if (isHls && hlsBuffered >= hlsMetadataMinBuffer &&
        system_time() - fLastHlsMetadataPoll >= hlsMetadataPollInterval) {
      _PollHlsPlaylistMetadata();
      fLastHlsMetadataPoll = system_time();
      _DispatchDueHlsMetadata();
    }

    fFfmpegReadDeadline = system_time() + (isHls ? 12000000 : 15000000);
    ret = av_read_frame(fmtCtx, pkt);
    fFfmpegReadDeadline = 0;
    if (ret < 0) {
      if (isHls && fRunning) {
        hlsRetries++;
        DEBUG_PRINT("av_read_frame error %d (HLS retry %d/%d)\n",
                    ret, hlsRetries, kMaxHlsRetries);
        if (hlsRetries >= kMaxHlsRetries) {
          DEBUG_PRINT("HLS max retries reached, giving up\n");
          break;
        }
        av_packet_unref(pkt);
        snooze(1000000);
        continue;
      }
      DEBUG_PRINT("av_read_frame ended (%d)\n", ret);
      break;
    }
    hlsRetries = 0;

    _ProcessFfmpegMetadata(fmtCtx->metadata);
    _PollFfmpegIcyMetadata(fmtCtx);
    if (pkt->stream_index >= 0 &&
        pkt->stream_index < (int)fmtCtx->nb_streams) {
      _ProcessFfmpegMetadata(fmtCtx->streams[pkt->stream_index]->metadata);
    }

#if defined(AV_PKT_DATA_STRINGS_METADATA)
#if LIBAVCODEC_VERSION_MAJOR >= 60
    size_t sideDataSize = 0;
#else
    int sideDataSize = 0;
#endif
    uint8_t *sideData = av_packet_get_side_data(
        pkt, AV_PKT_DATA_STRINGS_METADATA, &sideDataSize);
    if (sideData && sideDataSize > 0) {
      AVDictionary *sideDict = nullptr;
      if (av_packet_unpack_dictionary(sideData, sideDataSize, &sideDict) >= 0) {
        _ProcessFfmpegMetadata(sideDict);
        av_dict_free(&sideDict);
      }
    }
#endif

    if (IsLikelyId3(pkt->data, pkt->size)) {
      _ProcessHlsTimedMetadata(pkt->data, pkt->size);
      av_packet_unref(pkt);
      continue;
    }

    if (pkt->stream_index != audioIdx) {
      _ProcessHlsTimedMetadata(pkt->data, pkt->size);
      av_packet_unref(pkt);
      continue;
    }

    ret = avcodec_send_packet(codecCtx, pkt);
    if (ret >= 0) {
      while (avcodec_receive_frame(codecCtx, frame) == 0) {
        if (!fRunning)
          break;

        int outSamples = frame->nb_samples;
        size_t outBytes = outSamples * channels * sizeof(float);
        uint8 *outBuf = new uint8[outBytes];
        uint8 *outPtr = outBuf;

        int convertedSamples =
            swr_convert(swr, (uint8_t **)&outPtr, outSamples,
                        (const uint8_t **)frame->extended_data,
                        frame->nb_samples);
        (void)convertedSamples;
#if 0
        lastSwrResult = convertedSamples;

        float peak = convertedSamples > 0
                         ? PcmPeakFloat(outBuf, (size_t)convertedSamples *
                                                    channels * sizeof(float))
                         : 0.0f;
        if (peak > decodePeakSinceLog)
          decodePeakSinceLog = peak;
        decodedFramesSinceLog++;
        if (convertedSamples > 0) {
          convertedBytesSinceLog +=
              (size_t)convertedSamples * channels * sizeof(float);
        }
        ssize_t written = Write(outBuf, outBytes);
        if (written > 0)
          writtenBytesSinceLog += (size_t)written;
        bigtime_t now = system_time();
        if (now - lastDecodeDebugLog >= 1000000) {
          DEBUG_PRINT("FFmpeg decode: frames=%zu convertedBytes=%zu "
                      "writtenBytes=%zu peak=%.6f "
                      "lastSWR=%d frameSamples=%d fmt=%s buffered=%lld\n",
                      decodedFramesSinceLog, convertedBytesSinceLog,
                      writtenBytesSinceLog, decodePeakSinceLog, lastSwrResult,
                      frame->nb_samples,
                      SampleFmtName((AVSampleFormat)frame->format),
                      (long long)(fTotalWritten.load(
                                      std::memory_order_acquire) -
                                  fReadPos.load(std::memory_order_relaxed)));
          lastDecodeDebugLog = now;
          decodedFramesSinceLog = 0;
          convertedBytesSinceLog = 0;
          writtenBytesSinceLog = 0;
          decodePeakSinceLog = 0.0f;
        }
#else
        Write(outBuf, outBytes);
#endif

        delete[] outBuf;
      }
    }
    av_packet_unref(pkt);
  }

  if (fRunning) {
    avcodec_send_packet(codecCtx, nullptr);
    while (avcodec_receive_frame(codecCtx, frame) == 0) {
      int outSamples = frame->nb_samples;
      size_t outBytes = outSamples * channels * sizeof(float);
      uint8 *outBuf = new uint8[outBytes];
      uint8 *outPtr = outBuf;

      int convertedSamples =
          swr_convert(swr, (uint8_t **)&outPtr, outSamples,
                      (const uint8_t **)frame->extended_data,
                      frame->nb_samples);
      DEBUG_PRINT("FFmpeg drain frame: samples=%d converted=%d peak=%.6f\n",
                  frame->nb_samples, convertedSamples,
                  convertedSamples > 0
                      ? PcmPeakFloat(outBuf, (size_t)convertedSamples *
                                                channels * sizeof(float))
                      : 0.0f);

      ssize_t written = Write(outBuf, outBytes);
      DEBUG_PRINT("FFmpeg drain write: bytes=%ld requested=%zu\n",
                  (long)written, outBytes);
      delete[] outBuf;
    }
  }

  av_frame_free(&frame);
  av_packet_free(&pkt);
  swr_free(&swr);
  av_channel_layout_uninit(&outLayout);
  avcodec_free_context(&codecCtx);
  avformat_close_input(&fmtCtx);
  fRequestRunning = false;
  fRunning = false;
  release_sem(fDataReady);
  MaybeDeinitFfmpegNetwork();

  DEBUG_PRINT("FFmpeg stream loop ended\n");
}
