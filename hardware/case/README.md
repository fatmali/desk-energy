# Mochi: a soft pebble case for the Ideaspark ESP32 + 1.9" LCD

A two-tone pebble (ivory front, lavender back) that sits in a lavender cushion dock on the desk, or in a hook-over cradle on top of a monitor. Body is 76.3 x 44.0 x 24.3 mm with the default numbers.

## Files

| File | What it is |
|---|---|
| `mochi_case.scad` | Parametric source. Set `part` at the top to pick what to show or export |
| `stl/front.stl` | Front shell, already in print orientation |
| `stl/back.stl` | Back shell, already in print orientation |
| `stl/dock.stl` | Desk dock |
| `stl/monitor_mount.stl` | Monitor-top mount |
| `stl/fit_test.stl` | 20-minute coupon to check the board fit before printing shells |
| `stl/assembly.stl`, `stl/assembly_monitor.stl` | Full assemblies with a stand-in board (reference only, not for printing) |
| `viz/*.png` | Renders |

## Measure these first (they are guesses until you do)

No reference photos reached me, so these are defaults in section 2 of the .scad file. Board frame: look at the screen with the USB-C end pointing right; +bx is toward USB, +by is up, origin is the PCB centre.

| Parameter | Default | How to measure |
|---|---|---|
| `disp_stack_h` | 5.0 | PCB top surface to top of display glass |
| `aa_l`, `aa_w` | 42.7, 22.7 | Lit area of the screen (show a full white frame) |
| `aa_cx`, `aa_cy` | 0, 0 | Lit-area centre relative to PCB centre |
| `usb_cy` | 0 | USB-C centre across the short edge |
| `usb_cz` | +1.6 | USB-C centre above (+) or below (-) the PCB top surface. Underside mount is about -3.2 |
| `usb_overhang` | 1.0 | How far the USB shell passes the PCB edge |
| `btn_pos`, `btn_face` | (28, +/-9.5), front | BOOT and RESET centres, and whether they face the display side or the back |

The case centres the window on the body and shifts the PCB pocket to suit, so an off-centre screen still looks centred. The body grows automatically if `aa_cx` or `aa_cy` are non-zero. OpenSCAD prints a WARNING in the console if a change makes the USB opening break a face or lifts the PCB out of its pocket.

Then print `fit_test`, drop the board in, and confirm: PCB slides in without force, lit area is centred in the window, USB plug seats fully, a paperclip reaches both buttons.

## How the board is held

| Direction | Held by |
|---|---|
| Sideways | Pocket walls around the PCB edge (0.35 mm clearance per side) |
| Toward the front | Four corner pads under the PCB top surface. The glass floats 0.3 mm behind the bezel, so nothing presses on the display |
| Toward the back | Header pin tips rest on the back shell floor (0.2 mm gap) |

If you ever desolder the headers, set `board_total_h` to the new stack height and the case gets about 9 mm slimmer.

## Print settings

| Part | Orientation | Supports | Notes |
|---|---|---|---|
| Front shell | Face down | None | The bed surface becomes the front face. Textured PEI gives the best matte finish |
| Back shell | Outer face down | None | Vent slots and snap bumps print cleanly this way |
| Dock | Flat base down | None | |
| Monitor mount | On its side, as exported | None | One 24 mm internal bridge across the cradle |
| Fit test | Face down | None | |

General: 0.4 mm nozzle, 0.16 mm layers (0.2 is fine for dock and mount), 3 walls, 15% infill, matte PLA. Soft edges start at 45 degrees so nothing overhangs past that. Total filament is roughly 60 to 70 g for all four parts.

Tolerances to tune if your printer runs tight or loose:

| Parameter | Default | Change when |
|---|---|---|
| `pcb_clr` | 0.35 | PCB is tight (raise) or rattles (lower) |
| `lip_clr` | 0.15 | Shells will not close (raise to 0.2 to 0.25) |
| `snap_h` | 0.50 | Shells too hard to open (0.35) or fall apart (0.6) |
| `dock_clr` | 0.25 | Body is tight or loose in the dock or mount |
| `pin_gap` | 0.20 | Board rattles front to back (0.0 to 0.1) |

## Assembly

1. Lay the front shell face down on a soft cloth.
2. Lower the board in display first, USB end going in at a slight angle so the connector slips into its channel, then let the far end drop. The PCB should sit on the four corner pads.
3. Press the back shell on. The lip seats inside the front shell and the four bumps click.
4. Stand the body in the dock, or hook the mount over the monitor and stand it there. Plug in USB-C from the side.

To open: twist a coin or fingernail in the slot on the underside seam. No glue, no screws.

BOOT and RESET: press with a paperclip through the two pinholes beside the screen.

## Monitor mount

Set `mon_t` to your monitor's thickness at the top edge, `mon_lip_drop` to no more than its bezel height so the hook does not cover pixels, and `mon_lean_deg` for tilt. It hangs by gravity. A strip of thin foam tape inside the rear leg stops it sliding on glossy plastic.

## Known limits

- The USB opening is 13 x 7 mm so the cable's plastic boot can pass through the side wall. Unusually chunky cables need a larger `usb_w` / `usb_h`.
- With a display-side USB connector and a low `disp_stack_h`, the opening sits close to the front face. The script reports the remaining skin thickness.
- The stand-in board in the assemblies and renders is illustrative. Its component positions come from the parameters above, not from your actual board.
