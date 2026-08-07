#!/usr/bin/env python3
"""
build_slot_ghosts.py — Extract the empty-equipment-slot "ghost" silhouettes
(ring / hat / gloves / sword / ...) that the inventory shows when a slot is empty.

ROSE does NOT ship these as individual sprites: they are painted into the
inventory window background (`UI_INV_CONTAINER`, itself a sprite of
CONTROL/RES/UI_INVENTORY.DDS indexed by UI.TSI).  The client draws that one
image and then blits item icons on top of the slots.  Our Slate inventory builds
its slots individually, so we crop each ghost out into its own PNG and use it as
the empty-slot brush.

Slot geometry was measured off the container art by locating the slot-interior
brown (107,81,82): a 5x4 grid of 36x36 interiors at x = 26 + 50*col,
y = 69 + 46*row.  Column 3 is the paperdoll gap, so it holds no slots; (2,2) and
(3,4) are blank cells.  Verified by counting interior-brown pixels per cell:
every populated cell has some, every gap cell has zero.

Slot naming was verified BY EYE against the enlarged artwork (2026-07-20):
  row0: face mask | hat | BACKPACK | -- | arrow
  row1: sword     | torso armour | shield | -- | bullets
  row2: glove     | boot | (blank) | -- | CANNON SHELL (round bomb w/ fuse)
  row3: ring      | necklace | earring | -- | (blank)
An earlier revision mislabelled the backpack as "helmet" and the cannon shell as
"back"; ROSE has no separate helmet slot, and the right-hand column is the three
t_eSHOT ammo slots (arrow / bullet / cannon), not equipment.

The crop is alpha-keyed: the flat slot-interior brown becomes transparent and the
ghost keeps its original (already desaturated) colour, so it composites over the
Slate slot brush instead of pasting a brown square on top of it.

Writes SourceAssets/UI/SlotGhosts/slotghost_<slot>.png
Usage:  py -3.9 tools/build_slot_ghosts.py
"""
import os

import numpy as np
from PIL import Image

SRC = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "unreal-engine rose", "RoseUE",
    "SourceAssets", "UI", "Sprites", "UI_INV_CONTAINER.png"))
OUT = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "unreal-engine rose", "RoseUE",
    "SourceAssets", "UI", "SlotGhosts"))

CELL = 36
X0, XPITCH = 26, 50
Y0, YPITCH = 69, 46
BG = (107, 81, 82)          # slot-interior brown
# Soft key ramp on channel-distance from BG.  The ghost ink is mostly a *low*
# contrast shade (distance 8-25), so the ramp has to start almost at zero — a
# high KEY_LO hollows the silhouettes out.
KEY_LO, KEY_HI = 4.0, 22.0
# The slot frame's warm highlight bleeds a few pixels into some cells' corners
# (115,81,66).  It is the only thing here that is strongly orange: the ghost ink
# is neutral (B-G within +-6) while the frame is B-G = -15.  Reject on hue, not
# on distance, so the low-contrast ghost ink survives.
WARM_REJECT = 8

# (row, col) -> slot key used by ARoseCharacter::Equipped / the Slate paperdoll.
# "cannon" is ammo shot-type 2 (the UI labels it "Throw").
GHOSTS = {
    (0, 0): "faceitem",
    (0, 1): "cap",
    (0, 2): "back",       # backpack
    (0, 4): "arrow",
    (1, 0): "weapon",
    (1, 1): "body",
    (1, 2): "subwpn",     # shield
    (1, 4): "bullet",
    (2, 0): "arms",       # gloves
    (2, 1): "foot",       # boots
    (2, 4): "cannon",     # cannon shell / thrown bomb
    (3, 0): "ring",
    (3, 1): "necklace",
    (3, 2): "earring",
}


def main():
    a = np.array(Image.open(SRC).convert("RGBA")).astype(np.int16)
    os.makedirs(OUT, exist_ok=True)
    for (r, c), name in sorted(GHOSTS.items(), key=lambda kv: kv[1]):
        x, y = X0 + XPITCH * c, Y0 + YPITCH * r
        cell = a[y:y + CELL, x:x + CELL].copy()
        rgb = cell[:, :, :3]
        # Distance from the flat slot brown -> alpha.  Guards against accidentally
        # cropping a border: a cell that is not mostly brown is a geometry bug.
        dist = np.abs(rgb - np.array(BG)).max(axis=2).astype(np.float32)
        keyed = float((dist < KEY_LO).mean())
        if keyed < 0.30:
            raise SystemExit(f"[ghost] {name}: only {keyed:.0%} slot-brown at "
                             f"({x},{y}) — grid is off")
        alpha = np.clip((dist - KEY_LO) / (KEY_HI - KEY_LO), 0.0, 1.0) * 255.0
        alpha[(rgb[:, :, 1] - rgb[:, :, 2]) > WARM_REJECT] = 0.0
        cell[:, :, 3] = alpha.astype(np.int16)
        Image.fromarray(cell.astype(np.uint8), "RGBA").save(
            os.path.join(OUT, f"slotghost_{name}.png"))
        print(f"[ghost] slotghost_{name}.png  <- container ({x},{y}) "
              f"{CELL}x{CELL}  ({1 - keyed:.0%} ink)")
    print(f"[ghost] {len(GHOSTS)} slot ghosts -> {OUT}")


if __name__ == "__main__":
    main()
