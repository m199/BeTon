#include "MusicBrainzLookupController.h"

#include "MainWindow.h"
#include "Messages.h"

#include "Debug.h"
#include "MusicBrainzMatcherWindow.h"
#include "TrackMatchingUtils.h"
#include "MetadataPropertiesWindow.h"
#include "MetadataTagIO.h"

#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <Path.h>
#include <algorithm>
#include <set>
#include <atomic>
#include <cinttypes>
#include <cstdlib>
#include <exception>
#include <new>
#include <string>
#include <vector>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MusicBrainzLookupController"

/**
 * @brief Adds `TagData` fields to a media-update message payload.
 */
static void AddTagDataToMediaUpdate(BMessage &update, const TagData &td) {
  update.AddString("title", td.title);
  update.AddString("artist", td.artist);
  update.AddString("album", td.album);
  update.AddString("genre", td.genre);
  update.AddString("comment", td.comment);
  update.AddString("albumArtist", td.albumArtist);
  update.AddString("composer", td.composer);
  update.AddString("mbAlbumId", td.mbAlbumID);
  update.AddString("mbArtistId", td.mbArtistID);
  update.AddString("mbTrackId", td.mbTrackID);
  update.AddInt32("year", td.year);
  update.AddInt32("track", td.track);
  update.AddInt32("trackTotal", td.trackTotal);
  update.AddInt32("disc", td.disc);
  update.AddInt32("discTotal", td.discTotal);
  update.AddInt32("duration", td.lengthSec);
  update.AddInt32("bitrate", td.bitrate);
  update.AddInt32("sampleRate", td.sampleRate);
  update.AddInt32("channels", td.channels);
  update.AddInt32("rating", td.rating);
}

static void ReadMetadataForConfiguredTargets(const BString &filePath,
                                             TagData &td) {
  BPath path(filePath.String());
  MetadataWriteTargets targets = MetadataTagIO::WriteTargetsForPath(filePath);

  if (!targets.tags && targets.bfs)
    MetadataTagIO::ReadBfsAttributes(path, td);
  else
    MetadataTagIO::ReadTags(path, td);
}

static void WriteMetadataForConfiguredTargets(const BString &filePath,
                                              const TagData &td,
                                              const CoverBlob *cover) {
  BPath path(filePath.String());
  MetadataWriteTargets targets = MetadataTagIO::WriteTargetsForPath(filePath);

  if (targets.tags)
    MetadataTagIO::WriteTags(path, td);

  if (cover && cover->size() > 0)
    MetadataTagIO::WriteEmbeddedCover(path, *cover);

  if (targets.bfs)
    MetadataTagIO::WriteBfsAttributes(path, td, nullptr);
}

/**
 * @brief Constructs lookup controller bound to the main window.
 */
MusicBrainzLookupController::MusicBrainzLookupController(MainWindow *window)
    : fWindow(window) {}

/**
 * @brief Dispatches MusicBrainz-related messages.
 */
bool MusicBrainzLookupController::HandleMessage(BMessage *msg) {
  if (!fWindow || !msg)
    return false;

  switch (msg->what) {
  case MSG_MB_SEARCH: {
    StartSearch(msg);
    break;
  }

  case MSG_MB_CANCEL: {
    CancelOperation();
    break;
  }

  case MSG_MATCH_RESULT: {
    ApplyMatch(msg);
    break;
  }

  case MSG_MB_SEARCH_COMPLETE: {
    HandleSearchComplete(msg);
    break;
  }

  case MSG_MB_APPLY:
  case MSG_MB_APPLY_ALBUM: {
    ApplyMetadata(msg);
    break;
  }

  case MSG_COVER_FETCH_MB: {
    FetchCover(msg);
    break;
  }

  default:
    return false;
  }

  return true;
}

/**
 * @brief Starts asynchronous MusicBrainz search using current UI criteria.
 */
void MusicBrainzLookupController::StartSearch(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  BString artist, title, album;
  msg->FindString("artist", &artist);
  msg->FindString("title", &title);
  msg->FindString("album", &album);
  bool albumSearch = false;
  msg->FindBool("album_search", &albumSearch);

  DEBUG_PRINT(
      "MSG_MB_SEARCH received: A='%s', T='%s', Alb='%s'\n",
      artist.String(), title.String(), album.String());

  fWindow->fPendingFiles.clear();
  BString fpath;
  for (int32 i = 0; msg->FindString("file", i, &fpath) == B_OK; i++)
    fWindow->fPendingFiles.push_back(fpath);

  DEBUG_PRINT("MSG_MB_SEARCH context: %zu files\n",
              fWindow->fPendingFiles.size());

  int32 gen =
      fWindow->fMbSearchGeneration.fetch_add(1, std::memory_order_release) + 1;
  BMessenger replyTo = msg->ReturnAddress();
  fWindow->UpdateStatus(B_TRANSLATE("Searching on MusicBrainz..."));

  MainWindow *window = fWindow;
  window->LaunchThread("MBSearch", [window, artist, title, album, albumSearch,
                                    replyTo, gen]() {
    if (!window->fMbClient) {
      DEBUG_PRINT("Search thread abort: fMbClient is null\n");
      return;
    }

    DEBUG_PRINT("Thread running %s search... Gen=%ld\n",
                albumSearch ? "release" : "recording", (long)gen);
    auto abortCheck = [window, gen]() {
      return window->fMbSearchGeneration.load(std::memory_order_acquire) != gen;
    };
    std::vector<MBHit> hits =
        albumSearch
            ? window->fMbClient->SearchRelease(artist, album, abortCheck)
            : window->fMbClient->SearchRecording(artist, title, album,
                                                 abortCheck);
    DEBUG_PRINT("MusicBrainz search returned %zu hits\n",
                hits.size());

    std::vector<MBHit> *hitsPtr = new std::vector<MBHit>(hits);
    BMessage completion(MSG_MB_SEARCH_COMPLETE);
    completion.AddPointer("hits", hitsPtr);
    completion.AddMessenger("replyTo", replyTo);
    completion.AddInt32("generation", gen);
    completion.AddBool("album_search", albumSearch);
    if (BMessenger(window).SendMessage(&completion) != B_OK)
      delete hitsPtr;
  });
}

/**
 * @brief Cancels current search/apply flow and resets status timer.
 */
void MusicBrainzLookupController::CancelOperation() {
  if (!fWindow)
    return;

  DEBUG_PRINT(
      "MSG_MB_CANCEL received. Aborting current operations.\n");
  fWindow->fMbSearchGeneration.fetch_add(1, std::memory_order_release);
  fWindow->UpdateStatus(B_TRANSLATE("Cancelled by user."));

  BMessage msg(MSG_RESET_STATUS);
  delete fWindow->fStatusRunner;
  fWindow->fStatusRunner =
      new BMessageRunner(BMessenger(fWindow), &msg, 3000000, 1);
}

/**
 * @brief Handles search result list and returns formatted items to caller.
 */
void MusicBrainzLookupController::HandleSearchComplete(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  int32 gen = 0;
  msg->FindInt32("generation", &gen);
  if (gen != fWindow->fMbSearchGeneration.load(std::memory_order_acquire)) {
    std::vector<MBHit> *hits = nullptr;
    if (msg->FindPointer("hits", (void **)&hits) == B_OK && hits)
      delete hits;
    return;
  }

  std::vector<MBHit> *hits = nullptr;
  if (msg->FindPointer("hits", (void **)&hits) != B_OK || !hits)
    return;

  if (hits->empty()) {
    fWindow->UpdateStatus(B_TRANSLATE("MusicBrainz: Nothing found."));
  } else {
    BString s;
    s.SetToFormat(B_TRANSLATE("MusicBrainz: %zu hits."), hits->size());
    fWindow->UpdateStatus(s);
  }

  BMessenger replyTo;
  msg->FindMessenger("replyTo", &replyTo);
  bool albumSearch = false;
  msg->FindBool("album_search", &albumSearch);

  DEBUG_PRINT(
      "MB Search Complete. Hits: %zu. ReplyTo Valid: %ld\n",
      hits->size(), (long)(int32)replyTo.IsValid());

  if (replyTo.IsValid()) {
    int targetCount = (int)fWindow->fPendingFiles.size();
    if (targetCount > 0) {
      std::sort(hits->begin(), hits->end(),
                [targetCount](const MBHit &a, const MBHit &b) {
                  int diffA = std::abs(a.trackCount - targetCount);
                  int diffB = std::abs(b.trackCount - targetCount);
                  if (diffA != diffB)
                    return diffA < diffB;

                  return a.year > b.year;
                });
    }

    BMessage resp(MSG_MB_RESULTS);
    resp.AddBool("album_results", albumSearch);
    std::set<std::string> seenReleases;
    for (const auto &h : *hits) {
      if (albumSearch) {
        if (h.releaseId.IsEmpty())
          continue;
        if (!seenReleases.insert(h.releaseId.String()).second)
          continue;
      }

      BString item;

      BString extra;
      extra << h.releaseTitle;
      if (h.year > 0)
        extra << ", " << h.year;
      if (!h.country.IsEmpty())
        extra << ", " << h.country;
      if (h.trackCount > 0)
        extra << ", " << h.trackCount << " Tracks";

      if (albumSearch) {
        item.SetToFormat("%s - %s (%s)", h.artist.String(),
                         h.releaseTitle.String(), extra.String());
      } else {
        item.SetToFormat("%s - %s (%s)", h.artist.String(), h.title.String(),
                         extra.String());
      }

      resp.AddString("item", item);
      resp.AddString("id", h.recordingId);
      resp.AddString("releaseId", h.releaseId);
    }
    status_t err = replyTo.SendMessage(&resp);
    DEBUG_PRINT(
        "Sent MB Results to MetadataPropertiesWindow. Error: %ld\n",
        (long)err);
  }
  delete hits;
}

/**
 * @brief Applies fetched MusicBrainz metadata to selected files.
 */
void MusicBrainzLookupController::ApplyMetadata(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  BString recId, relId;
  msg->FindString("id", &recId);
  msg->FindString("releaseId", &relId);
  if (recId.IsEmpty() && relId.IsEmpty())
    return;

  fWindow->UpdateStatus(B_TRANSLATE("Fetching metadata from MusicBrainz..."));

  bool albumMode = (msg->what == MSG_MB_APPLY_ALBUM);

  std::vector<BString> files;
  BString f;
  for (int32 i = 0; msg->FindString("file", i, &f) == B_OK; i++)
    files.push_back(f);
  if (files.empty())
    return;

  BMessenger replyTo = msg->ReturnAddress();

  DEBUG_PRINT("MSG_MB_APPLY received. IDs: rec='%s', rel='%s'. "
              "Files: %zu\n",
              recId.String(), relId.String(), files.size());

  MainWindow *window = fWindow;
  int32 gen = window->fMbSearchGeneration.load(std::memory_order_acquire);
  window->LaunchThread("MBApply", [window, recId, relId, files, albumMode,
                                   replyTo, gen]() mutable {
    if (!window->fMbClient) {
      DEBUG_PRINT("Apply thread abort: fMbClient is null\n");
      return;
    }

    auto abortStatus = [window]() {
      BMessage statusDone(MSG_STATUS_UPDATE);
      statusDone.AddString("text", B_TRANSLATE("Cancelled."));
      BMessenger(window).SendMessage(&statusDone);
    };

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      abortStatus();
      return;
    }

    auto abortCheck = [window, gen]() {
      return window->fMbSearchGeneration.load(std::memory_order_acquire) != gen;
    };

    BString effectiveRelId = relId;
    if (effectiveRelId.IsEmpty()) {
      DEBUG_PRINT("Resolving release for recording: %s\n",
                  recId.String());
      effectiveRelId =
          window->fMbClient->BestReleaseForRecording(recId, abortCheck);
    }

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      abortStatus();
      return;
    }

    if (effectiveRelId.IsEmpty()) {
      DEBUG_PRINT("Error: Could not resolve release ID.\n");
      BMessage statusDone(MSG_STATUS_UPDATE);
      statusDone.AddString("text", B_TRANSLATE("Error: Release ID not found."));
      BMessenger(window).SendMessage(&statusDone);
      return;
    }

    DEBUG_PRINT("Fetching details for release: %s\n",
                effectiveRelId.String());
    MBRelease rel =
        window->fMbClient->GetReleaseDetails(effectiveRelId, abortCheck);

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      abortStatus();
      return;
    }

    DEBUG_PRINT("Release fetched: '%s' (%zu tracks)\n",
                rel.album.String(), rel.tracks.size());

    CoverBlob coverBlob;
    std::vector<uint8_t> coverData;
    BString coverMime;

    bool hasCover = false;

    if (!rel.releaseGroupId.IsEmpty()) {
      DEBUG_PRINT("Trying to fetch cover for Release Group: %s\n",
                  rel.releaseGroupId.String());
      hasCover = window->fMbClient->FetchCover(
          rel.releaseGroupId, coverData, &coverMime, 500, true, abortCheck);
    }

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      abortStatus();
      return;
    }

    if (!hasCover && !effectiveRelId.IsEmpty()) {
      DEBUG_PRINT("No Group cover, trying Release: %s\n",
                  effectiveRelId.String());
      hasCover = window->fMbClient->FetchCover(
          effectiveRelId, coverData, &coverMime, 500, true, abortCheck);
    }

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      abortStatus();
      return;
    }

    if (hasCover) {
      DEBUG_PRINT("Cover fetched: %zu bytes (%s)\n",
                  coverData.size(), coverMime.String());
      coverBlob.assign(coverData.data(), coverData.size());
    } else {
      DEBUG_PRINT("No cover found for release/group.\n");
    }

    if (albumMode && files.size() == 1) {
      BPath p(files[0].String());
      BPath parent;
      if (p.GetParent(&parent) == B_OK) {
        DEBUG_PRINT("Single file selected in Album Mode. "
                    "Scanning parent: %s\n",
                    parent.Path());
        BDirectory dir(parent.Path());
        BEntry entry;
        std::vector<BString> dirFiles;
        while (dir.GetNextEntry(&entry) == B_OK) {
          BPath ep;
          if (entry.GetPath(&ep) == B_OK && !entry.IsDirectory()) {
            BString pathStr = ep.Path();
            BString lowerPath = pathStr;
            lowerPath.ToLower();
            if (lowerPath.EndsWith(".mp3") || lowerPath.EndsWith(".flac") ||
                lowerPath.EndsWith(".wav") || lowerPath.EndsWith(".m4a") ||
                lowerPath.EndsWith(".ogg")
#if ENABLE_MIDI_PLAYBACK
                || lowerPath.EndsWith(".mid") ||
                lowerPath.EndsWith(".midi")
#endif
            ) {
              dirFiles.push_back(pathStr);
            }
          }
        }
        if (!dirFiles.empty()) {
          DEBUG_PRINT("Expanded single file to %zu files in %s\n",
                      dirFiles.size(), parent.Path());
          files = dirFiles;
        }
      }
    }

    DEBUG_PRINT("Starting processing loop for %zu files. Mode: "
                "%s\n",
                files.size(), albumMode ? "Album" : "Track");

    if (albumMode) {
      std::sort(files.begin(), files.end());

      std::vector<int> fileToTrackMap(files.size(), -1);
      std::vector<bool> trackUsed(rel.tracks.size(), false);
      int filesMatched = 0;
      bool durationMismatch = false;

      for (size_t i = 0; i < files.size(); i++) {
        BPath bp(files[i].String());
        TagData td;
        MetadataTagIO::ReadTags(bp, td);

        const MBTrack *bestMatch = nullptr;
        int bestTrackIdx = -1;

        if (td.track > 0) {
          for (size_t k = 0; k < rel.tracks.size(); k++) {
            if (!trackUsed[k] && rel.tracks[k].track == td.track) {
              int durDiff =
                  abs((int)rel.tracks[k].length - (int)td.lengthSec);
              if (durDiff < 15) {
                bestMatch = &rel.tracks[k];
                bestTrackIdx = k;
              } else {
                durationMismatch = true;
              }
              break;
            }
          }
        }

        if (!bestMatch) {
          int fnTrack = TrackMatchingUtils::ExtractTrackNumber(bp.Leaf());
          if (fnTrack > 0) {
            for (size_t k = 0; k < rel.tracks.size(); k++) {
              if (!trackUsed[k] && rel.tracks[k].track == (uint32)fnTrack) {
                int durDiff =
                    abs((int)rel.tracks[k].length - (int)td.lengthSec);
                if (durDiff < 15) {
                  bestMatch = &rel.tracks[k];
                  bestTrackIdx = k;
                }
                break;
              }
            }
          }
        }

        if (bestMatch) {
          fileToTrackMap[i] = bestTrackIdx;
          trackUsed[bestTrackIdx] = true;
          filesMatched++;
        }
      }

      int nextTrackIdx = 0;
      for (size_t i = 0; i < files.size(); i++) {
        if (fileToTrackMap[i] == -1) {
          while (nextTrackIdx < (int)rel.tracks.size() &&
                 trackUsed[nextTrackIdx]) {
            nextTrackIdx++;
          }
          if (nextTrackIdx < (int)rel.tracks.size()) {
            fileToTrackMap[i] = nextTrackIdx;
            trackUsed[nextTrackIdx] = true;
          }
        }
      }

      bool allMapped = true;
      for (int idx : fileToTrackMap)
        if (idx == -1)
          allMapped = false;

      bool confident =
          allMapped && !durationMismatch && (filesMatched >= (int)files.size() / 2);

      if (confident) {
        DEBUG_PRINT("Auto-Match confident. Applying tags "
                    "directly.\n");

        for (size_t i = 0; i < files.size(); i++) {
          int tIdx = fileToTrackMap[i];
          if (tIdx < 0)
            continue;

          const MBTrack &trk = rel.tracks[tIdx];
          TagData td;
          ReadMetadataForConfiguredTargets(files[i], td);

          td.artist = rel.albumArtist;
          td.album = rel.album;
          td.title = trk.title;
          td.year = rel.year;
          td.track = trk.track;
          td.trackTotal = (uint32)rel.tracks.size();
          td.disc = trk.disc;
          td.albumArtist = rel.albumArtist;
          td.mbAlbumID = rel.releaseId;
          td.mbTrackID = trk.recordingId;

          WriteMetadataForConfiguredTargets(files[i], td, &coverBlob);
          BMessage update(MSG_MEDIA_ITEM_FOUND);
          update.AddString("path", files[i]);
          AddTagDataToMediaUpdate(update, td);

          BMessenger(window).SendMessage(&update);
          if (window->fMediaLibraryCache)
            BMessenger(window->fMediaLibraryCache).SendMessage(&update);
          if (replyTo.IsValid())
            replyTo.SendMessage(&update);
        }
        BMessage statusMsg(MSG_STATUS_UPDATE);
        statusMsg.AddString(
            "text", B_TRANSLATE("Metadata applied successfully (Auto-Match)."));
        BMessenger(window).SendMessage(&statusMsg);
      } else {
        DEBUG_PRINT("Auto-Match NOT confident (Mismatch=%" PRId32
                    ", Matched=%" PRId32 "/%zu). Opening MusicBrainzMatcherWindow.\n",
                    durationMismatch, filesMatched, files.size());

        std::vector<MusicBrainzMatchTrackInfo> trackInfos;
        for (const auto &t : rel.tracks) {
          BString dur;
          dur.SetToFormat("%d:%02d", (int)(t.length / 60),
                          (int)(t.length % 60));
          trackInfos.push_back({t.title, dur, (int)t.track});
        }

        try {
          new MusicBrainzMatcherWindow(files, trackInfos, fileToTrackMap,
                            BMessenger(window));
        } catch (const std::bad_alloc &) {
          throw;
        } catch (const std::exception &ex) {
          DEBUG_PRINT("Failed to create MusicBrainzMatcherWindow: %s\n",
                      ex.what());
        } catch (...) {
          DEBUG_PRINT("Unknown non-std exception creating "
                      "MusicBrainzMatcherWindow\n");
        }

        if (window->Lock()) {
          window->fPendingRelease = rel;
          window->fPendingCoverBlob = coverBlob;
          window->Unlock();
        }
      }
    } else {
      for (const auto &path : files) {
        TagData td;
        ReadMetadataForConfiguredTargets(path, td);

        const MBTrack *trkMatch = nullptr;
        for (const auto &t : rel.tracks) {
          if (t.recordingId == recId) {
            trkMatch = &t;
            break;
          }
        }

        if (trkMatch) {
          DEBUG_PRINT("Applying Track Mode: File '%s' -> Track "
                      "Match '%s'\n",
                      path.String(), trkMatch->title.String());
        } else {
          DEBUG_PRINT("Warning: Track Mode, but bad recID match "
                      "for file '%s'\n",
                      path.String());
        }

        td.artist = rel.albumArtist;
        td.album = rel.album;
        td.year = rel.year;
        td.mbAlbumID = rel.releaseId;
        td.mbTrackID = recId;

        if (trkMatch) {
          td.title = trkMatch->title;
          td.track = trkMatch->track;
          td.disc = trkMatch->disc;
        }

        WriteMetadataForConfiguredTargets(path, td, &coverBlob);

        BMessage update(MSG_MEDIA_ITEM_FOUND);
        update.AddString("path", path);
        AddTagDataToMediaUpdate(update, td);

        DEBUG_PRINT("MSG_MEDIA_ITEM_FOUND sending (Path=%s, "
                    "Year=%lu)\n",
                    path.String(), (unsigned long)td.year);

        BMessenger(window).SendMessage(&update);
        if (window->fMediaLibraryCache)
          BMessenger(window->fMediaLibraryCache).SendMessage(&update);
        if (replyTo.IsValid())
          replyTo.SendMessage(&update);
      }
    }

    BMessage doneMsg(MSG_STATUS_UPDATE);
    doneMsg.AddString("text", B_TRANSLATE("Metadata successfully saved."));
    BMessenger(window).SendMessage(&doneMsg);

    if (!files.empty()) {
      BMessage coverMsg(MSG_COVER_FETCH_MB);
      coverMsg.AddString("file", files[0]);
      if (replyTo.IsValid())
        coverMsg.AddMessenger("original_reply_to", replyTo);
      BMessenger(window).SendMessage(&coverMsg);
    }
  });
}

/**
 * @brief Applies user-confirmed matcher mapping in a background thread.
 */
void MusicBrainzLookupController::ApplyMatch(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  DEBUG_PRINT("Matcher Applied. Processing...\n");

  int32 trackIdx;
  int32 i = 0;
  BString itemPath;
  std::vector<int32> trackMap;
  std::vector<BString> matchedFiles;

  while (msg->FindInt32("track_idx", i, &trackIdx) == B_OK) {
    if (msg->FindString("file_path", i, &itemPath) != B_OK) {
      if (i < (int32)fWindow->fPendingFiles.size())
        itemPath = fWindow->fPendingFiles[i];
      else
        break;
    }

    trackMap.push_back(trackIdx);
    matchedFiles.push_back(itemPath);
    i++;
  }

  MBRelease pendingRelease = fWindow->fPendingRelease;
  CoverBlob pendingCoverBlob = fWindow->fPendingCoverBlob;
  BMessenger target(fWindow);
  BMessenger cacheTarget(fWindow->fMediaLibraryCache);
  BMessenger propertiesTarget(fWindow->fMetadataPropertiesWindow);

  fWindow->LaunchThread("MBMatchApply", [matchedFiles, trackMap,
                                          pendingRelease, pendingCoverBlob,
                                          target, cacheTarget,
                                          propertiesTarget]() {
    for (size_t idx = 0; idx < matchedFiles.size(); idx++) {
      int32 trackIndex = trackMap[idx];
      if (trackIndex < 0 || trackIndex >= (int32)pendingRelease.tracks.size())
        continue;

      const MBTrack &trk = pendingRelease.tracks[trackIndex];
      BString filePath = matchedFiles[idx];

      TagData td;
      ReadMetadataForConfiguredTargets(filePath, td);

      td.artist = pendingRelease.albumArtist;
      td.album = pendingRelease.album;
      td.title = trk.title;
      td.year = pendingRelease.year;
      td.track = trk.track;
      td.trackTotal = (uint32)pendingRelease.tracks.size();
      td.disc = trk.disc;
      td.albumArtist = pendingRelease.albumArtist;
      td.mbAlbumID = pendingRelease.releaseId;
      td.mbTrackID = trk.recordingId;

      DEBUG_PRINT("Applying Tags to '%s':\n", filePath.String());
      DEBUG_PRINT("    Title: %s\n", td.title.String());
      DEBUG_PRINT("    MB Track ID: %s\n", td.mbTrackID.String());
      DEBUG_PRINT("    MB Album ID: %s\n", td.mbAlbumID.String());

      WriteMetadataForConfiguredTargets(filePath, td, &pendingCoverBlob);

      BMessage update(MSG_MEDIA_ITEM_FOUND);
      update.AddString("path", filePath);
      AddTagDataToMediaUpdate(update, td);

      target.SendMessage(&update);
      if (cacheTarget.IsValid())
        cacheTarget.SendMessage(&update);
      if (propertiesTarget.IsValid())
        propertiesTarget.SendMessage(&update);
    }

    BMessage statusMsg(MSG_STATUS_UPDATE);
    statusMsg.AddString("text", B_TRANSLATE("Metadata applied successfully "
                                            "(Manual)."));
    target.SendMessage(&statusMsg);
  });

  fWindow->UpdateStatus(B_TRANSLATE("Writing metadata..."));

  fWindow->fPendingFiles.clear();
  fWindow->fPendingCoverBlob.clear();
}

/**
 * @brief Fetches best-effort cover art for a file using MusicBrainz IDs.
 */
void MusicBrainzLookupController::FetchCover(BMessage *msg) {
  if (!fWindow || !msg)
    return;

  BString path;
  if (msg->FindString("file", &path) != B_OK) {
    DEBUG_PRINT("MSG_COVER_FETCH_MB: Could not find 'file' in "
                "message.\n");
    return;
  }
  DEBUG_PRINT("MSG_COVER_FETCH_MB: File = %s\n", path.String());

  BMessenger replyTo;
  if (msg->FindMessenger("original_reply_to", &replyTo) != B_OK)
    replyTo = msg->ReturnAddress();

  MainWindow *window = fWindow;
  int32 gen = window->fMbSearchGeneration.load(std::memory_order_acquire);
  window->LaunchThread("CoverFetchMB", [window, path, replyTo, gen]() {
    DEBUG_PRINT("MB Thread started for %s (Gen=%ld)\n",
                path.String(), (long)gen);
    if (!window->fMbClient) {
      DEBUG_PRINT("fMbClient is NULL!\n");
      return;
    }

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      DEBUG_PRINT("Aborted (Gen mismatch start)\n");
      return;
    }

    auto abortCheck = [window, gen]() {
      return window->fMbSearchGeneration.load(std::memory_order_acquire) != gen;
    };
    auto sendNoCoverReply = [replyTo]() {
      if (replyTo.IsValid()) {
        BMessage reply(MSG_PROP_SET_COVER_DATA);
        reply.AddBool("cover_not_found", true);
        replyTo.SendMessage(&reply);
      }
    };

    TagData td;
    if (!MetadataTagIO::ReadTags(BPath(path.String()), td)) {
      DEBUG_PRINT("Could not read tags from %s\n", path.String());
      sendNoCoverReply();
      return;
    }

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      DEBUG_PRINT("Aborted (Gen mismatch post-read)\n");
      return;
    }

    BString relId = td.mbAlbumID;
    DEBUG_PRINT("MB Album ID from tags: '%s'\n", relId.String());

    if (relId == "MusicBrainz Album Id" || relId.Length() < 30) {
      DEBUG_PRINT("ID '%s' seems invalid. Ignoring.\n",
                  relId.String());
      relId = "";
    }

    if (relId.IsEmpty()) {
      DEBUG_PRINT("No ID, trying search...\n");
      std::vector<MBHit> hits = window->fMbClient->SearchRecording(
          td.artist, td.title, td.album, abortCheck);

      if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
        DEBUG_PRINT("Aborted (Gen mismatch post-search)\n");
        return;
      }

      if (!hits.empty()) {
        relId = hits[0].releaseId;
        DEBUG_PRINT("Search found release ID: %s\n",
                    relId.String());
      } else {
        DEBUG_PRINT("Search returned 0 hits.\n");
      }
    }

    if (relId.IsEmpty()) {
      DEBUG_PRINT("resolving relId failed -> abort.\n");
      sendNoCoverReply();
      return;
    }

    if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen) {
      DEBUG_PRINT("Aborted (Gen mismatch pre-fetch)\n");
      return;
    }

    std::vector<uint8_t> data;
    BString mime;
    bool sentCoverReply = false;
    DEBUG_PRINT("Fetching cover for %s...\n", relId.String());
    if (window->fMbClient->FetchCover(relId, data, &mime, 500, false,
                                      abortCheck)) {
      if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen)
        return;
      DEBUG_PRINT("FetchCover success! %zu bytes, mime=%s\n",
                  data.size(), mime.String());
      BMessage reply(MSG_PROP_SET_COVER_DATA);
      reply.AddData("bytes", B_RAW_TYPE, data.data(), data.size());
      if (!mime.IsEmpty())
        reply.AddString("mime", mime.String());
      replyTo.SendMessage(&reply);
      sentCoverReply = true;
    } else {
      if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen)
        return;

      DEBUG_PRINT("FetchCover failed for Release ID. Trying "
                  "Release Group...\n");
      MBRelease mbRel = window->fMbClient->GetReleaseDetails(relId, abortCheck);

      if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen)
        return;

      if (!mbRel.releaseGroupId.IsEmpty()) {
        DEBUG_PRINT("Found Release Group ID: %s. Fetching...\n",
                    mbRel.releaseGroupId.String());
        if (window->fMbClient->FetchCover(mbRel.releaseGroupId, data, &mime,
                                          500, true, abortCheck)) {
          if (window->fMbSearchGeneration.load(std::memory_order_acquire) != gen)
            return;
          DEBUG_PRINT("FetchCover (Group) success! %zu bytes, "
                      "mime=%s\n",
                      data.size(), mime.String());
          BMessage reply(MSG_PROP_SET_COVER_DATA);
          reply.AddData("bytes", B_RAW_TYPE, data.data(), data.size());
          if (!mime.IsEmpty())
            reply.AddString("mime", mime.String());
          replyTo.SendMessage(&reply);
          sentCoverReply = true;
        } else {
          DEBUG_PRINT("FetchCover (Group) failed.\n");
        }
      } else {
        DEBUG_PRINT("No Release Group found for this release.\n");
      }
    }

    if (!sentCoverReply)
      sendNoCoverReply();

    BMessage statusDone(MSG_STATUS_UPDATE);
    statusDone.AddString("text", B_TRANSLATE("Ready."));
    BMessenger(window).SendMessage(&statusDone);
  });
}
