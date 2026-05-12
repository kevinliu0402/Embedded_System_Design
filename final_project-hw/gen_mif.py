#!/usr/bin/env python3
"""gen_mif.py - generate the four ROM init files for the maze game.

Outputs (all written next to this script):
  maze.mif    - Quartus altsyncram MIF for the 600 x 3-bit Maze ROM
  evader.mif  - 256 x 3-bit sprite
  ghost0.mif  - 256 x 3-bit sprite
  ghost1.mif  - 256 x 3-bit sprite

  maze.hex    - same data as a $readmemh-friendly file (one value/line)
  evader.hex
  ghost0.hex
  ghost1.hex

  maze_data.h - C header with the 20x30 uint8_t maze[] array used by
                the HPS application to do collision checks and BFS.

Cell encoding (matches game_peripheral.sv):
  0 = WALL   1 = PATH   2 = SPECIAL   3 = GATE

The maze layout below is a hand-authored 30x20 grid that satisfies the
constraints in the design document:
  * Fixed special tile coords: (3,2) (26,2) (3,10) (26,10) (14,17) (17,17)
  * Fixed gate tile coord:     (15,1)
  * Outer wall surrounds the playfield
  * Every special tile is reachable from the centre of the maze
"""

import os

# ----------------------------------------------------------------------
# Maze layout (30 cols x 20 rows) - programmatically built.
#  * Outer border = wall
#  * Symmetric pattern of inner walls forming corridors
#  * Special tiles forced at design-spec coordinates
#  * Gate forced at design-spec coordinate
# ----------------------------------------------------------------------
COLS = 30
ROWS = 20

# Inner-wall mask: 1 means "place a wall here" before specials/gate are
# stamped on top.  Hand-drawn so each S/G coordinate ends up on a path.
WALL_MASK = [
    # 0         1         2
    # 0123456789012345678901234567890
    "..............................",  # 0
    "..............................",  # 1
    "..............................",  # 2
    "..W.W.W.W.W.W..W.W.W.W.W.W.W..",  # 3
    "..............................",  # 4
    "..W.W.W.W.W.W..W.W.W.W.W.W.W..",  # 5
    "..............................",  # 6
    "..W.W.W.W.W.W..W.W.W.W.W.W.W..",  # 7
    "..............................",  # 8
    "..W.W.W.W.W.W..W.W.W.W.W.W.W..",  # 9
    "..............................",  # 10
    "..W.W.W.W.W.W..W.W.W.W.W.W.W..",  # 11
    "..............................",  # 12
    "..W.W.W.W.W.W..W.W.W.W.W.W.W..",  # 13
    "..............................",  # 14
    "..W.W.W.W.W.W..W.W.W.W.W.W.W..",  # 15
    "..............................",  # 16
    "..............................",  # 17
    "..............................",  # 18
    "..............................",  # 19
]


def build_maze():
    maze = [[1] * COLS for _ in range(ROWS)]
    # Outer wall
    for c in range(COLS):
        maze[0][c] = 0
        maze[ROWS - 1][c] = 0
    for r in range(ROWS):
        maze[r][0] = 0
        maze[r][COLS - 1] = 0
    # Inner walls from the mask
    for r, row in enumerate(WALL_MASK):
        for c, ch in enumerate(row[:COLS].ljust(COLS, ".")):
            if ch == "W":
                maze[r][c] = 0
    # Row 1 is solid wall *except* at the gate column (force later)
    for c in range(COLS):
        maze[1][c] = 0
    return maze


def force_special(maze, specials):
    for col, row in specials:
        maze[row][col] = 2


def force_gate(maze, gate):
    col, row = gate
    maze[row][col] = 3


SPECIALS = [(3, 2), (26, 2), (3, 10), (26, 10), (14, 17), (17, 17)]
GATE = (15, 1)


def parse_maze():
    maze = build_maze()
    force_special(maze, SPECIALS)
    force_gate(maze, GATE)
    return maze


# ----------------------------------------------------------------------
# 16x16 sprites
#   Each is a list of 16 strings, 16 chars each.
#   Character meaning (matches palette() in game_peripheral.sv):
#     '.' = 0 transparent
#     '1' = white
#     '2' = evader body  (gold)
#     '3' = evader outline (red-brown)
#     '4' = ghost0 body  (blue)
#     '5' = ghost0 outline (dark blue)
#     '6' = ghost1 body  (red)
#     '7' = ghost1 outline (dark red)
# ----------------------------------------------------------------------
EVADER = [
    "................",
    ".....333333.....",
    "....32222223....",
    "...3222222223...",
    "..322221122223..",
    "..322211112223..",
    "..322111111223..",
    "..321111111123..",
    "..321111111123..",
    "..322111111223..",
    "..322211112223..",
    "..322221122223..",
    "...3222222223...",
    "....32222223....",
    ".....333333.....",
    "................",
]

GHOST0 = [
    "................",
    "....555555......",
    "...54444445.....",
    "..5444444445....",
    "..5441441445....",
    "..5411111145....",
    "..5411111145....",
    "..5444444445....",
    "..5444444445....",
    "..5444444445....",
    "..5444444445....",
    "..5444444445....",
    "..5454545455....",
    "..5454545455....",
    "...545454545....",
    "................",
]

GHOST1 = [
    "................",
    "....777777......",
    "...76666667.....",
    "..7666666667....",
    "..7661661667....",
    "..7611111167....",
    "..7611111167....",
    "..7666666667....",
    "..7666666667....",
    "..7666666667....",
    "..7666666667....",
    "..7666666667....",
    "..7676767677....",
    "..7676767677....",
    "...767676767....",
    "................",
]


def parse_sprite(pixels):
    out = []
    assert len(pixels) == 16
    for row in pixels:
        row = row[:16].ljust(16, ".")
        for ch in row:
            if ch == ".":
                out.append(0)
            else:
                out.append(int(ch, 16))
    assert len(out) == 256
    return out


# ----------------------------------------------------------------------
# Writers
# ----------------------------------------------------------------------
def write_mif(path, data, width, depth):
    with open(path, "w") as f:
        f.write(f"WIDTH={width};\nDEPTH={depth};\n")
        f.write("ADDRESS_RADIX=UNS;\nDATA_RADIX=UNS;\nCONTENT BEGIN\n")
        for i, v in enumerate(data):
            f.write(f"  {i}\t:   {v};\n")
        f.write("END;\n")


def write_hex(path, data):
    # one nibble per line is enough for 3-bit values; readmemh tolerates this
    with open(path, "w") as f:
        for v in data:
            f.write(f"{v:x}\n")


def write_c_header(path, maze):
    with open(path, "w") as f:
        f.write("/* Auto-generated by gen_mif.py - do not edit */\n")
        f.write("#ifndef MAZE_DATA_H\n#define MAZE_DATA_H\n\n")
        f.write("#define MAZE_COLS 30\n")
        f.write("#define MAZE_ROWS 20\n\n")
        f.write("static const unsigned char maze_data[MAZE_ROWS][MAZE_COLS] = {\n")
        for row in maze:
            f.write("    { " + ", ".join(str(v) for v in row) + " },\n")
        f.write("};\n\n#endif\n")


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    maze = parse_maze()
    flat = [v for row in maze for v in row]
    write_mif(os.path.join(here, "maze.mif"), flat, 3, 600)
    write_hex(os.path.join(here, "maze.hex"), flat)

    for name, sprite in [("evader", EVADER),
                         ("ghost0", GHOST0),
                         ("ghost1", GHOST1)]:
        data = parse_sprite(sprite)
        write_mif(os.path.join(here, f"{name}.mif"), data, 3, 256)
        write_hex(os.path.join(here, f"{name}.hex"), data)

    # Maze data header for the HPS C application
    sw_dir = os.path.abspath(os.path.join(here, "..", "final_project-sw"))
    if os.path.isdir(sw_dir):
        write_c_header(os.path.join(sw_dir, "maze_data.h"), maze)
    write_c_header(os.path.join(here, "maze_data.h"), maze)

    print("Generated:")
    for fn in ("maze.mif", "maze.hex", "evader.mif", "evader.hex",
               "ghost0.mif", "ghost0.hex", "ghost1.mif", "ghost1.hex",
               "maze_data.h"):
        print(" ", fn)


if __name__ == "__main__":
    main()
