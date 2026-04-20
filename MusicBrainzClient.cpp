#include "MusicBrainzClient.h"
#include "Debug.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <DataIO.h>
#include <fcntl.h>
#include <Locker.h>
#include <memory>
#include <new>
#include <OS.h>
#include <stdexcept>
#include <String.h>
#include <Url.h>
#include <unistd.h>

#include <private/netservices/HttpRequest.h>
#include <private/netservices/HttpResult.h>
#include <private/netservices/UrlResult.h>

#include <musicbrainz5/Artist.h>
#include <musicbrainz5/ArtistCredit.h>
#include <musicbrainz5/Medium.h>
#include <musicbrainz5/MediumList.h>
#include <musicbrainz5/Metadata.h>
#include <musicbrainz5/NameCredit.h>
#include <musicbrainz5/Query.h>
#include <musicbrainz5/Recording.h>
#include <musicbrainz5/Release.h>
#include <musicbrainz5/ReleaseGroup.h>
#include <musicbrainz5/Track.h>
#include <musicbrainz5/TrackList.h>

/**
 * @class ScopedSilence
 * @brief Helper to silence libmusicbrainz5's stdout/stderr output during
 * queries.
 *
 * libmusicbrainz5 can be chatty on stderr. This class redirects stderr to
 * /dev/null within its scope, using a static lock to ensure thread safety
 * (preventing races on the global file descriptors).
 */
class ScopedSilence {
public:
  ScopedSilence() {
    fLocker.Lock();
    fflush(stderr);
    fOldStdErr = dup(STDERR_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    dup2(devNull, STDERR_FILENO);
    close(devNull);
  }

  ~ScopedSilence() {
    fflush(stderr);
    dup2(fOldStdErr, STDERR_FILENO);
    close(fOldStdErr);
    fLocker.Unlock();
  }

private:
  int fOldStdErr;
  static BLocker fLocker;
};

BLocker ScopedSilence::fLocker("SilenceLocker");

using namespace MusicBrainz5;
using namespace BPrivate::Network;

/**
 * @brief Runs a MusicBrainz query with a timeout in a separate thread.
 *
 * libmusicbrainz5 queries are blocking and can hang indefinitely if the network
 * stalls. This wrapper runs the query in a thread and waits with a timeout.
 *
 * @param userAgent The User-Agent string for the request.
 * @param entity The MB entity to query (e.g., "recording", "release").
 * @param id The entity ID (UUID).
 * @param resource Additional resource path (e.g., "inc").
 * @param params Query parameters.
 * @param shouldCancel Callback to check for cancellation request.
 * @return CMetadata result from the query, or empty if failed/timed out.
 */
static CMetadata RunQueryWithTimeout(const BString &userAgent,
                                     const std::string &entity,
                                     const std::string &id,
                                     const std::string &resource,
                                     const CQuery::tParamMap &params,
                                     std::function<bool()> shouldCancel) {
  struct Context {
    BString ua;
    std::string e, i, r;
    CQuery::tParamMap p;
    CMetadata *result = nullptr;
    std::string error;
  } ctx{userAgent, entity, id, resource, params};

  thread_id tid = spawn_thread(
      [](void *data) -> int32 {
        Context *c = (Context *)data;
        try {
          ScopedSilence silence;
          CQuery q(c->ua.String());
          c->result = new CMetadata(q.Query(c->e, c->i, c->r, c->p));
        } catch (const std::exception &ex) {
          c->error = ex.what();
        } catch (...) {
          c->error = "Unknown non-std exception in MusicBrainz query";
        }
        return 0;
      },
      "mb_query_thread", B_NORMAL_PRIORITY, &ctx);

  if (tid < 0)
    return CMetadata();

  resume_thread(tid);

  status_t exit;

  for (int i = 0; i < 200; i++) {
    if (shouldCancel && shouldCancel()) {
      kill_thread(tid);
      return CMetadata();
    }
    if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 100000, &exit) == B_OK) {

      if (!ctx.error.empty()) {
        throw std::runtime_error(ctx.error);
      }
      if (ctx.result) {
        CMetadata m = *ctx.result;
        delete ctx.result;
        return m;
      }
      return CMetadata();
    }
  }

  kill_thread(tid);
  throw std::runtime_error("Timeout waiting for MusicBrainz");
}

MusicBrainzClient::MusicBrainzClient(const BString &contact)
    : fContact(contact) {}

/**
 * @brief Enforces MusicBrainz rate limiting (approx. 1 request per second).
 */
void MusicBrainzClient::_RespectRateLimit() {
  bigtime_t now = system_time();
  bigtime_t diff = now - fLastCall;
  if (diff < 1100000) {
    snooze(1100000 - diff);
  }
}

/**
 * @brief Searches for recordings matching the criteria.
 *
 * Constructs a Lucene query string (artist AND recording AND release) and
 * sends it to MusicBrainz. Retries on failure with exponential backoff.
 *
 * @param artist Artist name.
 * @param title Track title.
 * @param albumOpt Optional album name.
 * @param shouldCancel Cancellation callback.
 * @return Vector of MBHit structures containing search results.
 */
std::vector<MBHit>
MusicBrainzClient::SearchRecording(const BString &artist, const BString &title,
                                   const BString &albumOpt,
                                   std::function<bool()> shouldCancel) {
  std::vector<MBHit> results;
  try {
    if (shouldCancel && shouldCancel())
      return results;
    _RespectRateLimit();

    BString ua;
    ua.SetToFormat("BeTon/0.1 (%s)", fContact.String());
    DEBUG_PRINT("[MBClient] User-Agent: '%s'\\n", ua.String());

    fLastCall = system_time();

    BString query;

    BString escArtist = artist;
    escArtist.ReplaceAll("\"", "\\\"");
    BString escTitle = title;
    escTitle.ReplaceAll("\"", "\\\"");

    query.SetToFormat("artist:\"%s\" AND recording:\"%s\"", escArtist.String(),
                      escTitle.String());

    if (!albumOpt.IsEmpty()) {
      BString escAlbum = albumOpt;
      escAlbum.ReplaceAll("\"", "\\\"");
      query << " AND release:\"" << escAlbum << "\"";
    }

    DEBUG_PRINT("[MBClient] Search Query: '%s'\\n", query.String());

    int retries = 3;
    CMetadata meta;
    while (retries > 0) {
      if (shouldCancel && shouldCancel()) {
        DEBUG_PRINT("[MBClient] Cancelled by user during retry loop.\\n");
        return results;
      }
      try {

        CQuery::tParamMap params;
        params["query"] = query.String();

        meta =
            RunQueryWithTimeout(ua, "recording", "", "", params, shouldCancel);
        break;
      } catch (const std::exception &e) {
        DEBUG_PRINT("[MBClient] Exception in Query: %s. Retries left: %d\\n",
                    e.what(), retries - 1);
        retries--;
        if (retries == 0)
          break;

        for (int i = 0; i < 10; i++) {
          if (shouldCancel && shouldCancel())
            return results;
          snooze(100000);
        }
      }
    }

    if (auto rl = meta.RecordingList()) {
      for (int i = 0; i < rl->NumItems(); i++) {
        if (auto rec = rl->Item(i)) {
          MBHit hit;
          hit.recordingId = rec->ID().c_str();
          hit.title = rec->Title().c_str();

          if (auto ac = rec->ArtistCredit()) {
            if (auto ncl = ac->NameCreditList()) {
              BString artStr;
              for (int k = 0; k < ncl->NumItems(); k++) {
                if (auto nc = ncl->Item(k)) {
                  if (auto a = nc->Artist()) {
                    if (k > 0)
                      artStr << ", ";
                    artStr << a->Name().c_str();
                  }
                }
              }
              hit.artist = artStr;
            }
          }

          if (auto rlist = rec->ReleaseList()) {
            if (rlist->NumItems() > 0) {
              for (int j = 0; j < rlist->NumItems(); j++) {
                if (auto rel = rlist->Item(j)) {
                  MBHit specificHit = hit;
                  specificHit.releaseId = rel->ID().c_str();
                  specificHit.releaseTitle = rel->Title().c_str();

                  specificHit.country = rel->Country().c_str();

                  std::string d = rel->Date().c_str();
                  if (!d.empty()) {
                    specificHit.year = atoi(d.c_str());
                  }

                  int totalTracks = 0;
                  if (auto ml = rel->MediumList()) {
                    for (int m = 0; m < ml->NumItems(); m++) {
                      if (auto med = ml->Item(m)) {
                        if (auto tl = med->TrackList()) {

                          totalTracks += tl->Count();
                        }
                      }
                    }
                  }
                  specificHit.trackCount = totalTracks;

                  results.push_back(specificHit);
                }
              }
            } else {

              results.push_back(hit);
            }
          } else {
            results.push_back(hit);
          }
        }
      }
    }

  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &e) {
    DEBUG_PRINT("[MBClient] std::exception in SearchRecording: %s\\n",
                e.what());
  } catch (...) {
    DEBUG_PRINT("[MBClient] Unknown non-std exception in SearchRecording\\n");
  }
  return results;
}

/**
 * @brief Fetches full release details given a release ID.
 *
 * Used to get the full tracklist and metadata for a specific album release.
 *
 * @param releaseId MBID of the release.
 * @param shouldCancel Cancellation callback.
 * @return MBRelease structure with detailed info.
 */
MBRelease
MusicBrainzClient::GetReleaseDetails(const BString &releaseId,
                                     std::function<bool()> shouldCancel) {
  MBRelease out;
  out.releaseId = releaseId;

  try {
    if (shouldCancel && shouldCancel())
      return out;
    _RespectRateLimit();

    BString ua;
    ua.SetToFormat("BeTon/0.1 (%s)", fContact.String());

    fLastCall = system_time();

    CQuery::tParamMap params;
    params["inc"] = "recordings media artist-credits release-groups";

    CMetadata meta = RunQueryWithTimeout(ua, "release", releaseId.String(), "",
                                         params, shouldCancel);

    auto rel = meta.Release();
    if (!rel)
      return out;

    out.album = rel->Title().c_str();

    if (auto ac = rel->ArtistCredit()) {
      if (auto ncl = ac->NameCreditList(); ncl && ncl->NumItems() > 0) {
        if (auto nc = ncl->Item(0); nc && nc->Artist())
          out.albumArtist = nc->Artist()->Name().c_str();
      }
    }

    if (auto rg = rel->ReleaseGroup()) {
      out.releaseGroupId = rg->ID().c_str();
    }

    {
      std::string d = rel->Date().c_str();
      if (!d.empty()) {
        out.year = atoi(d.c_str());
      }
    }

    if (auto ml = rel->MediumList()) {
      for (int m = 0; m < ml->NumItems(); m++) {
        if (auto med = ml->Item(m)) {
          int discNum = med->Position();
          if (auto tl = med->TrackList()) {
            for (int t = 0; t < tl->NumItems(); t++) {
              if (auto trk = tl->Item(t)) {
                MBTrack mt;
                mt.disc = discNum;
                mt.track = trk->Position();
                mt.length = trk->Length() / 1000;
                if (auto r = trk->Recording()) {
                  mt.title = r->Title().c_str();
                  mt.recordingId = r->ID().c_str();
                }
                out.tracks.push_back(mt);
              }
            }
          }
        }
      }
    }

  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &e) {
    DEBUG_PRINT("[MBClient] std::exception in GetReleaseDetails: %s\\n",
                e.what());
  } catch (...) {
    DEBUG_PRINT("[MBClient] Unknown non-std exception in GetReleaseDetails\\n");
  }

  return out;
}

/**
 * @brief Fetches cover art from the Cover Art Archive.
 *
 * @param entityId MBID (Release UUID usually).
 * @param outBytes Vector to store image data.
 * @param outMime BString to store MIME type.
 * @param sizeHint Size hint (e.g., 250, 500, 1200) or 0 for original.
 * @param isReleaseGroup True if entityId is a Release Group ID (fallback).
 * @param shouldCancel Cancellation callback.
 * @return True if successful.
 */
bool MusicBrainzClient::FetchCover(const BString &entityId,
                                   std::vector<uint8_t> &outBytes,
                                   BString *outMime, int sizeHint,
                                   bool isReleaseGroup,
                                   std::function<bool()> shouldCancel) {
  if (shouldCancel && shouldCancel())
    return false;
  outBytes.clear();
  if (outMime)
    outMime->Truncate(0);

  BString urlStr;
  BString entity = isReleaseGroup ? "release-group" : "release";

  if (sizeHint > 0 && !isReleaseGroup)
    urlStr.SetToFormat("https://coverartarchive.org/%s/%s/front-%d",
                       entity.String(), entityId.String(), sizeHint);
  else
    urlStr.SetToFormat("https://coverartarchive.org/%s/%s/front",
                       entity.String(), entityId.String());

  DEBUG_PRINT("[MBClient] FetchCover: URL='%s'\\n", urlStr.String());

  int status = _FetchUrl(urlStr, outBytes, outMime);

  if (status == 200)
    return true;

  if (sizeHint > 0 && status == 404 && !isReleaseGroup) {
    DEBUG_PRINT("[MBClient] FetchCover: 404 with size hint, retrying without "
                "size...\\n");
    return FetchCover(entityId, outBytes, outMime, 0, isReleaseGroup,
                      shouldCancel);
  }

  return false;
}

/**
 * @brief Internal helper to fetch data from a URL with redirect support.
 *
 * Uses Haiku's BUrlRequest and friends.
 *
 * @param urlStr The URL to fetch.
 * @param outBytes Output buffer.
 * @param outMime Output MIME string.
 * @param maxRedirects Maximum number of redirects to follow.
 * @return HTTP status code or 0 on error.
 */
int MusicBrainzClient::_FetchUrl(const BString &urlStr,
                                 std::vector<uint8_t> &outBytes,
                                 BString *outMime, int maxRedirects) {

  if (maxRedirects < 0) {
    DEBUG_PRINT("[MBClient] _FetchUrl: Max redirects reached.\\n");
    return 301;
  }

  DEBUG_PRINT("[MBClient] _FetchUrl: Requesting '%s' (redirects left=%d)\\n",
              urlStr.String(), maxRedirects);

  try {
    _RespectRateLimit();
    fLastCall = system_time();

    BMallocIO sink;
#if B_HAIKU_VERSION <= B_HAIKU_VERSION_1_BETA_5
    BUrl url(urlStr.String());
#else
    BUrl url(urlStr.String(), true);
#endif
    std::unique_ptr<BUrlRequest> req(
        BUrlProtocolRoster::MakeRequest(url, &sink));
    if (!req) {
      DEBUG_PRINT("[MBClient] _FetchUrl: Failed to make request object.\\n");
      return 0;
    }

    if (auto http = dynamic_cast<BHttpRequest *>(req.get())) {
      BString ua;
      ua.SetToFormat("BeTon/0.1 (%s)", fContact.String());
      http->SetUserAgent(ua.String());
      http->SetFollowLocation(false);
    }

    thread_id tid = req->Run();
    if (tid >= 0) {
      rename_thread(tid, "MB Request");
      status_t exit;

      if (wait_for_thread_etc(tid, B_RELATIVE_TIMEOUT, 20000000, &exit) !=
          B_OK) {
        DEBUG_PRINT(
            "[MBClient] _FetchUrl: Timeout waiting for request thread.\\n");
        req->Stop();
        kill_thread(tid);

        return 408;
      }
    }

    const BUrlResult &baseRes = req->Result();
    const BHttpResult *httpRes = dynamic_cast<const BHttpResult *>(&baseRes);
    if (!httpRes) {
      DEBUG_PRINT("[MBClient] _FetchUrl: Result is not BHttpResult.\\n");
      return 0;
    }

    int status = httpRes->StatusCode();
    DEBUG_PRINT("[MBClient] _FetchUrl: HTTP Status=%d\\n", status);

    if (status == 301 || status == 302 || status == 307) {
      BString loc = httpRes->Headers().HeaderValue("Location");
      if (!loc.IsEmpty()) {
        DEBUG_PRINT("[MBClient] _FetchUrl: Redirecting to '%s'\\n",
                    loc.String());
        return _FetchUrl(loc, outBytes, outMime, maxRedirects - 1);
      } else {
        DEBUG_PRINT("[MBClient] _FetchUrl: Redirect status %d but no Location "
                    "header.\\n",
                    status);
      }
    }

    if (status != 200)
      return status;

    BString ctype = httpRes->Headers().HeaderValue("Content-Type");
    if (outMime)
      *outMime = ctype;

    const void *buf = sink.Buffer();
    const size_t len = sink.BufferLength();
    DEBUG_PRINT("[MBClient] _FetchUrl: Got %zu bytes, type='%s'\\n", len,
                ctype.String());

    if (!buf || len == 0)
      return 500;

    outBytes.resize(len);
    std::memcpy(outBytes.data(), buf, len);
    return 200;

  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &e) {
    DEBUG_PRINT("[MBClient] std::exception in _FetchUrl: %s\\n", e.what());
    return 0;
  } catch (...) {
    DEBUG_PRINT("[MBClient] Unknown non-std exception in _FetchUrl\\n");
    return 0;
  }
}

BString
MusicBrainzClient::BestReleaseForRecording(const BString &recordingId,
                                           std::function<bool()> shouldCancel) {
  try {
    if (shouldCancel && shouldCancel())
      return "";
    _RespectRateLimit();

    BString ua;
    ua.SetToFormat("BeTon/0.1 (%s)", fContact.String());

    fLastCall = system_time();

    CQuery::tParamMap params;
    params["inc"] = "releases";

    CMetadata meta = RunQueryWithTimeout(ua, "recording", recordingId.String(),
                                         "", params, shouldCancel);

    if (auto rec = meta.Recording()) {
      if (auto rl = rec->ReleaseList(); rl && rl->NumItems() > 0) {
        return BString(rl->Item(0)->ID().c_str());
      }
    }
  } catch (const std::bad_alloc &) {
    throw;
  } catch (const std::exception &e) {
    DEBUG_PRINT("[MBClient] std::exception in BestReleaseForRecording: %s\\n",
                e.what());
  } catch (...) {
    DEBUG_PRINT(
        "[MBClient] Unknown non-std exception in BestReleaseForRecording\\n");
  }
  return "";
}
