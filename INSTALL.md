# Installing Vitaleon

[Deutsch weiter unten](#installation-auf-deutsch)

## What you need

- An Akai MPC or Akai Force running in standalone mode with MPC OS 3.10.1 or later. Check **Preferences > Info** and update if it is older.
- The release ZIP from [dreyandersson.com/vitaleon](https://dreyandersson.com/vitaleon/), about 100 MB. Most of it is the factory wavetables and sounds.

Tested on an MPC Live II and an Akai Force. Other models are untested. On the Force the `VST2 Plugins` folder sits on its internal SSD, `/media/FORCE SSD/VST2 Plugins`.

## Steps

1. Unzip the download. It contains the folder `Andersson Audio - VST - Vitaleon`. Keep the folder name exactly as it is.
2. Put that folder inside a folder named exactly `VST2 Plugins` at the top level of a drive. Create `VST2 Plugins` if it does not exist. Any of these drives works:
   - **Internal storage (recommended).** Connect the MPC to your computer in computer mode and copy the folder over, or copy it from a stick with the MPC's own file browser.
   - A USB stick, SD card or the built-in SSD.
3. On the MPC open **Preferences > Enhancements (Beta)** and switch on **Enable Enhancements** and **External Plugin Support**.
4. Plug in the drive, or restart the MPC if the plugin does not appear.
5. Create a plugin track and choose **Vitaleon** under External Plugins.

The result looks like this:

```
<drive>/VST2 Plugins/Andersson Audio - VST - Vitaleon/plugin.json
<drive>/VST2 Plugins/Andersson Audio - VST - Vitaleon/VST/...
<drive>/VST2 Plugins/Andersson Audio - VST - Vitaleon/Plugin Skins/...
<drive>/VST2 Plugins/Andersson Audio - VST - Vitaleon/Content/...
```

## Your own sounds

- Load sounds from any drive with **HOME > LIBRARY** in the plugin.
- Or copy them into the plugin's `Content` folder: `Presets` for `.vital`, `Banks` for `.vitalbank` and ZIP, `Wavetables` for `.vitaltable` and WAV, `Samples` for WAV, `LFOs` for `.vitallfo`. They are read when the plugin is loaded.

## Factory sounds and wavetables

The package includes 32 factory wavetables and 32 factory sounds. They are free to use with Vitaleon and in your own music, but they may not be redistributed. See `FACTORY-CONTENT-LICENSE.txt`.

## Updating

Replace the folder with the new one. If you put your own files into its `Content` folder, copy them out first and back in afterwards.

## Removing

Delete the folder `Andersson Audio - VST - Vitaleon`.

## If it does not appear

- Is the folder directly inside `VST2 Plugins`, and is `VST2 Plugins` at the top level of the drive?
- Are both switches under Enhancements (Beta) on?
- Try the internal storage instead of a USB stick.
- Restart the MPC.

---

# Installation auf Deutsch

## Voraussetzungen

- Eine Akai MPC oder Akai Force im Standalone-Betrieb mit MPC OS 3.10.1 oder neuer. Unter **Preferences > Info** nachsehen und bei Bedarf aktualisieren.
- Das Release-ZIP von [dreyandersson.com/vitaleon](https://dreyandersson.com/vitaleon/), rund 100 MB. Den größten Teil machen die Werks-Wavetables und Werkssounds aus.

Getestet auf einer MPC Live II und einer Akai Force. Andere Modelle sind ungetestet. Auf der Force liegt der Ordner `VST2 Plugins` auf der internen SSD, `/media/FORCE SSD/VST2 Plugins`.

## Schritte

1. Das ZIP entpacken. Es enthält den Ordner `Andersson Audio - VST - Vitaleon`. Den Ordnernamen nicht ändern.
2. Diesen Ordner in einen Ordner mit dem exakten Namen `VST2 Plugins` legen, der auf der obersten Ebene eines Laufwerks liegt. `VST2 Plugins` anlegen, falls er fehlt. Möglich sind:
   - **Interner Speicher (empfohlen).** Die MPC im Computer-Modus an den Rechner anschließen und den Ordner hinüberkopieren, oder ihn mit dem Dateibrowser der MPC von einem Stick kopieren.
   - USB-Stick, SD-Karte oder die eingebaute SSD.
3. Auf der MPC **Preferences > Enhancements (Beta)** öffnen und **Enable Enhancements** sowie **External Plugin Support** einschalten.
4. Das Laufwerk anstecken oder die MPC neu starten, falls das Plugin nicht erscheint.
5. Eine Plugin-Spur anlegen und unter External Plugins **Vitaleon** wählen.

## Eigene Sounds

- Sounds von jedem Laufwerk im Plugin über **HOME > LIBRARY** laden.
- Oder in den Ordner `Content` des Plugins kopieren: `Presets` für `.vital`, `Banks` für `.vitalbank` und ZIP, `Wavetables` für `.vitaltable` und WAV, `Samples` für WAV, `LFOs` für `.vitallfo`. Sie werden beim Laden des Plugins eingelesen.

## Werkssounds und Werks-Wavetables

Das Paket enthält 32 Werks-Wavetables und 32 Werkssounds. Du darfst sie frei mit Vitaleon und in eigener Musik benutzen, aber nicht weitergeben. Siehe `FACTORY-CONTENT-LICENSE.txt`.

## Aktualisieren

Den Ordner durch den neuen ersetzen. Eigene Dateien aus dem Ordner `Content` vorher herauskopieren und danach wieder hineinlegen.

## Entfernen

Den Ordner `Andersson Audio - VST - Vitaleon` löschen.

## Wenn das Plugin nicht erscheint

- Liegt der Ordner direkt in `VST2 Plugins`, und liegt `VST2 Plugins` auf der obersten Ebene des Laufwerks?
- Sind beide Schalter unter Enhancements (Beta) eingeschaltet?
- Den internen Speicher statt eines USB-Sticks versuchen.
- Die MPC neu starten.
