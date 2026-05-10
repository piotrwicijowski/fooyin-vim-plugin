# Future improvements

- navigation of tree structures: l/h to open/collapse, j/k should move within open tree (and not jump to the next sibling), have a configurable shortcut to jump to sibling - done
- undo redo - done
- make the playlist view the first that gets focus - done
- dot for repeat
- slash search - done
- o and g; to move to currently playing song - done
- save per-playlist cursor position when switching between playlists (fooyin by default does not have that, but the plugin could store it)
- rework mapping logic into configuration-driven - done
- allow for spatial navigation when in visual mode - the selection should be retained, on the new widget the mode should be normal - done
- search without wraparound
- visual mode exit without removing selection
- shift+j/shift+k for extending selection in normal mode
- shortcuts for "next playlist/previous playlist"
- marks. M - global marks (regardless of playlist), m - playlist local mark. Any chance to add mark column?
- F2 (rename) - detect if we have text input and don't capture normal mode bindings
- Move/copy this song right after the currently playing without moving focus there
- Allow binding any fooyin action without needing to explicitly declare a function for it in vim plugin
- Mode indicator

