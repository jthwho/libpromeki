# Raspberry Pi 4 Cross-Compile

**Status:** Sysroot + toolchain + full clean build shipped 2026-06-01.
**Hardware run:** pending (Pi 4 not yet on the rig).

Cross-compile preset and sysroot tooling for Raspberry Pi 4 (aarch64,
`bcm2835-codec` V4L2 m2m, dma-buf). Pairs with
[proav/v4l2-m2m-codec.md](../proav/v4l2-m2m-codec.md).

## What shipped

- `cmake/configs/cross-rpi4.cmake` — extends `cross-aarch64-linux.cmake`;
  pins `PROMEKI_TARGET_CPU=cortex-a72`, `PROMEKI_ENABLE_V4L2=ON`,
  `PROMEKI_ENABLE_DMABUF=ON`, `PROMEKI_CROSS_TOOLCHAIN_SUFFIX=-14`.
- `cmake/toolchains/aarch64-linux-gnu.cmake` — new
  `PROMEKI_CROSS_TOOLCHAIN_SUFFIX` knob (appended to `gcc`/`g++`).
- `scripts/build-rpi4-sysroot.sh` — Docker-based script: spins an arm64
  Debian Trixie container, installs dev packages, exports a rootfs to
  `/mnt/data/sysroots/rpi4`, relativizes symlinks. Re-runnable.
- `etc/udev-rules/99-promeki-dma-heap.rules` — grants `video` group
  `rw` on `/dev/dma_heap/*` so dma-buf allocation works without root.
  Install: `sudo cp etc/udev-rules/*.rules /etc/udev/rules.d/ && sudo
  udevadm control --reload-rules && sudo udevadm trigger --subsystem-match
  dma_heap`. Then `sudo usermod -aG video $USER` + re-login.

## Configuration

```sh
# Build the Trixie sysroot once
bash scripts/build-rpi4-sysroot.sh   # → /mnt/data/sysroots/rpi4

# Install GCC-14 cross toolchain
sudo apt install gcc-14-aarch64-linux-gnu g++-14-aarch64-linux-gnu

# Configure
cmake -B build-rpi4 \
      -DPROMEKI_CONFIG_FILE=cross-rpi4 \
      -DPROMEKI_SYSROOT=/mnt/data/sysroots/rpi4 \
      -DPROMEKI_STAGING_PREFIX=/mnt/data/sysroots/rpi4

# Build (from repo root — wrapper detects build-rpi4 via CWD trick)
cd build-rpi4 && build
```

**Target OS:** Raspberry Pi OS Trixie (Debian 13, GCC 14, glibc 2.41).
Bookworm (GCC 12) is NOT viable — `std::format` requires GCC ≥ 13.

## Open work

- [ ] **Run on actual Pi 4 hardware.** Confirm `libpromeki.so` loads cleanly;
  run `unittest-promeki` (requires copying the `build-rpi4/lib/` bundle to
  the Pi).
- [ ] **V4L2 M2M encoder smoke test.** `sudo modprobe bcm2835-codec` (if not
  auto-loaded), run `unittest-promeki -tc='*Vicodec*'` adapted for Pi device
  node (auto-probes `/dev/video*` so should just work).
- [ ] **dma-buf zero-copy verification.** Install udev rule + re-login, then
  confirm `DmaHeap::isAvailable()` returns true and the dma-buf dmabufbufferimpl
  subcases run rather than skip.
- [ ] **qemu-user CI lane.** Wire `CMAKE_CROSSCOMPILING_EMULATOR` to
  `qemu-aarch64-static` in the toolchain so `build check` runs the test suite
  under qemu on the x86 build host. See [infra/qemu-cross-testing.md](qemu-cross-testing.md)
  for the full plan.
- [ ] **Staging/deploy helper.** A `cmake --install build-rpi4 --prefix <path>`
  or `rsync` recipe for copying the bundle to the Pi over SSH.
