# Mastering research notes → design decisions

This file summarises the mastering practice and DSP literature behind each stage, and what Master Blaster takes from it.

## 1. Signal-chain order

A typical modern mastering chain runs: **corrective EQ → tonal/broad EQ → dynamics (multiband, then wideband "glue") → saturation/clipping → stereo/mono checks → true-peak limiter → dither**. Two common variations:

* Bass is made mono *before* the dynamics, so the compressors react to the same low end that reaches a mono system or vinyl. Master Blaster puts the stereo stage right after the EQ for that reason.
* Saturation or a soft clipper goes *before* the limiter, so the limiter has less peak-to-average range to deal with ("clip then limit"). The result is louder for the same amount of distortion.

## 2. EQ — topology-preserving SVF

* A direct-form biquad can be numerically noisy at low frequencies and behave badly when its coefficients move. The Cytomic/Simper **trapezoidal-integrated state-variable filter** avoids both problems. It is the standard "modern" mastering EQ core.
* Each band is a broad, low-Q move (shelves plus two bells with Q around 0.7). That matches the "small, wide moves" mastering rule.
* The low cut is a 4th-order Butterworth (24 dB/oct, Q = 0.541 / 1.307) at about 25 Hz. It removes sub-sonic energy that eats limiter headroom without adding anything audible.
* All parameters are smoothed. The coefficients update every 32 samples, which the SVF tolerates without zipper noise.

### Matched-response EQ (v1.1)

A bilinear-transform filter (the SVF included) maps the analog frequency axis onto 0…fs/2 with a tan() warp. Near Nyquist, bands and shelves get squeezed: an air shelf at 12–16 kHz at 44.1 kHz misses its analog curve by 1–4 dB. The four EQ bands now use **Vicanek's matched second-order design** (2016):

* poles by impulse invariance,
* the numerator solved so |H| equals the analog |H| at DC, at Nyquist and at the centre frequency.

Measured worst-case error from 20 Hz to 20 kHz is 0.3–0.4 dB, against 1–3.9 dB for the bilinear design. There is no oversampling and no latency. The low cut stays an SVF, because cramping doesn't affect it.

### Mid/Side processing

The EQ and multiband can work on L/R, on Mid only, or on Side only. In Mid or Side mode of the multiband, the untouched channel goes through the same allpass pair as the band sum (AP(f_lo)·AP(f_hi)), so M and S stay phase-coherent and the stereo image doesn't smear around the crossovers.

### Dynamic EQ

Each band computes y = x + (g − 1)·BP(x), where BP is a unity-peak, constant-Q state-variable band-pass. The detector uses the same band-pass output, so the band reacts only to what it treats. The gain g can change every sample with no coefficient recalculation, so there is no zipper noise and no latency.

## 3. Crossovers — Linkwitz-Riley 4th order

* LR4 low and high outputs add up to an allpass: flat magnitude, phase-coherent, **zero latency**. A linear-phase FIR crossover would add 20–80 ms of latency and pre-ringing, so this is the better choice for a low-latency master bus.
* In the 3-band split, the low band runs through the high crossover's matching allpass so the three bands sum flat. The tests measure a deviation below 0.001 dB.
* The mono-bass stage applies an LR4 high-pass to the **side** channel only, and runs mid through the matching allpass so M and S stay phase-aligned.

## 4. Compression — Giannoulis / Massberg / Reiss (JAES 2012)

The paper compares compressor topologies and recommends:

* a **log-domain** gain computer with a **quadratic soft knee**,
* the level detector placed **after** the gain computer (it smooths the gain-reduction signal),
* a **smooth branching peak detector** (separate attack and release one-poles, with no release drift).

Both of Master Blaster's compressors follow that design. The glue compressor also has:

* **Auto release:** two detectors run in parallel and the larger gain reduction wins. A fast one (release ÷ 3) catches transients; a slow one (attack × 15, release × 4) tracks sustained level. After a drum hit the gain recovers quickly, while dense passages settle into steady reduction instead of pumping.
* **Auto makeup:** adds back a 2.5 s average of the gain reduction, so you can set the compression by ear without the level changing.
* The same design as the multiband for everything else:

* a short RMS pre-average (5 ms) so it responds to energy rather than single peaks,
* a **sidechain high-pass** (90 Hz by default) so the kick and bass don't pump the whole mix,
* a parallel **mix** control (New-York-style blending).

## 5. Saturation — oversampled, low-latency

* A waveshaper produces harmonics above Nyquist that alias back down, so it has to be oversampled.
* **Polyphase IIR half-band filters** (Niemitalo / de Soras "hiir" elliptic allpass design) give over 100 dB of image rejection (the tests measure more than 160 dB) with a group delay of only a few samples. A linear-phase FIR with similar rejection needs tens of samples.
* The 4x structure is a steep 16-coefficient stage followed by a cheap 4-coefficient stage.
* The shaper is `tanh` normalised to unity small-signal gain. "Warmth" adds a DC bias, which produces even-order harmonics; a 2 Hz DC blocker removes the resulting offset.
* The dry/wet mix happens inside the oversampled domain, so it can't comb-filter.

## 6. True-peak limiting

* **ITU-R BS.1770-4 Annex 2** defines true peak as the peak of the signal oversampled at least 4x. Streaming encoders (AAC/Opus/MP3) can add further overshoot, so a −1 dBTP ceiling is the usual recommendation (Spotify, Apple Music/"Apple Digital Masters", AES TD1004).
* Master Blaster detects peaks with a **16x, 48-tap-per-phase** windowed-sinc interpolator on the sidechain only. A **parabolic fit** through the highest interpolated point and its neighbours estimates the peak between grid points. Measured worst-case underestimate is under 0.05 dB for content up to 0.45 fs (19.8 kHz at 44.1 kHz). The detector adds a +0.06 dB margin, so in the tests the output stays under the ceiling. Above 0.45 fs (the last ~2 kHz before Nyquist) the error grows quickly; real masters have very little energy there.
* Gain path (the construction from Signalsmith Audio's limiter write-up):
  1. required gain = ceiling / peak,
  2. instant attack, program-dependent exponential release,
  3. **sliding-window minimum** (hold) over the lookahead,
  4. **two cascaded box filters** whose lengths add up to lookahead + 1.

  The boxes give a smooth S-curve attack. Because the hold covers the averaging window, the gain is guaranteed to have reached its target by the time the delayed sample arrives, so there is no sample-peak overshoot. Both box filters rebuild their sums exactly, so floating-point error cannot drift.
* **Auto release** lengthens the release time while the average gain reduction stays high. Transients recover fast; dense passages recover slowly, which avoids pumping and low-frequency distortion.
* The lookahead (0.5–5 ms) is the main latency cost. At the 2 ms default the whole plugin has about 2.5 ms of latency.

## 6b. Honest A/B: level-matched bypass and delta

Listeners reliably prefer the louder of two versions, so an unmatched bypass mostly tells you "the limiter made it louder". The compare stage:

* delays the dry input by the plugin's latency,
* measures both the dry input and the output with **K-weighted, 3-second energy averages**, the same weighting as LUFS,
* scales the dry signal to the output's loudness.

**Bypass (matched)** plays that scaled dry signal. **Delta** plays *processed − matched dry*: only what the chain changes in tone and dynamics. A transparent chain's delta tests as silent (below −100 dBFS).

## 7. Loudness targets

| Destination | Typical integrated target | Peak |
|---|---|---|
| Spotify / YouTube / Tidal / Amazon | −14 LUFS (normalised down if louder) | −1 dBTP (−2 dBTP if louder than −14) |
| Apple Music | −16 LUFS (Sound Check) | −1 dBTP |
| EBU R128 broadcast | −23 LUFS | −1 dBTP |
| Club/CD "competitive" | −9 … −6 LUFS | −1 … −0.1 dBTP |

The meter follows BS.1770-4 / EBU Tech 3341:

* **K-weighting** filters computed for any sample rate,
* momentary (400 ms) and short-term (3 s) windows,
* **gated integrated** loudness: −70 LUFS absolute gate, −10 LU relative gate, 75 %-overlapped blocks.

The tests check a 997 Hz sine against the spec's reference values. **Auto Match** works in passes:

1. Measure the integrated loudness for 6 s.
2. Move the limiter drive by the error, overshooting slightly on later passes because a hard-working limiter gives back less than dB-for-dB.
3. Repeat until within 0.3 LU, or after 5 passes.

## 8. Dither

When the master is rendered to 16-bit, the plugin applies TPDF dither, optionally with first-order noise shaping (the error spectrum gets a (1 − z⁻¹) shape). Dither must be the **last** process. Leave it off if your DAW dithers on export, or if you render 24-bit or float.

## 9. Precision & hygiene

* The whole chain runs in **64-bit floating point**. The plugin also accepts 64-bit buffers from hosts that offer them.
* Each module runs continuously and is crossfaded by its on/off switch, so toggling is click-free and filter states stay warm. A fully bypassed chain is a **bit-exact** delayed copy of the input (tested).
* The audio thread never allocates. Every parameter pointer is cached once.

## References

* ITU-R BS.1770-4, *Algorithms to measure audio programme loudness and true-peak audio level*
* EBU R128 and EBU Tech 3341/3342
* D. Giannoulis, M. Massberg, J. D. Reiss, "Digital Dynamic Range Compressor Design — A Tutorial and Analysis", JAES 60(6), 2012
* A. Simper, "Linear Trapezoidal Integrated SVF" (Cytomic technical papers)
* L. de Soras, *hiir* polyphase IIR half-band library; O. Niemitalo, polyphase IIR half-band design notes
* S. Linkwitz / R. Riley, active crossover alignments (LR4)
* Signalsmith Audio, "Designing a limiter" (hold + cascaded box-filter gain smoothing)
* M. Vicanek, "Matched Second Order Digital Filters", 2016
* AES TD1004.1.15-10, *Recommendation for Loudness of Audio Streaming and Network File Playback*
