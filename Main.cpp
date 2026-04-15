#include "Debug.h"
#include "MainWindow.h"
#include <Application.h>
#include <Catalog.h>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Application"

bool gIsDebug = false;

/**
 * @class BeTonApp
 * @brief The Application class.
 *
 * Initializes the application and creates the main window.
 */
class BeTonApp : public BApplication {
public:
  BeTonApp() : BApplication("application/x-vnd.BeTon") {}

  void ReadyToRun() override {
    MainWindow *window = new MainWindow();
    window->Show();
  }
};

int main(int argc, char **argv) {
  srand(static_cast<unsigned int>(time(nullptr)));
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--debug") == 0) {
      gIsDebug = true;
    }
  }

  if (!gIsDebug) {

    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
  }

  if (gIsDebug) {
    printf("[Main] Starting in DEBUG mode\n");
  }

  BeTonApp app;
  app.Run();
  return 0;
}
