#ifndef BETON_MESSAGES_H
#define BETON_MESSAGES_H

/** @name Media Scanning & Library Management */
///@{
#define MSG_START_SCAN 'msta'       ///< Start the directory scan.
#define MSG_SCAN_DONE 'mdon'        ///< Scanning is complete.
#define MSG_SCAN_FINISHED 'scfd'    ///< Final cleanup after scan.
#define MSG_SCAN_PROGRESS 'mprg'    ///< Periodic progress update from scanner.
#define MSG_MEDIA_ITEM_FOUND 'mitm' ///< (Legacy) Single item found.
#define MSG_MEDIA_BATCH 'mbat'      ///< Batch of items from scanner to cache.
#define MSG_MEDIA_ITEM_REMOVED 'mirm' ///< Item removed from library.
#define MSG_LOAD_CACHE 'load'         ///< Request to load initial cache.
#define MSG_CACHE_LOADED 'cach'       ///< Cache loading complete.
#define MSG_RESCAN 'resc'             ///< Trigger a quick rescan.
#define MSG_RESCAN_FULL 'rscn'        ///< Trigger a full, deep rescan.
#define MSG_BASE_OFFLINE 'moff'       ///< Base path is offline/unreachable.
#define MSG_MANAGE_DIRECTORIES 'mdir' ///< Open directory manager.
#define MSG_INIT_LIBRARY 'liby'       ///< Initialize library views.
#define MSG_BATCH_TIMER 'batc'        ///< Batch update timer tick.
#define MSG_LAZY_LOAD 'lzld'          ///< Lazy loading trigger.
#define MSG_DIR_ADD 'dadd'            ///< Add directory to library.
#define MSG_DIR_REMOVE 'drmv'         ///< Remove directory from library.
#define MSG_DIR_OK 'doky'             ///< Directory settings confirm.
///@}

/** @name Playback Control */
///@{
#define MSG_PLAY 'play'           ///< Request to start playback.
#define MSG_PAUSE 'paus'          ///< Request to pause playback.
#define MSG_PLAYPAUSE 'ppau'      ///< Toggle play/pause.
#define MSG_STOP 'stop'           ///< Stop playback.
#define MSG_PLAY_NEXT 'pnxt'      ///< Play next track manually or auto.
#define MSG_NEXTS 'nxts'          ///< (Alternate) Play next.
#define MSG_PREV_SONG 'prvs'      ///< Play previous track.
#define MSG_SEEK_REQUEST 'seek'   ///< User requested seek (slider).
#define MSG_VOLUME_CHANGED 'volu' ///< Volume slider changed.
#define MSG_TIME_UPDATE 'tmuc'    ///< Periodic playback time update.
#define MSG_TRACK_ENDED 'tend'    ///< Current track finished playing.
#define MSG_NOW_PLAYING 'nply'    ///< Notification of new track playing.
#define MSG_PLAY_BTN 'plyB'       ///< Play button clicked.
#define MSG_PREV_BTN 'prvB'       ///< Previous button clicked.
#define MSG_SHUFFLE_TOGGLE 'shuf' ///< Toggle shuffle mode.
#define MSG_REPEAT_TOGGLE 'rept'  ///< Toggle repeat mode.
///@}

/** @name UI & Selection */
///@{
#define MSG_SELECTION_CHANGED 'slch'         ///< Generic selection change.
#define MSG_SELECTION_CHANGED_CONTENT 'selC' ///< Main content list selection.
#define MSG_SELECTION_CHANGED_GENRE 'selG'   ///< Genre filter selection.
#define MSG_SELECTION_CHANGED_ARTIST 'selA'  ///< Artist filter selection.
#define MSG_SELECTION_CHANGED_ALBUM 'selB'   ///< Album filter selection.
#define MSG_DELETE_ITEM 'delI'               ///< User requested delete.
#define MSG_SHOW_CONTEXT_MENU 'ctxR'         ///< Right-click context menu.
#define MSG_REVEAL_IN_TRACKER 'rvit'         ///< Show file in Tracker.
#define MSG_ARTWORK_ON 'aron'                ///< Show artwork panel.
#define MSG_ARTWORK_OFF 'arof'               ///< Hide artwork panel.
#define MSG_SEARCH_MODIFY 'srch'             ///< Search query changed.
#define MSG_SEARCH_EXECUTE 'srex'            ///< Search execute (Enter).
#define MSG_UPDATE_INFO 'updI'               ///< Request to update info panel.
#define MSG_VIEW_INFO 'vifo'                 ///< View track info/properties.
#define MSG_VIEW_COVER 'vico'                ///< View cover art.
#define MSG_STATUS_UPDATE 'mstp'             ///< Update status bar text.
#define MSG_RESET_STATUS 'rstS'              ///< Clear status bar text.
#define MSG_NEXT_FILE 'next'                 ///< Navigate to next file (UI).
#define MSG_PREV_FILE 'prev'                 ///< Navigate to prev file (UI).
#define MSG_COUNT_UPDATED 'cntu'          ///< Track count updated (filtering).
#define MSG_LIBRARY_PREVIEW 'libP'        ///< Update library preview stats.
#define MSG_SEEKBAR_COLOR_DROPPED 'sbcd'  ///< Color dropped on SeekBar.
#define MSG_SELECTION_COLOR_SYSTEM 'scsy' ///< Use system selection color.
#define MSG_SELECTION_COLOR_MATCH 'scmt'  ///< Match selection to SeekBar color.
#define MSG_TOOLTIPS_ON 'tton'            ///< Enable tooltips.
#define MSG_TOOLTIPS_OFF 'ttof'           ///< Disable tooltips.
///@}

/** @name Playlist Management */
///@{
#define MSG_PLAYLIST_SELECTED 'plst'  ///< Playlist selected in sidebar.
#define MSG_NEW_PLAYLIST 'npls'       ///< Create new playlist request.
#define MSG_ADD_TO_PLAYLIST 'addp'    ///< Add selection to playlist.
#define MSG_LIST_PLAYLIST 'plLS'      ///< List playlists (internal).
#define MSG_DELETE_PLAYLIST 'dplt'    ///< Delete selected playlist.
#define MSG_RENAME_PLAYLIST 'rnpl'    ///< Rename selected playlist.
#define MSG_PLAYLIST_SELECTION 'selP' ///< Selection changed in playlist view.
#define MSG_PLAYLIST_CREATED 'npln'   ///< New playlist created.
#define MSG_SAVE_PLAYLIST_SELECTION 'spls' ///< Save playlist to file.
#define MSG_NEW_SMART_PLAYLIST 'nspl'  ///< Create smart/generative playlist.
#define MSG_GENERATE_PLAYLIST 'gnpl'   ///< Generate playlist from rules.
#define MSG_SET_PLAYLIST_FOLDER 'setp' ///< Set playlist storage folder.
#define MSG_PLAYLIST_FOLDER_SELECTED 'sfld' ///< Playlist folder picked.
#define MSG_NAME_PROMPT_RENAME 'nplr'       ///< Rename confirmed.
#define MSG_NAME_PROMPT_OK 'ok__'           ///< Name entry confirmed.
#define MSG_NAME_PROMPT_CANCEL 'cncl'       ///< Name entry cancelled.
#define MSG_REORDER_PLAYLIST 'rord'         ///< Reorder items in playlist.
#define MSG_PLAYLIST_ORDER_CHANGED 'plOC'   ///< Playlist sidebar order changed.
///@}

/** @name Metadata & MusicBrainz */
///@{
#define MSG_MB_SEARCH 'mbsr'          ///< Start MusicBrainz search.
#define MSG_MB_SEARCH_COMPLETE 'mbsc' ///< Search results ready.
#define MSG_MB_RESULTS 'mbrs'         ///< (Alias) Results ready.
#define MSG_MB_LIST_SEL 'mbls'        ///< Result list selection.
#define MSG_MB_APPLY 'mbap'           ///< Apply single track metadata.
#define MSG_MB_APPLY_ALBUM 'mbal'     ///< Apply full album metadata.
#define MSG_MB_CANCEL 'mbcl'          ///< Cancel metadata dialog.
///@}

/** @name Properties Window */
///@{
#define MSG_PROPERTIES 'prop'          ///< Open properties window.
#define MSG_PROP_SAVE 'prsv'           ///< Save changes to file.
#define MSG_PROP_APPLY 'prap'          ///< Apply changes (no close).
#define MSG_PROP_CANCEL 'prcl'         ///< Close properties.
#define MSG_PROP_SET_COVER_DATA 'pcvd' ///< Set cover image data.
#define MSG_PROP_REQUEST_COVER 'prcv'  ///< Request cover fetch.
///@}

/** @name Cover Art Handling */
///@{
#define MSG_COVER_LOAD 'cvld'              ///< Load cover from file/tag.
#define MSG_COVER_CLEAR 'cvcl'             ///< Clear current cover.
#define MSG_COVER_FETCH_URL 'cvul'         ///< Fetch cover from URL.
#define MSG_COVER_FETCH_MB 'cvmb'          ///< Fetch cover from MB.
#define MSG_COVER_APPLY_ALBUM 'cvba'       ///< Apply cover to whole album.
#define MSG_COVER_CLEAR_ALBUM 'cvca'       ///< Clear cover for album.
#define MSG_COVER_DROPPED_APPLY_ALL 'cvda' ///< dropped cover -> all files.
#define MSG_COVER_BITMAP_READY 'cvbr'      ///< Cover bitmap loaded & ready.
///@}

/** @name Matching Window */
///@{
#define MSG_MATCH_APPLY 'mapl'  ///< Apply match results.
#define MSG_MATCH_CANCEL 'mcnl' ///< Cancel matching.
#define MSG_MATCH_RESULT 'mtch' ///< Match result data.
#define MSG_MOVE_UP 'mvup'      ///< Move item up.
#define MSG_MOVE_DOWN 'mvdn'    ///< Move item down.
#define MSG_SMART_MATCH 'smrt'  ///< Trigger smart matching.
#define MSG_DRAG_ITEM 'drgI'    ///< Drag started.
///@}

/** @name Debug / Misc */
///@{
#define MSG_TEST_MODE 'tstM'       ///< Trigger test mode.
#define MSG_REGISTER_TARGET 'regt' ///< Register messaging target.
///@}

#endif // BETON_MESSAGES_H
