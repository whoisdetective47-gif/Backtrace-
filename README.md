# Sound Detective 47 — Plugin Suite (Backtrace + Dust 12.47)

JUCE 8 / C++ audio plugins. This repo is a small monorepo: the active project is
**Backtrace**, with the sibling **Dust 12.47** and shared DSP alongside it.

> **Backtrace** is a DAW-synced **preverb & transition designer** with two modes:
> - **Live Preverb (Mode 2)** — insert on any track; while the DAW plays it generates a
>   real-time reverse-reverb swell that leads INTO the incoming audio. Uses Swell Length as
>   the Pre-Swell Length and reports that as latency (a mix effect, not zero-latency).
>   Toggle **LIVE PREVERB** at the top of the FX column. *(Phase-1 prototype.)*
> - **Capture / Print (Mode 1)** — drop in a word / hit / phrase, generate an FX tail
>   (reverb/delay), reverse it, shape it, and print/export/drag the result. The precision
>   mode — the classic manual Cubase reverse-reverb move, as a designed tool.

---

## Repo layout

| Path | What |
|---|---|
| `Plugins/Backtrace/` | The Backtrace plugin (Source/, Assets/) — **the active work** |
| `Plugins/Dust1200/` | Dust 12.47 (sibling plugin) |
| `Common/` | Shared DSP (e.g. `Common/DSP/StereoWidener.h`) used by both |
| `Docs/Backtrace_DesignSpec.md` | **Full phase-by-phase history (Phases 1–32).** Read this first. |
| `CMakeLists.txt` | Top-level build (adds both plugin targets, fetches JUCE) |
| `build/` | **gitignored** — JUCE is fetched here and artifacts built here |

---

## Build (Mac Studio)

```sh
git clone https://github.com/whoisdetective47-gif/Backtrace-.git SoundDetectivePlugins
cd SoundDetectivePlugins
cmake -B build -DCMAKE_BUILD_TYPE=Release        # first run downloads JUCE — slow, one-time
cmake --build build --config Release --target Backtrace_All -j 8
```

Requirements: CMake ≥ 3.21, Xcode command-line tools. JUCE is fetched automatically by
CMake (FetchContent) into `build/` — nothing to install by hand.

### Formats & auto-install
Builds **VST3 + AU + Standalone**. `COPY_PLUGIN_AFTER_BUILD TRUE` auto-copies the VST3/AU
into `~/Library/Audio/Plug-Ins/...` on each build (no manual/sudo install — keep it that
way). Plugin name: **"Detective 47s Backtrace"**. AU id: `aufx Bktr SDet`.

### Validate / sanity
```sh
auval -v aufx Bktr SDet                                   # AU validation (expect SUCCEEDED)
./build/.../BacktraceSafetyTest                           # DSP safety (expect "ALL SAFE")
open "build/Plugins/Backtrace/Backtrace_artefacts/Release/Standalone/Detective 47s Backtrace.app"
```

---

## How Backtrace works (the signal flow)

1. **Source** — drag a word/hit/phrase into the SOURCE lane (or a slot).
2. **Tail Start marker** — a red `TAIL` marker on the source waveform marks where the
   source ends and the generated FX tail begins (default = source end; drag it earlier to
   start the tail sooner). The reverse is built from the **wet tail after** this marker.
3. **Tail Type** — picks which FX generates the tail (Reverb Swell default, Delay Swell,
   Delay→Reverb, Reverb→Delay, Parallel, Post Color).
4. **Swell Length** — 1/4 … 4 bars; a real length-aware tail is grown (no metallic stretch).
5. **Reverse Swell** → the tail is reversed into a rising swell that **lands** at the loud
   end (`LANDING` marker on the printed lane).
6. **Global Swell Macros** — Swell · Tail · Ghost · Damage · Reveal · Width.
7. **FINAL FILTER** — Harrison-style resonant HPF/LPF (slope 12/24, Curve, Character),
   optional Motion that sweeps cutoffs (knobs animate during Audition Swell).
8. **Audition / Print / Export WAV / Drag to DAW** — all share one render path, so they
   always match.

---

## Current state — verified vs. needs-ears

**Objectively verified (measured):** swell lengths/durations, Swell macro depth, Tail Type
distinctness, Width mono-safety, pitch-0 default (INIT preset), Harrison filter darkness +
motion reaching endpoint + knob animation, Reveal through the Harrison engine, Tail Start
changing the render. AU passes, DSP safety ALL SAFE throughout.

**Open character calls (need the main monitors):** see the testing message below.

---

## ▶ CONTINUATION — for the next Claude session (Mac Studio)

You're picking up **exactly where the laptop session left off** (last commit:
`Backtrace: capture-based reverse-FX swell plugin (full source)`). Standing constraints
for this suite:

- **Auto-install builds** — never hand the user repeated sudo/manual install steps.
- **End build responses with a `NEXT STEP` block** at the very bottom.
- **Verify the actual musical/functional behavior** the user asked about — not a tangential
  property. Measure the swell/filter/etc., don't just confirm it compiles.
- No trademarked names in public UI. Don't break **hear = print = export = drag**.

Fastest ramp: skim `Docs/Backtrace_DesignSpec.md` (Phases 1–32). The user is on a Mac
Studio through main monitors and is doing a **listening pass** — your job is to take their
ear feedback and tune the engine, not to re-verify what's already measured.

### 📨 MESSAGE TO SEND THE USER (copy/paste, then help them work through it)

> Backtrace is synced and built on the Mac Studio — same state as the laptop. Through your
> main monitors, here's what to check, roughly in priority order. Tell me what's off and
> I'll tune it.
>
> **1. THE MAIN FEATURE (most important).** Load a vocal word/phrase, INIT preset (pitch 0,
> Reverb Swell), Swell macro at 100%. Press Reverse Swell and audition. **Does it sound
> like a real manual Cubase reverse-reverb swell?** A/B it against your Cubase reference.
> This is the whole target — be picky.
>
> **2. Source End / Tail Start (new).** See the red **TAIL** marker at the end of the
> source. Drag it to just after the word, then Reverse Swell again. Confirm the swell now
> builds from the **tail** (not the reversed word) and lands cleanly into the word. The
> printed lane should show a **LANDING** tab at the loud end.
>
> **3. FINAL FILTER.** Turn Filter on. LPF → 500 Hz: **obviously dark?** HPF → 2 kHz:
> **clearly thin?** Push **Character**: adds analog vibe without harshness? Turn on
> **Motion** (LPF 20k→500) and Audition Swell: does it clearly reach dark by the end, and
> does the LPF **knob animate** as it plays? Try Slope 12 vs 24.
>
> **4. Reveal macro.** 0% = dark/muffled, 100% = open/full-range. Strong and immediate?
>
> **5. Delay machines** (Tail Type → Delay Swell, then audition each): Reel Echo, Digital
> Pedal, Tape Witness, **Magnetic Drum** (is the oily drum tail usable?), **Vault Delay**
> (dirty but musical — not a noise wall?), **Cold Rack** (just confirm it's fine).
>
> **6. Macros.** Ghost 0→100: clear → haunted/blurred (not just "more reverb")? Damage
> 0→100: musical aging before it gets extreme? Width 0→100: widens nicely and stays
> mono-safe? Tail: more/less tail body.
>
> **7. Workflow integrity.** Print, Export WAV, and Drag-to-DAW should all give the exact
> audio you auditioned. Changing a tail-gen param should flip the **"PRINTED SWELL — OUT OF
> DATE"** warning until you re-Reverse.
>
> Give me the verdicts (especially #1 and #2) and I'll start tuning.

---

*Generated with [Claude Code](https://claude.com/claude-code)*
