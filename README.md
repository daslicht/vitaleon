# Vitaleon

A wavetable synthesizer plugin for Akai MPC and Force standalone, by Andersson Audio.

Vitaleon runs the sound engine of Matt Tytel's Vital 1.0.6 (GPLv3) inside the MPC's plugin format, with a touch interface built for the MPC screen. It is an independent project and is **not affiliated with or endorsed by Matt Tytel or Vital Audio**.

**Download:** the ready-to-install package, with the factory wavetables and sounds, is on [dreyandersson.com/vitaleon](https://dreyandersson.com/vitaleon/). This repository holds the source code.

- Three wavetable oscillators, a sampler, two voice filters, six envelopes, eight LFOs, four random sources, four macros
- A 64-route modulation matrix shown as a table: source, amount, target
- Nine effects: distortion, chorus, flanger, phaser, delay, reverb, compressor, EQ, filter
- Imports Vital presets (`.vital`), banks (`.vitalbank`, ZIP), wavetables (`.vitaltable`, WAV), LFO shapes (`.vitallfo`) and WAV samples
- The release package adds 32 factory wavetables and a bank of 32 factory sounds
- Live oscillator and LFO graphs, Q-Link mapping per page, host tempo sync
- Two skins: the MPC layout, and a wider layout for the Akai Force that the firmware picks automatically

## Status

Version 0.3.34. This is a young project.

| | |
|---|---|
| Tested on | MPC Live II with MPC OS 3.10.1 and an Akai Force, the 32-bit build |
| Not tested | Other MPC and Force models. The package also contains a 64-bit build that has not run on a device yet. |
| Requires | MPC OS 3.10.1 or later with **Enhancements (Beta)** and **External Plugin Support** switched on |

If you try it on another model, a report of what works is welcome.

## Install

Download the release ZIP from [dreyandersson.com/vitaleon](https://dreyandersson.com/vitaleon/), unzip it and copy the folder `Andersson Audio - VST - Vitaleon` into a folder named `VST2 Plugins` at the top level of a drive the MPC can read. The MPC's internal storage is the recommended place. Full steps in English and German: [INSTALL.md](INSTALL.md).

## First steps

1. Create a plugin track and choose **Vitaleon** under External Plugins.
2. The plugin starts with the first factory sound loaded. Pick another in the MPC's **PRESET** list: it holds the 32 factory sounds (BA bass, LD lead, PD pad, KY keys, PL pluck, SQ sequence, FX).
3. The footer switches between HOME, OSC, FILTER, ENV, LFO, FX and OTHER; on the Force, LFO and OTHER share the MOD tab. Each group has its pages across the top.
4. **HOME > LIBRARY** browses drives. OPEN enters a drive, folder or bank; LOAD loads a sound. The dice deals a random preset, never repeating one until every installed sound has played. **LEARN** binds the dice to a MIDI note (any channel): that note then deals the next card instead of sounding, and the assignment is saved with the project. The heart in front of OPEN/LOAD marks a preset as a favourite; **FAVS** switches the LIST to a view of the hearted presets only (the count reads in the status line); in that view the LIST works like the wavetable list: the page cell is a wheel, one click is one favourite, the highlighted row is where LOAD takes the sound from, and the page follows the cursor. **FAV** binds a second MIDI note that switches the favourite of the loaded sound. Favourites are stored in `Favourites.json` next to the plugin (key: the preset's path inside `Content/Presets`, or `<bank> / <entry>` for an archive), so they survive projects and reboots; a renamed preset file loses its heart.
5. **OTHER > MATRIX** holds the modulation routes. PICK on a row opens the target chooser.
6. Macros 1 to 4 are assigned in every factory sound. [FACTORY.md](FACTORY.md) lists all tables, sounds and macro names.

Save your edits with the MPC project. The plugin state, including imported tables and samples, is stored in the project.

## Performance

The MPC has a small processor for a synth of this size. Vitaleon is set up around that:

- A sound has at most eight voices. Imported sounds start at 1x oversampling, which can be raised under **HOME > VOICES**.
- The effect chain runs on a second processor core. This adds 128 samples of latency, which is reported to the MPC.
- Effects whose mix sits at zero behind a macro are skipped until the macro opens them.
- If the audio load stays too high, the plugin lowers the voice limit in pairs and raises it again when the load falls.

On the test device, at buffer size 128, one held note of a heavy third-party preset used about 30 % of the audio block time. Dense sounds with many unison voices and long chords can still overload the device.

## What it does not do

- Features added to Vital after 1.0.6. Newer spectral modes are switched off on import, and the import tells you so. Details in [COMPATIBILITY.md](COMPATIBILITY.md).
- The original desktop interface, the full wavetable editor, text-to-wavetable, custom tunings.
- FLAC or MP3 files. Samples and wavetables must be WAV.
- Saving sounds back to `.vital` files. Sounds are saved with the MPC project.

## Factory content

The release package contains 32 factory wavetables and a bank of 32 factory sounds, created by Drey Andersson. The wavetables are synthesised; no samples or ROM data are used. Each sound is built on one of the tables. [FACTORY.md](FACTORY.md) lists them.

The factory content is **not part of this repository and not under the GPL**. You may use it with Vitaleon and in your own music; you may not redistribute it or ship it with another product. If you make your own port or build, please create your own wavetables and sounds. Terms: [FACTORY-CONTENT-LICENSE.txt](FACTORY-CONTENT-LICENSE.txt).

## Build from source

Vitaleon builds with Akai's MPC Plugin Builder SDK plus a small patch. See [BUILDING.md](BUILDING.md).

## Licence

The Vitaleon program, meaning everything in this repository, is free software under the **GNU General Public License, version 3 or later**. See [LICENSE](LICENSE) and [THIRD-PARTY.txt](THIRD-PARTY.txt).

The factory wavetables and factory sounds in the release package are a separate work with their own licence, [FACTORY-CONTENT-LICENSE.txt](FACTORY-CONTENT-LICENSE.txt). All rights in them are reserved.

The source of the sound engine is Vital by Matt Tytel, https://github.com/mtytel/vital. "Vital" is a trademark of its owner. This project uses the name only to say where the engine comes from.

History of changes: [CHANGELOG.md](CHANGELOG.md).
