// kmswitch enclosure - parametric, OpenSCAD
// Render:  openscad -D 'variant="A"' -D 'part="bottom"' -o bottom.stl kmswitch_case.scad
// variant: "A" = RP2040-Zero carrier (80 x 48 mm), "B" = 2x RP2040 chip-down (80 x 54 mm)
// part:    "bottom", "lid", "assembly" (preview with a dummy board)
//
// Coordinates follow the KiCad board: x to the right, y measured from the BACK edge
// (the edge with the three USB-C ports). The front edge holds microSD and USB-A.

variant = "A";
part = "assembly";
show_board = true;          // dummy board in the assembly preview

/* [Board] */
board_w  = 80;
board_h  = variant == "A" ? 48 : 54;
board_t  = 1.6;
hole_in  = 3.5;               // mounting hole distance from the edges
hole_xy  = [[hole_in, hole_in], [board_w - hole_in, hole_in],
            [hole_in, board_h - hole_in], [board_w - hole_in, board_h - hole_in]];

/* [Case] */
wall      = 2.0;
floor_t   = 2.0;
lid_t     = 2.0;
gap       = 0.5;               // board to inner wall
standoff  = 5.0;               // board underside above the floor (room for THT leads)
above     = variant == "A" ? 11.0 : 9.0;   // room above the board top (modules on headers / 100 uF cap)
corner_r  = 3.0;
lip       = 1.6;               // lid lip depth
fit       = 0.2;               // lid lip clearance
light_win = 0.6;               // lid thickness over LED windows (print in white/translucent)

/* [Screws: M2.5 self-tapping into the standoffs] */
screw_d   = 2.8;
pilot_d   = 2.2;
head_d    = 5.2;
head_h    = 1.6;
post_d    = 5.6;

inner_w = board_w + 2 * gap;
inner_d = board_h + 2 * gap;
outer_w = inner_w + 2 * wall;
outer_d = inner_d + 2 * wall;
tray_h  = floor_t + standoff + board_t + above;
board_z = floor_t + standoff;               // board underside
top_z   = board_z + board_t;                // board top surface

// board point (x, y from back edge) -> case coordinates (front edge at y = 0)
function bx(x) = wall + gap + x;
function by(y) = wall + gap + (board_h - y);

// ---- port definitions: [x, z above board top, width, height]
zero_usb_z = 5.2;    // RP2040-Zero USB-C centre above the carrier (2.54 mm header + 1 mm PCB + 1.63 mm)
usbc_z     = 1.63;   // board-mounted USB-C centre
back_ports = variant == "A"
    ? [[17, zero_usb_z, 12.5, 7.0], [36, usbc_z, 12.5, 7.0], [63, zero_usb_z, 12.5, 7.0]]
    : [[17, usbc_z, 12.5, 7.0],    [36, usbc_z, 12.5, 7.0], [63, usbc_z, 12.5, 7.0]];
back_labels = [["PC 1", 17], ["5V", 36], ["PC 2", 63]];
usba  = [34, 3.6, 14.6, 7.6];     // USB-A host port (front)
sd    = [17, 0.9, 13.0, 2.6];     // microSD slot (front)

// lid features: [x, y, d]
led_windows = variant == "A"
    ? [[17, 12, 8], [63, 12, 8]]            // RP2040-Zero WS2812 shines through a thin window
    : [[28.5, 28.5, 3], [74.5, 28.5, 3]];   // green LEDs on GP25
button_holes = variant == "A"
    ? [[11.3, 2.95], [22.7, 2.95], [57.3, 2.95], [68.7, 2.95]]   // BOOT / RESET of each RP2040-Zero, next to its USB-C
    : [[7.5, 24], [7.5, 28.5], [53.5, 24], [53.5, 28.5]];

module rbox(w, d, h, r) {
    hull() for (x = [r, w - r], y = [r, d - r]) translate([x, y, 0]) cylinder(r = r, h = h, $fn = 40);
}

module port_cut(x, zc, w, h, face) {
    // face = "back" (y = outer_d) or "front" (y = 0); rounded rectangle through the wall
    yy = face == "back" ? outer_d - wall - 1 : -1;
    translate([bx(x) - w / 2, yy, top_z + zc - h / 2])
        hull() for (dx = [1, w - 1], dz = [1, h - 1])
            translate([dx, 0, dz]) rotate([-90, 0, 0]) cylinder(r = 1, h = wall + 2, $fn = 24);
}

module bottom() {
    difference() {
        rbox(outer_w, outer_d, tray_h, corner_r);
        translate([wall, wall, floor_t]) rbox(inner_w, inner_d, tray_h, max(corner_r - wall, 0.5));
        for (p = back_ports) port_cut(p[0], p[1], p[2], p[3], "back");
        port_cut(usba[0], usba[1], usba[2], usba[3], "front");
        port_cut(sd[0], sd[1], sd[2], sd[3], "front");
        // finger recess around the microSD slot
        translate([bx(sd[0]), -0.01, top_z + sd[1]]) scale([1, 0.35, 0.6]) rotate([-90, 0, 0])
            cylinder(r = 8, h = 3, $fn = 60);
        // labels on the back wall
        for (l = back_labels)
            translate([bx(l[1]), outer_d - 0.6, board_z - 2.6])
                rotate([90, 0, 180]) linear_extrude(1) text(l[0], size = 2.6, halign = "center", valign = "center",
                                                            font = "Liberation Sans:style=Bold");
        // ventilation slots in the floor
        for (i = [0 : 5]) translate([outer_w / 2 - 15 + i * 6, outer_d / 2 - 8, -1]) rbox(2, 16, floor_t + 2, 0.9);
    }
    // standoffs
    for (h = hole_xy) translate([bx(h[0]), by(h[1]), 0]) difference() {
        cylinder(d = post_d, h = board_z, $fn = 40);
        translate([0, 0, floor_t]) cylinder(d = pilot_d, h = board_z, $fn = 24);
    }
}

module lid() {
    difference() {
        union() {
            rbox(outer_w, outer_d, lid_t, corner_r);
            // lip that sits inside the walls
            translate([wall + fit, wall + fit, -lip])
                difference() {
                    rbox(inner_w - 2 * fit, inner_d - 2 * fit, lip, max(corner_r - wall, 0.5));
                    translate([1.2, 1.2, -1]) rbox(inner_w - 2 * fit - 2.4, inner_d - 2 * fit - 2.4, lip + 2, 0.5);
                }
            // posts that press the board onto the standoffs
            for (h = hole_xy) translate([bx(h[0]), by(h[1]), -above]) cylinder(d = post_d, h = above, $fn = 40);
        }
        for (h = hole_xy) translate([bx(h[0]), by(h[1]), 0]) {
            translate([0, 0, -above - 1]) cylinder(d = screw_d, h = above + lid_t + 2, $fn = 24);
            translate([0, 0, lid_t - head_h]) cylinder(d = head_d, h = head_h + 1, $fn = 32);
        }
        for (w = led_windows) translate([bx(w[0]), by(w[1]), light_win])
            cylinder(d = w[2], h = lid_t, $fn = 40);
        for (b = button_holes) translate([bx(b[0]), by(b[1]), -1]) cylinder(d = 1.8, h = lid_t + 2, $fn = 20);
        // engraved top labels
        translate([outer_w / 2, outer_d / 2 + 4, lid_t - 0.6]) linear_extrude(1)
            text("kmswitch", size = 6, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
        translate([outer_w / 2, outer_d / 2 - 4, lid_t - 0.6]) linear_extrude(1)
            text(str("variant ", variant), size = 3, halign = "center", valign = "center", font = "Liberation Sans");
        for (l = back_labels) translate([bx(l[1]), outer_d - 5, lid_t - 0.6]) linear_extrude(1)
            text(l[0], size = 3, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
        translate([bx(sd[0]), 5, lid_t - 0.6]) linear_extrude(1)
            text("SD", size = 3, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
        translate([bx(usba[0]), 5, lid_t - 0.6]) linear_extrude(1)
            text("USB", size = 3, halign = "center", valign = "center", font = "Liberation Sans:style=Bold");
    }
}

module dummy_board() {
    color("darkgreen") translate([wall + gap, wall + gap, board_z]) difference() {
        cube([board_w, board_h, board_t]);
        for (h = hole_xy) translate([h[0], board_h - h[1], -1]) cylinder(d = 2.7, h = 4, $fn = 20);
    }
    color("silver") {
        for (p = back_ports) translate([bx(p[0]) - 4.5, outer_d - wall - gap - 7.3, top_z + p[1] - 1.6]) cube([9, 7.3, 3.2]);
        translate([bx(usba[0]) - 7, wall + gap - 0.3, top_z]) cube([14, 14, 7]);
        translate([bx(sd[0]) - 6, wall + gap, top_z]) cube([12, 13, 1.5]);
    }
    if (variant == "A") color("royalblue") for (x = [17, 63])
        translate([bx(x) - 9, by(23.5), top_z + 2.54]) cube([18, 23.5, 1]);
}

if (part == "bottom") bottom();
else if (part == "lid") translate([0, outer_d, lid_t]) rotate([180, 0, 0]) lid();   // printed upside down
else {
    bottom();
    if (show_board) dummy_board();
    color("lightgray", 0.5) translate([0, 0, tray_h]) lid();
}

echo(str("variant ", variant, ": outer ", outer_w, " x ", outer_d, " x ", tray_h + lid_t, " mm; screws M2.5 x ",
         ceil(lid_t + above + board_t + 5), " mm (self-tapping)"));
