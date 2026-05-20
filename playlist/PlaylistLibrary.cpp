#include "PlaylistLibrary.h"
#include "Debug.h"
#include "PlaylistSidebarView.h"
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>
#include <stdio.h>

PlaylistLibrary::PlaylistLibrary(BMessenger target) : fTarget(target) {
  fPlaylistView = new PlaylistSidebarView("playlist", fTarget, this);
}

PlaylistLibrary::~PlaylistLibrary() {}

PlaylistSidebarView *PlaylistLibrary::View() const { return fPlaylistView; }

void PlaylistLibrary::LoadAvailablePlaylists() {
  fPlaylistView->AddItem("Radio", false, PlaylistItemKind::Radio);
  fPlaylistView->AddItem("DLNA", false, PlaylistItemKind::DLNA);

  BPath path;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK)
    return;

  if (fPlaylistBasePath.IsEmpty())
    return;
  path.SetTo(fPlaylistBasePath);
  BDirectory dir(path.Path());
  if (dir.InitCheck() != B_OK)
    return;

  BEntry entry;
  while (dir.GetNextEntry(&entry) == B_OK) {
    BPath filePath(&entry);
    if (!filePath.Path() || !filePath.Leaf())
      continue;

    BString name = filePath.Leaf();
    if (name.EndsWith(".m3u"))
      name.Truncate(name.Length() - 4);

    fPlaylistView->AddItem(name, filePath.Path());
  }
}

/**
 * @brief Loads a playlist from disk.
 * @param name The name of the playlist (without extension).
 * @return std::vector<BString> List of file paths in the playlist.
 */
std::vector<BString> PlaylistLibrary::LoadPlaylist(const BString &name) {
  std::vector<BString> paths;
  BPath dirPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &dirPath) != B_OK)
    return paths;

  if (fPlaylistBasePath.IsEmpty())
    return paths;
  dirPath.SetTo(fPlaylistBasePath);
  BString playlistFile = name;
  playlistFile += ".m3u";
  dirPath.Append(playlistFile.String());

  BFile file(dirPath.Path(), B_READ_ONLY);
  if (file.InitCheck() != B_OK)
    return paths;

  BString line;
  char ch;
  while (file.Read(&ch, 1) == 1) {
    if (ch == '\n') {
      line.Trim();
      if (!line.IsEmpty() && !line.StartsWith("#")) {
        paths.push_back(line);
      }
      line.Truncate(0);
    } else {
      line += ch;
    }
  }

  return paths;
}

/**
 * @brief Saves a playlist to disk.
 * @param name The name of the playlist (without extension).
 * @param paths List of file paths to save.
 */
void PlaylistLibrary::SavePlaylist(const BString &name,
                                   const std::vector<BString> &paths) {
  BPath dirPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &dirPath) != B_OK)
    return;

  if (fPlaylistBasePath.IsEmpty())
    return;
  dirPath.SetTo(fPlaylistBasePath);
  create_directory(dirPath.Path(), 0777);

  BString fileName = name;
  fileName.Append(".m3u");
  BPath playlistPath(dirPath.Path(), fileName.String());

  BFile file(playlistPath.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
  if (file.InitCheck() != B_OK)
    return;

  for (const auto &path : paths) {
    file.Write(path.String(), path.Length());
    file.Write("\n", 1);
  }

  DEBUG_PRINT("Playlist '%s' saved (%zu entries)\n", name.String(),
              paths.size());

  if (fPlaylistView->FindIndexByName(name) < 0) {
    fPlaylistView->AddItem(name, playlistPath.Path());
  }
}

void PlaylistLibrary::AddPlaylistEntry(const BString &name,
                                       const BString &fullPath) {
  fPlaylistView->AddItem(name, fullPath);
}

void PlaylistLibrary::GetPlaylistNames(BMessage &out, bool onlyWritable) const {
  const int32 count = fPlaylistView->CountItems();
  for (int32 i = 0; i < count; ++i) {
    if (onlyWritable && !fPlaylistView->IsWritableAt(i))
      continue;
    BString name = fPlaylistView->ItemAt(i);
    name.Trim();
    if (!name.IsEmpty())
      out.AddString("name", name);
  }
}

bool PlaylistLibrary::IsPlaylistWritable(const BString &name) const {
  const int32 idx = fPlaylistView->FindIndexByName(name);
  return (idx >= 0) ? fPlaylistView->IsWritableAt(idx) : false;
}

void PlaylistLibrary::Select(int32 index) { fPlaylistView->Select(index); }

int32 PlaylistLibrary::CountItems() const {
  return fPlaylistView->CountItems();
}

void PlaylistLibrary::CreateNewPlaylist(const BString &name) {
  CreatePlaylistFile(name);
  fPlaylistView->SelectByName(name);
}

void PlaylistLibrary::CreatePlaylistFile(const BString &name) {
  std::vector<BString> empty;
  SavePlaylist(name, empty);
  DEBUG_PRINT("New playlist '%s' created\n", name.String());
}

void PlaylistLibrary::DeletePlaylist(const BString &name) {
  BPath dirPath;
  if (find_directory(B_USER_SETTINGS_DIRECTORY, &dirPath) != B_OK)
    return;
  if (fPlaylistBasePath.IsEmpty())
    return;
  dirPath.SetTo(fPlaylistBasePath);

  BString fileName = name;
  fileName += ".m3u";
  dirPath.Append(fileName.String());

  BEntry entry(dirPath.Path());
  if (entry.Exists()) {
    if (entry.Remove() == B_OK) {
      DEBUG_PRINT("Playlist '%s' deleted (%s)\n",
                  name.String(), dirPath.Path());
    } else {
      DEBUG_PRINT("Could not delete playlist '%s'\n",
                  name.String());
    }
  }
}

void PlaylistLibrary::AddItemToPlaylist(const BString &playlistName, const BString &path) {
  DEBUG_PRINT("AddItemToPlaylist called: %s -> %s\n",
              path.String(), playlistName.String());

  std::vector<BString> items = LoadPlaylist(playlistName);
  for (const auto &item : items) {
    if (item.Compare(path) == 0) {
      DEBUG_PRINT("Path already exists, skipping\n");
      return;
    }
  }

  items.push_back(path);
  SavePlaylist(playlistName, items);
  DEBUG_PRINT("Path added and saved\n");
}

void PlaylistLibrary::RenamePlaylist(const BString &oldName,
                                     const BString &newName) {
  fPlaylistView->RenameItem(oldName, newName);
}

void PlaylistLibrary::SetPlaylistFolderPath(const BString &path) {
  fPlaylistBasePath = path;
}

/**
 * @brief Reorders an item within a playlist and saves the change.
 * @param name Playlist name.
 * @param fromIndex Original index.
 * @param toIndex New index.
 */
void PlaylistLibrary::ReorderPlaylistItem(const BString &name, int32 fromIndex,
                                          int32 toIndex) {
  if (fromIndex == toIndex)
    return;

  std::vector<BString> paths = LoadPlaylist(name);
  if (fromIndex < 0 || fromIndex >= (int32)paths.size())
    return;
  if (toIndex < 0 || toIndex >= (int32)paths.size())
    return;

  // Move the item within the in-memory list.
  BString item = paths[fromIndex];
  paths.erase(paths.begin() + fromIndex);
  paths.insert(paths.begin() + toIndex, item);

  // Persist the updated order to disk.
  SavePlaylist(name, paths);
}
