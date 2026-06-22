#include "Debug.h"
#include "MainWindow.h"
#include "Messages.h"
#include <Application.h>
#include <Catalog.h>
#include <cstring>
#include <signal.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Application"

bool gIsDebug = false;

static void _MakeShortcutSpec(BMessage* spec, const char* appPath, const char* arg, int32 key) {
  spec->MakeEmpty();
  spec->AddString("class", "ShortcutsSpec");
  spec->AddString("class", "ShortcutsSpec");

  char command[512];
  snprintf(command, sizeof(command), "%s %s", appPath, arg);
  spec->AddString("command", command);
  spec->AddInt32("key", key);
  spec->AddInt32("mcidx", 0);
  spec->AddInt32("mcidx", 0);
  spec->AddInt32("mcidx", 0);
  spec->AddInt32("mcidx", 0);

  BMessage modtester;
  modtester.AddString("class", "MinMatchFieldTester");
  int mods[] = {1, 4, 2, 64};
  for (int i = 0; i < 4; ++i) {
    BMessage slave;
    slave.AddString("class", "HasBitsFieldTester");
    slave.AddInt32("rqBits", 0);
    slave.AddInt32("fbBits", mods[i]);
    modtester.AddMessage("mSlave", &slave);
  }
  modtester.AddInt32("mMin", 4);
  spec->AddMessage("modtester", &modtester);

  BMessage act;
  act.AddString("class", "LaunchCommandActuator");
  act.AddString("largv", appPath);
  act.AddString("largv", arg);
  spec->AddMessage("act", &act);
}

/**
 * @class BeTonApp
 * @brief The Application class.
 *
 * Initializes the application and creates the main window.
 */
class BeTonApp : public BApplication {
public:
  BeTonApp() : BApplication("application/x-vnd.BeTon"), fPendingCommand(0) {}

  void ReadyToRun() override {
    MainWindow *window = new MainWindow();
    window->Show();

    app_info info;
    if (GetAppInfo(&info) == B_OK) {
      BPath path(&info.ref);
      RegisterShortcuts(path.Path());
    }

    if (fPendingCommand != 0) {
      window->PostMessage(fPendingCommand);
    }
  }

  void SendCommandToWindow(uint32 command) {
    for (int32 i = 0; ; ++i) {
      BWindow *win = WindowAt(i);
      if (!win)
        break;
      if (dynamic_cast<MainWindow *>(win)) {
        win->PostMessage(command);
        break;
      }
    }
  }

  void ArgvReceived(int32 argc, char **argv) override {
    uint32 cmd = 0;
    for (int32 i = 1; i < argc; ++i) {
      if (strcmp(argv[i], "--play-pause") == 0 || strcmp(argv[i], "play-pause") == 0 || strcmp(argv[i], "ppau") == 0) {
        cmd = MSG_PLAYPAUSE;
      } else if (strcmp(argv[i], "--stop") == 0 || strcmp(argv[i], "stop") == 0) {
        cmd = MSG_STOP;
      } else if (strcmp(argv[i], "--next") == 0 || strcmp(argv[i], "next") == 0) {
        cmd = MSG_PLAY_NEXT;
      } else if (strcmp(argv[i], "--prev") == 0 || strcmp(argv[i], "prev") == 0 || strcmp(argv[i], "prvs") == 0) {
        cmd = MSG_PREV_SONG;
      }
    }

    if (cmd != 0) {
      bool launching = true;
      for (int32 i = 0; ; ++i) {
        BWindow *win = WindowAt(i);
        if (!win)
          break;
        if (dynamic_cast<MainWindow *>(win)) {
          launching = false;
          win->PostMessage(cmd);
          break;
        }
      }
      if (launching) {
        fPendingCommand = cmd;
      }
    }
  }

  void RegisterShortcuts(const char* appPath) {
    BPath settingsPath;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
      return;
    settingsPath.Append("shortcuts_settings");

    BFile file(settingsPath.Path(), B_READ_WRITE | B_CREATE_FILE);
    if (file.InitCheck() != B_OK)
      return;

    BMessage msg;
    off_t size = 0;
    file.GetSize(&size);
    bool exists = (size > 0);
    if (exists) {
      if (msg.Unflatten(&file) != B_OK) {
        return;
      }
    }

    struct {
      int32 key;
      const char* arg;
    } targets[] = {
      {0xc00cd, "--play-pause"},
      {0xc00b7, "--stop"},
      {0xc00b5, "--next"},
      {0xc00b6, "--prev"}
    };

    bool changed = false;

    for (int t = 0; t < 4; ++t) {
      int32 key = targets[t].key;
      const char* arg = targets[t].arg;

      bool found = false;
      BMessage spec;
      int32 i = 0;
      while (msg.FindMessage("spec", i, &spec) == B_OK) {
        int32 specKey = 0;
        spec.FindInt32("key", &specKey);
        if (specKey == key) {
          const char* currentCommand = spec.FindString("command");
          char expectedCommand[512];
          snprintf(expectedCommand, sizeof(expectedCommand), "%s %s", appPath, arg);
          if (!currentCommand || strcmp(currentCommand, expectedCommand) != 0) {
            BMessage newSpec;
            _MakeShortcutSpec(&newSpec, appPath, arg, key);
            msg.ReplaceMessage("spec", i, &newSpec);
            changed = true;
          }
          found = true;
          break;
        }
        i++;
      }

      if (!found) {
        BMessage newSpec;
        _MakeShortcutSpec(&newSpec, appPath, arg, key);
        msg.AddMessage("spec", &newSpec);
        changed = true;
      }
    }

    if (changed) {
      file.Seek(0, SEEK_SET);
      file.SetSize(0);
      msg.Flatten(&file);
    }
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
private:
  uint32 fPendingCommand;
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
