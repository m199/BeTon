#ifndef BETON_RADIO_STATION_H
#define BETON_RADIO_STATION_H

#include <String.h>

/**
 * @struct RadioStation
 * @brief Represents a single internet radio station.
 *
 * Stores the station's display name, stream URL, genre classification,
 * and an optional logo URL. Used by RadioStationLibrary for persistence and
 * by the UI for display in radio mode.
 */
struct RadioStation {
  BString name;     ///< Display name of the station.
  BString url;      ///< Stream URL (http/https).
  BString genre;    ///< Genre classification.
  BString country;  ///< Country of origin (e.g. "Germany").
  BString language; ///< Broadcast language (e.g. "German").
  BString logoUrl;  ///< Optional URL to station logo image.
  bool favorite;    ///< User favorite flag.

  RadioStation() : favorite(false) {}

  /**
   * @brief Convenience constructor for quick station creation.
   * @param n Station name.
   * @param u Stream URL.
   * @param g Genre.
   * @param c Country.
   * @param l Language.
   */
  RadioStation(const BString &n, const BString &u, const BString &g = "",
               const BString &c = "", const BString &l = "")
      : name(n), url(u), genre(g), country(c), language(l), favorite(false) {}
};

#endif // BETON_RADIO_STATION_H
