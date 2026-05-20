#ifndef SETTINGS_CONTROLLER_H
#define SETTINGS_CONTROLLER_H

class BMessage;
class MainWindow;

/**
 * @class SettingsController
 * @brief Persists and restores UI/application state for MainWindow.
 */
class SettingsController {
public:
  explicit SettingsController(MainWindow *window);

  /**
   * @brief Serializes current runtime settings to disk.
   */
  void SaveSettings();
  /**
   * @brief Loads settings from disk and applies them to runtime state/UI.
   */
  void LoadSettings();

private:
  /**
   * @brief Writes all relevant settings fields into a BMessage archive.
   */
  void SaveSettingsToMessage(BMessage &state);
  /**
   * @brief Reads persisted settings from a BMessage archive.
   */
  void LoadSettingsFromMessage(BMessage &state);
  /**
   * @brief Applies loaded values that require initialized subsystems/UI.
   */
  void ApplyLoadedSettings();

  MainWindow *fWindow;
};

#endif // SETTINGS_CONTROLLER_H
