# Bugs found in the current implementation

- When the plugin is loaded, original shortcuts do not work - fixed
- Something still not working with cross-playlist copy and paste - fixed
- The spatial navigation does not work - fixed
- no way to disambiguate p and shift+p - fixed
- single char shortcuts not appearing on the shortcuts page - fixed
- search index in relation to current position - fixed
- visual mode UseConfigBindings=true does not go to normal mode - fixed
- bindings are not reloaded on config change
- doing dd and undo changes line focus
- focus on playlist organizer and doing dd deletes from current playlist
- when a track is playing, pasting above the playing track changes the selection to the currently playing track. This is a bug in both legacy and UseConfigBindings=true modes - fixed
