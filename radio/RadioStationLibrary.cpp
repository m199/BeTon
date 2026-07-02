#include "RadioStationLibrary.h"
#include "Debug.h"
#include "Messages.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Url.h>
#include <UrlProtocolRoster.h>
#include <UrlRequest.h>
#include <private/netservices/HttpRequest.h>
#include <private/netservices/HttpResult.h>

#include <DataIO.h>
#include <OS.h>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace BPrivate::Network;

RadioStationLibrary::RadioStationLibrary(BMessenger target) : fTarget(target) {}

RadioStationLibrary::~RadioStationLibrary() {}

/**
 * @brief Builds the full path to the radio settings file.
 * @return Absolute path string, e.g. ~/config/settings/Beton/radio.settings.
 */
BString RadioStationLibrary::_SettingsPath() const {
  BPath path;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
    return "";
  path.Append("BeTon/radio.settings");
  return BString(path.Path());
}

/**
 * @brief Loads radio stations from the BMessage-based settings file.
 *
 * The file format stores each station as a nested BMessage with
 * fields: name, url, genre, country, language, logoUrl, favorite.
 *
 * @return True if at least one station was loaded.
 */
bool RadioStationLibrary::LoadStations() {
  fStations.clear();

  BString settingsPath = _SettingsPath();
  if (settingsPath.IsEmpty())
    return false;

  BFile file(settingsPath.String(), B_READ_ONLY);
  if (file.InitCheck() != B_OK) {
    DEBUG_PRINT("No settings file found: %s\n",
                settingsPath.String());
    return false;
  }

  BMessage archive;
  if (archive.Unflatten(&file) != B_OK) {
    DEBUG_PRINT("Could not unflatten settings\n");
    return false;
  }

  BMessage stationMsg;
  for (int32 i = 0; archive.FindMessage("station", i, &stationMsg) == B_OK;
       i++) {
    RadioStation rs;
    rs.name = stationMsg.GetString("name", "");
    rs.url = stationMsg.GetString("url", "");
    rs.genre = stationMsg.GetString("genre", "");
    rs.country = stationMsg.GetString("country", "");
    rs.language = stationMsg.GetString("language", "");
    rs.logoUrl = stationMsg.GetString("logoUrl", "");
    rs.favorite = stationMsg.GetBool("favorite", false);

    if (!rs.url.IsEmpty()) {
      fStations.push_back(rs);
    }
  }

  DEBUG_PRINT("Loaded %zu stations\n", fStations.size());
  return !fStations.empty();
}

/**
 * @brief Saves all radio stations to the settings file.
 *
 * Creates the Beton settings directory if it does not exist.
 * Each station is stored as a nested BMessage.
 *
 * @return True on success.
 */
bool RadioStationLibrary::SaveStations() {
  BString settingsPath = _SettingsPath();
  if (settingsPath.IsEmpty())
    return false;

  BPath dirPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &dirPath) != B_OK)
    return false;
  dirPath.Append("BeTon");
  create_directory(dirPath.Path(), 0755);

  BMessage archive;
  for (const auto &rs : fStations) {
    BMessage stationMsg;
    stationMsg.AddString("name", rs.name);
    stationMsg.AddString("url", rs.url);
    stationMsg.AddString("genre", rs.genre);
    stationMsg.AddString("country", rs.country);
    stationMsg.AddString("language", rs.language);
    stationMsg.AddString("logoUrl", rs.logoUrl);
    stationMsg.AddBool("favorite", rs.favorite);
    archive.AddMessage("station", &stationMsg);
  }

  BFile file(settingsPath.String(),
             B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
  if (file.InitCheck() != B_OK) {
    DEBUG_PRINT("Could not create settings file: %s\n",
                settingsPath.String());
    return false;
  }

  if (archive.Flatten(&file) != B_OK) {
    DEBUG_PRINT("Could not flatten settings\n");
    return false;
  }

  DEBUG_PRINT("Saved %zu stations to %s\n", fStations.size(),
              settingsPath.String());
  return true;
}

/**
 * @brief Adds a new station and persists immediately.
 * @param station The station to add.
 * @return Index of the newly added station.
 */
int32 RadioStationLibrary::AddStation(const RadioStation &station) {
  fStations.push_back(station);
  SaveStations();
  DEBUG_PRINT("Added station '%s' (%s)\n",
              station.name.String(), station.url.String());
  return static_cast<int32>(fStations.size() - 1);
}

/**
 * @brief Removes a station by index and persists.
 * @param index Index of the station to remove.
 * @return True if the station was removed.
 */
bool RadioStationLibrary::RemoveStation(int32 index) {
  if (index < 0 || index >= static_cast<int32>(fStations.size()))
    return false;

  DEBUG_PRINT("Removing station '%s'\n",
              fStations[index].name.String());
  fStations.erase(fStations.begin() + index);
  SaveStations();
  return true;
}

/**
 * @brief Replaces a station at the given index and persists.
 * @param index Index of the station to replace.
 * @param station New station data.
 * @return True if the station was updated.
 */
bool RadioStationLibrary::EditStation(int32 index, const RadioStation &station) {
  if (index < 0 || index >= static_cast<int32>(fStations.size()))
    return false;

  fStations[index] = station;
  SaveStations();
  DEBUG_PRINT("Edited station %ld: '%s'\n", (long)index,
              station.name.String());
  return true;
}

const std::vector<RadioStation> &RadioStationLibrary::AllStations() const {
  return fStations;
}

const RadioStation *RadioStationLibrary::StationAt(int32 index) const {
  if (index < 0 || index >= static_cast<int32>(fStations.size()))
    return nullptr;
  return &fStations[index];
}

int32 RadioStationLibrary::CountStations() const {
  return static_cast<int32>(fStations.size());
}

/**
 * @brief Imports stations from an .m3u file.
 *
 * Supports both simple M3U (one URL per line) and extended M3U
 * (#EXTM3U with #EXTINF lines providing station names).
 *
 * @param path Path to the .m3u file.
 * @return Number of stations imported.
 */
int32 RadioStationLibrary::ImportM3U(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) {
    DEBUG_PRINT("ImportM3U: could not open '%s'\n", path);
    return 0;
  }

  int32 imported = 0;
  char line[2048];
  BString pendingName;

  while (fgets(line, sizeof(line), fp)) {
    BString s(line);
    s.RemoveAll("\r");
    s.RemoveAll("\n");
    s.Trim();

    if (s.IsEmpty())
      continue;

    if (s.StartsWith("#EXTM3U"))
      continue;

    if (s.StartsWith("#EXTINF:")) {
      int32 comma = s.FindFirst(',');
      if (comma >= 0) {
        pendingName = "";
        s.CopyInto(pendingName, comma + 1, s.Length() - comma - 1);
        pendingName.Trim();
      }
      continue;
    }

    if (s.StartsWith("#"))
      continue;

    if (s.StartsWith("http://") || s.StartsWith("https://")) {
      RadioStation rs;
      rs.url = s;
      if (!pendingName.IsEmpty()) {
        rs.name = pendingName;
        pendingName = "";
      } else {
        rs.name = s;
      }
      fStations.push_back(rs);
      imported++;
    }
  }

  fclose(fp);

  if (imported > 0) {
    SaveStations();
    DEBUG_PRINT("ImportM3U: %ld stations from '%s'\n",
                (long)imported, path);
  }

  return imported;
}

/**
 * @brief Imports stations from a .pls file.
 *
 * Parses the INI-style PLS format, extracting File and Title entries.
 * Example:
 * @code
 * [playlist]
 * File1=http://stream.example.com/radio.mp3
 * Title1=Example Radio
 * NumberOfEntries=1
 * @endcode
 *
 * @param path Path to the .pls file.
 * @return Number of stations imported.
 */
int32 RadioStationLibrary::ImportPLS(const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) {
    DEBUG_PRINT("ImportPLS: could not open '%s'\n", path);
    return 0;
  }

  std::vector<BString> urls;
  std::vector<BString> titles;

  char line[2048];
  while (fgets(line, sizeof(line), fp)) {
    BString s(line);
    s.RemoveAll("\r");
    s.RemoveAll("\n");
    s.Trim();

    if (s.IStartsWith("File")) {
      int32 eq = s.FindFirst('=');
      if (eq >= 0) {
        BString url;
        s.CopyInto(url, eq + 1, s.Length() - eq - 1);
        url.Trim();
        urls.push_back(url);
      }
    } else if (s.IStartsWith("Title")) {
      int32 eq = s.FindFirst('=');
      if (eq >= 0) {
        BString title;
        s.CopyInto(title, eq + 1, s.Length() - eq - 1);
        title.Trim();
        titles.push_back(title);
      }
    }
  }

  fclose(fp);

  int32 imported = 0;
  for (size_t i = 0; i < urls.size(); i++) {
    if (urls[i].StartsWith("http://") || urls[i].StartsWith("https://")) {
      RadioStation rs;
      rs.url = urls[i];
      rs.name = (i < titles.size() && !titles[i].IsEmpty()) ? titles[i]
                                                             : urls[i];
      fStations.push_back(rs);
      imported++;
    }
  }

  if (imported > 0) {
    SaveStations();
    DEBUG_PRINT("ImportPLS: %ld stations from '%s'\n",
                (long)imported, path);
  }

  return imported;
}

/**
 * @brief Resolves a playlist URL (.m3u/.pls) to its first stream URL.
 *
 * Downloads the playlist file via HTTP(S), parses it, and returns the
 * first stream URL found. If the input URL is not a playlist (no .m3u,
 * .m3u8 or .pls extension), it is returned unchanged.
 *
 * @param url The input URL (may be playlist or direct stream).
 * @return The resolved direct stream URL.
 */
BString RadioStationLibrary::ResolveStreamUrl(const BString &url) {
  BString workUrl = url;
  workUrl.Trim();

  bool isM3U = workUrl.IEndsWith(".m3u") || workUrl.IEndsWith(".m3u8");
  bool isPLS = workUrl.IEndsWith(".pls");
  bool hasAudioExt = workUrl.IEndsWith(".mp3") || workUrl.IEndsWith(".ogg") ||
                     workUrl.IEndsWith(".flac") || workUrl.IEndsWith(".aac") ||
                     workUrl.IEndsWith(".wav") || workUrl.IEndsWith(".m4a");

  // Known direct stream links and common stream ports can be used as-is.
  if ((hasAudioExt && !isM3U && !isPLS) || 
      workUrl.FindFirst(":8000") >= 0 || workUrl.FindFirst(":8443") >= 0 ||
      workUrl.FindFirst(":8005") >= 0 || workUrl.FindFirst(":8080") >= 0) {
    return workUrl;
  }

  DEBUG_PRINT("Resolving URL: %s\n", workUrl.String());

  BMallocIO sink;
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
  BUrl burl(workUrl.String());
#else
  BUrl burl(workUrl.String(), true);
#endif

  std::unique_ptr<BUrlRequest> req(
      BUrlProtocolRoster::MakeRequest(burl, &sink, nullptr, &fUrlContext));
  if (!req) {
    DEBUG_PRINT("ResolveStreamUrl: no request object\n");
    return workUrl;
  }

  BHttpRequest *http = dynamic_cast<BHttpRequest *>(req.get());
  if (http) {
    http->SetMethod(B_HTTP_HEAD);
    http->SetFollowLocation(false);
    BHttpHeaders headers;
    headers.AddHeader("User-Agent", "Beton/1.0 (Haiku OS; VLC-like)");
    http->SetHeaders(headers);
  }

  thread_id tid = req->Run();
  if (tid >= 0) {
    status_t exit;
    // HEAD should be quick; allow up to 10s for slow TLS handshakes.
    if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 10000000, &exit) != B_OK) {
      DEBUG_PRINT("ResolveStreamUrl: HEAD timeout\n");
      req->Stop();
      wait_for_thread(tid, &exit);
    }
  }

  // Evaluate HEAD response first (cheap probe before GET).
  bool identified = false;
  if (http) {
    const BHttpResult &res = dynamic_cast<const BHttpResult &>(http->Result());
    int32 statusCode = res.StatusCode();
    
    // Follow explicit redirects ourselves because follow-location is disabled.
    if (statusCode >= 300 && statusCode < 400) {
       BString location = res.Headers().HeaderValue("Location");
       if (!location.IsEmpty()) {
          DEBUG_PRINT("Following redirect to: %s\n", location.String());
          return ResolveStreamUrl(location);
       }
    }

    if (statusCode == 200) {
      BString contentType = res.ContentType();
      contentType.ToLower();

      DEBUG_PRINT("HEAD Success: %s\n", contentType.String());

      if (contentType.FindFirst("text/html") >= 0) {
        return "ERR:UNSUPPORTED";
      }

      if (contentType.FindFirst("mpegurl") >= 0 ||
          contentType.FindFirst("m3u") >= 0) {
        isM3U = true;
      } else if (contentType.FindFirst("scpls") >= 0 ||
                 contentType.FindFirst("pls") >= 0) {
        isPLS = true;
      } else if (contentType.FindFirst("audio/") >= 0 ||
                 contentType.FindFirst("video/") >= 0 ||
                 contentType.FindFirst("flv") >= 0) {
        return workUrl;
      }
      identified = true;
    } else {
      DEBUG_PRINT("HEAD failed (Status %ld), falling back to GET sniffing...\n",
                  (long)statusCode);
    }
  }

  // If HEAD is inconclusive, do a short GET-based content sniff.
  if (!identified) {
    sink.SetSize(0);
    sink.Seek(0, SEEK_SET);
    std::unique_ptr<BUrlRequest> getReq(
        BUrlProtocolRoster::MakeRequest(burl, &sink, nullptr, &fUrlContext));
    if (getReq) {
      BHttpRequest *getHttp = dynamic_cast<BHttpRequest *>(getReq.get());
      if (getHttp) {
        getHttp->SetFollowLocation(false);
        BHttpHeaders headers;
        headers.AddHeader("User-Agent", "Beton/1.0 (Haiku OS; VLC-like)");
        getHttp->SetHeaders(headers);
      }
      
      tid = getReq->Run();
      if (tid >= 0) {
        status_t exit;
        // Wait up to 10s for headers (TLS/remote endpoints can be slow).
        if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 10000000, &exit) != B_OK) {
           DEBUG_PRINT("GET Sniff timed out (Stopping request)\n");
           getReq->Stop();
           wait_for_thread(tid, &exit);
        }
        
        if (getHttp) {
          const BHttpResult &res = dynamic_cast<const BHttpResult &>(getHttp->Result());
          int32 code = res.StatusCode();
          
          // Handle redirects in the same way as in the HEAD branch.
          if (code >= 300 && code < 400) {
            BString location = res.Headers().HeaderValue("Location");
            if (!location.IsEmpty()) {
               DEBUG_PRINT("Following GET redirect to: %s\n", location.String());
               return ResolveStreamUrl(location);
            }
          }

          BString contentType = res.ContentType();
          contentType.ToLower();
          DEBUG_PRINT("GET Sniff: Status %ld, Type '%s'\n", (long)code, contentType.String());
          
          if (code >= 200 && code < 300) {
            if (contentType.FindFirst("text/html") >= 0) return "ERR:UNSUPPORTED";
            if (contentType.FindFirst("mpegurl") >= 0 || contentType.FindFirst("m3u") >= 0) isM3U = true;
            else if (contentType.FindFirst("scpls") >= 0 || contentType.FindFirst("pls") >= 0) isPLS = true;
            else if (contentType.FindFirst("audio/") >= 0 ||
                     contentType.FindFirst("video/") >= 0 ||
                     contentType.FindFirst("flv") >= 0) return workUrl;
          }
        }
      }
    }
  }

  // If it looks like a playlist, fetch and parse it.
  if (isM3U || isPLS) {


    DEBUG_PRINT("Fetching playlist content via GET...\n");
    sink.SetSize(0);
    sink.Seek(0, SEEK_SET);

    std::unique_ptr<BUrlRequest> getReq(
        BUrlProtocolRoster::MakeRequest(burl, &sink, nullptr, &fUrlContext));
    if (getReq) {
      if (auto getHttp = dynamic_cast<BHttpRequest *>(getReq.get())) {
        getHttp->SetFollowLocation(true);
      }
      tid = getReq->Run();
      if (tid >= 0) {
        status_t exit;
        if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 10000000, &exit) !=
            B_OK) {
          DEBUG_PRINT("ResolveStreamUrl: GET timeout\n");
          getReq->Stop();
          wait_for_thread(tid, &exit);
          return "ERR:CONNECTION";
        }
      }
    }
  } else {
    // Fallback: nothing indicates a playlist, keep original URL.
    return workUrl;
  }

  const void *buf = sink.Buffer();
  size_t len = sink.BufferLength();
  if (!buf || len == 0) {
    DEBUG_PRINT("ResolveStreamUrl: empty response\n");
    return workUrl;
  }

  BString content((const char *)buf, len);
  DEBUG_PRINT("ResolveStreamUrl: got %zu bytes\n", len);

  if (isM3U) {
    // Detect HLS manifests.
    if (content.FindFirst("#EXT-X-TARGETDURATION") >= 0 ||
        content.FindFirst("#EXT-X-STREAM-INF") >= 0) {
      DEBUG_PRINT("Detected HLS stream: %s\n", url.String());
      return workUrl; // Forward M3U8 URL; playback backend decides support.
    }

    BString line;
    int32 start = 0;
    while (start < content.Length()) {
      int32 nl = content.FindFirst('\n', start);
      if (nl < 0)
        nl = content.Length();
      content.CopyInto(line, start, nl - start);
      line.RemoveAll("\r");
      line.Trim();
      start = nl + 1;

      if (line.IsEmpty() || line.StartsWith("#"))
        continue;

      BString resolved = line;
      if (!line.StartsWith("http://") && !line.StartsWith("https://")) {
        // Resolve relative path against the playlist URL.
        int32 lastSlash = url.FindLast('/');
        if (lastSlash > 8) { // Keep scheme/host prefix (e.g. "https://.../").
          url.CopyInto(resolved, 0, lastSlash + 1);
          resolved << line;
        }
      }

      if (resolved.StartsWith("http://") || resolved.StartsWith("https://")) {
        // Nested playlist URLs are resolved recursively.
        if ((resolved.IEndsWith(".m3u") || resolved.IEndsWith(".m3u8") ||
             resolved.IEndsWith(".pls")) &&
            resolved != workUrl) {
          DEBUG_PRINT("Found nested playlist: %s\n",
                      resolved.String());
          return ResolveStreamUrl(resolved);
        }

        DEBUG_PRINT("Resolved to: %s\n", resolved.String());
        return resolved;
      }
    }
  } else if (isPLS) {
    BString line;
    int32 start = 0;
    while (start < content.Length()) {
      int32 nl = content.FindFirst('\n', start);
      if (nl < 0)
        nl = content.Length();
      content.CopyInto(line, start, nl - start);
      line.RemoveAll("\r");
      line.Trim();
      start = nl + 1;

      if (line.IStartsWith("File")) {
        int32 eq = line.FindFirst('=');
        if (eq >= 0) {
          BString streamUrl;
          line.CopyInto(streamUrl, eq + 1, line.Length() - eq - 1);
          streamUrl.Trim();
          if (streamUrl.StartsWith("http://") ||
              streamUrl.StartsWith("https://")) {
            DEBUG_PRINT("Resolved to: %s\n",
                        streamUrl.String());
            return streamUrl;
          }
        }
      }
    }
  }

  DEBUG_PRINT("ResolveStreamUrl: no stream URL found\n");
  return workUrl;
}
