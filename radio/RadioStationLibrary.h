#ifndef BETON_RADIO_STATION_LIBRARY_H
#define BETON_RADIO_STATION_LIBRARY_H

#include "RadioStation.h"
#include <Messenger.h>
#include <String.h>
#include <vector>
#include <UrlContext.h>

/**
 * @class RadioStationLibrary
 * @brief Manages internet radio stations: loading, saving, importing.
 *
 * Stations are persisted in BMessage format at
 * ~/config/settings/BeTon/radio.settings.
 *
 * Supports manual station management (add/edit/remove) and
 * import from .m3u and .pls playlist files.
 */
class RadioStationLibrary {
public:
  /**
   * @brief Constructs the RadioStationLibrary.
   * @param target Messenger for UI notifications.
   */
  RadioStationLibrary(BMessenger target);
  ~RadioStationLibrary();

  /**
   * @brief Loads stations from the settings file on disk.
   * @return True if at least one station was loaded.
   */
  bool LoadStations();

  /**
   * @brief Saves all stations to the settings file on disk.
   * @return True on success.
   */
  bool SaveStations();

  /** @name Station Management */
  ///@{

  /**
   * @brief Adds a new station to the list.
   * @param station The station to add.
   * @return Index of the newly added station.
   */
  int32 AddStation(const RadioStation &station);

  /**
   * @brief Removes a station by index.
   * @param index Index of the station to remove.
   * @return True if the station was removed.
   */
  bool RemoveStation(int32 index);

  /**
   * @brief Replaces the station at the given index.
   * @param index Index of the station to replace.
   * @param station New station data.
   * @return True if the station was updated.
   */
  bool EditStation(int32 index, const RadioStation &station);

  /**
   * @brief Returns all stations.
   * @return Const reference to the station vector.
   */
  const std::vector<RadioStation> &AllStations() const;

  /**
   * @brief Returns a station by index.
   * @param index Index of the station.
   * @return Pointer to the station, or nullptr if out of range.
   */
  const RadioStation *StationAt(int32 index) const;

  /**
   * @brief Returns the number of stations.
   */
  int32 CountStations() const;

  ///@}

  /** @name Import */
  ///@{

  /**
   * @brief Imports stations from an .m3u file.
   * @param path Path to the .m3u file.
   * @return Number of stations imported.
   */
  int32 ImportM3U(const char *path);

  /**
   * @brief Imports stations from a .pls file.
   * @param path Path to the .pls file.
   * @return Number of stations imported.
   */
  int32 ImportPLS(const char *path);

  ///@}

  /**
   * @brief Resolves a playlist URL (.m3u/.pls) to its first stream URL.
   *
   * @param url The input URL.
   * @return The resolved direct stream URL.
   */
  BString ResolveStreamUrl(const BString &url);

private:
  /**
   * @brief Determines the full path to the radio settings file.
   */
  BString _SettingsPath() const;

  BMessenger fTarget;
  std::vector<RadioStation> fStations;
  BPrivate::Network::BUrlContext fUrlContext;
};

#endif // BETON_RADIO_STATION_LIBRARY_H
