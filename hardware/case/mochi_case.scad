// =====================================================================
//  MOCHI  -  a soft pebble enclosure for the Ideaspark ESP32 + 1.9" LCD
//  Parametric OpenSCAD model. All dimensions in mm.
//
//  Parts:  front shell, back shell, desk dock, monitor mount
//  Pick what to show/export with `part` below (or -D part="..." on CLI).
//
//  Frames used in this file
//   Case frame : X = width, Y = height (up when standing), Z = depth.
//                z = 0 is the FRONT face (the print-bed face of the
//                front shell), z grows toward the back of the device.
//   Board frame: look at the display with the USB-C end pointing RIGHT.
//                +bx = toward the USB end, +by = up, origin = PCB centre.
//                Every "VERIFY" position below is given in this frame.
// =====================================================================

part = "assembly"; // [assembly, assembly_monitor, exploded, front, back, dock, monitor_mount, fit_test, board, board_dark, board_metal, board_screen]

/* [1. Board - measured] */
pcb_l         = 63.8;   // PCB length
pcb_w         = 31.5;   // PCB width
pcb_t         = 1.6;    // bare PCB thickness
board_total_h = 20.0;   // display glass top to header pin tips

/* [2. Board - VERIFY with calipers before printing the shells] */
// PCB top surface to top of display glass
disp_stack_h  = 5.0;
// Display ACTIVE (lit) area, long side x short side
aa_l          = 42.7;
aa_w          = 22.7;
// Centre of the active area relative to PCB centre (board frame)
aa_cx         = 0.0;
aa_cy         = 0.0;
// USB-C: centre across the short edge (0 = centred)
usb_cy        = 0.0;
// USB-C: centre height relative to PCB TOP surface.
// Connector on the display side ~ +1.6, on the underside ~ -3.2
usb_cz        = 1.6;
// How far the USB-C shell sticks out past the PCB edge
usb_overhang  = 1.0;
// BOOT / RESET button centres (board frame) and which face they are on
btn_pos       = [[28.0, 9.5], [28.0, -9.5]];
btn_face      = "front"; // [front, back, none]

/* [3. Layout choices] */
usb_side      = "right"; // [right, left]   as seen from the front
lean_deg      = 14;      // desk viewing angle (lean back)
mon_lean_deg  = -4;      // monitor mount angle (negative = tips forward)
mon_t         = 15;      // monitor thickness at its top edge
mon_lip_drop  = 5;       // how far the front hook drops over the bezel
mon_leg_drop  = 32;      // rear leg length

/* [4. Fit and print tolerances] */
pcb_clr       = 0.35;    // gap around the PCB edge, per side
glass_gap     = 0.30;    // air gap between glass and bezel
pin_gap       = 0.20;    // gap between pin tips and back shell floor
lip_clr       = 0.15;    // gap between back-shell lip and front shell
dock_clr      = 0.25;    // gap between body and dock / mount cradle
snap_h        = 0.50;    // snap bump height (lower = looser)

/* [5. Shell geometry] */
front_t       = 2.0;     // bezel thickness in front of the glass
back_t        = 1.8;     // back face thickness
corner_r      = 12;      // outline corner radius
wall_corner   = 3.4;     // minimum wall at the PCB corners
edge_r        = 8;       // soft edge radius (starts at 45 deg: no supports)
back_depth    = 9;       // depth of the back shell
lip_out       = 1.6;     // outer wall left outside the lip
lip_t         = 1.4;     // lip thickness
lip_h         = 3.5;     // lip engagement
win_margin    = 0.6;     // window is this much larger than the active area
win_r         = 2.0;     // window corner radius
win_chamfer   = 1.2;     // bezel chamfer depth
usb_w         = 13.0;    // USB opening, sized for the cable boot
usb_h         = 7.0;
btn_d         = 2.2;     // pinhole diameter (paperclip)
rest_x        = 1.5;     // PCB rest pads: reach along the PCB length
rest_y        = 5.0;     // PCB rest pads: reach along the short edge
vent_rows_y   = [10, -1];
vent_n        = 7;
vent_pitch    = 5;
vent_len      = 7;
vent_wd       = 1.6;

/* [6. Dock and mount] */
dock_h        = 14;
dock_floor    = 1.6;
dock_front    = 9;       // dock extends this far in front of the body
dock_back     = 13;      // and this far behind it
dock_side     = 6;
dock_edge_r   = 5;
cradle_h      = 9;
cradle_wall   = 5;
mount_plate_t = 3;
mount_lip_t   = 2.4;
mount_leg_t   = 3.5;

/* [Hidden] */
$fn    = 72;
edge_n = 8;
eps    = 0.01;
s      = (usb_side == "right") ? 1 : -1;

// ---------------- derived ----------------
pocket_l = pcb_l + 2*pcb_clr;
pocket_w = pcb_w + 2*pcb_clr;
dcorner  = (corner_r - wall_corner) / sqrt(2);
half_w   = pocket_l/2 + abs(aa_cx) - dcorner + corner_r;
half_h   = pocket_w/2 + abs(aa_cy) - dcorner + corner_r;

z_glass   = front_t + glass_gap;
z_pcb_top = z_glass + disp_stack_h;
z_pcb_bot = z_pcb_top + pcb_t;
z_pins    = z_glass + board_total_h;
D         = z_pins + pin_gap + back_t;
zs        = D - back_depth;              // seam between the shells
z_rab     = zs - lip_h - 0.4;

// PCB centre in the case frame: the ACTIVE AREA is what gets centred.
pcx =  s*aa_cx;
pcy = -s*aa_cy;
function bm(b) = [pcx - s*b[0], pcy + s*b[1]];   // board frame -> case XY
ex    = -s;                                       // case X direction of the USB end
usb_y = pcy + s*usb_cy;
usb_z = z_pcb_top - usb_cz;

face_inset = edge_r*(1 - sin(45));
edge_hgt   = edge_r*cos(45);

echo(str("BODY  W x H x D = ", 2*half_w, " x ", 2*half_h, " x ", D));
echo(str("DIMS half_w=", half_w, " half_h=", half_h, " D=", D,
         " z_glass=", z_glass, " z_pcb_top=", z_pcb_top, " zs=", zs));
echo(str("USB slot spans z = ", usb_z-usb_h/2, " .. ", usb_z+usb_h/2,
         "  (front skin left: ", usb_z-usb_h/2, " mm)"));
if (usb_z - usb_h/2 < 0.8) echo("WARNING: USB opening breaks through the front face. Raise front_t or reduce usb_h.");
if (z_rab < z_pcb_bot + 0.8) echo("WARNING: PCB is not fully inside the locating pocket. Reduce back_depth.");
if (usb_z + usb_h/2 > D - back_t) echo("WARNING: USB opening reaches the back face.");

// ---------------- primitives ----------------
module outline(inset = 0) {
    offset(r = corner_r - inset)
        square([2*(half_w - corner_r), 2*(half_h - corner_r)], center = true);
}

function edge_pts() = [for (i = [0:edge_n]) let (th = 45 + 45*i/edge_n)
                         [edge_r*(cos(45) - cos(th)), edge_r*(1 - sin(th))]];

// The whole pebble. grow > 0 gives an enlarged copy (used to cut cradles).
module body(grow = 0) {
    hull() for (p = edge_pts()) {
        translate([0, 0, p[0] - grow])          linear_extrude(eps) outline(p[1] - grow);
        translate([0, 0, D - p[0] + grow - eps]) linear_extrude(eps) outline(p[1] - grow);
    }
}

module rrect(l, w, r) { offset(r = r) square([l - 2*r, w - 2*r], center = true); }

// ---------------- shared cutters ----------------
module usb_cut() {
    x0 = pcx + ex*(pcb_l/2 - 2);
    x1 = ex*(half_w + 2);
    hull() for (dy = [-1, 1])
        translate([min(x0, x1), usb_y + dy*(usb_w - usb_h)/2, usb_z])
            rotate([0, 90, 0]) cylinder(d = usb_h, h = abs(x1 - x0));
}

// channel so the connector's overhang can drop in from the back
module usb_relief() {
    xw = pcx + ex*pocket_l/2;
    d  = usb_overhang + 0.4;
    translate([min(xw, xw + ex*d), usb_y - 5.2, usb_z]) cube([d, 10.4, max(0.1, zs - usb_z)]);
}

module btn_holes(face) {
    if (btn_face == face) for (b = btn_pos) {
        p = bm(b);
        if (face == "front") {
            translate([p[0], p[1], -1]) cylinder(d = btn_d, h = front_t + 2, $fn = 32);
            translate([p[0], p[1], -eps]) cylinder(d1 = btn_d + 1.2, d2 = btn_d, h = 0.6, $fn = 32);
        } else {
            translate([p[0], p[1], D - back_t - 1]) cylinder(d = btn_d, h = back_t + 2, $fn = 32);
            translate([p[0], p[1], D - 0.6 + eps]) cylinder(d1 = btn_d, d2 = btn_d + 1.2, h = 0.6, $fn = 32);
        }
    }
}

snap_r  = 0.9;
snap_len = 8;
snap_xs = [-half_w*0.42, half_w*0.42];
snap_z  = zs - lip_h + 1.6;
module snap_shape(extra = 0) {
    for (sy = [-1, 1], x = snap_xs)
        translate([x, sy*(half_h - lip_out - lip_clr - (snap_r - snap_h)), snap_z])
            hull() for (dx = [-1, 1]) translate([dx*snap_len/2, 0, 0]) sphere(r = snap_r + extra, $fn = 24);
}

// ---------------- front shell ----------------
module pcb_rests() {
    // four pads the PCB top surface sits on, in the pocket corners
    for (sx = [-1, 1], sy = [-1, 1])
        translate([pcx + sx*(pocket_l/2 - (rest_x + pcb_clr)/2 + eps),
                   pcy + sy*(pocket_w/2 - (rest_y + pcb_clr)/2 + eps),
                   front_t - eps + (z_pcb_top - front_t)/2])
            cube([rest_x + pcb_clr, rest_y + pcb_clr, z_pcb_top - front_t], center = true);
}

module window() {
    l = aa_l + 2*win_margin;
    w = aa_w + 2*win_margin;
    translate([0, 0, -1]) linear_extrude(front_t + 2) rrect(l, w, win_r);
    hull() {
        translate([0, 0, -eps]) linear_extrude(eps) rrect(l + 2*win_chamfer, w + 2*win_chamfer, win_r + win_chamfer);
        translate([0, 0, win_chamfer]) linear_extrude(eps) rrect(l, w, win_r);
    }
}

module front_shell() {
    difference() {
        intersection() {
            body();
            translate([-500, -500, 0]) cube([1000, 1000, zs]);
        }
        difference() {                                   // PCB pocket
            translate([pcx, pcy, front_t]) linear_extrude(D) rrect(pocket_l, pocket_w, 0.6);
            pcb_rests();
        }
        translate([0, 0, z_rab]) linear_extrude(D) outline(lip_out);   // seat for the lip
        window();
        usb_cut();
        usb_relief();
        btn_holes("front");
        snap_shape(0.12);
    }
}

// ---------------- back shell ----------------
module lip() {
    z0 = zs - lip_h;
    difference() {
        union() {
            hull() {
                translate([0, 0, z0]) linear_extrude(eps) outline(lip_out + lip_clr + 0.5);
                translate([0, 0, z0 + 0.8]) linear_extrude(eps) outline(lip_out + lip_clr);
            }
            translate([0, 0, z0 + 0.8]) linear_extrude(lip_h - 0.8 + eps) outline(lip_out + lip_clr);
        }
        translate([0, 0, z0 - 1]) linear_extrude(lip_h + 2) outline(lip_out + lip_clr + lip_t);
    }
}

module vents() {
    for (y = vent_rows_y, i = [0:vent_n - 1])
        translate([(i - (vent_n - 1)/2)*vent_pitch, y, D - back_t - 1])
            linear_extrude(back_t + 2) rrect(vent_wd, vent_len, vent_wd/2 - eps);
}

module back_shell() {
    difference() {
        union() {
            intersection() {
                body();
                translate([-500, -500, zs]) cube([1000, 1000, D]);
            }
            lip();
            snap_shape();
        }
        translate([0, 0, zs - lip_h - 1])
            linear_extrude(D - back_t - (zs - lip_h - 1)) outline(lip_out + lip_clr + lip_t);
        vents();
        usb_cut();
        usb_relief();
        btn_holes("back");
        // coin / fingernail slot on the underside to pop the shells apart
        translate([-6, -half_h - 1, zs - eps]) cube([12, 2.4, 1.5]);
    }
}

// ---------------- stand-in board (for checking and renders only) ----------------
module board_dark() {
    translate([pcx, pcy, z_pcb_top]) linear_extrude(pcb_t) rrect(pcb_l, pcb_w, 1);
    // display module
    translate([0, 0, z_glass]) linear_extrude(disp_stack_h) rrect(aa_l + 8.5, aa_w + 3.1, 1.5);
    // header strips
    for (sy = [-1, 1]) translate([pcx - 24.1, pcy + sy*(pcb_w/2 - 1.6) - 1.25, z_pcb_bot]) cube([48.2, 2.5, 2.5]);
    // buttons
    if (btn_face != "none") for (b = btn_pos) { p = bm(b);
        translate([p[0] - 1.5, p[1] - 2, btn_face == "back" ? z_pcb_bot : z_pcb_top - 1.8]) cube([3, 4, 1.8]); }
}
module board_metal() {
    for (sy = [-1, 1], i = [0:18])
        translate([pcx + (i - 9)*2.54 - 0.32, pcy + sy*(pcb_w/2 - 1.6) - 0.32, z_pcb_bot]) cube([0.64, 0.64, z_pins - z_pcb_bot]);
    // USB-C
    xe = pcx + ex*(pcb_l/2 + usb_overhang);
    translate([min(xe, xe - ex*7.3), usb_y - 4.47, usb_z - 1.6]) cube([7.3, 8.94, 3.2]);
    // ESP32 module can (illustrative position)
    translate([pcx - ex*8 - 9, pcy - 8, z_pcb_bot]) cube([18, 16, 3.1]);
}
module board_screen() {
    translate([0, 0, z_glass - 0.05]) linear_extrude(0.05) rrect(aa_l, aa_w, 1.5);
}
module board() {
    color("#1d2330") board_dark();
    color("#c9c9c9") board_metal();
    color("#7ad1c0") board_screen();
}

// ---------------- desk dock ----------------
dock_w  = 2*half_w + 2*dock_side;
dock_y0 = -(D/2 + dock_back);
dock_y1 =  (D/2 + dock_front);
dock_r  = 14;
desk_z0 = dock_floor + (D/2)*sin(abs(lean_deg));

module place_desk() {
    translate([0, 0, desk_z0]) rotate([lean_deg, 0, 0]) translate([0, D/2, 0])
        rotate([90, 0, 0]) translate([0, half_h, 0]) children();
}

module dock_outline(inset = 0) {
    translate([0, (dock_y0 + dock_y1)/2])
        offset(r = dock_r - inset) square([dock_w - 2*dock_r, (dock_y1 - dock_y0) - 2*dock_r], center = true);
}

module dock() {
    difference() {
        hull() {
            linear_extrude(eps) dock_outline(0.8);
            translate([0, 0, 0.8]) linear_extrude(eps) dock_outline(0);
            for (i = [0:edge_n]) let (th = 90*i/edge_n)
                translate([0, 0, dock_h - dock_edge_r + dock_edge_r*sin(th) - eps])
                    linear_extrude(eps) dock_outline(dock_edge_r*(1 - cos(th)));
        }
        place_desk() body(dock_clr);
    }
}

// ---------------- monitor mount ----------------
mount_w  = 2*half_w + 10;
cradle_d = D + 2*cradle_wall;
cradle_yc = mount_lip_t - cradle_d/2;
mon_z0   = mount_plate_t + dock_floor + (D/2)*sin(abs(mon_lean_deg));

module place_monitor() {
    translate([0, cradle_yc, mon_z0]) rotate([mon_lean_deg, 0, 0]) translate([0, D/2, 0])
        rotate([90, 0, 0]) translate([0, half_h, 0]) children();
}

module mount_profile() {
    y_back = min(-mon_t - mount_leg_t, cradle_yc - cradle_d/2);
    offset(r = 1.2) offset(r = -2.2) offset(r = 1) union() {
        translate([y_back, 0]) square([mount_lip_t - y_back, mount_plate_t]);            // top plate
        translate([0, -mon_lip_drop]) square([mount_lip_t, mon_lip_drop + 1]);            // front hook
        translate([-mon_t - mount_leg_t, -mon_leg_drop]) square([mount_leg_t, mon_leg_drop + 1]); // rear leg
        translate([cradle_yc - cradle_d/2, mount_plate_t - 1]) square([cradle_d, cradle_h + 1]);  // cradle
    }
}

module monitor_mount() {
    difference() {
        rotate([90, 0, 90]) linear_extrude(mount_w, center = true) mount_profile();
        place_monitor() body(dock_clr);
    }
}

// ---------------- fit-test coupon (prints in ~20 min) ----------------
module fit_test() {
    intersection() {
        front_shell();
        translate([pcx, pcy, 0]) linear_extrude(z_pcb_bot + 1.5)
            rrect(pocket_l + 2*(half_w - pocket_l/2 - abs(aa_cx)) , pocket_w + 5, 3);
    }
}

// ---------------- assemblies ----------------
c_ivory = "#F3EADB";
c_lav   = "#BFB0E3";

module device(explode = 0) {
    color(c_ivory) translate([0, 0, -explode]) front_shell();
    board();
    color(c_lav) translate([0, 0, explode]) back_shell();
}

if (part == "assembly")         { place_desk() device(); color(c_lav) dock(); }
if (part == "assembly_monitor") { place_monitor() device(); color(c_lav) monitor_mount();
                                  %translate([-60, -mon_t, -60]) cube([120, mon_t, 60]); }
if (part == "exploded")         { device(22); }
// printable parts are emitted already in their print orientation
if (part == "front")            front_shell();
if (part == "back")             translate([0, 0, D]) rotate([180, 0, 0]) back_shell();
if (part == "dock")             dock();
if (part == "monitor_mount")    translate([0, 0, mount_w/2]) rotate([0, 90, 0]) monitor_mount();
if (part == "fit_test")         fit_test();
if (part == "board")            board();
if (part == "board_dark")       board_dark();
if (part == "board_metal")      board_metal();
if (part == "board_screen")     board_screen();
// raw (case-frame) copies used by the render script
if (part == "raw_back")         back_shell();
if (part == "raw_mount")        monitor_mount();
