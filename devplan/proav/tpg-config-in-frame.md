# TPG: encode TPG config to frame metadata

Have `TpgMediaIO` serialise its configuration to JSON and stamp it
into the output frame's `Metadata`. Lets a downstream sink record
the TPG details into the output file, so the inverse Inspector
configuration can be reconstructed automatically when the file is
played back later.

Concretely: the same config that drives generation (pattern,
frequencies, channel modes, payload type, image-data parameters)
travels with each frame as a single `Metadata::TpgConfig` JSON
blob. The Inspector or a future `inspector-init-from-tpg` helper
reads it back and knows exactly what to validate.

## Tasks

- [ ] New `Metadata::TpgConfig` ID (`Variant::TypeJsonObject` or
  `String` carrying serialised JSON — pick whichever round-trips
  through DataStream cleanly).
- [ ] `TpgMediaIO` emits the JSON snapshot on the first frame and
  whenever a live reconfigure happens. Subsequent frames can elide
  it to keep frame metadata small (or stamp once per second as a
  recovery anchor — pick a policy).
- [ ] Inspector grows a "configure from TPG metadata" path:
  reads `Metadata::TpgConfig`, sets up the matching pattern decoders.
- [ ] Round-trip test: TPG → debugmedia → Inspector → assert the
  recovered config matches.
