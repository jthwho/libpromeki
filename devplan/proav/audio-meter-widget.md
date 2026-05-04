# `AudioMeterWidget` for the SDL viewer

Add graphical metering to the SDL viewer. Should be its own
`AudioMeterWidget` that takes input from the existing `AudioMeter` /
`AudioPeakRmsMeter` (Phase 4z) and displays the metrics.

Required:

- Clip marker (latched, with a configurable hold).
- Peak (instantaneous + peak-hold).
- RMS over the meter's configured window.

Stretch goal:

- LUFS / LKFS readout once the meter grows true loudness metrics.

## Configuration

- dBFS range and reference gridlines (the widget owns presentation;
  dBFS-vs-dBSPL offsets aren't our business).
- Layout — horizontal vs vertical meter strips.

## Update cadence

Update at the SDL video frame rate, not every audio chunk, so a slow
playback doesn't drown the GUI in redraw events. The meter itself
keeps accumulating per-chunk; the widget samples its output on each
paint.

## Tasks

- [ ] `AudioMeterWidget` derives from `SDLVideoWidget` (or sibling).
- [ ] Wires up a Signal/Slot connection from the audio side that
  pushes the meter's snapshot into the widget's render state.
- [ ] Configurable scale + layout via constructor / setters.
- [ ] Demo case in `tui-demo` / a new `sdl-meter-demo`.
