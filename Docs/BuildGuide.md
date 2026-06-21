# Detective 47's Dust 1200 — Build Guide

## First time only

Make sure these are installed (you already did this):
- Xcode (from App Store)
- CMake (from cmake.org — already in PATH)

---

## Build

Open Terminal and run:

```bash
cd ~/SoundDetectivePlugins
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j 4
```

First build downloads JUCE (~5 min). Subsequent builds are ~30 seconds.

---

## Install — VST3 (Cubase, Ableton, Reaper, Bitwig, FL Studio)

```bash
cp -R ~/SoundDetectivePlugins/build/Dust1200_artefacts/Release/VST3/"Detective 47s Dust 1200.vst3" \
      /Library/Audio/Plug-Ins/VST3/
```

Then rescan in your DAW.

---

## Install — AU (Logic Pro, GarageBand)

```bash
cp -R ~/SoundDetectivePlugins/build/Dust1200_artefacts/Release/AU/"Detective 47s Dust 1200.component" \
      /Library/Audio/Plug-Ins/Components/
```

Then restart Logic.

---

## Add your logo

1. Place your logo PNG at:
   `~/SoundDetectivePlugins/Plugins/Dust1200/Assets/logo_detective47_dust1200.png`

2. Rebuild:
   ```bash
   cmake --build build --config Release -j 4
   ```

3. Reinstall the VST3 using the install command above.

The logo will appear in the top-left of the plugin automatically.

---

## Cubase VST3 path troubleshooting

If plugin doesn't appear after rescan, try the system folder:
```bash
sudo cp -R ~/SoundDetectivePlugins/build/Dust1200_artefacts/Release/VST3/"Detective 47s Dust 1200.vst3" \
           /Library/Audio/Plug-Ins/VST3/
```

---

## Before selling

1. Get Apple Developer account ($99/yr)
2. Code-sign: `codesign --deep -s "Developer ID Application: Your Name" "Detective 47s Dust 1200.vst3"`
3. Notarize: `xcrun notarytool submit ...`
4. Register VST3 license at developer.steinberg.net (free)
5. Change COMPANY_NAME in CMakeLists.txt to your legal entity name
