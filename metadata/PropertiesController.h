#ifndef BETON_PROPERTIES_CONTROLLER_H
#define BETON_PROPERTIES_CONTROLLER_H

#include <Message.h>

class MainWindow;
class MetadataPropertiesWindow;

/**
 * @class PropertiesController
 * @brief Handles metadata-properties UI actions and write requests.
 *
 * This controller opens the properties window, forwards metadata save
 * operations to `MetadataService`, and applies quick rating changes.
 */
class PropertiesController {
public:
  /**
   * @brief Creates a properties controller.
   * @param window Owning main window context.
   */
  PropertiesController(MainWindow* window);

  /**
   * @brief Destroys the controller.
   */
  ~PropertiesController();

  /**
   * @brief Saves edited metadata fields from properties UI.
   * @param msg Message payload with edited tag fields and file list.
   */
  void SavePropertyTags(BMessage* msg);

  /**
   * @brief Opens the metadata properties window for current selection.
   * @param msg Message containing selected refs and/or explicit file list.
   */
  void OpenMetadataPropertiesWindow(BMessage* msg);

  /**
   * @brief Applies a rating update for one or more selected files.
   * @param msg Message containing rating value and refs list.
   */
  void SetRating(BMessage* msg);

private:
  /** @brief Owning main window and shared app state access. */
  MainWindow* fWindow;
};

#endif // BETON_PROPERTIES_CONTROLLER_H
