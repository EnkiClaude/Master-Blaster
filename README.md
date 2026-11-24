# Master Blaster

A one-stop **mastering chain** for your master bus: VST3 and AU (plus a standalone app). It runs in 64-bit floating point, its true-peak limiter does not overshoot its ceiling, and the latency is about **2.5 ms** with default settings.

![UI](docs/ui.png)

## Signal chain

```
Input → EQ → Stereo → Multiband → Dynamic EQ → Glue comp → Saturate (4x OS) → Drive → Clipper (4x OS) → True-peak limiter → Compare/Monitor → Dither
```

| Stage | What it does | Technique |
|---|---|---|
| **Input / EQ** | input trim, 24 dB/oct low cut, low shelf, two bells, "Air" shelf; **Stereo / Mid / Side** mode | Vicanek matched-response biquads: the analog curve holds right up to 20 kHz (no bilinear cramping) |
| **Stereo** | width (M/S), mono bass below a chosen frequency | LR4 high-pass on the side channel, matching allpass on mid (phase-coherent) |
| **Multiband** | 3-band compressor with per-band gain and GR meters; **Stereo / Mid / Side** mode | LR4 crossovers that sum flat (zero latency); in M or S mode the untouched channel gets the matching allpass |
| **Dynamic EQ** | two bells that cut only while their band is loud (de-harsh, de-ess, tame boom) | constant-Q SVF band-pass, x + (g − 1)·BP(x): per-sample gain with no coefficient recalculation |
| **Glue Comp** | stereo-linked bus compressor, sidechain HPF, parallel mix, **auto release**, **auto makeup** | Giannoulis/Reiss soft-knee feed-forward design; dual-envelope program-dependent release |
| **Saturate** | tanh saturation with even-harmonic "Warmth" | 4x polyphase-IIR oversampling (>100 dB image rejection, a few samples of delay) |
| **Clipper** | soft clipper before the limiter ("clip then limit"); level relative to the ceiling, knee softness | 4x polyphase-IIR oversampled tanh-knee clipper; in the tests it cuts the limiter's average gain reduction by about a third |
| **Limiter** | brickwall limiter, ceiling in dBTP, auto release, lookahead 0.5–5 ms | 16x true-peak detection with parabolic refinement (<0.05 dB error to 0.45 fs); hold plus cascaded-box gain smoothing |
| **Compare** | **Bypass**: the dry input at the processed loudness. **Delta**: only what the processing changes | latency-aligned dry path, K-weighted 3 s loudness match |
| **Monitor** | **Mono** (L+R)/2 for mono-compatibility checks, **Side** (L−R)/2 to hear the stereo-only content | monitoring only; the meters keep reading the stereo output |
| **Dither** | 24-bit or 16-bit TPDF, optional noise shaping | the last stage in the chain |
| **Meters** | momentary, short-term and integrated LUFS, true-peak max, **loudness range (LRA)**, **peak-to-loudness ratio (PLR)**, **60 s loudness history**, GR per stage, correlation | ITU-R BS.1770-4 / EBU R128 / EBU Tech 3342 |
| **Analyzer** | long-term output spectrum (+3 dB/oct tilt), EQ curve, **reference-track overlay** (loudness-aligned) | 8192-point FFT, 1/6-octave smoothing |

Workflow tools:
* **Presets:** 11 factory starting points (Streaming −14, Apple −16, Loud EDM, Hip-Hop, Rock, Gentle Acoustic, Podcast −16, Broadcast −23, De-harsh, Wide & Bright…), plus saving your own. Factory presets also appear as host programs.
* **A/B/C/D snapshots:** switch between whole settings instantly. They're saved with your session.
* **Undo/Redo** for every change, including preset loads and Auto Match.
* **Auto Match:** measures integrated loudness, adjusts the limiter drive, and repeats until the output is on target.
* Tooltips on every control. Click a knob's value to type one ("3.5k" works). Double-click a knob to reset it. Switching a module off dims its controls.

[`docs/RESEARCH.md`](docs/RESEARCH.md) explains the mastering practice and DSP research behind each choice.

## Quick workflow

1. Put Master Blaster last on your master bus and pick a preset close to your genre or destination.
2. Make broad EQ moves (±1–2 dB). Use the analyzer's **reference overlay** to compare your tonal balance with a commercial track. Use **Mid/Side** mode for centre-only or sides-only moves.
3. Glue comp: aim for 1–3 dB of gain reduction. **Auto Gain** keeps the level steady while you set it.
4. Set **Target** (−14 LUFS for streaming), play a loud section and press **Auto Match**.
5. Check your decisions with **Bypass (matched)** and **Delta**. Because the bypass plays at the same loudness, "louder sounds better" can't fool you.
6. Keep the ceiling at **−1 dBTP** for streaming/lossy formats. Turn dither on only when rendering to 16-bit, and only if your DAW isn't already dithering.

**CPU:** a module that is switched off costs nothing, and the true-peak detectors use SIMD and skip work whenever the signal is provably below the level they care about. On the test machine (one core of a 2.3 GHz Xeon, 48 kHz) the defaults use about 2.5% and everything on about 5.5%; v1.1 used about 9.5% either way.

Every module's **ON** switch crossfades, so you can toggle while playing. With every module off, the plugin outputs a bit-exact, latency-compensated copy of its input.

## Building

You need CMake ≥ 3.22 and a C++17 compiler. JUCE 8.0.4 is fetched automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release      # DSP test suite
```

* **macOS (VST3 + AU, universal binary):** add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. The bundles land in `build/MasterBlaster_artefacts/Release/{VST3,AU}`. Copy them to `~/Library/Audio/Plug-Ins/VST3` and `~/Library/Audio/Plug-Ins/Components`.
* **Windows:** copy the `.vst3` to `C:\Program Files\Common Files\VST3`.
* **Linux:** copy the `.vst3` to `~/.vst3`.
* If you have a local JUCE checkout, pass `-DJUCE_DIR=/path/to/JUCE`.

**Releases:** pushing a version tag (for example `git tag v1.2.0 && git push origin v1.2.0`) builds all three platforms, runs every check, and publishes a GitHub Release with a zip per platform.

**Prebuilt binaries:** the GitHub Actions workflow builds macOS (VST3 + AU, universal), Windows and Linux. It runs the tests, pluginval and `auval`, and uploads the bundles as workflow artifacts. The builds are unsigned. On macOS, after copying the plugins, run:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Master Blaster.component" ~/Library/Audio/Plug-Ins/VST3/"Master Blaster.vst3"
```

## Tests

* `tests/dsp_tests.cpp` runs without JUCE. It checks:
  * half-band image rejection and oversampler passband flatness,
  * that the LR4 3-band sum is flat,
  * LUFS accuracy against BS.1770 reference signals at 44.1/48/96 kHz,
  * limiter ceiling compliance, checked independently with 16x interpolation,
  * that the limiter latency matches what the plugin reports,
  * that the bypassed chain is bit-exact,
  * full-chain CPU use.
* The DSP suite also covers the newer features:
  * the matched EQ against its analog prototype, up to 20 kHz,
  * true-peak detector accuracy against an exact analytic signal,
  * dynamic EQ behaviour (cuts loud in-band, leaves quiet and off-band material alone),
  * mono input stays mono in Mid/Side modes,
  * the delta of a transparent chain is silent,
  * the level-matched bypass is within 0.5 LU of the processed output,
  * auto makeup holds the loudness.
* `tools/host_check` (`-DMB_BUILD_HOST_CHECK=ON`) builds two tools:
  * `mb_host_check` loads the built VST3 the way a DAW does. It processes audio at several sample rates and irregular block sizes, round-trips the state, and checks that the reported latency changes with the lookahead.
  * `mb_ui_snapshot` checks presets, snapshots, undo, user preset save/load, state restore and reference-track analysis, then renders the UI.

## Layout

```
Source/dsp/        framework-free DSP (Filters, Oversampling, Dynamics, Loudness, MasterChain)
Source/            JUCE processor, parameters, editor
tests/             DSP test suite
tools/host_check/  VST3 host check and UI snapshot tools
docs/              research notes and screenshot
```
