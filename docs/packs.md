# Pack format

Place each UTF-8 JSON manifest in `SKSE/Plugins/SkyEmote/Packs/`.
The format is defined by [pack.schema.json](pack.schema.json). The bundled
[vanilla manifest](../assets/SKSE/Plugins/SkyEmote/Packs/vanilla.json) is an example.

`schema` selects the format version. `id` is a stable, lowercase identifier;
`name` is the displayed label. Category IDs are unique within a pack, section
IDs within their category, and emote IDs across the entire pack. Pack IDs are
unique across installed manifests. Preserve IDs when renaming or moving emotes:
the pair `(pack ID, emote ID)` identifies a favorite independently of its labels.

Array order defines the display order of categories, sections and emotes.
Each emote names one animation graph `event`, preserving its exact case.
`mode: "once"` describes a one-shot animation. Native one-shots finish when the
graph reports `IdleStop`. Optional `duration_seconds` supplies a positive finite
fallback timeout, which stops the animation if no completion arrives. Omit this
field to rely on graph completion and manual/gameplay cancellation.

`mode: "loop"` normally starts an animation that repeats within the graph.
Its event is sent once. For a native one-shot that should repeat, add a positive
finite `repeat_interval_seconds`. This resends the event at that interval without
resetting the graph between repetitions. Pausing the game freezes playback clocks.
Set the interval to the clip's actual duration to avoid cutting off its motion.

`duration_seconds` is valid only for `once`. `repeat_interval_seconds` is valid
only for `loop`. Omit unused timing fields instead of setting them to zero.
Manifests contain event names, not scripts or animation-file paths.

Some native animations require Skyrim's idle-form entry path. Optional
`idle_form: "Skyrim.esm|000F64D8"` selects an IDLE record for the main action.
Use the plugin filename and a nonzero local FormID of six or eight hexadecimal
digits, without a load-order prefix. The ID must fit in 24 bits. The plugin must
be loaded and its IDLE record's event must match `event`. Missing records,
wrong form types, mismatched events and rejected starts produce playback errors.
Setup steps still send their graph events directly. Started native forms also
use Skyrim's standalone idle-form exit during Stop or unrelated replacement.

Unknown fields and unsupported schema versions are invalid. A valid event name
does not establish that the animation graph contains it.

Nesting follows the schema: pack object → categories array → category object →
sections array → section object → emotes array → emote object → setup array →
step object. Step fields are scalar values. Deeper nesting is invalid.
Duplicate JSON keys are invalid, as are duplicate IDs in their respective
scopes. Invalid manifests produce diagnostics identifying their filename,
location where available, and reason.

Only regular `.json` files in the pack directory are loaded; discovery is not
recursive and the extension is case-insensitive. Packs are loaded in filename order. The browser places Vanilla before custom
packs. If multiple valid manifests use the
same pack ID, all conflicting manifests are excluded. Other valid packs remain
available when a file cannot be read or fails validation.

## Held states and setup

A loop can declare a visible `state`, such as `Reading`. Labels are case-sensitive
and scoped to their pack. `entry_seconds` delays committing that state until the
main event's entry finishes. Omit it when the event enters its held state immediately.
`state_only: true` makes selecting an already achieved state a no-op.

An action can declare `requires_state` and a `setup` array. Each step has an
`event`, positive finite `seconds`, and optional `state_after`. Setup steps run
only as needed to reach the required state. Their order and state labels matter.
When setup is provided, its last step must reach `requires_state` for an action
retaining a held state. Omitting setup makes the action apply only to its already
held or entering required state. Otherwise it does nothing. Selecting another action
while its setup is running preserves the accepted step and elapsed entry time.

A one-shot action with `requires_state` and no target `state` ends that state. It may omit
setup. Ending an inactive state does nothing. Ending an achieved or entering state
plays its exit event without resetting to standing first. Stop remains the
immediate way to clear all playback, including setup and held states.

For example, a page-turn action can require `Reading`, set up `IdleBook_Read`
for its entry duration with `state_after: "Reading"`, then send its page-turn event
while retaining `state: "Reading"`. A read action uses the same state label with
`state_only: true`. Use the actual event names and entry durations for your graph.

Stateful events rejected during a transition are retried every quarter second
for up to four seconds. Entry clocks start only after acceptance. If a follow-up
still fails after reaching a parent state, that state is kept and the action
reports an error. A gesture's `IdleStop` does not discard its declared parent
state. An exit clears the held state when accepted, then completes naturally.

## Stable aliases

Keep IDs stable across updates. If an ID must change, optional root `aliases`
can map an old emote ID to a current one, for example
`"aliases": {"old-wave": "wave"}`. Aliases cannot shadow current emotes or
point to other aliases. Favorites resolve aliases without changing their saved keys.
A missing pack or removed emote does not delete its favorite.

## Variants

An emote can set `variant_of` to a primary emote ID in its own section. It keeps
its own name, event, playback settings and favorite ID. The menu presents the
family as one row with a picker. Chains, self-links and cross-section links are
invalid. Array order defines picker order. Include a parenthesized variant label
in each family member's name, including the primary. Without one the UI adds
`(Default)` or `(Variant N)` when a dropdown is present. The built-in `vanilla` pack always
displays separate rows, including dances and applause.

`allow_movement: true` permits an animation's authored walking motion without
cancelling it on graph speed. The default is false. Gameplay movement buttons,
jumping and other interruption actions still cancel it. This field does not
enable player controls, teleport the actor or bypass other playback guards.

## Installation diagnostics

Optional root `required_files` lists paths relative to Skyrim's Data directory,
using forward slashes. Paths cannot be absolute, contain empty/`.`/`..`
components, backslashes, Windows reserved punctuation or trailing dots/spaces.
Files are checked at startup and may be deployed through mod-manager symlinks.

A generated behavior pack can declare `"graph": {"variable": "MyPackRevision",
"minimum": 1}`. Author an integer variable with this initial revision into the
third-person graph. SkyEmote inspects the loaded template, not a mutable live
value. The minimum is a nonnegative 32-bit integer. Missing, older or unreadable
markers produce installation warnings. They never disable local playback.
A current marker identifies the generated graph revision, not every clip's
correctness. Event acceptance remains the animation graph's decision.
Vanilla needs no generated behaviors or revision marker.

## Stop buttons

A section can include `{"id": "stop-pose", "name": "Stop pose", "action": "stop"}`.
This entry uses the global stop command, including cancelling pending setup.
It has no event, mode or other playback/variant fields and cannot be a variant
parent. It can be favorited like other actions.

`hidden: true` retains an emote for stable ID/favorite lookup while omitting it
from the browser and variant picker. Its default is false. A visible variant requires a visible primary. A hidden variant
can still supply the matching exit for an active held pose. On a family of
state exits, the browser selects the variant whose `requires_state` matches
the active state in the same pack. Favorites keep their explicit saved variant.
