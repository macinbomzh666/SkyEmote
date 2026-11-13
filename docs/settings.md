# Settings

Edit `Data/SKSE/Plugins/SkyEmote.ini` before starting Skyrim:

```ini
[Input]
MenuKey=26
StopKey=0
QuickEmoteKey=0
QuickEmoteTimeoutSeconds=5
MouseSensitivity=1.0
```

`MenuKey` is a decimal DirectInput keyboard scan code. The default `26` is the
physical `[` key on a US keyboard. `27` is `]`. The menu key cannot be unbound.
Zero, Escape (`1`) and other invalid values use `26`.
Restart Skyrim after editing the file manually.

`StopKey` uses the same scan codes. Its default `0` is unbound. A bound key runs
Stop animation during gameplay or free camera, while the menu and Quick Emote
are inactive. Inside the menu, use the Stop animation button. Typing,
pause/loading screens, other menu input contexts and loss of focus suppress the
shortcut. Open menu, Stop and Quick Emote must use distinct keys.
Invalid/conflicting INI values leave optional bindings unbound.

`QuickEmoteKey` uses the same scan codes and defaults to `0` (unbound).
`QuickEmoteTimeoutSeconds` sets how long Quick Emote waits for a favorite.
It defaults to five seconds and is configured only in the INI. Missing,
nonfinite or nonpositive durations use that default.

`MouseSensitivity=1.0` uses Skyrim's native menu-cursor handler. Cursor positions
are converted from the game's cursor dimensions to the render buffer dimensions,
so there is no separate resolution multiplier on that path. Larger values speed
up the menu cursor, smaller values slow it down. This does not change Skyrim's
camera sensitivity or preferences.

The slider reads `fMouseHeadingSensitivityMin:Controls` and
`fMouseHeadingSensitivityMax:Controls` from Skyrim and divides them by the
player's current `fMouseHeadingSensitivity:Controls`. Thus `1.0` is the current
native setting and the endpoints express Skyrim's look-sensitivity range as
multipliers. The lower endpoint is extended to half that native minimum and
the upper endpoint to three times that native maximum. Limits refresh whenever
the menu opens. An INI sensitivity outside those limits resets to `1.0` and
saves that default. If limits are unavailable, the slider is disabled. Edit the INI only before starting Skyrim.

The **Settings** tab changes input controls while Skyrim is running. Drag the
sensitivity slider to apply it immediately. Click the menu key button, then
press a replacement keyboard key. During rebinding, Escape restores `[` and
keeps the menu open. Switching tabs or closing the menu cancels rebinding.
The Stop animation and Quick Emote key buttons display `...` until bound.
Click a button and press a key, or press Escape to unbind. Right-click also
unbinds. Assigning another global binding's key is rejected while capture stays
active. Restoring `[` for Open menu clears any conflicting optional binding.
Reset input settings restores `[`, unbound Stop/Quick Emote and sensitivity
`1.0`. It preserves the INI-only timeout.

Changes save automatically to the INI when a slider edit ends, a key is rebound,
or the menu closes. Other INI settings are preserved. If writing fails, changes
still apply for the current game session. The footer reports the failure and
`SkyEmote.log` records the file path and operating-system error codes.

Press the configured key to open or close the menu. Escape also closes it.
While editing search, the configured key types normally. Use Escape to close.
Open it during gameplay with a living, loaded player. It closes when loading,
opening another application menu, losing focus, dying or losing the renderer.
The menu consumes gameplay input while open and uses a software cursor.

Quick Emote enables favorite shortcuts without opening the menu or showing a
cursor. Press its activation key, then a favorite key. Player and camera
controls stay blocked until a favorite is selected, Escape is pressed or the
timeout expires. Other keys do not extend the timeout. Escape/timeout release
controls without stopping an existing emote. Losing focus or leaving gameplay
also releases capture. The menu's last tab and search are preserved.

Quick Emote also releases capture when another input context covers or replaces
its cursor context, or another plugin enables text entry. If a context covers
ours, cursor-context cleanup waits until it leaves. Opening SkyEmote or arming
Quick Emote waits for that cleanup. An overlay that leaves the context stack and
text-entry count unchanged cannot be detected by these checks.

Selecting an emote ends the previous animation and waits for weapons to finish
sheathing before starting. An in-progress draw finishes first, then sheathing is
requested once. The wait is asynchronous and cancels with an error if the weapon
transition does not finish within four seconds. Stop cancels a pending selection
without forcing Skyrim out of its weapon transition. Closing the menu
keeps the animation playing. Moving, jumping, attacking, drawing weapons or
activating something cancels it. Being in combat alone does not block emotes.

The state row identifies a held pose and whether it is transitioning. Related
buttons reuse that state. You can select Turn one page, Raise a glass or Float
idle directly from standing and their setup will run first. Repeatedly selecting
Read book, Hold drink or an already achieved Rise does not restart its entry.
The Nocturnal (buggy) section remains available, but cancelling these native
animations can leave movement locked. Floating-specific workarounds have been
removed. Its actions use the same state/setup and Stop behavior as other emotes.
Reading and drinking retain their tested state handling. Addon state labels in
the menu resolve to emote names while internal state IDs remain unchanged.

## Favorites

Use `+` to save an emote and `-` to remove it. The Favorites tab lists saved
emotes with their pack and category. Uninstalled entries remain removable.

Each favorite has a binding button at its right edge. It shows `...` when
unbound, or the assigned key when bound. Click it and a tooltip prompts you to
press a keyboard key. The prompt stays visible while rebinding. Press Escape
to clear the binding. Backspace also clears it. Right-click clears it directly.
Assigning an occupied key moves that key from its previous favorite. The Open menu key is reserved. Settings warns if it shadows a favorite.
Stop or Quick Emote can share a favorite's key because they work in different
contexts. To use the activation key as a favorite, press it again after arming
Quick Emote.

With the menu open, pressing a bound key plays its saved emote and closes the
menu. For example, bind a dance to `1`, then use `[` followed by `1`. Shortcuts
work across catalogue tabs, but stay inactive during search typing or key
rebinding. With the menu closed, favorite shortcuts work only while Quick Emote
is armed. An unavailable favorite keeps its binding. Invoking it reports an
error in the menu, or ends Quick Emote and logs a warning. Removing a favorite
also removes its binding. Switching away from Favorites cancels an unfinished
binding.

Favorites and their hotkeys are stored in
`Data/SKSE/Plugins/SkyEmote.favorites.json`, beside `SkyEmote.ini`. Back up this
file before reinstalling the mod or transferring favorites. If this file is
absent, the plugin moves a valid previous `SkyEmote/favorites.json` from the
SKSE log directory. The old file is removed after a successful migration. An
existing valid new file takes precedence, and any leftover old file is deleted.
If neither exists, an empty default file is created in the new location.
A damaged file or failed migration is reported and never overwritten by
subsequent favorite edits. Valid favorites and their shortcuts remain available
if copying or legacy cleanup fails, but editing stays disabled. Repair the
reported file or migration path and restart Skyrim to enable saving again.
Legacy EmoteMenu favorites are untouched.

The open menu owns a cursor input context so gameplay hotkeys from plugins
that respect Skyrim input contexts do not activate while searching.

## Browsing

Choose a pack tab and category, or Favorites. Search stays visible below the
state row, above the tabs, while scrolling. It matches emote, pack, category
and section names. It does not filter Settings. `L` marks a looping action. The arrow beside an
emote opens its variants and plays the selected variant. Every dropdown choice,
including the default, has a parenthesized label. Vanilla actions have
separate rows. An empty arrow slot keeps buttons aligned.

The menu starts at 85% of screen width. Pack and category tabs stay visible while
only the emote list scrolls. Each page retains its own scroll position.
Wide windows stack whole sections in the shorter of two columns, preserving
section order within each column. A single section uses the full width.
Favorites use two columns and include pack/category names.
Hover a button for its full name and looping/one-shot classification.
A separator marks the one-line footer for status and error/warning counts.
Full diagnostics are written to `SkyEmote.log` alongside the other SKSE logs.

Installation warnings are yellow and separate from malformed-pack errors.
They report files missing at startup or a missing/older/unreadable loaded graph
revision. Reinstall/deploy the pack, regenerate its behaviors if required and
restart Skyrim. Warnings never block trying a local animation. A revision
marker is diagnostic metadata, not proof that every animation works. The bundled
vanilla catalogue does not require Pandora generation.

Use the global Stop button to end floating.

Tabs are ordered Settings, Favorites, Vanilla, then custom packs in filename
order. The first opening selects Favorites. Later openings return to the last
selected tab and category during the current game session.
The menu uses Sovngarde Light with a dark palette and plain rectangular borders.
If the font file is unavailable, it uses ImGui's embedded font.

Harkon groups the four Walk and explain gestures under World. Stand proud enters
its held stance directly, skipping the turning/backward furniture-entry motion.
