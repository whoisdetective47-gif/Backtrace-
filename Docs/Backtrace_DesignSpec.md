# Backtrace — Design Spec

**Sound Detective 47: Backtrace** — a capture-based reverse FX workstation built on the Dust Vault engine.

> Status: **in implementation.** Phases 1–7 built and installed (VST3 / AU / Standalone; AU passes `auval`): Manual + DAW Sync capture, waveform, trim locators, reverse + Land at Source, Print → new Vault slot, Library export/import, drag-and-drop export, and the pitch wheel (semitones + octave buttons, applied offline to the processed result). Capture→edit→export runtime-verified in the Standalone. **Formant preservation is deferred** — `ModernPitchShifter` is a time-domain shifter with no formant correction; it needs a phase-vocoder/PSOLA engine (separate DSP task).
>
> **Phase 8 COMPLETE — all 6 delay flavors built to spec, AU-validated:** Reel Echo (multi-head tape), Digital Pedal (clean pedal), Magnetic Drum (Binson rotating drum), Tape Witness (Echoplex gritty slap), Cold Rack (PCM studio), Vault Delay (dirty sampler — SR/bit reduction + jitter degrading inside the feedback loop). All share an 8-macro front panel (Time/Feedback/Tone/Character/Movement/Width/Duck/Mix) with per-flavor defaults; selected via a dropdown that remaps the knob cluster. Each is a `DelayMachine` subclass applied offline in the shared render path (prints/exports/drags). Deferred per-flavor: Head Pattern / Mode switches, granular advanced controls, tempo Sync, named presets. Next: reverbs (9).
>
> **Phase 9 COMPLETE — all 5 reverb spaces built to spec, AU-validated:** Velvet Hall (Lexicon modulated hall), Modern Space (Bricasti realistic room — stereo ER engine), Shimmer (octave-bloom — pitch in feedback, 10th knob = Shimmer), Studio Plate (EMT-140 Dattorro plate), 626 Spring (dispersive spring tank + transient splash). Each `ReverbSpace` subclass applied offline after the delay (Delay → Reverb) in the shared render path; reverb decay extends the print/audition tail; written to sidecar `fx.reverb`. Per-flavor 10-knob layout + dropdown remap. Both FX halves now done (6 delays + 5 reverbs = 11 flavors).
>
> **Phase 10 COMPLETE — Routing Modes.** A routing dropdown + live signal-flow bar drive all 7 modes (Delay→Reverb, Reverb→Delay, Parallel, Feedback Verb→Delay, Reverse Before FX, Reverse Tail, Full Reverse Print). The central `renderProcessed` honors the mode and is shared by audition/print/export/drag (what you hear = what prints; routing written to sidecar `edit.routing`). Default = Reverse Before FX. "Reverse" button → "Audition", "Print Reverse" → "Print", print prefix → `BACKTRACE_Print_`. Parallel = averaged wet paths (clearer not louder); Feedback Verb→Delay = conservative reverb-into-delay-input injection (true per-sample interleave is a later refinement). The render-based architecture means mode switches re-render independently (machines reset each pass) — no live crossfade needed, no runaway.
>
> **Phase 11A COMPLETE — Preset System & Factory Bank.** Full creative state now serializes to JSON via `getStateVar`/`setStateVar` using **stable string IDs** (delay/reverb/routing) — this also fixed a latent bug where `getStateInformation` only persisted the output gain (session restore was incomplete). 40 factory presets across 8 categories (`Utilities/FactoryPresets.h`), built from flavor defaults + targeted overrides. Preset browser: category-grouped dropdown, prev/next, Save As (user presets → `~/Music/Backtrace/Presets/*.json`), delete (factory read-only), and a dirty `*` indicator. Verified live: Detective Throw and Evidence Riser recall full state (flavors, per-flavor knob clusters incl. Shimmer's 10th knob, routing + flow bar, pitch), factory Del disabled, dirty fires on tweak. AU passes. Next: 11B — three-panel GUI polish.
>
> Phase 9 architecture: a parallel `ReverbSpace` base + per-flavor 10-knob layout (`ReverbSpace.h`) with its own dropdown + remappable cluster. Built so far: **Velvet Hall** (Lexicon-style) — modulated 4-line Hadamard FDN + diffusers + early reflections + in-loop damping; Decay→RT60 0.3–18 s, mod 0.05–1.2 Hz, in/out cuts, dB output. And **Modern Space** (Bricasti-style) — clean/deep/realistic: 12-tap stereo early-reflection engine + 6 diffusers + 4-line FDN with much subtler mod (0.03–0.7 Hz); Decay→RT60 0.2–12 s, refined damping/cuts. And **Shimmer** (octave-bloom ambient) — `ModernPitchShifter` (+12) inside the FDN feedback path so the octave halo blooms/regenerates from the tail; Decay→RT60 0.5–30 s; heavy feedback safety (low-cut before pitch, damp after, tanh limit, decay reduced as Shimmer rises → no runaway at 100%/100%); structured for a future Freeze. Per spec, its 10th knob is **Shimmer** (replacing Duck). And **Studio Plate** (EMT-140-style, public name to avoid trademark) — Dattorro-style figure-8 plate tank (serial allpass diffusion + cross-coupled delays + damping, no room ER engine); Size = plate spread (not room), Decay→RT60 0.4–8 s, very high default diffusion, Tone-swept damping. Reverbs share the 10-macro panel tuned to per-flavor measurement specs. Built: 4 of 5 reverbs. Remaining: 626 spring.
>
> Phase 8 architecture: a `DelayMachine` base + per-flavor knob layout (`DelayMachine.h`) feeds a dropdown selector whose knob cluster remaps per flavor. Built so far: **Reel Echo** — full multi-head tape spec: 8 macros (Time/Feedback/Tone/Character/Movement/Width/Duck/Mix), multi-stage `Saturation`, Movement = wow+flutter+motor-drift modulating delay *time*, tone-shaped + soft-limited + DC/low-cut feedback, Character degradation (HF loss/hiss/dropout), dual-head stereo + crossfeed, ducking, output ceiling. (Deferred: selectable Head Pattern + the granular advanced controls + tempo Sync + named presets.) And **Digital Pedal** (clean Boss-DD-style: Time/Feedback/Tone/Character grain via `BitReducer`/Mod/Width/Mix/Duck, soft-limited feedback). Delay applies offline after reverse+pitch with a feedback ring-out tail; flavor + named params written to sidecar `fx.delay`. Also **Magnetic Drum** — Binson-style: 3-head drum taps, all-pass diffusion network for halo/smear, slow circular+uneven rotational drift, round magnetic saturation, mechanical hum, orbital stereo; same 8-macro panel, spec defaults. Provisional UI names: Reel Echo (tape), Cold Rack (PCM rack, TBD), Digital Pedal, Magnetic Drum. Also **Tape Witness** — Maestro/Echoplex-style: single-head slap/echo, hot mid-forward preamp (driven + top-rolled), gritty per-pass feedback saturation, tape-SPLICE bumps in the movement, narrower stereo; same 8-macro panel. Also **Cold Rack** — PCM studio digital: clean repeats, smooth controlled modulation, all-pass diffusion that builds through feedback (ambient cloud), wide ping-pong/crossfeed stereo, controlled low/high-cut, subtle early-digital color via light `BitReducer`; same 8-macro panel. Built: 5 of 6 (Reel Echo, Digital Pedal, Magnetic Drum, Tape Witness, Cold Rack). Remaining delay flavor: Dirty Sampler. Then reverbs (9), routing (10), presets (11).
>
> **Safety verified:** an offline DSP stress test (`Plugins/Backtrace/Tests/SafetyTest.cpp`, target `BacktraceSafetyTest`) runs full-scale noise + impulse through all 11 flavors + pitch at MAX feedback/decay for 12 s — all stay bounded (peaks ≤1.0, threshold 4.0), finite (no NaN), DC-free, decaying. Combined with the render-based architecture (live path is passthrough; FX render offline into finite limited buffers) the plugin cannot run away in a host.
>
> **Reverse Swell workflow** (locator-capture creative path): DAW-Locators capture is sample-accurate at the left locator (insert-based, so it requires playing through the range once — no ARA). A **Swell Length** (1/2/4/8 bars, default 2) + **Reverse Swell** action render the captured source FORWARD through the FX wet-only, reverse the wet tail, fit to the swell length, end-align the loud peak to the source start, and de-click — producing a wet-only reversed swell that crescendos into the original word. Swell mode is honored by audition/print/export/drag. Verified: 2 bars → exactly 4.0 s @ 120 BPM, independent of source length.
>
> **Phase 11B COMPLETE — final product GUI.** Three-panel layout (header + LEFT Dust Vault / CENTER waveform hero / RIGHT FX + BOTTOM 8-macro strip), built without touching the DSP/render path. Header: Detective 47 logo *placeholder* + BACKTRACE + subtitle + preset browser. Vault: 8-slot model (`VaultSlot`) — capture commits to active slot (Source), Print stores into next free slot (Printed, never overwrites source), slot select updates waveform/trim, Clear/Rename. 8 macros alias key per-flavor params (Pitch/Delay/Feedback/Reverb/Swell/Ghost/Damage/Width) and recall via state. Final reverb UI names: Velvet Hall / Modern Space / Ghost Shimmer / Iron Plate 140 / Rust Spring 626. Verified: capture→slot1, print→slot2, slot-switch waveform update, macro recall, AU passes, DSP safety unchanged. **Remaining: drop in the real logo asset; subjective audio + Cubase session sign-off (yours).**
>
> **Phase 11C COMPLETE — preset bank + naming + branding.** Real **Detective 47 logo embedded** (`Assets/logo_detective47_dust1200_BLUE.png` via `BacktraceBinaryData`). Final UI names live: delays (Reel Echo…Vault Delay), reverbs (Velvet Hall / Modern Space / Ghost Shimmer / Iron Plate 140 / Rust Spring 626), buttons (Capture / Audition / Print / Drag Out / Clear / Rename / Save As / Reset). Factory bank rebuilt to **47 presets across 8 categories** (Vocal Throws, Reverse Blooms, Drum FX, Guitar Swells, Synth/Cinematic, Dirty Vault, Clean Studio, Experimental) with the exact spec names; each recalls flavors/routing/pitch/macros (verified: Ghost Vocal Rise → pitch +7, Ghost Shimmer, Reverse Before FX). **Reset to Default** added (does not clear Vault). AU passes, DSP safety unchanged, render path untouched. **Plugin is feature-complete; remaining = subjective audio + Cubase session sign-off (user).**
>
> **Phase 11D COMPLETE — RC polish (code side).** True slot-aware output: a **Printed/Processed slot now auditions, prints, drags and exports its committed audio as-is** (`writeProcessed`/`startReverseAudition` branch on `slot.status >= SlotProcessed`); a Source slot still renders through FX. So "selected-slot export" is literal. Short **de-click fades (~4 ms)** added at `renderProcessed` boundaries (timing preserved; capture transients intentionally NOT faded). Guidance UX: empty-state waveform text ("No evidence captured — set locators, choose a Capture Mode, then press Capture…"), and status now reports **"Captured to Slot X"** / **"Printed to Slot X"** / **"Armed — playback will capture the range"**. Active Vault slot highlight brightened to amber; disabled buttons (Clear on empty slot, Del on factory preset) grey correctly. Verified live: capture→Slot 1 Source, print→Slot 2 Print (8.3 s), slot-switch waveform update, printed-slot audition plays committed audio. AU passes, DSP safety ALL SAFE, render path intact.
>
> **NOT yet release candidate** — by the user's own criteria, RC requires the real-host acceptance pass that only the user can run: Cubase locator capture on real audio, drag-out grid alignment, click/pop listening, level consistency, and the 47-preset listening pass. Engine is RC-ready; sign-off is pending those ear/DAW checks.
>
> **Phase 12 COMPLETE — Printed-Swell Editor + Final Filter + bigger controls.** The render path is now **two-stage**: stage 1 (`regenerateSwell`) renders the printed swell into `swellBuffer`; stage 2 (`applyEdit`) applies the user's printed-swell A/B trim + fade-in/out + HPF/LPF (static or start→end motion) and is shared by audition/print/export/drag, so hear = print = export = drag still holds. **Two waveform lanes**: SOURCE CAPTURE (`WaveformCanvas`, source A/B → feeds the render) and PRINTED SWELL (`SwellCanvas`, own A/B + draggable fade handles + envelope overlay → trims the output). Stale tracking (`swellStale`) re-renders lazily on FX/source change; structural changes blank the lane (re-render via Reverse Swell). **FINAL FILTER**: HPF/LPF + End knobs + Filter/Motion toggles; `StateVariableTPTFilter` in series, 32-sample stepped cutoff for click-free sweeps; fades/filter persisted to preset/session state. **Swell length** now float (1 Beat/1 Bar/2 Bars/4 Bars) and independent of capture length — verified live: **1.2 s source → 4.0 s (2-bar) printed swell → Print → Slot 2 "Swell Print 4.0s"**. **Bigger controls**: `KnobLNF` (bold value ring + bright pointer dot; amber macros / blue FX), enlarged macro strip, taller window (1000×752), Print/Drag Out/Swell Length moved to center. AU passes, DSP safety ALL SAFE. **Deferred (noted): filter Record-automation mode (start/end only for v1), non-linear fade curves, Custom swell length — and subjective audio/click/level sign-off (user).**
>
> **Phase 13 COMPLETE — drag-to-DAW + workflow clarity + fade bug fix.** (1) **Fade-on-regenerate bug FIXED**: `regenerateSwell` preserves the user's printed-swell trim + fades when re-rendering the *same* length (verified live: fade-in 1945 ms survives Reverse Swell), so Reverse Swell = Audition = Print = Export = Drag all bake the same faded buffer. (2) **Drag-to-DAW**: Vault slots (`SlotButton`) and the printed-swell waveform (`SwellCanvas`) are external file-drag sources; slot drag = slot audio verbatim, waveform/Drag-to-DAW button = edited swell (trim+fade+filter via `applyEdit`); editor is a `DragAndDropContainer`; occupied slots show a drag cursor; useful names (`Backtrace_GhostVocalRise_2bars.wav`, `Backtrace_Slot01_<name>.wav`) — printed WAV verified 4.000 s / 24-bit / non-empty. **Export WAV + Reveal in Finder** fallbacks added. (3) **Play Source** (raw source) vs **Audition Swell** (processed) split via `startSourceAudition` + `auditionWhat`. (4) **Rename** confirms on Return / cancels on Escape. (5) Workflow clarity: step-numbered captions (1 Source / 2 Printed Swell / 3 Drag to DAW), state-aware empty/helper text, button renames (Drag to DAW, Audition Swell), fade hit-zone widened (top strip). AU passes, DSP safety ALL SAFE. **Note:** automated harness can't deliver synthetic mouse-drags or drop onto Cubase — the actual drop into Cubase + by-ear fade/click checks need the user; file-creation half is verified valid.
>
> **Phase 14 COMPLETE — drag-in is now the PRIMARY workflow; live capture demoted.** The product is now *drag source in → make swell → drag result out*, working with the transport stopped. (1) **Source lane (`WaveformCanvas`) and Vault slots (`SlotButton`) are `FileDragAndDropTarget`s** — drop a WAV/AIFF/FLAC/MP3 and it imports into the active slot / that slot, becomes the active source, waveform appears immediately (drop-hover highlight). `importToSlot(slot,file)` routes drag-drop + the Import Audio button. (2) **Import resamples to the engine rate** (`LagrangeInterpolator`) so a 44.1k file dropped in a 48k session keeps correct pitch/length — verified: 48k file → 96k engine shows 1.00 s / 96000 smp, swell renders non-silent (peak 0.22), export = valid 4.000 s WAV. (3) **Live capture demoted** to a dim "ADVANCED CAPTURE (DAW)" section at the bottom of the left panel; **Import Audio** is the prominent primary action. (4) **New copy**: source empty-state "Drag a word, hit, or phrase here. / Then choose Swell Length and press Reverse Swell."; state-aware hints ("Source loaded…", "Swell ready – Audition or drag to Cubase"). AU passes, DSP safety ALL SAFE. **Verified end-to-end with real audio via the import path** (same code as the drop). **Still needs the user:** the actual Finder→plugin drop and plugin→Cubase drop (cross-app drags can't be driven by the test harness) — Export WAV + Reveal fallback covers that case.
>
> **Phase 15 COMPLETE — Printed-Swell musical editor (ruler/grid/zoom/snap) + drag-in hardening.** `SwellCanvas` now has a **bars/beats ruler** (verified live: 2-bar swell shows 1.1 1.2 1.3 1.4 2.1 2.2 2.3 2.4, bar-starts amber), **subtle beat grid lines** (bar lines brighter), **visual zoom** (Zoom-/Zoom+/Fit — view window into the buffer; audio length unchanged, verified "out 4.00s" constant while zoomed), and **Grid Snap** (Off/1Bar/1/2/1/4/1/8/1/16) applied to A/B locators + fade handles — verified: B snapped to beat 2.2 = exact "out 2.50 s" (5 quarter-notes @120). Ruler/grid follow Swell Length + host time-sig (`getHostTsNum`, default 4/4); snap quantises handle POSITIONS only, never the audio. **Drag-in hardened**: `BacktraceEditor` is now an editor-wide `FileDragAndDropTarget` catch-all (drop an audio file ANYWHERE imports), on top of the per-lane/per-slot targets. AU passes, DSP safety ALL SAFE. **Drag-in bug clarification:** the user's "dragging clips does nothing" is because Cubase arrange-page **audio events are not OS files** — `FileDragAndDropTarget` only fires for real files (Finder, Cubase Pool/MediaBay) or the Import Audio button; native DAW-event drag is host-specific and not yet supported. Filter-automation start/end have no positional markers (whole-buffer sweep), so snap there is N/A.
>
> **Phase 16 — DAW-event drag-in (text-path) + the file-promise finding.** Added `TextDragAndDropTarget` to `BacktraceEditor` (`pathsFromDragText`): hosts that place a file path / `file://` URL on the drag as TEXT now import. **Root-cause of "dragging clips does nothing":** JUCE 8's macOS drag code (`juce_NSViewComponentPeer_mac.mm::getDroppedFiles`) registers for `kPasteboardTypeFileURLPromise` but only RESOLVES the **iTunes** promise format; for a generic file promise (Cubase timeline event) it reads an `NSURL` that isn't on the pasteboard yet → empty → nothing imports. Cubase **Pool/MediaBay** drags usually provide a real `NSURL` (already work); arrange-page **events** provide a promise (don't). Full fix = patch JUCE to resolve generic promises via `NSFilePromiseReceiver` (threads `sender` through `getDroppedFiles`); that's a vendored-dependency change, untestable without Cubase, so it's deferred pending a user Cubase test of the text-path path. Reliable import paths remain **Finder/Pool drag + Import Audio**. AU passes, DSP safety ALL SAFE.
>
> **Phase 17 COMPLETE — vendored JUCE file-promise patch (DAW timeline drag).** Backtrace now has **three** drag-in mechanisms: real file URL (Cubase Pool/MediaBay, Finder), file-path-as-text (`TextDragAndDropTarget`, Phase 16), and **generic file promise** (arrange-page audio events). The promise path is a durable configure-time patch (`cmake/PatchJuceFilePromise.cmake`, wired in the top `CMakeLists.txt` behind `-DBACKTRACE_PATCH_JUCE_FILEPROMISE=ON`) that injects, into JUCE's `juce_NSViewComponentPeer_mac.mm::sendDragCallback`, a fallback that asks the drag source to write the promised file to a temp dir (`namesOfPromisedFilesDroppedAtDestination:`) and feeds the path into JUCE's normal `FileDragAndDropTarget`/`importFiles` flow. Patch is **idempotent** (marker-guarded), **safe no-op** if the JUCE source ever changes (warns, skips — existing drag untouched), and survives clean rebuilds (re-applied after FetchContent re-fetch). Verified: patch message prints, patched JUCE compiles, **AU SUCCEEDED** (loads + instantiates the editor with the new drag handler), standalone runs, DSP safety ALL SAFE. **Unverifiable here:** the actual Cubase event→promise resolution needs the user in Cubase; `Import Audio` + Pool/Finder drag remain the guaranteed paths.
>
> **Phase 18 — IMPORT drag/drop reset to a clean, PROVEN baseline.** Reverted the experimental JUCE drag patches (file-promise + broad-type diagnostic) to **pristine JUCE** (git checkout; both options now default OFF) — they were noise/risk. Added **plugin-level import logging** → `~/Desktop/backtrace_import_log.txt` (isInterestedInFileDrag, filesDropped, per-file accept/reject, LOADED len/sr/seconds, or read failure). **Verified Finder WAV drag → standalone end-to-end** (clean build): log shows `isInterestedInFileDrag ACCEPT` → `filesDropped` fires → `LOADED bt_import_test.wav -> slot 1 len=76800 sr=96000 (0.80s)`, source waveform + Play Source appear. So `FileDragAndDropTarget` + `importToSlot` + resample are correct; Import Audio button + Finder drag are reliable. **Cubase diagnostic finding (kept for reference):** arrange-event drag delivers `com.steinberg.pasteboardtype.* + public.utf8-plain-text` (text, not a file/promise) — that's the only iffy path; Finder/Pool/Import are the supported ones. The import log is left in so a Cubase test self-diagnoses (no log line = host didn't forward the drag; reject = extension; LOADED = success).
>
> **Phase 19 — Cubase arrange/Pool drag SOLVED (real fix from the diagnostic).** The import logger revealed Cubase drags an event/Pool item as `vst-xml` TEXT containing `<filename>/abs/path.wav</filename>` (+ `<start>`/`<end>` sample region) — not a file or promise. Fix: `pathsFromDragText` + new `importFromDragText` parse the `<filename>` (XML-unescaped) so `isInterestedInTextDrag` now ACCEPTs (Cubase shows "drop OK" instead of "no drop"), and `importToSlot(slot,file,start,len)` imports just the dragged region (honors `<start>`/`<end>`; whole file if absent), capped at 60 s, resampled to engine rate. Verified loader handles spaces/parens paths (`1. KICK OUT  (TK2).wav` → slot, 0.60 s). **Import Audio** now opens to the last-used folder (not the empty Library), accepts wav/aif/aiff/flac/mp3/m4a/ogg/caf, and a successful drag sets that folder (so Import lands in the project's audio folder). All import paths share `onSourceImported`. Import debug log stays at `~/Desktop/backtrace_import_log.txt` for the Cubase re-test. AU passes, DSP safety ALL SAFE, installed VST3 contains the parser.
>
> **Phase 19 CONFIRMED WORKING IN CUBASE + cleaned for release.** User verified: dragging arrange/Pool audio events into Backtrace now imports (log showed `vst-xml import "...stam_18.wav" region start=113391 len=1510818 → LOADED slot 1 (15.74 s)`, region honored). Cleanup: import logging routed to `DBG` (compiles out in Release — no Desktop log file); the temporary diagnostic + file-promise CMake patch scripts deleted and their options removed; JUCE source pristine again (0 mods). The cross-host drag-in solution is purely in the plugin: `TextDragAndDropTarget` + `importFromDragText` parse Cubase's `vst-xml` (native `NSPasteboardTypeString` delivery — no JUCE patch needed) while `FileDragAndDropTarget` handles Finder/file-URL hosts. Final release binary: AU passes, DSP safety ALL SAFE, parser present, no debug-log code. **Drag source in → make swell → drag result out is now fully working in Cubase.**
>
> **Phase 20 COMPLETE — Source/Print slot banks + audition playhead.** Vault split into two banks: `sourceSlots[8]` (imported/captured originals that feed the render) and `printSlots[8]` (rendered swells), with `activeSource`/`activePrint` + a `viewingPrint` flag. Editor shows a **[Source Slots | Printed Swells]** tab toggle over the 8 slot buttons; selecting a source drives the source lane + render, selecting a print loads it into the printed-swell lane (`regenerateSwell` branches on `viewingPrint`). Print commits the edited swell into the next free **Print** slot (auto-switches to the Printed Swells tab; name = preset + bars, e.g. "Ghost Vocal Rise 2bar 4.0s Print"). Clear/Rename/drag-export act on the active bank; `exportSlotFile(i,print,dir)`. **Multi-file drop** fills empty source slots in order (>8 → "Imported N, M skipped"); Cubase multi-event vst-xml likewise fills source slots. **Audition playhead**: high-contrast white line in both lanes — `WaveformCanvas`/`SwellCanvas::setPlayhead`, driven by the 20 Hz timer from `getPlayPos()`/`getAuditionWhat()` (source lane for Play Source = trimIn+pos, printed lane for Audition Swell = swellTrimIn+pos, zoom-aware), resets on stop. Verified live: drag→source slot 1, Reverse Swell→playhead sweeps the printed lane (caught ~30% at 1.2 s of 4 s), Print→Print slot 1 on the Printed Swells tab. AU passes, DSP safety ALL SAFE.
>
> **Phase 21 — "Discard Evidence" removal term.** Detective-themed slot removal across both banks. `SlotButton` right-click (`isPopupMenu` → `onContextMenu`, no select) opens a menu: section header (Source/Print Slot N) → **Discard Evidence** (clears that slot, disabled if empty) → **Discard All Sources** / **Discard All Prints** / **Discard Entire Vault** (each via `confirmBulk` AlertWindow: Discard/Cancel). The old "Clear" button is renamed **Discard Evidence** (acts on the active bank's selected slot) with tooltip "Remove this audio from Backtrace. Original files are not deleted." (a `TooltipWindow` was added). `discardSlot(i,print)` clears the slot → Empty, refreshes the source/printed lanes, rebuilds the list; disk files are never touched. Verified live: right-click Source Slot 1 → menu shows all items → Discard Evidence → slot returns to "1 —" (Empty). AU passes, DSP safety ALL SAFE.
>
> **Phase 22 COMPLETE — fade curves + handle-bug fix + numeric controls.** Shared `UI/FadeCurve.h` (`btFadeGain`: Linear/Exp/Log/S-Curve/Equal-Power) used by BOTH `applyEdit` (DSP) and the `SwellCanvas` draw, so the drawn curve == the rendered fade (applies to audition/print/export/drag via the single `applyEdit` path; every curve hits 0 at the boundary → click-free). Per-fade curve type via `fadeInCurve`/`fadeOutCurve` (persisted in state) + UI combos; the actual curve is drawn over the waveform with shaded fade regions. **Handle-selection bug FIXED**: fade strip now picks the handle by **closest pixel distance** to its anchor (fade-in end / fade-out start), not by which half — verified live: pushed fade-in to beat 3.1 (past centre), dragged it, Fade In changed to 1547 ms while Fade Out stayed 8 ms (no steal). Fades clamped non-overlapping; handles enlarged. **Numeric controls**: editable Fade In/Out ms fields (Return-commit, verified typing 1500/3000) + **Reset** menu (Reset Fade In / Out / Both → short safety fade). AU passes, DSP safety ALL SAFE. (Custom drawn-curve mode deferred per spec.)
>
> **Phase 22b — right-click fade-curve menu (spec §6).** Right-clicking the printed-swell fade region opens a context-aware menu for the nearer fade (`SwellCanvas::onFadeMenu` picks 0=in / 1=out by closest handle): section header "Fade In/Out Curve" → Linear/Exp/Log/S-Curve/Equal-Pwr Fade (current one ticked) → Reset Fade In/Out + Reset Both Fades. Verified live (header "Fade In Curve", S-Curve ticked, all items present). AU passes, DSP safety ALL SAFE. **Still deferred (optional):** freehand "Draw" custom fade-curve mode.
>
> **Phase 23 — Swell Length now controls real rendered duration (critical fix) + more options.** Root cause: `renderSwell` copied a fixed-length wet tail end-aligned and left the front silent when `swellLen > wetLen` (dead space; longer Swell Length didn't lengthen the audio). Fix: dropped the 2 s padding floor (`wetLen = selLen + active-FX ring-out`) and **time-fit (resample) the reversed swell to EXACTLY `swellLen`** via `LagrangeInterpolator`, so the rise spans the whole selected duration with the loud onset still landing at the end. Verified live: 2 Bars→4 Bars went out 4.00 s → **out 8.00 s**, waveform filled, ruler showed 4 bars; exported `Backtrace_..._4bars.wav` = **8.000 s, non-silent across both halves (RMS .050/.047)**. Changing Swell Length now **regenerates immediately** (editor), and the ruler bars are derived from the actual buffer duration + host tempo so waveform/audio/sidecar/WAV all agree. **More options**: 1/8 / 1/4 / 1/2 note, 1 / 2 / 4 / 8 Bars (note values tempo-relative via host time-sig). All output paths use the same time-fitted `swellBuffer` (audition/print/export/drag). AU passes, DSP safety ALL SAFE. **Note:** time-fit is resampling (pitch shifts with extreme stretch — expected for a reverse swell); a pitch-preserving stretch would need a phase vocoder (future). Freehand Draw fade-curve mode still the only deferred fade item.
>
> **Phase 23b — pitch-preserving swell stretch + Keep Pitch toggle.** After the time-fit resample (which shifts pitch by the stretch ratio), `renderSwell` now applies a compensating pitch shift (`+12*log2(swellLen/wetLen)`, clamped ±24 st, via the validated `applyPitch`/`ModernPitchShifter`) so the stretched swell keeps the source pitch. `keepPitch` atomic (default ON, persisted in state, marks swell stale on change) + **Keep Pitch** toggle in the swell-length row; OFF = creative pitch-drop (plain resample). Verified objectively (330 Hz tone, 1-bar swell): Keep Pitch ON dominant ~496 Hz (within the source 330–660 Hz range), OFF ~1394 Hz — the correction is active and in the right direction. AU passes, DSP safety ALL SAFE. **Limitation:** the corrector is a time-domain shifter clamped to ±24 st, so very large stretches (>~4×, e.g. 8 bars from a sub-second source with no FX) only partially preserve pitch and add artifacts — a phase-vocoder time-stretch would be cleaner (future). This is the recommended Phase-23 follow-up the user selected.
>
> **Phase 23c — Swell Length drives the actual generated swell (generate → trim → reverse → fit).** This is the core promise: capture word → choose swell length → generate a reverse-FX bloom of that exact musical length. `renderSwell` is now a single generator used by audition/print/export/drag/waveform:
> 1. **Generate** the forward FX bloom into a GENEROUS buffer (up to ~14 s past the source) so the whole natural wash is captured; reverb Decay floored (≥0.62) for a rich multi-second wash. (`applyReverb`/`applyDelay` gained a `fillSec` arg that floors Decay/Feedback for the swell render only.)
> 2. **Trim** trailing silence to the part with real energy (last sample above −40 dB of the bloom peak) → `washLen`.
> 3. **Reverse** the real wash → a rising bloom that builds into the source onset at the end.
> 4. **Fit** that wash to EXACTLY `swellLen` via `DSP/TimeStretch.h::btPhaseVocoderStretch` (Keep Pitch ON, pitch-preserved) or Lagrange resample (OFF, creative). Because a real multi-second wash is trimmed then fitted, every length gets the SAME rising profile spread across its duration — not silence + a blip.
>
> `DSP/TimeStretch.h::btPhaseVocoderStretch` — STFT phase vocoder (N=2048, fixed synthesis hop Hs=N/4, derived analysis hop Ha=Hs/ratio so any stretch/compress ratio overlaps cleanly; phase-advance + princarg, Hann analysis+synthesis, window-power normalization, `juce::dsp::FFT`).
>
> **Objectively verified at 120 BPM** (330 Hz source through Ghost Shimmer): durations exact — 1/4 note 0.50 s, 2 bars 4.00 s, 4 bars 8.00 s, 8 bars 16.00 s; rising quartile profile CONSISTENT across all lengths (Q1≈−32, Q2≈−18, Q3≈−5, Q4 0 dB) — i.e. audible energy in every quarter, building into the end (the old direct-render gave Q1 −67 dB / Q2 −39 dB = silent front half). `#if JUCE_DEBUG` log in `regenerateSwell` prints bars/bpm/targetSec/samples + per-quarter RMS. AU passes, DSP safety ALL SAFE.
>
> **Phase 23d — loudness compensation that preserves the swell envelope.** Render order is now: generate FX tail → reverse/route → time-fit to length → rise envelope (the reversed wash) + user fades → tone/filter shaping → **single-scalar loudness comp** → safety peak ceiling. The loudness comp (`applyEdit`) measures the buffer peak and applies ONE gain over the whole buffer toward a 0.70 target (boost capped ~+18 dB) so different lengths land at a comparable level — it multiplies every sample equally, so the quiet-start→loud-end ratio is untouched (NO per-window normalize, NO compression, NO limiting before the rise curve). `applyOutputSafety` stays a DC-blocker + soft-clip > 0.9 only (never flattens). Debug log adds **Q4/Q1 ratio** and a `WARNING: Swell envelope flattened` if ratio < 1.5. **Verified** 1/4 note…8 bars (Ghost Vocal Rise): durations 0.50/2.00/4.00/8.00/16.00 s, comparable peaks (0.49–0.70), envelope intact at every length (Q4/Q1 = 31×–48×, monotonic Q1<Q2<Q3<Q4). AU passes, DSP safety ALL SAFE.
>
> **Phase 23e — Routing actually drives the swell render (was ignored).** The rewritten `renderSwell` had hard-coded the FX order, so the Routing dropdown changed nothing audible. Routing is now read inside `renderSwell`: a new `applySwellFX(buf,total,mode,fillSec)` applies the per-mode delay/reverb topology (Delay→Reverb, Reverb→Delay, Parallel blend, Feedback Verb→Delay inject), and the reverse POSITION is mode-driven — Reverse Before FX / Full Reverse Print reverse the SOURCE, Reverse After FX reverses the wet TAIL (classic rising swell), and the four topology modes are FORWARD routed throws (no reverse) so each is its own sound. All consumers (waveform/audition/print/export/drag) share the one `renderSwell`→`swellBuffer`→`applyEdit` buffer, so they always match. UI label "Reverse Tail" → "Reverse After FX"; factory **Ghost Vocal Rise** routing changed `reverse_before_fx` → `reverse_tail` so its default stays the classic rise. Routing persists in session state (`getStateVar`) and user/factory presets (`buildState` carries it). `#if JUCE_DEBUG` log prints mode/reverse-position/fx-order/checksum. **Verified**: same source+preset+length+knobs, all 6 modes export DISTINCT buffers (6/6 unique md5; topology modes front-loaded "fall", Reverse After FX "rise"). AU passes, DSP safety ALL SAFE. NOTE: topology modes are forward by design (matches their per-mode descriptions); if all six should be reverse swells instead, revisit `reverseTail`.
>
> **Phase 23f — Pitch always starts at 0 (INIT preset).** Fresh inserts started at +7 st because the constructor's `loadPreset(0)` opened the first factory preset, which was the pitched "Ghost Vocal Rise" (+7). Fix: added `btpreset::initState()` (pitch 0, delay Off, Velvet Hall @ 0.25 mix, Reverse Before FX, no shimmer/feedback/damage) and a factory preset **"INIT - Clean Backtrace"** (category "Utility / Init") pinned at index 0, so `loadPreset(0)` on a fresh insert now yields pitch 0. `resetToDefault()` also uses `initState()`. `pitchSemitones` already defaulted to 0; the GUI reads it via `syncControlsFromProcessor` (label "0 st", centered slider). Ghost Vocal Rise keeps +7 (intentional). **Verified** in the standalone (cleared persisted state to simulate a fresh insert): fresh → INIT / 0 st / centered; Reset from +7 → 0 st; Ghost Vocal Rise → +7 st; back to INIT → 0 st. Pitch persists in session/preset state via `getStateVar`/`setStateVar`.
>
> **Phase 24 — Global Swell Macro strip.** The bottom strip is now 6 global final-swell macros (Pitch/Delay/Feedback/Reverb removed — those live in the FX panel only): **Swell · Tail · Ghost · Damage · Reveal · Width**, under a "GLOBAL SWELL MACROS" header + helper text + per-knob tooltips. Each is a real processor atomic, persisted in `getStateVar`/`setStateVar` (`macros{}`), defaulted strong when absent (Swell 1.0, Tail 0.35, Ghost 0, Damage 0, Reveal 1.0, Width 0.7).
> - **Swell** (headline): a reverse-rise envelope `env(x)=blend(1, 0.04+0.96·x³, swell)` baked into `swellBuffer` from a cached `swellRawBuffer` (cheap reshape, no bloom re-render → responsive + the canvas shows it live). `env(1)=1` always, so length/landing/grid never move — intensity only. Loudness comp (single scalar) keeps the end strong. Default **100%**. Verified on the rising base: 100% → Q4/Q1 **262×**, 0% → 17.8×, both peak 0.70, identical 192000 samples.
> - **Tail** → reverb Decay + delay Feedback (re-render). **Ghost** → reverb Diffuse/Shimmer/Mod/Width + delay Movement/Character (re-render, blur/atmosphere). **Damage** → `tanh` saturation + signal-gated dust (applyEdit, bounded). **Reveal** → global 12 dB/oct tone LPF, 1.0 = open (applyEdit). **Width** → mono-safe M/S, mid always full (applyEdit).
> - INIT routing changed `reverse_before_fx` → `reverse_tail` so the Swell-100% default demonstrates the dramatic rising pull immediately (the Swell rise needs a rising base; on a falling base it only flattens). AU passes, DSP safety ALL SAFE.
>
> **Phase 25 — Natural-bloom reverse swell (match the Cubase reference; kill phase-vocoder artifacts).** The swell was time-stretching the reverb wash with a phase vocoder to fit the length — that mangled the bloom (metallic/watery, "sounds like shit"). A real producer reverse swell NEVER stretches: it renders a reverb tail of the right length and reverses it. `renderSwell` now does the same — the reverb Decay scales with swell length (`0.5 + fillSec·0.05`) so the natural tail reaches the length, then it reverses **exactly swellLen of the real wet bloom with NO stretch** when `washLen ≥ swellLen` (the common case, 1–2 bar). The phase-vocoder stretch survives only as a fallback for very long swells the reverb can't reach. Swell macro retuned: at **100% = the natural bloom untouched** (drama from the real tail, per the reference), below 100% lifts the early part for a subtler throw; `env(1)=1` so timing/landing never move. Verified (2-bar, Swell 100%): smooth natural envelope −18 dB quiet start → peak near the end (no spikiness), exact 4.000 s, no stretch. AU passes, DSP safety ALL SAFE. **Width** also upgraded to the shared Dust 12.47 `Common/DSP/StereoWidener.h` (two-stage mono-safe M/S; loudness comp moved before the widener so the mono fold-down stays constant). Awaiting the user's A/B against their real Cubase reference.
>
> **Phase 26 — Length-aware iterative tail-fit (real reverse-reverb engine).** Per the user, decay-scaling alone isn't enough and blind stretching is wrong. `renderSwell` Stage A now: renders the wet reverb tail, MEASURES where it decays to −40 dB, and **re-renders with adjusted decay (≤3 passes)** until the natural tail reaches the swell length (ratio in 0.95–1.6). Diffusion/density rises with length for body on long swells. Then Stage B reverses **exactly swellLen of that real tail with NO stretch** (the common 1–4 bar case); the phase-vocoder time-fit is only the last-mile fallback for lengths the reverb can't physically reach. `applyReverb`/`applySwellFX` gained `decayOverride`/`density` args so the fit loop drives the decay directly. **Verified** (Velvet Hall, Swell 100%): 1/2/4-bar all produce the SAME smooth natural envelope (−23 dB quiet start → peak near the end) at exact 2/4/8 s, max adjacent-segment jump only 5–7 dB (no phase-vocoder spikiness), consistent ~0.85 peak. AU passes, DSP safety ALL SAFE. Awaiting the user's A/B vs their real Cubase reference.
>
> **Phase 27 — Tail Type system (replaces the Routing dropdown) + Audition Tail.** The developer-ish "Routing" dropdown is reframed as **Tail Type** = which FX generates the forward wet tail that then gets reversed. Options: **Reverb Swell** (default), Delay Swell, Delay→Reverb Swell, Reverb→Delay Swell, Parallel Swell, Post Color. EVERY type now builds a forward wet tail and reverses it (the old "forward topology" and "reverse-before" modes are gone — `reverseSrc` removed, `reverseTail` always true). The `RoutingMode` enum (name kept for code stability) is redefined to these 6; `routingModeFromName` migrates legacy preset/session strings; default atomic = `ReverbSwell`. `applySwellFX` selects the generator (reverb-only / delay-only / delay→reverb / reverb→delay / parallel); the iterative tail-fit runs whenever reverb generates the tail (`tailUsesReverb`). New **`renderTail` + `startTailAudition` + "Audition Tail" button** play the FORWARD wet tail before reversal so the user can vet the FX ("weak tail → weak swell"). UI: caption "ROUTING"→"TAIL TYPE", flow text now `src → reverb tail → REVERSE → swell`. INIT + Ghost Vocal Rise presets → `reverb_swell`. Verified: fresh insert defaults to Reverb Swell, Audition Tail plays (button→Stop), AU passes, DSP safety ALL SAFE, Dust 12.47 still builds.
>
> **Phase 28 — Engine tuning pass (cache rule, filter motion, delay safety).** (1) **Stale-render rule:** `markDirty()` now only marks the preset dirty; new `markTailDirty()` also sets `swellStale`. Tail-generator params (Tail Type, delay/reverb params+flavors, pitch, Tail/Ghost macros, swell length, keep pitch) call `markTailDirty`; final-edit params (Swell/Reveal/Width/Damage, fades, filter) call `markDirty` (live). `ensureSwell()` now rebuilds ONLY when no print exists — a stale print is kept (Audition/Print/Export all use the same cached print) and the editor shows "PRINTED SWELL — OUT OF DATE: press Reverse Swell" + highlights the button; Reverse Swell force-rebuilds. (2) **Filter Motion fixed:** the `StateVariableTPTFilter` misbehaved when the cutoff was swept down from a near-nyquist start (the end never darkened). Replaced with cascaded one-pole HPF/LPF (12 dB/oct, robust at every cutoff) + LOG (perceptually even) interpolation. Verified: LPF 20 kHz→558 Hz now rolls HF off toward the end (last 10% HF/LF 0.18 vs the old SVF leaving the end bright). (3) **Vault Delay** de-chaosed: the in-loop `tanh(s·(1+drive·3))` amplified small signals (loop gain > 1 → wall of noise); now unity-gain limiting in the loop (decays at ~fbGain), feedback capped 0.85, crunch moved to the wet output. (4) **Magnetic Drum** feedback normalised to ~unity small-signal gain so the drum echoes decay cleanly instead of building/mushing. AU passes, DSP safety ALL SAFE (tails decay), Dust 12.47 still builds. Magnetic Drum / Vault Delay character still want the user's ear A/B; Cold Rack unconfirmed.
>
> **Phase 29 — Harrison 32C-style FINAL FILTER.** Replaced the weak filter with a resonant zero-delay-feedback (TPT) state-variable filter (`TptSvf`, unconditionally stable at any cutoff/Q — no near-nyquist blow-up, no zipper). Selectable **Slope** 12/24 dB/oct (cascaded sections, default 24), gentle **resonance** + analog **Character/Edge** (`filterDrive`: k=1/Q from ~0.78→2.2 plus soft tanh drive), and motion sweeps cutoffs in LOG-frequency space with a **Curve** option (Linear/Exp/Log/S). New editor controls: Slope combo, Curve combo, Character knob (+ tooltips); persisted in edit state (`filterSlope/Curve/Drive`); all live (final-edit). **Verified**: static LPF 500 Hz now cuts the 2–12 kHz band ~54 dB (HF/LF 0.0018 from ~1.0 broadband) with a slight resonant lift at cutoff — obviously dark, vs the old ~-12 dB. Motion uses the same proven log-freq interpolation (the end reaches the endpoint). AU passes, DSP safety ALL SAFE, no regression to delay/reverb tails. (Reveal macro remains a separate quick global-tone LPF; can be upgraded to the same engine if wanted. Minor: Slope combo label truncates to "2…" — cosmetic.)
>
> **Phase 30 — Playback-only filter-motion knob animation.** During internal **Audition Swell** (only), when Filter+Motion are on, the editor `timerCallback` (now 30 fps) drives the HPF/LPF **start** knobs through the live interpolated cutoff using the SAME shaping + log-frequency interpolation as `applyFilterMotion` (progress = `playPos/playLen`). Strictly **display-only**: `setValue(..., dontSendNotification)` never writes the parameter; the End knobs stay parked at their destinations; on stop the start knobs restore to their stored values. Tail/Source auditions don't animate (no filter applied there). Verified: LPF swept 20000→6265 Hz mid-audition (matching the curve), End stable at 500, restored to 20000 on stop (param untouched → export unchanged). AU passes, no DSP on the UI thread, no re-render triggered.
>
> **Phase 31 — Reveal → Harrison engine + Slope display.** The Reveal macro now uses the shared resonant `TptSvf` (24 dB/oct, gentle Harrison edge) instead of its old weak 2× one-pole — `TptSvf` moved to the top of the .cpp so both `applyEdit` (Reveal) and `applyFilterMotion` share it. Reveal maps log-freq: 1.0 = open/full-range (no effect, default), 0.5 = balanced (~3.6 kHz), 0 = dark (~650 Hz); it's a GLOBAL openness stage on top of the manual FINAL FILTER + Motion and never touches the user's explicit HPF/LPF endpoints. Applies in `applyEdit` → audition/print/export/drag/slots all match. **Verified**: Reveal 0% cuts 2–12 kHz to HF/LF 0.006 (from ~1.0 broadband) — powerfully dark. Slope combo widened to show "24 dB" (was "2…"). AU passes, DSP safety ALL SAFE.
>
> **Phase 32 — Source End / Tail Start hitpoint (core engine).** A boundary that defines where the imported source ends and the generated FX tail begins; the reverse swell is built from the WET TAIL *after* this marker. Processor: `tailStartSamp` (samples from trim start; -1 = auto = end of trimmed source), `getTailStart()/setTailStart()`, auto-resets to source end on any source/trim change, manual override wins; `setTailStart`→`markTailDirty` (print goes stale). `renderSwell` now reverses `work[tailStart .. tailStart+swellLen]` (no stretch when `washLen-tailStart >= swellLen`, else reverse+fit the tail region), and the iterative decay-fit targets `(washLen - tailStart) ≈ swellLen` — so the landing falls on the post-source tail (classic reverse-reverb/delay), skipping the source-feed region. Tail generation still runs full-length (`renderLen` unchanged); the marker only defines the reverse region. Recorded in the export sidecar; all render paths (audition/print/export/drag/slots) share `getTailStart()`. UI: draggable warm-red **Tail Start** marker on the source `WaveformCanvas` (bottom tab, ms readout, grabbed by a low click so it doesn't collide with the B locator; re-defaults to source end when A/B move) + a matching **LANDING** tab on the printed `SwellCanvas`. **Verified**: marker renders at source end (500 ms for a 0.5 s source), drags to 250 ms (readout updates), and the resulting render differs (md5 + quartile RMS both change) while keeping the rising swell envelope; AU passes, safety SAFE. (Optional Audition-Tail two-region shading not added — the source marker + Landing tab convey the boundary.)
>
> **Phase 33 — Global ringout + defaults + Motion Mode.** Three changes (all reverb-agnostic; **delay work frozen**). **(1) Global ringout/tail/decay:** every reverb already runs through the shared `applyReverb` loop over the full `renderLen`, but `renderSwell` only printed a ringout when the natural −40 dB tail (`washLen-tailStart`) reached `swellLen` and clamped it to that point — so short-tail reverbs (Ghost Shimmer, 626) hard-cut at the landing. Now `renderLen` includes the requested ringout, Stage B never reverses `work` in place (forward tail stays intact), and the ringout is computed for EVERY reverb, clamped to where the forward tail naturally falls to ~−60 dB. Reverb only colours the tail; the shared stage decides continuation. Guarded by RenderTest §1b (all 5 reverbs extend past a stable landing, audible tail, no hard-cut). **(2) Defaults:** reverb = Ghost Shimmer (INIT flavor 3); fade-in curve = Exp ("Expression") with a subtle ~7 % start fade ON; Ringout ON (`macroRingout` 0.35 + `setStateVar` ringout default 0.35); Motion OFF; the FINAL FILTER caption is now "FILTER" and the **Filter toggle sits before Motion** (Motion depends on Filter). **(3) Motion Mode** (`filterMotionMode` 0/1/2, persisted): `applyFilterMotion` pivots the sweep on **Peak Land** (the landing `landFrac`), not the buffer end — **Rise Only** opens to Peak Land then holds over the tail, **Rise+Fall** opens then closes back down over the tail, **Fall Only** starts open and closes through the whole swell+tail. UI combo (Slope/Curve/**Motion Mode**) + the display-only knob animation both follow the mode. **Verified**: RenderTest §4b (Rise+Fall restores lows in the tail vs Rise Only held thin; Fall Only starts thin at Peak Land; all 3 modes distinct), 43/43 render checks, AU passes, safety SAFE; UI confirmed (Ghost Shimmer + Ringout 35 % + Pull 0.50 + Fade Exp + Filter-before-Motion + Rise Only on a fresh insert).
>
> **Phase 35 — Live Preverb (Mode 2, real-time DAW-synced reverse reverb).** Product pivot toward a real-time preverb/transition designer (competing with Low End Candy "Preverb"), **without removing Capture/Print** (now the precision mode). New `DSP/LivePreverb.h`: reverse reverb = convolution (`juce::dsp::Convolution`, NonUniform 512-head, low CPU, glitch-free) with a **time-reversed reverb IR** — the reversed forward-reverb tail builds UP to a peak, so convolving the live input makes every sound bloom a swell that LEADS INTO it; an internal dry-delay lands the dry on the swell peak and the plugin reports that as **latency = pre-swell length** (a mix effect, not zero-latency). Processor: `liveMode/liveWet/liveDry` (persisted), `preverbLengthSamples()` reuses the DAW-tempo-locked Swell Length, `rebuildLiveIR()` renders the reverb tail of an impulse → reverse → fade-in → normalise → `loadImpulseResponse`, run on the existing **RenderThread worker** (serialised with offline renders → no reverb-object race; IR-only passes never signal `renderDone`). `processBlock` branches to `livePreverb.process` (delayed dry + reversed-IR wet, tanh ceiling) only in Live mode & not auditioning; `markTailDirty` rebuilds the kernel when live; latency pushed to the host from the editor timer (`pushLiveLatencyIfChanged`). UI: a **LIVE PREVERB** toggle + **Wet** slider + live latency/status readout at the top of the FX column (Capture/Print untouched). **Verified** (RenderTest §10, stable across 3 runs): kernel loads, latency = pre-swell (24511 smp / 510 ms for 0.25 bars; 4365 ms for 2 bars in the standalone), output finite + bounded (no runaway/hard-cut), and the swell **rises into a peak that follows the trigger** (a real lead-in, not a burst). AU passes (latency property), DSP safety ALL SAFE, 47/47 render checks; Capture mode unaffected. **Phase-1 limitations (by design)**: latency = pre-swell length; reverb IR uses the live reverb settings (Expression Curve / Tail / Filter Motion / Stereo Bloom layering, free ms lengths, and print/export-in-live are later phases); long pre-swells = long latency.
>
> **Phase 36 — Live Preverb 1.5 (controllable, musical, smoother).** Cubase test said the concept works but was *too loud and too rough*. This pass fixes the audio + adds control. **Gain staging** (`rebuildLiveIR`): the kernel is **L2 (energy) normalised** to `kLiveIRGain` (0.40) instead of peak-normalised — peak-norm let sustained material sum into a blast; L2 gives a consistent ~input-level wet. The reverb tail is shorter/more-damped (decay 0.22–0.55, was up to 0.90) so tonal sources don't ring/resonate loud, HPF ~150 Hz de-muds + LPF ~8 kHz de-harshes the kernel, shimmer is cut to 25 % (`applyReverb` gained a `shimmerScale`), and the envelope is a long squared fade-in + short landing taper — together killing the metallic/"8-bit" artifact. **Verified** (RenderTest §10): full-wet sustained 220 Hz tone peaks 0.56 (controlled, not destroying), **default 25 % Mix peaks 0.33 < the 0.5 dry source** (not louder than dry), no runaway, lead-in intact; 50/50 checks ×3 stable. **MIX** knob (`macroMix`, default 0.25, bottom-right of the macro strip) = global dry/wet blend, per-sample linear-ramped in `LivePreverb::process` (zipper-free). **Tempo-synced TIME** (`liveTimeIndex` 1/32…4 bars) + **FEEL** (`liveFeel` Straight ×1 / Dotted ×1.5 / Triplet ×2/3) replace the bar selector for live; `preverbLengthSamples()` = note×feel×tempo, DAW-locked (UI combos in the FX live strip; verified 1/4 Straight @120 = 510 ms latency). The live latency readout now uses the true processing SR (`getCurrentSampleRate()`), not the slot's. Defaults: Live OFF, Ghost Shimmer, Exp curve, 1/4 Straight, Mix 25 %, conservative wet, unity-safe. AU passes, DSP safety ALL SAFE, Capture/Print untouched. **Still open for the next 1.5 sub-pass**: the real-time Live Timeline scope (LAND marker + pre-swell region) and the full two-mode GUI restructure (de-emphasise Capture controls when Live is on).
>
> Internal model names are used throughout for code stability; **public UI display names are final** per Phase 11C.

---

## 1. Concept

Backtrace captures audio passing through the plugin, lets the user trim it with locators, reverse the selection, process it through character delays and reverbs, print the result into a new Vault slot, and export it — either ad-hoc (drag) or into a managed Sample Library for evidence / reuse on future tracks.

Core loop:

```
capture → waveform → trim → reverse → FX (delay/reverb) → print to new slot → export
                                                                   ↑
                                              import from Library ─┘  (closes the loop)
```

**v1 scope guard:** Backtrace records only the audio passing through this plugin instance, gated by host transport. No ARA, no DAW project-file or arbitrary-clip access.

---

## 2. Phase plan (stability priority)

User-authoritative ordering preserved; new subsystems inserted adjacently.

| # | Phase | Complexity | Risk | Notes |
|---|-------|-----------|------|-------|
| 1 | Capture into Vault (Manual) | MED | LOW | Reuses `CaptureEngine` |
| **1b** | **DAW Sync Capture** | HIGH | MED | Transport reader, capture modes, fallback, Locator Lock |
| 2 | Waveform display | MED | LOW | Peak-decimated render |
| 3 | Trim locators | MED | LOW | Left/right, samples + bars/beats |
| 4 | Reverse playback **+ Land at Source** | MED-HIGH | MED | Land at Source = reverse alignment |
| 5 | Print processed result → new Vault slot | LOW | LOW | Second capture pass |
| **5b** | **Export to Library** | LOW | LOW | Managed folder + sidecar metadata |
| **5c** | **Import from Library** | LOW | LOW | Library file → new Vault slot |
| 6 | Drag-and-drop export | MED | LOW | Ad-hoc 24-bit WAV |
| 7 | Pitch wheel | LOW | LOW | Reuses `ModernPitchShifter` |
| 8 | Delay machines (6) | MED-HIGH | MED | Mostly assembled from existing DSP |
| 9 | Reverb spaces (5) | V.HIGH | HIGH | New algorithm design |
| 10 | Routing modes (7) | HIGH | MED | Signal-flow matrix + visualizer |
| 11 | Presets & GUI polish | MED | LOW | |

---

## 3. UI face (v3)

Three-panel layout with a routing bar across the bottom.

```
┌──────────────────────────────────────────────────────────────┐
│ SOUND DETECTIVE 47 · BACKTRACE        presets   A/B   output  │
├────────────┬─────────────────────────────┬───────────────────┤
│ VAULT      │ WAVEFORM / TIMELINE         │ FX                │
│ ·session   │  [ waveform + A/B locators ]│  delay  ▸ cluster │
│ capture    │  reverse · grid sync align  │  reverb ▸ collapsed│
│ slots      │                             │  pitch   tone     │
│ print drag │                             │                   │
│ ⇅ export   │                             │                   │
│   import   │                             │                   │
│ LIBRARY    │                             │                   │
│ ·saved     │                             │                   │
├────────────┴─────────────────────────────┴───────────────────┤
│ ROUTING  [delay→reverb] [rev→dly] [parallel] … [full print]   │
│ in → reverse → delay·tape echo → reverb·plate → out           │
└──────────────────────────────────────────────────────────────┘
```

- **Left (Vault):** session capture lifecycle + persistent Library, joined by export/import transfer controls.
- **Center (Waveform):** the trimmed A→B selection is the only audio the FX chain sees; trimmed tails dimmed.
- **Right (FX):** Delay / Reverb are model selectors that **collapse to the chosen flavor and reveal that flavor's knob cluster below** (one FX focused at a time). Pitch + Tone always-visible knobs.
- **Bottom (Routing):** 7 mode chips + a live signal-flow strip that redraws per mode (Parallel splits, Feedback draws a loop-back).

### FX knob clusters (per flavor)

| Flavor | Cluster knobs |
|--------|---------------|
| Tape Echo | Time, Repeats, Feedback, Wow/Flutter, Tape Age |
| Magnetic Drum | Time, Repeats, Motor Drift, Heads, Saturation |
| Gritty Tape Slap | Time, Repeats, Drive, Splice, Mix |
| Digital Rack | Time, Repeats, Diffusion, Mod, Width/Duck |
| Pedal Digital | Time, Repeats, Mod, Mode (hold/freeze), Tone |
| Dirty Sampler | Time, Repeats, Bit-Depth, Sample-Rate, Jitter |

---

## 4. DAW Sync Capture (Phase 1b)

### 4.1 Host transport (`juce::AudioPlayHead::getPosition()`)

Read `Optional<PositionInfo>` every `processBlock`:

| Need | Accessor |
|------|----------|
| Tempo | `getBpm()` |
| Time signature | `getTimeSignature()` → `{numerator, denominator}` |
| Play / loop state | `getIsPlaying()`, `getIsLooping()` |
| Sample position | `getTimeInSamples()` |
| PPQ / bar start | `getPpqPosition()`, `getPpqPositionOfLastBarStart()` |
| Cycle / locators | `getLoopPoints()` → `Optional<LoopPoints>{ppqStart, ppqEnd}` |

`getLoopPoints()` returning `nullopt`/zero-length is the trigger for the fallback path.

### 4.2 Capture Mode selector

| Mode | Waits for | Stops at |
|------|-----------|----------|
| Manual | arm (records immediately) | second click / 60 s max |
| 1 / 2 / 4 Bars | next bar line | start + N bars |
| DAW Locators | playhead reaches `ppqStart` | playhead reaches `ppqEnd` |
| Next Cycle | loop wrap (ppq jumps back) | next wrap |
| Fallback 1/2/4/8 Bars | next bar line | start + N bars |

### 4.3 Capture state machine (audio thread, lock-free flags)

```
IDLE ──arm──▶ ARMED ──transport hits start──▶ CAPTURING ──hits end──▶ FINALIZE ──▶ IDLE
                │                                   │
                └──── disarm / stop ◀───────────────┘
```

`CaptureEngine.start()` resets `writePos` to 0; gate it on the computed start edge.

### 4.4 Sample-accurate edges (don't gate whole blocks)

```cpp
ppqPerSample  = bpm / (60.0 * sampleRate);
samplesToStart = (ppqStart - ppqAtBlockStart) / ppqPerSample;
if (samplesToStart >= 0 && samplesToStart < numSamples)
    capture.start();                              // begin this block
    capture.pushBlock(L + off, R + off, n - off); // off = round(samplesToStart)
```

Same math for the stop edge. Loop wrap = `ppqPosition` decreased since last block.

**Bars → samples:** `samplesPerBar = (60/bpm) × (4 × numerator / denominator) × sampleRate`.
At 120 BPM 4/4 → 2.0 s/bar → 4 bars = 352,800 samples @ 44.1k.

### 4.5 Fallback

If `getLoopPoints()` is unavailable: show `"host locator unavailable"`, present 1/2/4/8-bar chips, capture that musical length starting from the next bar line (or from arm, per setting).

### 4.6 Locator Lock

- Disarmed: panel tracks `getLoopPoints()` live (moving the loop updates the readout).
- On **arm**: snapshot `{ppqStart, ppqEnd}` so a later loop nudge can't corrupt the in-progress capture.
- Locked length (bars/beats/samples/bpm/timeSig) is written into the slot's sidecar metadata (§6).

### 4.7 Land at Source (pairs with Phase 4 reverse)

Reversing flips the buffer — the original first sample becomes the last. To make the reversed result land on the target, right-align the reversed buffer so its **final sample sits at `ppqEnd`** (front-padded with silence / pre-roll if shorter than the slot).

- Default target = right locator.
- Later option: target = last detected transient (for swells that crest *on* the downbeat rather than decay into it).

---

## 5. Sample Library (Phases 5b / 5c)

The Library is a persistent on-disk store; the Vault is the session buffer. Three distinct export paths:

| Action | Destination | Use |
|--------|-------------|-----|
| Drag | Ad-hoc (DAW track, desktop) | One-off into current session |
| Export | Managed Library folder (`~/SD47/Library/…`) | Evidence archive + sample collection |
| Import | Library file → **new** Vault slot | Reuse a past capture on a future track |

**Confirmed decisions:**
1. **Format:** 24-bit WAV — same writer as drag-out (one code path).
2. **Evidence sidecar:** every export writes a `.btmeta.json` alongside the WAV (§6).
3. **Import target:** always creates a **new** Vault slot (non-destructive); an imported slot is just another capture and can be re-trimmed/reversed/reprocessed.

---

## 6. Unified slot metadata — `*.btmeta.json`

One schema serves both Locator Lock (§4.6) and Library provenance (§5). Written next to the WAV on export; embedded in slot state in-session.

```json
{
  "name": "vox_tail_02",
  "created": "2026-06-24T14:32:00Z",
  "source": "Backtrace insert · track 3",
  "capture": {
    "mode": "daw_locators",
    "bpm": 120.0,
    "timeSig": [4, 4],
    "ppqStart": 32.0,
    "ppqEnd": 48.0,
    "bars": 4,
    "samples": 352800,
    "sampleRate": 44100,
    "locatorLocked": true
  },
  "edit": {
    "trimIn": 0,
    "trimOut": 352800,
    "reverse": true,
    "landAtSource": { "enabled": true, "target": "right_locator" },
    "pitchSemitones": 7
  },
  "fx": {
    "routing": "delay_to_reverb",
    "delay":  { "model": "tape_echo", "params": { "time": 375, "repeats": 0.65, "feedback": 0.40, "wow": 0.12, "tapeAge": 0.30 } },
    "reverb": { "model": "plate", "params": { } }
  }
}
```

This makes every Library file self-documenting — re-importing a slot restores its musical length, edit state, and the exact FX chain that produced it.

---

## 7. Delay machines (Phase 8)

6 flavors. **Most are assembled from existing `Common/DSP` modules**, which sharply lowers risk.

| Internal name | Inspiration (dev ref) | Reuses | New work |
|---------------|----------------------|--------|----------|
| `TapeEcho` | Tape / Space Echo | `MachineDrift` (wow/flutter), `Saturation`, `Filters` | delay line, multi-head taps |
| `MagneticDrum` | Magnetic drum / Binson | `MachineDrift` (motor drift), `Saturation` | multi-head drum pattern, metallic mvmt |
| `GrittyTapeSlap` | Maestro / Echoplex | `Saturation` (preamp drive), `MachineDrift` | splice bumps, slap feedback |
| `DigitalRack` | PCM rack | `Filters`, `StereoWidener` | diffusion, mod, ducking |
| `PedalDigital` | Boss DD | `Filters` | hold/freeze/mod modes, bright edge |
| `DirtySampler` | Dirty sampler / Vault | `BitReducer`, `SampleRateReducer`, `JitterEngine`, `Saturation` | crunchy feedback path |

Common params: Time, Repeats/Feedback (clamp ≤ 0.98), per-flavor Character, Mix. Soft-clip before the feedback tap; watch for NaN/Inf.

---

## 8. Reverb spaces (Phase 9)

5 flavors — highest-risk phase (new algorithms).

| Internal name | Inspiration | Algorithm sketch | Character knobs |
|---------------|-------------|------------------|-----------------|
| `StudioHall` | Lexicon hall/chamber | convolver IR + modulated tail | Decay, Damping, Diffusion, Width, Mix |
| `ModernSpace` | Bricasti | multi-stage Schroeder + late refl | Decay, Damping, Early/Late, Width, Mix |
| `Shimmer` | Shimmer | hall core + octave-shift tail (`ModernPitchShifter`) | Decay, Shimmer, Detune, Width, Mix |
| `SpringTank` | 626 spring | SVF spring model + `Saturation` | Decay, Tension, Splash, Drive, Mix |
| `Plate` | EMT 140 | modal / coupled-plate + input drive | Decay, Damping, Drive, Width, Mix |

Clamp decay→feedback so tails never grow. Gentle `tanh` saturation on early reflections only.

---

## 9. Routing modes (Phase 10)

| Mode | Flow |
|------|------|
| Delay → Reverb | in → reverse → delay → reverb → out |
| Reverb → Delay | in → reverse → reverb → delay → out |
| Parallel | in → reverse → {delay, reverb} → mix → out |
| Feedback Reverb → Delay | reverb tail fed back into delay input |
| Reverse before FX | reverse the source, then FX |
| Reverse tail | FX first, then reverse the processed tail |
| Full reverse print | entire trimmed region reversed → print |

Routing visualizer reflects the active mode. Feedback mode caps total loop gain ≤ 0.95 with a soft clipper.

---

## 10. Pitch wheel (Phase 7)

Reuses `ModernPitchShifter` (`setPitch(semitones)`, `setGlideTime`, `processSample(ch, in, ratio)`) and `PitchSliderLookAndFeel`. Semitone markers, interval detents, ±octave buttons; optional formant preservation for vocals. Range ±24 st + octave buttons. Reports latency to host.

---

## 11. Reuse inventory (existing `Common/`)

| Module | Used by |
|--------|---------|
| `Vault/CaptureEngine.h` | Capture, Print, Import (all slot writes) |
| `DSP/ModernPitchShifter.h` | Pitch wheel, Shimmer |
| `DSP/BitReducer.h`, `SampleRateReducer.h`, `JitterEngine.h` | Dirty Sampler delay |
| `DSP/MachineDrift.h` | Tape Echo, Magnetic Drum, Gritty Tape |
| `DSP/Saturation.h` | All delays, Spring/Plate reverbs |
| `DSP/Filters.h`, `StereoWidener.h`, `DCBlocker.h` | Digital Rack, Pedal, width/tone |
| `UI/PitchSliderLookAndFeel.h` | Pitch wheel |

---

## 12. Naming (internal → public TBD)

Public UI names must avoid trademarks; finalize before release.

| Internal | Public (TBD) |
|----------|--------------|
| TapeEcho / MagneticDrum / GrittyTapeSlap / DigitalRack / PedalDigital / DirtySampler | — |
| StudioHall / ModernSpace / Shimmer / SpringTank / Plate | — |

---

## 13. Suggested file layout

```
Plugins/Backtrace/
  CMakeLists.txt
  Source/
    PluginProcessor.{h,cpp}        // transport read, capture SM, routing
    PluginEditor.{h,cpp}           // 3-panel face + routing bar
    Audio/   ReverseEngine, RoutingMatrix, VaultSlotWriter, SyncCapture
    DSP/     DelayMachines/*, ReverbSpaces/*
    UI/      WaveformCanvas, RoutingVisualizer, SyncCapturePanel,
             LibraryBrowser, PitchWheelController
    Utilities/ PresetManager, LibraryStore (export/import + .btmeta.json)
Common/Vault/  WaveformDisplay.h, LocatorPair.h, SlotPrinter.h   // new
```
