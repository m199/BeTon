/**
 * @file SyncSettingsDialog.cpp
 * @brief Implementation of synchronization settings dialog.
 */

#include "SyncSettingsDialog.h"
#include "Messages.h"
#include "MusicSource.h"

#include <Catalog.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <cstring>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SyncSettingsDialog"

enum {
  MSG_PRIMARY_CHANGED = 'prch',
  MSG_SECONDARY_CHANGED = 'sech',
  MSG_CONFLICT_CHANGED = 'coch',
  MSG_SYNC_OK = 'syok',
  MSG_SYNC_CANCEL = 'syca'
};

SyncSettingsDialog::SyncSettingsDialog(BMessenger target, int32 index,
                                       const BString &path, bool isBfs,
                                       const MusicSource *existing)
    : BWindow(BRect(100, 100, 500, 400), B_TRANSLATE("Sync Settings"),
              B_TITLED_WINDOW,
              B_NOT_RESIZABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
      fTarget(target), fIndex(index), fPath(path), fIsBfs(isBfs),
      fExisting(existing), fPrimaryMenu(nullptr), fSecondaryMenu(nullptr),
      fConflictMenu(nullptr), fOkButton(nullptr), fCancelButton(nullptr) {
  _BuildUI();
  _LoadExistingSettings();
  _UpdateControls();
  CenterOnScreen();
}

SyncSettingsDialog::~SyncSettingsDialog() {}

void SyncSettingsDialog::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_SYNC_OK:
    _SendSettings();
    break;

  case MSG_SYNC_CANCEL:
    Quit();
    break;

  case MSG_PRIMARY_CHANGED:
  case MSG_SECONDARY_CHANGED:
  case MSG_CONFLICT_CHANGED:
    _UpdateControls();
    break;

  default:
    BWindow::MessageReceived(msg);
  }
}

void SyncSettingsDialog::_SendSettings() {
  MusicSource out;
  out.path = fPath;

  if (fPrimaryMenu && fPrimaryMenu->Menu()) {
    BMenuItem *item = fPrimaryMenu->Menu()->FindMarked();
    if (item)
      out.primary =
          static_cast<SourceType>(fPrimaryMenu->Menu()->IndexOf(item));
  }

  if (fSecondaryMenu && fSecondaryMenu->Menu()) {
    BMenuItem *item = fSecondaryMenu->Menu()->FindMarked();
    if (item) {
      int32 index = fSecondaryMenu->Menu()->IndexOf(item);
      if (index == 0)
        out.secondary = SOURCE_NONE;
      else if (index == 1)
        out.secondary = SOURCE_TAGS;
      else if (index == 2)
        out.secondary = SOURCE_BFS;
    }
  }

  if (fConflictMenu && fConflictMenu->Menu()) {
    BMenuItem *item = fConflictMenu->Menu()->FindMarked();
    if (item)
      out.conflictMode =
          static_cast<ConflictMode>(fConflictMenu->Menu()->IndexOf(item));
  }

  BMessage msg(MSG_SYNC_SETTINGS_RESULT);
  msg.AddInt32("index", fIndex);
  BMessage srcMsg;
  out.SaveTo(&srcMsg);
  msg.AddMessage("source", &srcMsg);

  fTarget.SendMessage(&msg);
  Quit();
}

void SyncSettingsDialog::_BuildUI() {
  BStringView *pathView = new BStringView("path", fPath.String());

  BPopUpMenu *primaryPop = new BPopUpMenu("");
  primaryPop->AddItem(new BMenuItem(MusicSource::SourceTypeName(SOURCE_TAGS),
                                    new BMessage(MSG_PRIMARY_CHANGED)));
  if (fIsBfs) {
    primaryPop->AddItem(new BMenuItem(MusicSource::SourceTypeName(SOURCE_BFS),
                                      new BMessage(MSG_PRIMARY_CHANGED)));
  }
  primaryPop->AddItem(new BMenuItem(MusicSource::SourceTypeName(SOURCE_NONE),
                                    new BMessage(MSG_PRIMARY_CHANGED)));
  primaryPop->ItemAt(0)->SetMarked(true);
  fPrimaryMenu =
      new BMenuField("primary", B_TRANSLATE("Primary Source:"), primaryPop);

  BPopUpMenu *secondaryPop = new BPopUpMenu("");
  secondaryPop->AddItem(new BMenuItem(MusicSource::SourceTypeName(SOURCE_NONE),
                                      new BMessage(MSG_SECONDARY_CHANGED)));
  secondaryPop->AddItem(new BMenuItem(MusicSource::SourceTypeName(SOURCE_TAGS),
                                      new BMessage(MSG_SECONDARY_CHANGED)));
  if (fIsBfs) {
    secondaryPop->AddItem(new BMenuItem(MusicSource::SourceTypeName(SOURCE_BFS),
                                        new BMessage(MSG_SECONDARY_CHANGED)));
    secondaryPop->ItemAt(2)->SetMarked(true);
  } else {
    secondaryPop->ItemAt(0)->SetMarked(true);
  }
  fSecondaryMenu = new BMenuField("secondary", B_TRANSLATE("Secondary Source:"),
                                  secondaryPop);

  BPopUpMenu *conflictPop = new BPopUpMenu("");
  conflictPop->AddItem(
      new BMenuItem(MusicSource::ConflictModeName(CONFLICT_OVERWRITE),
                    new BMessage(MSG_CONFLICT_CHANGED)));
  conflictPop->AddItem(
      new BMenuItem(MusicSource::ConflictModeName(CONFLICT_FILL_EMPTY),
                    new BMessage(MSG_CONFLICT_CHANGED)));
  conflictPop->AddItem(
      new BMenuItem(MusicSource::ConflictModeName(CONFLICT_ASK),
                    new BMessage(MSG_CONFLICT_CHANGED)));
  conflictPop->ItemAt(2)->SetMarked(true);
  fConflictMenu = new BMenuField(
      "conflict", B_TRANSLATE("Conflict Resolution:"), conflictPop);

  fOkButton = new BButton("ok", B_TRANSLATE("OK"), new BMessage(MSG_SYNC_OK));
  fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
                              new BMessage(MSG_SYNC_CANCEL));

  BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
      .SetInsets(B_USE_WINDOW_SPACING)
      .Add(new BStringView("label", B_TRANSLATE("Configure metadata "
                                                "synchronization:")))
      .Add(pathView)
      .AddGlue()
      .Add(fPrimaryMenu)
      .Add(fSecondaryMenu)
      .Add(fConflictMenu)
      .AddGlue()
      .AddGroup(B_HORIZONTAL)
      .AddGlue()
      .Add(fCancelButton)
      .Add(fOkButton)
      .End()
      .End();

  fOkButton->MakeDefault(true);
}

void SyncSettingsDialog::_LoadExistingSettings() {
  if (!fExisting)
    return;

  BMenu *primaryMenu = fPrimaryMenu->Menu();
  const char *primaryName = MusicSource::SourceTypeName(fExisting->primary);
  for (int32 i = 0; i < primaryMenu->CountItems(); i++) {
    BMenuItem *item = primaryMenu->ItemAt(i);
    if (item && strcmp(item->Label(), primaryName) == 0) {
      item->SetMarked(true);
      break;
    }
  }

  BMenu *secondaryMenu = fSecondaryMenu->Menu();
  const char *secondaryName = MusicSource::SourceTypeName(fExisting->secondary);
  for (int32 i = 0; i < secondaryMenu->CountItems(); i++) {
    BMenuItem *item = secondaryMenu->ItemAt(i);
    if (item && strcmp(item->Label(), secondaryName) == 0) {
      item->SetMarked(true);
      break;
    }
  }

  BMenu *conflictMenu = fConflictMenu->Menu();
  if (static_cast<int32>(fExisting->conflictMode) <
      conflictMenu->CountItems()) {
    BMenuItem *item =
        conflictMenu->ItemAt(static_cast<int32>(fExisting->conflictMode));
    if (item)
      item->SetMarked(true);
  }
}

void SyncSettingsDialog::_UpdateControls() {
  if (!fIsBfs) {
    BMenuItem *item =
        fPrimaryMenu->Menu()->FindItem(MusicSource::SourceTypeName(SOURCE_BFS));
    if (item)
      item->SetEnabled(false);

    item = fSecondaryMenu->Menu()->FindItem(
        MusicSource::SourceTypeName(SOURCE_BFS));
    if (item)
      item->SetEnabled(false);
  }
}
