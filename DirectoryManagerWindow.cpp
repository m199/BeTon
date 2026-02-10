#include "DirectoryManagerWindow.h"
#include "Messages.h"
#include "MusicSource.h"
#include "SyncSettingsDialog.h"
#include "TagSync.h"

#include <Alert.h>
#include <Catalog.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <NodeInfo.h>
#include <Path.h>
#include <StorageDefs.h>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DirectoryManagerWindow"

DirectoryManagerWindow::DirectoryManagerWindow(BMessenger cacheManager)
    : BWindow(BRect(100, 100, 500, 400), B_TRANSLATE("Manage Music Folders"),
              B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS),
      fCacheManager(cacheManager) {
  fDirectoryList = new BListView("directoryList");
  fDirectoryList->SetInvocationMessage(new BMessage(MSG_DIR_EDIT));
  BScrollView *scroll =
      new BScrollView("scroll", fDirectoryList, 0, false, true);

  fBtnAdd = new BButton("Add", B_TRANSLATE("Add"), new BMessage(MSG_DIR_ADD));
  fBtnRemove = new BButton("Remove", B_TRANSLATE("Remove"),
                           new BMessage(MSG_DIR_REMOVE));
  fBtnOK = new BButton("OK", B_TRANSLATE("OK"), new BMessage(MSG_DIR_OK));

  fAddPanel =
      new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr,
                     B_DIRECTORY_NODE, false, nullptr, nullptr, true, true);

  BBox *buttonBox = new BBox(B_FANCY_BORDER);
  BLayoutBuilder::Group<>(buttonBox, B_HORIZONTAL, 10)
      .SetInsets(10, 10, 10, 10)
      .Add(fBtnAdd)
      .Add(fBtnRemove)
      .AddGlue()
      .Add(fBtnOK);

  BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
      .SetInsets(10, 10, 10, 10)
      .Add(scroll)
      .Add(buttonBox);

  font_height fh;
  be_plain_font->GetHeight(&fh);
  float fontHeight = fh.ascent + fh.descent + fh.leading;

  ResizeTo(fontHeight * 27, fontHeight * 20);
  CenterOnScreen();

  LoadSettings();
}

void DirectoryManagerWindow::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_DIR_ADD:
    fAddPanel->Show();
    break;

  case B_REFS_RECEIVED: {
    entry_ref ref;
    if (msg->FindRef("refs", &ref) == B_OK) {
      AddDirectory(ref);
    }
    break;
  }

  case MSG_DIR_REMOVE:
    RemoveSelectedDirectory();
    break;

  case MSG_DIR_EDIT: {
    int32 index = fDirectoryList->CurrentSelection();
    if (index >= 0) {
      EditDirectory(index);
    }
    break;
  }

  case MSG_DIR_OK:
    SaveSettings();
    if (fCacheManager.IsValid()) {
      fCacheManager.SendMessage(MSG_RESCAN);
    }
    Quit();
    break;

  case MSG_SYNC_SETTINGS_RESULT: {
    int32 index;
    BMessage srcMsg;
    if (msg->FindInt32("index", &index) == B_OK &&
        msg->FindMessage("source", &srcMsg) == B_OK) {
      MusicSource src;
      src.LoadFrom(&srcMsg);

      if (index < 0) {
        fSources.push_back(src);
        fDirectoryList->AddItem(new BStringItem(src.path.String()));
      } else if (index >= 0 && index < (int32)fSources.size()) {
        fSources[index] = src;
        BStringItem *item =
            static_cast<BStringItem *>(fDirectoryList->ItemAt(index));
        if (item) {
          item->SetText(src.path.String());
          fDirectoryList->InvalidateItem(index);
        }
      }
      SaveSettings();
    }
    break;
  }

  default:
    BWindow::MessageReceived(msg);
    break;
  }
}

void DirectoryManagerWindow::AddDirectory(const entry_ref &ref) {
  BPath path(&ref);
  if (path.InitCheck() != B_OK)
    return;

  for (size_t i = 0; i < fSources.size(); ++i) {
    if (fSources[i].path == path.Path())
      return;
  }

  bool isBfs = TagSync::IsBeFsVolume(path);

  SyncSettingsDialog *dialog =
      new SyncSettingsDialog(BMessenger(this), -1, path.Path(), isBfs);
  dialog->Show();
}

void DirectoryManagerWindow::RemoveSelectedDirectory() {
  int32 index = fDirectoryList->CurrentSelection();
  if (index < 0)
    return;

  delete fDirectoryList->RemoveItem(index);
  fSources.erase(fSources.begin() + index);
  SaveSettings();
}

void DirectoryManagerWindow::EditDirectory(int32 index) {
  if (index < 0 || index >= static_cast<int32>(fSources.size()))
    return;

  MusicSource &src = fSources[index];
  BPath path(src.path.String());
  bool isBfs = TagSync::IsBeFsVolume(path);

  SyncSettingsDialog *dialog = new SyncSettingsDialog(
      BMessenger(this), index, src.path.String(), isBfs, &src);
  dialog->Show();
}

void DirectoryManagerWindow::SaveSettings() {
  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
    return;

  settingsPath.Append("BeTon");
  create_directory(settingsPath.Path(), 0755);
  settingsPath.Append("directories.settings");

  BFile file(settingsPath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
  if (file.InitCheck() != B_OK)
    return;

  BMessage archive;
  for (size_t i = 0; i < fSources.size(); ++i) {
    BMessage srcMsg;
    fSources[i].SaveTo(&srcMsg);
    archive.AddMessage("source", &srcMsg);
  }

  archive.Flatten(&file);
}

void DirectoryManagerWindow::LoadSettings() {
  BPath settingsPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &settingsPath) != B_OK)
    return;

  settingsPath.Append("BeTon/directories.settings");
  BFile file(settingsPath.Path(), B_READ_ONLY);

  if (file.InitCheck() == B_OK) {
    BMessage archive;
    if (archive.Unflatten(&file) == B_OK) {
      for (int32 i = 0;; i++) {
        BMessage srcMsg;
        if (archive.FindMessage("source", i, &srcMsg) != B_OK)
          break;
        MusicSource src;
        src.LoadFrom(&srcMsg);
        fSources.push_back(src);
        fDirectoryList->AddItem(new BStringItem(src.path.String()));
      }
      return;
    }
  }

  MigrateFromOldFormat();
}

void DirectoryManagerWindow::MigrateFromOldFormat() {
  BPath oldPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &oldPath) != B_OK)
    return;

  oldPath.Append("BeTon/directories.txt");
  BFile oldFile(oldPath.Path(), B_READ_ONLY);
  if (oldFile.InitCheck() != B_OK)
    return;

  BString line;
  char ch;
  while (oldFile.Read(&ch, 1) == 1) {
    if (ch == '\n') {
      if (!line.IsEmpty()) {
        MusicSource src(line);
        src.primary = SOURCE_TAGS;
        src.secondary = SOURCE_NONE;
        src.conflictMode = CONFLICT_OVERWRITE;

        fSources.push_back(src);
        fDirectoryList->AddItem(new BStringItem(line.String()));
        line.Truncate(0);
      }
    } else {
      line += ch;
    }
  }

  if (!fSources.empty()) {
    SaveSettings();
  }
}
