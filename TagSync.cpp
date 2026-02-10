#include "TagSync.h"
#include "Debug.h"
#include "MusicSource.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include <Entry.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Volume.h>
#include <fs_attr.h>

#include <taglib/attachedpictureframe.h>
#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/popularimeterframe.h>
#include <taglib/tag.h>
#include <taglib/textidentificationframe.h>
#include <taglib/tfile.h>
#include <taglib/tpropertymap.h>
#include <taglib/unsynchronizedlyricsframe.h>

/**
 * @brief Converts a TagLib::String to a BString.
 */
static inline BString TL(const TagLib::String &s) {
  BString str(s.to8Bit(true).c_str());
  str.Trim();
  return str;
}

/**
 * @brief Converts a BString to a TagLib::String (UTF-8).
 */
static inline TagLib::String TLs(const BString &s) {
  return TagLib::String(s.String(), TagLib::String::UTF8);
}

/**
 * @brief Parses a TagLib string as an unsigned integer.
 * @param s The string to parse.
 * @return The parsed uint32 value, or 0 on failure.
 */
static uint32 _toUInt(const TagLib::String &s) {
  const char *cs = s.toCString(true);
  if (!cs || !*cs)
    return 0;
  char *end = nullptr;
  long v = strtol(cs, &end, 10);
  if (!end)
    return 0;
  return (v > 0) ? (uint32)v : 0;
}

/**
 * @brief Parses a slash-separated string pair (e.g., "1/10"  into two integers.
 */
static void _parsePair(const TagLib::String &s, uint32 &first, uint32 &second) {
  first = second = 0;
  const char *cs = s.toCString(true);
  if (!cs)
    return;
  const char *slash = strchr(cs, '/');
  if (!slash) {
    first = _toUInt(s);
    return;
  }
  TagLib::ByteVector av(cs, (unsigned int)(slash - cs));
  TagLib::String a(av, TagLib::String::UTF8);
  TagLib::String b(slash + 1, TagLib::String::UTF8);
  first = _toUInt(a);
  second = _toUInt(b);
}

/**
 * @brief Trims leading and trailing whitespace from a BString in place.
 */
static void _trimBString(BString &s) {
  while (s.Length() > 0 &&
         (s[0] == ' ' || s[0] == '\t' || s[0] == '\n' || s[0] == '\r')) {
    s.Remove(0, 1);
  }
  while (s.Length() > 0) {
    char last = s[s.Length() - 1];
    if (last == ' ' || last == '\t' || last == '\n' || last == '\r') {
      s.Truncate(s.Length() - 1);
    } else {
      break;
    }
  }
}

/**
 * @brief Helper to get the first non-empty string for a set of keys from a
 * PropertyMap.
 */
static TagLib::String _getTL(const TagLib::PropertyMap &p,
                             std::initializer_list<const char *> keys) {
  for (auto k : keys) {
    auto it = p.find(TagLib::String(k, TagLib::String::UTF8));
    if (it != p.end() && !it->second.isEmpty())
      return it->second.front();
  }
  return TagLib::String();
}

static BString _getStr(const TagLib::PropertyMap &p,
                       std::initializer_list<const char *> keys) {
  TagLib::String s = _getTL(p, keys);
  return s.isEmpty() ? BString() : TL(s);
}

static void _setOrErase(TagLib::PropertyMap &pm, const char *key,
                        const BString &v) {
  TagLib::String k(key, TagLib::String::UTF8);
  if (v.IsEmpty())
    pm.erase(k);
  else
    pm.replace(k, TagLib::StringList(TLs(v)));
}

/**
 * @brief Formats a pair of integers as a slash-separated string (e.g., "1/12").
 */
static TagLib::String _pairStr(uint32 n, uint32 tot) {
  if (n == 0 && tot == 0)
    return TagLib::String();
  if (tot == 0)
    return TagLib::String(std::to_string(n), TagLib::String::UTF8);
  return TagLib::String((std::to_string(n) + "/" + std::to_string(tot)).c_str(),
                        TagLib::String::UTF8);
}

static const char *sniff_mime(const uint8 *d, size_t n) {
  if (!d || n < 8)
    return nullptr;

  static const uint8 pngSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (n >= 8 && memcmp(d, pngSig, 8) == 0)
    return "image/png";

  if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8)
    return "image/jpeg";

  return nullptr;
}

/**
 * @brief Maps an internal rating (0-10) to a byte value (0-255).
 * Windows Explorer style:
 * 1* = 1, 2* = 64, 3* = 128, 4* = 196, 5* = 255.
 * We interpolate for half stars.
 */
static uint8_t _ratingToByte(uint32 rating) {
  if (rating == 0)
    return 0;
  if (rating >= 10)
    return 255;

  switch (rating) {
  case 1:
    return 1;
  case 2:
    return 64;
  case 3:
    return 96;
  case 4:
    return 128;
  case 5:
    return 160;
  case 6:
    return 196;
  case 7:
    return 208;
  case 8:
    return 224;
  case 9:
    return 240;
  case 10:
    return 255;
  }
  return 0;
}

/**
 * @brief Maps a byte rating (0-255) to internal rating (0-10).
 */
static uint32 _byteToRating(uint8_t val) {
  if (val == 0)
    return 0;
  if (val < 8)
    return 1;
  if (val < 64)
    return 2;
  if (val < 96)
    return 3;
  if (val < 128)
    return 4;
  if (val < 160)
    return 5;
  if (val < 196)
    return 6;
  if (val < 208)
    return 7;
  if (val < 224)
    return 8;
  if (val < 240)
    return 9;
  return 10;
}

/**
 * @brief Reads metadata from a file into a TagData struct.
 * @param path The file path to read from.
 * @param out Output structure for metadata.
 * @return True if successful, false otherwise.
 */
bool TagSync::ReadTags(const BPath &path, TagData &out) {
  if (path.InitCheck() != B_OK)
    return false;

  bool foundData = false;

  TagLib::FileRef fr(path.Path());
  if (!fr.isNull()) {
    if (TagLib::Tag *t = fr.tag()) {
      out.title = TL(t->title());
      out.artist = TL(t->artist());
      out.album = TL(t->album());
      out.genre = TL(t->genre());
      out.comment = TL(t->comment());
      out.year = t->year();
      out.track = t->track();
      foundData = true;
    }

    if (const TagLib::AudioProperties *ap = fr.audioProperties()) {
      const int ms = ap->lengthInMilliseconds();
      out.lengthSec = (ms > 0) ? (ms / 1000) : 0;
      out.bitrate = ap->bitrate();
      out.sampleRate = ap->sampleRate();
      out.channels = ap->channels();
      foundData = true;
    }

    if (fr.file()) {
      const TagLib::PropertyMap &pm = fr.file()->properties();

      out.albumArtist =
          _getStr(pm, {"ALBUMARTIST", "ALBUM ARTIST", "TPE2", "aART"});

      out.composer =
          _getStr(pm, {"COMPOSER", "TCOM", "©wrt", "composer", "Composer"});

      if (out.trackTotal == 0) {
        BString s = _getStr(pm, {"TRACKTOTAL", "TOTALTRACKS", "TOTAL TRACKS"});
        if (!s.IsEmpty())
          out.trackTotal =
              _toUInt(TagLib::String(s.String(), TagLib::String::UTF8));
      }
      TagLib::String trkPair = _getTL(pm, {"TRACKNUMBER", "TRCK", "trkn"});
      if (!trkPair.isEmpty()) {
        uint32 n = 0, tot = 0;
        _parsePair(trkPair, n, tot);
        if (n && !out.track)
          out.track = n;
        if (tot)
          out.trackTotal = tot;
      }

      if (out.disc == 0)
        out.disc = _toUInt(_getTL(pm, {"DISCNUMBER", "DISC NUMBER", "TPOS"}));
      if (out.discTotal == 0) {
        BString s = _getStr(pm, {"DISCTOTAL", "TOTALDISCS", "TOTAL DISCS"});
        if (!s.IsEmpty())
          out.discTotal =
              _toUInt(TagLib::String(s.String(), TagLib::String::UTF8));
      }
      TagLib::String tpos = _getTL(pm, {"TPOS", "DISCNUMBER", "disk"});
      if (!tpos.isEmpty()) {
        uint32 d = 0, tot = 0;
        _parsePair(tpos, d, tot);
        if (d && !out.disc)
          out.disc = d;
        if (tot)
          out.discTotal = tot;
      }

      if (out.rating == 0) {
        TagLib::String r = _getTL(pm, {"RATING", "rating"});
        if (!r.isEmpty()) {
          int v = _toUInt(r);
          if (v > 10)
            v /= 10;
          if (v > 5)
            v = 5;
          out.rating = v * 2;
        }
      }

      out.mbAlbumID =
          _getStr(pm, {"MUSICBRAINZ_ALBUMID", "MusicBrainz Album Id"});
      out.mbArtistID =
          _getStr(pm, {"MUSICBRAINZ_ARTISTID", "MusicBrainz Artist Id"});
      out.mbTrackID =
          _getStr(pm, {"MUSICBRAINZ_TRACKID", "MusicBrainz Track Id"});
    }
  }

  {
    TagLib::MPEG::File mf(path.Path());
    if (mf.isOpen()) {
      if (TagLib::ID3v2::Tag *id3 = mf.ID3v2Tag()) {
        const TagLib::ID3v2::FrameList &popm = id3->frameList("POPM");
        if (!popm.isEmpty()) {
          for (auto *f : popm) {
            TagLib::ID3v2::PopularimeterFrame *pf =
                dynamic_cast<TagLib::ID3v2::PopularimeterFrame *>(f);
            if (pf) {
              out.rating = _byteToRating(pf->rating());

              if (pf->email() == "Windows Media Player 9 Series")
                break;
            }
          }
        }

        const TagLib::ID3v2::FrameList &txxx = id3->frameList("TXXX");
        for (auto it = txxx.begin(); it != txxx.end(); ++it) {
          TagLib::ID3v2::UserTextIdentificationFrame *u =
              dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame *>(*it);
          if (!u)
            continue;

          const BString desc = TL(u->description());
          BString val;
          const auto &fl = u->fieldList();

          if (fl.size() >= 2) {
            val = TL(fl[1]);
          } else if (fl.size() == 1) {

            BString item0 = TL(fl.front());

            if (item0 != TL(u->description()))
              val = item0;
            else
              val = "";
          } else {
            val = "";
          }

          if (desc.ICompare("MusicBrainz Album Id") == 0)
            out.mbAlbumID = val;
          else if (desc.ICompare("MusicBrainz Artist Id") == 0)
            out.mbArtistID = val;
          else if (desc.ICompare("MusicBrainz Track Id") == 0)
            out.mbTrackID = val;
          else if (desc.ICompare("AcoustID Fingerprint") == 0)
            out.acoustIdFp = val;
          else if (desc.ICompare("AcoustID Id") == 0)
            out.acoustId = val;
        }

        foundData = true;
      }
    }
  }

  {
    TagLib::MP4::File mf(path.Path());
    if (mf.isValid() && mf.tag()) {
      TagLib::MP4::Tag *tag = mf.tag();

      if (tag->contains("rate")) {
        TagLib::MP4::Item item = tag->item("rate");
        if (item.toInt() > 0) {
          int val = item.toInt();
          if (val > 0 && val <= 100) {
            out.rating = (val + 5) / 10;
          } else if (val > 100) {
            out.rating = _byteToRating(val);
          }
        }
      }

      foundData = true;
    }
  }

  TagData bfsData;
  if (ReadBfsAttributes(path, bfsData)) {
    if (out.title.IsEmpty())
      out.title = bfsData.title;
    if (out.artist.IsEmpty())
      out.artist = bfsData.artist;
    if (out.album.IsEmpty())
      out.album = bfsData.album;
    if (out.genre.IsEmpty())
      out.genre = bfsData.genre;
    if (out.comment.IsEmpty())
      out.comment = bfsData.comment;
    if (out.year == 0)
      out.year = bfsData.year;
    if (out.track == 0)
      out.track = bfsData.track;
    if (out.trackTotal == 0)
      out.trackTotal = bfsData.trackTotal;
    if (out.disc == 0)
      out.disc = bfsData.disc;
    if (out.discTotal == 0)
      out.discTotal = bfsData.discTotal;
    if (out.albumArtist.IsEmpty())
      out.albumArtist = bfsData.albumArtist;
    if (out.composer.IsEmpty())
      out.composer = bfsData.composer;
    if (out.mbAlbumID.IsEmpty())
      out.mbAlbumID = bfsData.mbAlbumID;
    if (out.mbArtistID.IsEmpty())
      out.mbArtistID = bfsData.mbArtistID;
    if (out.mbTrackID.IsEmpty())
      out.mbTrackID = bfsData.mbTrackID;

    if (out.lengthSec == 0)
      out.lengthSec = bfsData.lengthSec;
    if (out.bitrate == 0)
      out.bitrate = bfsData.bitrate;
    if (out.sampleRate == 0)
      out.sampleRate = bfsData.sampleRate;
    if (out.channels == 0)
      out.channels = bfsData.channels;

    if (out.rating == 0)
      out.rating = bfsData.rating;

    foundData = true;
  }

  return foundData;
}

static void set_basic_tags(TagLib::Tag *t, const TagData &td) {
  if (!t)
    return;
  t->setTitle(TLs(td.title));
  t->setArtist(TLs(td.artist));
  t->setAlbum(TLs(td.album));
  t->setComment(TLs(td.comment));
  t->setGenre(TLs(td.genre));
  t->setYear(td.year);
  t->setTrack(td.track);
}

bool TagSync::WriteTagsToFile(const BPath &path, const TagData &td,
                              const CoverBlob *coverOpt) {
  if (path.InitCheck() != B_OK)
    return false;
  BString p = path.Path();
  BString lower = p;
  lower.ToLower();

  if (lower.EndsWith(".mp3")) {
    TagLib::MPEG::File f(path.Path());
    if (!f.isOpen())
      return false;

    TagLib::ID3v2::Tag *id3 = f.ID3v2Tag(true);
    set_basic_tags(id3 ? static_cast<TagLib::Tag *>(id3) : f.tag(), td);

    if (id3) {
      const TagLib::ID3v2::FrameList &popm = id3->frameList("POPM");
      for (auto *frame : popm) {
        id3->removeFrame(frame, true);
      }

      if (td.rating > 0) {
        TagLib::ID3v2::PopularimeterFrame *pf =
            new TagLib::ID3v2::PopularimeterFrame();

        /**
         * @brief Set the email to "Windows Media Player 9 Series".
         *
         * This specific string is required by Windows Explorer, Windows Media
         * Player, and many other players associated with the Windows ecosystem
         * to recognize and display the rating (star) field. Using other email
         * strings often results in the rating being ignored by these
         * applications. Its ridiculous!
         */
        pf->setEmail("Windows Media Player 9 Series");
        pf->setRating(_ratingToByte(td.rating));
        id3->addFrame(pf);
      }
    }

    auto setTXXX = [&](const char *desc, const BString &val) {
      if (!id3)
        return;

      TagLib::String d(desc, TagLib::String::UTF8);
      std::vector<TagLib::ID3v2::Frame *> toRemove;

      const TagLib::ID3v2::FrameList &l = id3->frameList("TXXX");
      for (auto *f : l) {
        auto *uf =
            dynamic_cast<TagLib::ID3v2::UserTextIdentificationFrame *>(f);
        if (uf && uf->description().upper() == d.upper()) {
          toRemove.push_back(f);
        }
      }

      for (auto *f : toRemove) {
        id3->removeFrame(f, true);
      }

      if (!val.IsEmpty()) {
        auto *frame = new TagLib::ID3v2::UserTextIdentificationFrame(
            d, TagLib::StringList(TLs(val)));
        id3->addFrame(frame);
      }
    };

    if (id3) {
      {
        TagLib::ID3v2::TextIdentificationFrame *fr =
            dynamic_cast<TagLib::ID3v2::TextIdentificationFrame *>(
                id3->frameListMap()["TPE2"].isEmpty()
                    ? nullptr
                    : id3->frameListMap()["TPE2"].front());
        if (td.albumArtist.IsEmpty()) {
          if (fr)
            id3->removeFrame(fr, true);
        } else {
          if (!fr) {
            fr = new TagLib::ID3v2::TextIdentificationFrame(
                "TPE2", TagLib::String::Latin1);
            id3->addFrame(fr);
          }
          fr->setText(TLs(td.albumArtist));
        }
      }

      {
        TagLib::ID3v2::TextIdentificationFrame *fr =
            dynamic_cast<TagLib::ID3v2::TextIdentificationFrame *>(
                id3->frameListMap()["TCOM"].isEmpty()
                    ? nullptr
                    : id3->frameListMap()["TCOM"].front());
        if (td.composer.IsEmpty()) {
          if (fr)
            id3->removeFrame(fr, true);
        } else {
          if (!fr) {
            fr = new TagLib::ID3v2::TextIdentificationFrame(
                "TCOM", TagLib::String::Latin1);
            id3->addFrame(fr);
          }
          fr->setText(TLs(td.composer));
        }
      }

      {
        TagLib::ID3v2::TextIdentificationFrame *fr =
            dynamic_cast<TagLib::ID3v2::TextIdentificationFrame *>(
                id3->frameListMap()["TRCK"].isEmpty()
                    ? nullptr
                    : id3->frameListMap()["TRCK"].front());
        TagLib::String v = _pairStr(td.track, td.trackTotal);
        if (v.isEmpty()) {
          if (fr)
            id3->removeFrame(fr, true);
        } else {
          if (!fr) {
            fr = new TagLib::ID3v2::TextIdentificationFrame(
                "TRCK", TagLib::String::Latin1);
            id3->addFrame(fr);
          }
          fr->setText(v);
        }
      }

      {
        TagLib::ID3v2::TextIdentificationFrame *fr =
            dynamic_cast<TagLib::ID3v2::TextIdentificationFrame *>(
                id3->frameListMap()["TPOS"].isEmpty()
                    ? nullptr
                    : id3->frameListMap()["TPOS"].front());
        TagLib::String v = _pairStr(td.disc, td.discTotal);
        if (v.isEmpty()) {
          if (fr)
            id3->removeFrame(fr, true);
        } else {
          if (!fr) {
            fr = new TagLib::ID3v2::TextIdentificationFrame(
                "TPOS", TagLib::String::Latin1);
            id3->addFrame(fr);
          }
          fr->setText(v);
        }
      }

      setTXXX("MusicBrainz Album Id", td.mbAlbumID);
      setTXXX("MusicBrainz Artist Id", td.mbArtistID);
      setTXXX("MusicBrainz Track Id", td.mbTrackID);

      (void)coverOpt;
    }

    return f.save(TagLib::MPEG::File::AllTags, TagLib::File::StripNone,
                  TagLib::ID3v2::v4, TagLib::File::DoNotDuplicate);
  }

  if (lower.EndsWith(".m4a") || lower.EndsWith(".mp4") ||
      lower.EndsWith(".aac")) {
    TagLib::MP4::File f(path.Path());
    if (!f.isValid() || !f.tag())
      return false;

    auto *tag = f.tag();
    set_basic_tags(tag, td);

    if (!td.albumArtist.IsEmpty())
      tag->setItem("aART",
                   TagLib::MP4::Item(TagLib::StringList(TLs(td.albumArtist))));
    else
      tag->removeItem("aART");

    {
      TagLib::String key = TagLib::String("\xC2\xA9"
                                          "wrt",
                                          TagLib::String::UTF8);
      if (!td.composer.IsEmpty())
        tag->setItem(key,
                     TagLib::MP4::Item(TagLib::StringList(TLs(td.composer))));
      else
        tag->removeItem(key);
    }

    tag->setItem("trkn", TagLib::MP4::Item((int)td.track, (int)td.trackTotal));
    tag->setItem("disk", TagLib::MP4::Item((int)td.disc, (int)td.discTotal));

    auto setFreeform = [&](const char *name, const BString &val) {
      if (val.IsEmpty()) {
        if (tag->contains(name))
          tag->removeItem(name);
      } else {
        TagLib::MP4::Item item(TagLib::StringList(TLs(val)));

        TagLib::String key = TagLib::String("----:com.apple.iTunes:") +
                             TagLib::String(name, TagLib::String::UTF8);
        tag->setItem(key, item);
      }
    };
    setFreeform("MusicBrainz Album Id", td.mbAlbumID);
    setFreeform("MusicBrainz Artist Id", td.mbArtistID);
    setFreeform("MusicBrainz Track Id", td.mbTrackID);

    if (td.rating > 0) {
      uint32 percentage = td.rating * 10;
      tag->setItem("rate", TagLib::MP4::Item((int)percentage));
    } else {
      tag->removeItem("rate");
    }

    return f.save();
  }

  {
    TagLib::FileRef fr(path.Path());
    if (fr.isNull())
      return false;

    set_basic_tags(fr.tag(), td);

    if (fr.file()) {
      TagLib::PropertyMap pm = fr.file()->properties();

      _setOrErase(pm, "ALBUMARTIST", td.albumArtist);
      _setOrErase(pm, "COMPOSER", td.composer);

      BString tt =
          td.trackTotal
              ? BString().SetToFormat("%lu", (unsigned long)td.trackTotal)
              : "";
      _setOrErase(pm, "TRACKTOTAL", tt);
      _setOrErase(pm, "TOTALTRACKS", tt);

      BString d =
          td.disc ? BString().SetToFormat("%lu", (unsigned long)td.disc) : "";
      BString dt =
          td.discTotal
              ? BString().SetToFormat("%lu", (unsigned long)td.discTotal)
              : "";
      _setOrErase(pm, "DISCTOTAL", dt);
      _setOrErase(pm, "TOTALDISCS", dt);

      _setOrErase(pm, "MUSICBRAINZ_ALBUMID", td.mbAlbumID);
      _setOrErase(pm, "MUSICBRAINZ_ARTISTID", td.mbArtistID);
      _setOrErase(pm, "MUSICBRAINZ_TRACKID", td.mbTrackID);

      TagLib::String trkPair = _pairStr(td.track, td.trackTotal);
      if (!trkPair.isEmpty())
        pm.replace("TRACKNUMBER", TagLib::StringList(trkPair));
      else
        pm.erase("TRACKNUMBER");

      TagLib::String dskPair = _pairStr(td.disc, td.discTotal);
      if (!dskPair.isEmpty())
        pm.replace("TPOS", TagLib::StringList(dskPair));
      else
        pm.erase("TPOS");

      fr.file()->setProperties(pm);
    }
    return fr.save();
  }
}

bool TagSync::WriteTags(const BPath &path, const TagData &in) {
  return WriteTagsToFile(path, in, nullptr);
}

bool TagSync::IsBeFsVolume(const BPath &path) {
  BEntry entry(path.Path());
  if (entry.InitCheck() != B_OK)
    return false;

  BVolume vol;
  if (entry.GetVolume(&vol) != B_OK)
    return false;

  fs_info info{};
  if (fs_stat_dev(vol.Device(), &info) != 0)
    return false;

  return strcmp(info.fsh_name, "bfs") == 0;
}

static bool write_attr_int(BNode &n, const char *name, int32 v) {
  return n.WriteAttr(name, B_INT32_TYPE, 0, &v, sizeof(v)) ==
         (ssize_t)sizeof(v);
}
static bool write_attr_str(BNode &n, const char *name, const BString &s) {
  return n.WriteAttr(name, B_STRING_TYPE, 0, s.String(), s.Length() + 1) ==
         (ssize_t)(s.Length() + 1);
}
static bool remove_attr(BNode &n, const char *name) {
  const status_t st = n.RemoveAttr(name);
  return st == B_OK || st == B_ENTRY_NOT_FOUND;
}

static bool write_attr_str_opt(BNode &n, const char *key, const BString &s) {
  if (s.IsEmpty())
    return remove_attr(n, key);
  return write_attr_str(n, key, s);
}
static bool write_attr_uint_opt(BNode &n, const char *key, uint32 v,
                                bool keepZero = false) {
  if (!keepZero && v == 0)
    return remove_attr(n, key);
  return write_attr_int(n, key, (int32)v);
}

bool TagSync::WriteBfsAttributes(const BPath &path, const TagData &td,
                                 const CoverBlob *, size_t) {
  BEntry e(path.Path());
  if (!e.Exists()) {
    DEBUG_PRINT("[bfs] file not found: %s\\n", path.Path());
    return false;
  }
  BNode n(&e);
  if (n.InitCheck() != B_OK) {
    DEBUG_PRINT("[bfs] BNode init failed for %s\\n", path.Path());
    return false;
  }

  bool ok = true;

  ok &= write_attr_str_opt(n, "Media:Title", td.title);
  ok &= write_attr_str_opt(n, "Audio:Artist", td.artist);
  ok &= write_attr_str_opt(n, "Audio:Album", td.album);
  ok &= write_attr_str_opt(n, "Media:Genre", td.genre);
  ok &= write_attr_str_opt(n, "Media:Comment", td.comment);

  ok &= write_attr_uint_opt(n, "Media:Year", td.year);
  ok &= write_attr_uint_opt(n, "Audio:Track", td.track);

  ok &= write_attr_uint_opt(n, "Media:Length", td.lengthSec);
  ok &= write_attr_uint_opt(n, "Audio:Bitrate", td.bitrate);
  ok &= write_attr_uint_opt(n, "Audio:Rate", td.sampleRate);
  ok &= write_attr_uint_opt(n, "Audio:Channels", td.channels);

  ok &= write_attr_str_opt(n, "Media:AlbumArtist", td.albumArtist);
  ok &= write_attr_str_opt(n, "Media:Composer", td.composer);
  ok &= write_attr_uint_opt(n, "Media:TrackTotal", td.trackTotal);
  ok &= write_attr_uint_opt(n, "Media:Disc", td.disc);
  ok &= write_attr_uint_opt(n, "Media:DiscTotal", td.discTotal);

  ok &= write_attr_str_opt(n, "Media:MBAlbumID", td.mbAlbumID);
  ok &= write_attr_str_opt(n, "Media:MBArtistID", td.mbArtistID);
  ok &= write_attr_str_opt(n, "Media:MBTrackID", td.mbTrackID);
  ok &= write_attr_str_opt(n, "Media:AAID", td.acoustId);

  ok &= write_attr_uint_opt(n, "Media:Rating", td.rating);

  DEBUG_PRINT("[bfs] write attrs %s: %s\\n", path.Path(), ok ? "OK" : "FAILED");
  return ok;
}

bool TagSync::WriteEmbeddedCover(const BPath &file, const uint8 *data,
                                 size_t size, const char *mimeOpt) {
  if (file.InitCheck() != B_OK)
    return false;

  bool removeOnly = (data == nullptr || size == 0);

  const char *mime =
      removeOnly ? nullptr : (mimeOpt ? mimeOpt : sniff_mime(data, size));

  BString p = file.Path();
  BString lower = p;
  lower.ToLower();

  if (lower.EndsWith(".mp3")) {
    TagLib::MPEG::File f(file.Path());
    if (!f.isOpen())
      return false;

    TagLib::ID3v2::Tag *id3 = f.ID3v2Tag(true);
    if (!id3)
      return false;

    TagLib::ID3v2::FrameList apic = id3->frameList("APIC");
    for (auto it = apic.begin(); it != apic.end(); ++it)
      id3->removeFrame(*it, true);

    if (!removeOnly) {
      auto *pic = new TagLib::ID3v2::AttachedPictureFrame;
      pic->setType(TagLib::ID3v2::AttachedPictureFrame::FrontCover);
      pic->setMimeType(mime ? mime : "image/jpeg");
      pic->setPicture(TagLib::ByteVector(reinterpret_cast<const char *>(data),
                                         static_cast<unsigned int>(size)));
      id3->addFrame(pic);
    }

    return f.save(TagLib::MPEG::File::AllTags, TagLib::File::StripNone,
                  TagLib::ID3v2::v3, TagLib::File::DoNotDuplicate);
  }

  if (lower.EndsWith(".m4a") || lower.EndsWith(".mp4") ||
      lower.EndsWith(".aac")) {
    TagLib::MP4::File f(file.Path());
    if (!f.isValid() || !f.tag())
      return false;

    f.tag()->removeItem("covr");

    if (!removeOnly) {
      TagLib::MP4::CoverArt::Format fmt;
      if (mime && strcmp(mime, "image/png") == 0)
        fmt = TagLib::MP4::CoverArt::PNG;
      else if (mime && strcmp(mime, "image/jpeg") == 0)
        fmt = TagLib::MP4::CoverArt::JPEG;
      else {

        return false;
      }

      TagLib::MP4::CoverArt art(
          fmt, TagLib::ByteVector(reinterpret_cast<const char *>(data),
                                  static_cast<unsigned int>(size)));
      TagLib::MP4::CoverArtList list;
      list.append(art);
      f.tag()->setItem("covr", list);
    }
    return f.save();
  }

  if (lower.EndsWith(".flac")) {
    TagLib::FLAC::File f(file.Path());
    if (!f.isValid())
      return false;

    const TagLib::List<TagLib::FLAC::Picture *> &pics = f.pictureList();
    for (unsigned int i = 0; i < pics.size(); ++i)
      f.removePicture(pics[i]);

    if (!removeOnly) {
      auto *pic = new TagLib::FLAC::Picture;
      pic->setType(TagLib::FLAC::Picture::FrontCover);
      if (mime)
        pic->setMimeType(mime);
      else
        pic->setMimeType("image/jpeg");
      pic->setData(TagLib::ByteVector(reinterpret_cast<const char *>(data),
                                      static_cast<unsigned int>(size)));
      f.addPicture(pic);
    }
    return f.save();
  }

  return false;
}

bool TagSync::WriteEmbeddedCover(const BPath &file, const CoverBlob &blob,
                                 const char *mimeOpt) {
  return WriteEmbeddedCover(file, (const uint8 *)blob.data(), blob.size(),
                            mimeOpt);
}

bool TagSync::ExtractEmbeddedCover(const BPath &file, CoverBlob &outCover) {
  outCover.clear();
  const char *p = file.Path();
  if (!p)
    return false;

  {
    TagLib::MPEG::File f(p);
    if (f.isOpen()) {
      if (TagLib::ID3v2::Tag *id3 = f.ID3v2Tag(false)) {
        const TagLib::ID3v2::FrameList &apic = id3->frameList("APIC");
        for (auto it = apic.begin(); it != apic.end(); ++it) {
          if (auto *pic =
                  dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(*it)) {
            const TagLib::ByteVector &bv = pic->picture();
            if (!bv.isEmpty()) {
              outCover.assign(bv.data(), bv.size());
              return outCover.size() > 0;
            }
          }
        }
      }
    }
  }

  {
    TagLib::FLAC::File f(p);
    if (f.isValid()) {
      const TagLib::List<TagLib::FLAC::Picture *> &pics = f.pictureList();
      if (!pics.isEmpty() && pics[0]) {
        const TagLib::ByteVector &bv = pics[0]->data();
        if (!bv.isEmpty()) {
          outCover.assign(bv.data(), bv.size());
          return outCover.size() > 0;
        }
      }
    }
  }

  {
    TagLib::MP4::File f(p);
    if (f.isValid() && f.tag()) {
      const TagLib::MP4::ItemMap &im = f.tag()->itemMap();
      auto it = im.find("covr");
      if (it != im.end()) {
        const TagLib::MP4::CoverArtList list = it->second.toCoverArtList();
        if (!list.isEmpty()) {
          const TagLib::MP4::CoverArt &art = list.front();
          const TagLib::ByteVector &bv = art.data();
          if (!bv.isEmpty()) {
            outCover.assign(bv.data(), bv.size());
            return outCover.size() > 0;
          }
        }
      }
    }
  }

  return false;
}

bool TagSync::ReadBfsAttributes(const BPath &path, TagData &out) {
  BNode node(path.Path());
  if (node.InitCheck() != B_OK)
    return false;

  char buffer[512];
  memset(buffer, 0, sizeof(buffer));
  int32 intVal;

  if (node.ReadAttr("Media:Title", B_STRING_TYPE, 0, buffer, sizeof(buffer)) >
      0) {
    out.title = buffer;
    _trimBString(out.title);
  }

  if (node.ReadAttr("Audio:Artist", B_STRING_TYPE, 0, buffer, sizeof(buffer)) >
      0) {
    out.artist = buffer;
    _trimBString(out.artist);
  }

  if (node.ReadAttr("Audio:Album", B_STRING_TYPE, 0, buffer, sizeof(buffer)) >
      0) {
    out.album = buffer;
    _trimBString(out.album);
  }

  if (node.ReadAttr("Media:Genre", B_STRING_TYPE, 0, buffer, sizeof(buffer)) >
      0) {
    out.genre = buffer;
    _trimBString(out.genre);
  } else if (node.ReadAttr("Audio:Genre", B_STRING_TYPE, 0, buffer,
                           sizeof(buffer)) > 0) {
    out.genre = buffer;
    _trimBString(out.genre);
  }

  if (node.ReadAttr("Media:Comment", B_STRING_TYPE, 0, buffer, sizeof(buffer)) >
      0) {
    out.comment = buffer;
    _trimBString(out.comment);
  }

  if (node.ReadAttr("Media:Year", B_INT32_TYPE, 0, &intVal, sizeof(intVal)) > 0)
    out.year = static_cast<uint32>(intVal);

  if (node.ReadAttr("Audio:Track", B_INT32_TYPE, 0, &intVal, sizeof(intVal)) >
      0)
    out.track = static_cast<uint32>(intVal);

  if (node.ReadAttr("Audio:AlbumArtist", B_STRING_TYPE, 0, buffer,
                    sizeof(buffer)) > 0) {
    out.albumArtist = buffer;
    _trimBString(out.albumArtist);
  }

  if (node.ReadAttr("Audio:Composer", B_STRING_TYPE, 0, buffer,
                    sizeof(buffer)) > 0) {
    out.composer = buffer;
    _trimBString(out.composer);
  }

  if (node.ReadAttr("Media:Rating", B_INT32_TYPE, 0, &intVal, sizeof(intVal)) >
      0) {
    if (intVal >= 1 && intVal <= 10)
      out.rating = static_cast<uint32>(intVal);
  }

  if (node.ReadAttr("Media:MBAlbumID", B_STRING_TYPE, 0, buffer,
                    sizeof(buffer)) > 0) {
    out.mbAlbumID = buffer;
    _trimBString(out.mbAlbumID);
  }

  if (node.ReadAttr("Media:MBArtistID", B_STRING_TYPE, 0, buffer,
                    sizeof(buffer)) > 0) {
    out.mbArtistID = buffer;
    _trimBString(out.mbArtistID);
  }

  if (node.ReadAttr("Media:MBTrackID", B_STRING_TYPE, 0, buffer,
                    sizeof(buffer)) > 0) {
    out.mbTrackID = buffer;
    _trimBString(out.mbTrackID);
  }

  if (node.ReadAttr("Media:AAID", B_STRING_TYPE, 0, buffer, sizeof(buffer)) >
      0) {
    out.acoustId = buffer;
    _trimBString(out.acoustId);
  } else if (node.ReadAttr("Media::AAID", B_STRING_TYPE, 0, buffer,
                           sizeof(buffer)) > 0) {
    out.acoustId = buffer;
    _trimBString(out.acoustId);
  }

  return true;
}

TagData TagSync::MergeMetadata(const TagData &primary, const TagData &secondary,
                               int32 mode) {
  TagData result = primary;

  auto mergeField = [mode](BString &target, const BString &secValue) {
    if (mode == CONFLICT_OVERWRITE) {
      return;
    } else if (mode == CONFLICT_FILL_EMPTY) {
      if (target.IsEmpty())
        target = secValue;
    }
  };

  auto mergeUint = [mode](uint32 &target, uint32 secValue) {
    if (mode == CONFLICT_OVERWRITE) {
      return;
    } else if (mode == CONFLICT_FILL_EMPTY) {
      if (target == 0)
        target = secValue;
    }
  };

  mergeField(result.title, secondary.title);
  mergeField(result.artist, secondary.artist);
  mergeField(result.album, secondary.album);
  mergeField(result.genre, secondary.genre);
  mergeField(result.comment, secondary.comment);
  mergeField(result.albumArtist, secondary.albumArtist);
  mergeField(result.composer, secondary.composer);
  mergeField(result.mbAlbumID, secondary.mbAlbumID);
  mergeField(result.mbArtistID, secondary.mbArtistID);
  mergeField(result.mbTrackID, secondary.mbTrackID);
  mergeField(result.acoustId, secondary.acoustId);
  mergeField(result.acoustIdFp, secondary.acoustIdFp);

  mergeUint(result.year, secondary.year);
  mergeUint(result.track, secondary.track);
  mergeUint(result.trackTotal, secondary.trackTotal);
  mergeUint(result.disc, secondary.disc);
  mergeUint(result.discTotal, secondary.discTotal);
  mergeUint(result.rating, secondary.rating);

  return result;
}

bool TagSync::ApplySync(const BPath &path, const TagData &source,
                        bool towardsBfs) {
  if (towardsBfs) {
    return WriteBfsAttributes(path, source, nullptr);
  } else {
    return WriteTags(path, source);
  }
}

bool TagSync::SmartMerge(const TagData &primary, const TagData &secondary,
                         TagData &out, bool &hasConflict) {
  bool changed = false;
  hasConflict = false;
  out = primary;

  auto mergeField = [&](BString &target, const BString &prim,
                        const BString &sec) {
    if (prim == sec)
      return;

    if (prim.IsEmpty() && !sec.IsEmpty()) {
      target = sec;
      changed = true;
    } else if (!prim.IsEmpty() && sec.IsEmpty()) {
      target = prim;
    } else if (!prim.IsEmpty() && !sec.IsEmpty() && prim != sec) {
      hasConflict = true;
    }
  };

  auto mergeUint = [&](uint32 &target, uint32 prim, uint32 sec) {
    if (prim == sec)
      return;

    if (prim == 0 && sec != 0) {
      target = sec;
      changed = true;
    } else if (prim != 0 && sec == 0) {
      target = prim;
    } else if (prim != 0 && sec != 0 && prim != sec) {
      hasConflict = true;
    }
  };

  mergeField(out.title, primary.title, secondary.title);
  mergeField(out.artist, primary.artist, secondary.artist);
  mergeField(out.album, primary.album, secondary.album);
  mergeField(out.genre, primary.genre, secondary.genre);
  mergeField(out.comment, primary.comment, secondary.comment);
  mergeField(out.albumArtist, primary.albumArtist, secondary.albumArtist);
  mergeField(out.composer, primary.composer, secondary.composer);
  mergeField(out.mbAlbumID, primary.mbAlbumID, secondary.mbAlbumID);
  mergeField(out.mbArtistID, primary.mbArtistID, secondary.mbArtistID);
  mergeField(out.mbTrackID, primary.mbTrackID, secondary.mbTrackID);
  mergeField(out.acoustId, primary.acoustId, secondary.acoustId);
  mergeField(out.acoustIdFp, primary.acoustIdFp, secondary.acoustIdFp);

  mergeUint(out.year, primary.year, secondary.year);
  mergeUint(out.track, primary.track, secondary.track);
  mergeUint(out.trackTotal, primary.trackTotal, secondary.trackTotal);
  mergeUint(out.disc, primary.disc, secondary.disc);
  mergeUint(out.discTotal, primary.discTotal, secondary.discTotal);

  mergeUint(out.rating, primary.rating, secondary.rating);

  return changed || hasConflict;
}
