#include "Debug.h"
#include "MainWindow.h"
#include "Messages.h"
#include <Application.h>
#include <Catalog.h>
#include <cstring>
#include <signal.h>

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

  void MessageReceived(BMessage *msg) override {
    switch (msg->what) {
      case MSG_PLAYPAUSE:
      case MSG_PLAY:
      case MSG_PAUSE:
      case MSG_STOP:
      case MSG_PLAY_NEXT:
      case MSG_PREV_SONG: {
        for (int32 i = 0; ; ++i) {
          BWindow *win = WindowAt(i);
          if (!win)
            break;
          if (dynamic_cast<MainWindow *>(win)) {
            win->PostMessage(msg);
            break;
          }
        }
        break;
      }
      default:
        BApplication::MessageReceived(msg);
        break;
    }
  }
};

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);

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
    DEBUG_PRINT("Starting in DEBUG mode\n");
  }

  BeTonApp app;
  app.Run();
  return 0;
}
