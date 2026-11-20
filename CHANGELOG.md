# Changelog

Device results refer to an MPC Live II with MPC OS 3.10.1.

## 0.3.30

- The Force skin draws no Q-Link bank frames. The Force has eight Q-Links in two banks, so its knobs address the eight visible slots of a page directly; a four-slot outline (matrix included) suggested a grouping the hardware does not have. MPCs keep their frames.

## 0.3.29

- The modulation matrix uses the Force width: source, amount and target columns and the PICK/BIP/STER/MUTE buttons spread to x = 1237.
- The matrix Q-Link frame surrounds the whole four-row block of the active bank. Around the amount column only it read like a stray box under the AMOUNT caption.

## 0.3.28

- The Force layout is designed at Force size instead of being a stretched MPC skin: three 394 px columns at x 24, 442 and 860, its own knob (120 px), fader (372 x 64), switch (168 x 88) and segment button (186 x 56) artwork behind the `-f` asset names, 26 px slot captions, and graphs, envelope, library, browser panel and footer buttons widened to match.
- Both skins pass the same six checks; `surface_states.py` and `check_surface.py` audit either file through `VITALEON_SKIN` and `VITALEON_SAFE_RIGHT`.

## 0.3.27

- The Force gets its own skin, `Plugin Skins/TUI - Force.json`. The Force has no Q-Link sidebar over the right edge, so the MPC layout ends at x = 1000 and leaves a black strip; the Force layout scales every column, graph and frame out to x = 1256. The firmware prefers the product-specific file, so MPCs keep the untouched `TUI.json`.

## 0.3.26

- Controls take the hardware focus again. Every page was wrapped in a container marked `acceptsHWFocus: "No"`, and the MPC gives nothing inside such a container any focus, so neither a touch nor the data wheel reached a control. The wrapper is `"Children"` now, which the SDK reference (7.3) describes for a panel that shows and hides its contents. This was wrong since the pages got their own wrapper, not only since 0.3.25.
- The oscillator sub-tabs (SOUND, PHASE, UNISON, ...) can be tapped again. The graph container that 0.3.25 added covered the whole page and was drawn above them; it is drawn below them now.
- No preset or bank file ships with the plugin: `Content/` holds the README files only. Replace the plugin folder instead of deleting it, or copy your own `Content/` back afterwards.

## 0.3.25

- Every control answers the data wheel. A slot takes the hardware focus itself and its knob, fader or buttons do not, which is the pattern the SDK's own generator uses. Before, a value pair, a switch or a route amount could not be focused at all.
- Slots are 308 by 156 px (was 296), in three columns at x 24, 356 and 688; the last one ends at 996, clear of the sidebar that starts at 1025. A fader is 300 by 56 px and a segment button 148 by 52 so that they fit this width.
- A page holds at most six controls per section. Sections that held more continue with a second one (PLAY 2, EXPRESSION 2, ...) instead of squeezing a seventh control in.
- The oscillator wave frame appears once, next to the oscillator graph on SOUND, and no longer sits in the OSC pages.
- `tools/check_touch_targets.py` fails when a slot does not take the hardware focus, or when a knob, fader or button inside a slot does. The slot pattern of 0.3.23 gives 619 findings, this skin gives none.

Not yet tried on a device.

## 0.3.24

- The voice control ends at eight voices (was 32).

## 0.3.23

- Controls respond directly again. While a control moves, the plugin no longer calls into the MPC; 0.3.22 did so on every movement and the controls felt sluggish on the device.
- Small Q-Link turns on a control with fixed steps (voices, lists) add up instead of being lost.

## 0.3.22

- Knobs and faders follow the Q-Links smoothly. They are now bound to the Q-Link slots themselves, so the MPC moves the control that its knob writes.
- A frame marks the four controls that the Q-Links currently reach. It follows the knob group turned last.
- Removed the display refresh of 0.3.21: on the device it made every control stutter.

## 0.3.21

- Not released. It refreshed the whole display after every Q-Link move, which made the controls stutter on the device.

## 0.3.20

- The PRESET list holds the installed sounds and nothing else. The eight basic built-in sounds are gone.
- A new instance starts with the first sound of the list loaded.
- A plugin without any installed sound (a build from source) has one sound named Init.
- A sound with more voices than the one before gets all its voices at once. Before, the voice limit rose over several seconds.

## 0.3.19

- Library page reworked. Removable drives are listed right after Plugin Content. Rows show kind, name and one button: OPEN for a drive, folder or bank, LOAD for a sound.
- A bank opens like a folder and lists its sounds with LOAD. UP returns to the bank's folder.
- Library buttons no longer stay lit after a tap.

## 0.3.18

- Fixed audio dropouts while a live graph was moving. Measured on the device: announcing each graph value to the host delayed the MPC's audio callback (240 late callbacks in 10 s). The plugin now asks for one display update per picture; with that, no late callback was measured at up to 30 pictures a second.
- Finer graphs: oscillator wave 128 columns by 108 levels, LFO and modulation curve 192 by 96.

## 0.3.17

- Modulation matrix as a table: source, amount, target, with BIPOLAR, STEREO and MUTE per row. PICK opens a target chooser, first the group, then the entry.
- 32 factory wavetables, 256 frames of 2048 samples each.
- Factory bank with 32 sounds, each built on one of the factory wavetables.
- Table, sample and LFO-shape selectors can be set by touch.

## 0.3.16

- The effect chain runs on a second processor core. Adds 128 samples of latency, reported to the host.

## 0.3.15

- Effects whose mix is zero behind a macro are not processed.
- CPU guard: under sustained overload the voice limit steps down in pairs and recovers afterwards.

## 0.3.10 to 0.3.13

- Choice fields are faders with a value overlay. The SDK's menu overlay shows an empty list on MPC OS 3.10.1 and is not used.
- Live graphs follow the playing sound. The LFO playhead rests when the LFO is not routed.

## 0.3.4 to 0.3.7

- Tolerant import: settings this engine does not know are skipped and reported instead of rejecting the sound.
- Single `.vital` files can be loaded from the library.
- Imported sounds start at 1x oversampling and at most eight voices.
- The engine sleeps when silent.
- Duplicate controls removed; oscillator, envelope and LFO pages with larger graphs.

## 0.3.2 and 0.3.3

- Section buttons inside a page switch their content.

## 0.3.1 and earlier

- First port of the engine, the importer and the touch surface.
