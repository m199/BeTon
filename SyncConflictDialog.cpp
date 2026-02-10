/**
 * @file SyncConflictDialog.cpp
 * @brief Implementation of metadata conflict resolution dialog.
 */

#include "SyncConflictDialog.h"
#include "Debug.h"
#include "Messages.h"

#include <Catalog.h>
#include <LayoutBuilder.h>
#include <SeparatorView.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SyncConflictDialog"

SyncConflictDialog::SyncConflictDialog(BMessenger target,
                                       const BString &filePath,
                                       const TagData &tags, const TagData &bfs,
                                       int32 index, int32 total)
    : BWindow(BRect(0, 0, 450, 350), B_TRANSLATE("Metadata Conflict"),
              B_MODAL_WINDOW,
              B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS |
                  B_CLOSE_ON_ESCAPE),
      fTarget(target), fFilePath(filePath), fTags(tags), fBfs(bfs),
      fIndex(index), fTotal(total), fUseTags(nullptr), fUseBfs(nullptr) {
  BuildLayout();
  CenterOnScreen();
}

void SyncConflictDialog::BuildLayout() {
  BString fileInfo;
  fileInfo.SetToFormat(B_TRANSLATE("File %ld of %ld"), (long)(fIndex + 1),
                       (long)fTotal);

  BString tagsInfo;
  tagsInfo << B_TRANSLATE("Title: ") << fTags.title << "\n"
           << B_TRANSLATE("Artist: ") << fTags.artist << "\n"
           << B_TRANSLATE("Album: ") << fTags.album;

  BString bfsInfo;
  bfsInfo << B_TRANSLATE("Title: ") << fBfs.title << "\n"
          << B_TRANSLATE("Artist: ") << fBfs.artist << "\n"
          << B_TRANSLATE("Album: ") << fBfs.album;

  fUseTags = new BRadioButton("useTags", B_TRANSLATE("Use Tags"), nullptr);
  fUseBfs = new BRadioButton("useBfs", B_TRANSLATE("Use BFS"), nullptr);
  fUseTags->SetValue(B_CONTROL_ON);

  BButton *cancelBtn = new BButton("cancel", B_TRANSLATE("Cancel"),
                                   new BMessage(MSG_SYNC_CONFLICT_SKIP));
  BButton *applyBtn = new BButton("apply", B_TRANSLATE("Apply"),
                                  new BMessage(MSG_SYNC_CONFLICT_OK));
  BButton *allBtn = new BButton("all", B_TRANSLATE("Apply to All"),
                                new BMessage(MSG_SYNC_CONFLICT_ALL));

  applyBtn->MakeDefault(true);

  fFileLabel = new BStringView("fileLabel", fileInfo);

  BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
      .SetInsets(B_USE_WINDOW_INSETS)
      .Add(fFileLabel)
      .Add(new BStringView("pathLabel", fFilePath))
      .Add(new BSeparatorView(B_HORIZONTAL))
      .AddGroup(B_HORIZONTAL)
      .AddGroup(B_VERTICAL)
      .Add(new BStringView("tagsLabel", B_TRANSLATE("Tags")))
      .Add(new BStringView("tagsInfo", tagsInfo))
      .Add(fUseTags)
      .End()
      .Add(new BSeparatorView(B_VERTICAL))
      .AddGroup(B_VERTICAL)
      .Add(new BStringView("bfsLabel", B_TRANSLATE("BFS Attributes")))
      .Add(new BStringView("bfsInfo", bfsInfo))
      .Add(fUseBfs)
      .End()
      .End()
      .Add(new BSeparatorView(B_HORIZONTAL))
      .AddGroup(B_HORIZONTAL)
      .Add(cancelBtn)
      .AddGlue()
      .Add(allBtn)
      .Add(applyBtn)
      .End()
      .End();
}

void SyncConflictDialog::MessageReceived(BMessage *msg) {
  switch (msg->what) {
  case MSG_SYNC_CONFLICT_SKIP:
  case MSG_SYNC_CONFLICT_OK:
  case MSG_SYNC_CONFLICT_ALBUM:
  case MSG_SYNC_CONFLICT_ALL:
    SendChoice(msg->what);
    break;

  default:
    BWindow::MessageReceived(msg);
  }
}

void SyncConflictDialog::SendChoice(uint32 what) {
  BMessage reply(what);
  reply.AddString("path", fFilePath);
  reply.AddInt32("index", fIndex);
  reply.AddInt32("total", fTotal);
  reply.AddBool("useTags", fUseTags ? fUseTags->Value() == B_CONTROL_ON : true);

  fTarget.SendMessage(&reply);
  PostMessage(B_QUIT_REQUESTED);
}

void SyncConflictDialog::UpdateTotal(int32 newTotal) {
  if (Lock()) {
    fTotal = newTotal;
    if (fFileLabel) {
      BString fileInfo;
      fileInfo.SetToFormat(B_TRANSLATE("File %ld of %ld"), (long)(fIndex + 1),
                           (long)fTotal);
      fFileLabel->SetText(fileInfo.String());
      DEBUG_PRINT("[SyncConflictDialog] UpdateTotal: %s\n", fileInfo.String());
    }
    Unlock();
  }
}
