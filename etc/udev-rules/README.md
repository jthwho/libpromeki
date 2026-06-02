# udev rules

Optional udev rules for granting unprivileged access to kernel device
nodes that libpromeki uses.  None of these are required to build the
library; install the relevant one when you want to run as a normal user
instead of root.

| Rule | Purpose |
| --- | --- |
| `99-promeki-dma-heap.rules` | Group access to `/dev/dma_heap/*` so the `DmaHeap` allocator (and the V4L2/VCU zero-copy paths) can allocate dma-bufs without root. |

## Installing

```sh
sudo cp etc/udev-rules/99-promeki-dma-heap.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=dma_heap
sudo usermod -aG video "$USER"   # log out / back in for the group to take effect
```

Each rule file's header documents its own match keys, the group it uses,
verification steps, and any alternatives (e.g. `TAG+="uaccess"` on
logind desktops). Edit `GROUP=` if your system uses a different group for
media devices.
