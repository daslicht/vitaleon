**Vital ↔ Vitaleon: Patch-Austausch und Echtzeit-Editor. Plan, nicht implementiert.**

Ziel: Patches in beide Richtungen zwischen Desktop-Vital und Vitaleon auf dem MPC; danach optional Echtzeit-Editieren am Desktop bei Performance auf dem MPC. **Kein SSH, kein MIDI als Transport.**

## Ausgangslage (im Code geprüft)

| Baustein | Befund |
|---|---|
| `.vital`-Import in Vitaleon | vorhanden und vollständig: `src/FullAssets.inc`, jeder numerische Key gegen die Control-Tabelle geprüft, min/max-validiert |
| `.vital`-Export aus Vitaleon | **fehlt vollständig** — `synth_version` wird nur gelesen (`FullAssets.inc:118`), nie geschrieben |
| Netzwerk/IPC im Plugin | **keine einzige Zeile** (`socket`, `sendto`, `mkfifo`, `pipe`: 0 Treffer) |
| Threads | `service_` (Service-Loop, Park-Gate) und `fxThread_` — der Service-Thread ist der einzige zulässige Ort für I/O |
| Kommando-Queue | `requestService()` → `serviceChanges()` unter `lifecycleMutex_`/`parkEngine()`; Preset-Laden hat genau einen Pfad: `pendingPreset_` |
| Parameter-Namen | 772 Controls, namensgleich mit Vital → Remote-Mapping ist reine Namensauflösung |
| UI-Rückkanal | `publishNormalised()/publish()` schreibt Werte ohne `parameterChanged` in die MPC-Oberfläche |
| Export-Bausteine | `WavetableCreator::stateToJson`, `LineGenerator::stateToJson`, alle Wavetable-Modifier vorhanden |
| Lücke | `Sample::stateToJson/jsonToState` wurde beim Port entfernt; Original liegt in `References/vital/src/synthesis/producers/sample_source.cpp:307/324` |
| Messlast | eigener Kommentar im Code: ~780 `audioMasterAutomate`/s → 240 verspätete Audiocallbacks. Remote-Streaming muss gedrosselt werden |

## Stufe 0 — Machbarkeits-Probe auf dem Gerät (Entscheidungsgate)

Kleines Zusatz-Target im bestehenden `VITALEON_TEST`-Muster: UDP-Socket binden, Paket senden/empfangen, Ergebnis in eine Logdatei. Start auf dem MPC Live II.

Beantwortet: (a) erlaubt MPC OS 3.10.1 einer Plugin-`.so` `socket()`/`bind()`/UDP (SELinux/Sandbox)? (b) gibt es ein nutzbares Interface (WLAN/USB-Ethernet) mit LAN-Route zum Desktop? (c) Latenz/Jitter für Regler-Daten?

Ergebnis: eine Seite Messprotokoll (`docs/`-frei, als Abschnitt in `REMOTE-BRIDGE.md` nachgetragen). **Negativ ⇒ Stufe 2/3 entfallen, Stufe 1 bleibt der Weg.** Kein verlorener Aufwand.

## Stufe 1 — `.vital`-Export (Datei-Round-Trip, kein Netz)

1. `Sample::stateToJson/jsonToState` aus der Referenz wieder aufnehmen (Base64-int16-PCM) und an den eigenen Asset-Serialisierer anschließen.
2. Neuer Preset-Writer: `settings` aus den 772 Controls, `modulations` (max. 64 Routen mit Quelle/Ziel/`line_mapping`), 3 `wavetables` via `WavetableCreator::stateToJson`, 8 LFO-Kurven via `LineGenerator::stateToJson`, Sample, `synth_version`.
3. Stack-/Spectral-IDs so schreiben, wie der Importer sie liest (5→11, 6..11→−1; Spectral-Mode >11 → 0), damit der Rundlauf stabil ist.
4. Rundlauf-Test: import → export → re-import, Zustand identisch (Fixture-Suite unter `tests/`). Abnahme: echtes Vital auf dem Desktop lädt das exportierte File.
5. UI: eine Aktion im LIBRARY-Bereich („EXPORT"), Ausführung im Service-Thread hinter dem Park-Gate, kein Audio-Thread-Zugriff.

**Entscheidung zu offener Frage 3:** `synth_version` wird als `"1.0.6"` geschrieben — das ist die Baseline, gegen die der Importer gebaut ist, und damit maximal kompatibel mit echten Vital-Installationen. Folge, bewusst akzeptiert: das exportierte Preset enthält keine Features neuerer Vital-Versionen; Vitaleon-eigene Erweiterungen (`kPointCycle`, `kTripleOctave`, `kSubharmonicSeries`) werden beim Export weggelassen bzw. auf den Vital-äquivalenten Wert abgebildet, statt inkompatible IDs zu schreiben.

## Stufe 2 — Parameter-Bridge im LAN (nur bei positivem Stufe 0)

- Protokoll: kompaktes UDP, OSC-kompatibel. Nachrichten: `set <control-name> <value>`, `load <pfad>`, `dump` (voller Zustand), `subscribe` (MPC → Desktop).
- Server im bestehenden `serviceLoop()`: Pakete einsammeln, in die vorhandene Queue einspeisen, Werte über `publish()` setzen, Presets über `pendingPreset_` laden. Bündelung/Drosselung nach dem Muster der 300-ms-Slot-Rest-Logik.
- **Besitz-Regel** gegen Überschreiben: genau ein Master pro Zeitfenster („Remote übernimmt" / „MPC übernimmt"), analog Touch-Mode im DAW. Ohne diese Regel bekämpfen sich Desktop und MPC.
- Assets laufen **nicht** über den Parameter-Strom, sondern über Stufe 1 (Datei) — sonst wird die Verbindung unzuverlässig.

## Stufe 3 — Desktop-Gegenseite

- **3a (klein):** schlanker Companion (VST/CLI), öffnet `.vital`, mappt Regler auf die Bridge, schreibt Änderungen zurück. Schnell, aber nicht „Vital".
- **3b (Vision):** eigener Vital-Build aus `References/vital` (GPL-Quellen liegen vor) mit „Remote"-Modus: Vital-UI bleibt der Editor, Controls schreiben in die Bridge statt in die lokale Engine, Assets per Datei. Setzt eine stabile Bridge aus Stufe 2 voraus und ist ein eigenes Teilprojekt.

## Nicht Teil des Plans

SSH/sshd auf dem MPC · MIDI-Übertragung (SysEx/CC) · Änderungen am DSP-Kern · Änderungen an den 1979 Parameter-IDs.

## Offene Punkte

1. Sockets auf dem Gerät erlaubt? → Stufe 0.
2. Netzwerk am Aufführungsort verfügbar/erwünscht, oder ist USB-Stick der realistische Weg? → entscheidet, ob Stufe 2/3 überhaupt Sinn haben.
3. `synth_version` = `"1.0.6"` — im Plan beantwortet (ja), Korrektur jederzeit möglich.
