# Maze Chase Game on DE1-SoC

Final project for **CSEE 4840 - Embedded Systems**, implementing a real
time chase game on the Terasic DE1-SoC board.  The directory layout
mirrors `lab3/` exactly so the build flow is unchanged.

```
final_project/
├── final_project-hw/   # FPGA: Verilog, .qsys, Makefile, ROM .mif/.hex
└── final_project-sw/   # Linux: kernel driver + user-space game loop
```

The hardware implements a 640x480 VGA maze renderer (`game_peripheral`)
that exposes 16 x 32-bit registers over the Lightweight HPS-to-FPGA
bridge.  The HPS-side kernel driver (`game.c`) wraps those registers
behind a `/dev/game` misc device, and the user-space application
(`game_app.c`) runs the actual game loop: 60 Hz VSYNC poll, 5 Hz BFS
ghost AI, USB keyboard input, and win/loss handling.

## Build flow

### Hardware

```
cd final_project-hw
python3 gen_mif.py        # writes the four ROM init files + maze_data.h
make qsys                 # generate soc_system.qip from soc_system.qsys
make project              # bootstrap Quartus project
make quartus              # full compile -> soc_system.sof
make rbf                  # convert to soc_system.rbf for SD card
make dtb                  # generate soc_system.dtb
```

### Software (target board)

```
cd final_project-sw
make                      # builds game.ko + game_app
insmod game.ko
./game_app
```

## Mapping to the design document

| Section in design doc | File |
| --- | --- |
| §2 Top-level block diagram | `final_project-hw/soc_system_top.sv` + `soc_system.qsys` |
| §3 Display geometry / compositor | `compositor` module in `game_peripheral.sv` |
| §4.1 Maze representation | `gen_mif.py` -> `maze_data.h` |
| §4.2 Game main loop | `game_app.c` `main()` |
| §4.3 BFS ghost AI | `bfs_next_step()` in `game_app.c` |
| §4.4 Tile activation | `tile_timer` module in `game_peripheral.sv` |
| §4.5 Win/Loss | `check_win_loss()` in `game_app.c` |
| §4.6 VGA rendering pipeline | `compositor`, `maze_rom`, `sprite_rom` |
| §6 Register map | `avalon_slave_0` interface in `game_peripheral.sv` |
| §7 Module interfaces | submodules in `game_peripheral.sv` |
| §8 C software interface | `game.h`, `game.c`, `game_app.c` |
| §9 USB HID input | `open_keyboard()`, `poll_input()` in `game_app.c` |
| §10 Clock domains / CDC | `vsync_cdc` module in `game_peripheral.sv` |
| §12 Memory init files | `gen_mif.py` |

## Notes for graders

* The design document specifies `.mif` ROM init files; `gen_mif.py`
  emits both `.mif` (for Quartus altsyncram instantiation) and `.hex`
  (used by the SystemVerilog `$readmemh` so the same RTL is portable
  to simulators that don't accept `.mif`).
* The HPS application uses ioctls instead of raw `mmap(/dev/mem)`
  because that matches the lab3 driver pattern used throughout the
  course.  Performance is identical for the tiny per-frame transfers
  this game needs.
* `vsync_cdc` brings the pixel-clock VSYNC pulse safely into the
  50 MHz clk domain so the `tile_timer` count is jitter-free.
