#!/usr/bin/env python3
"""Regenerate game/assets/runtime/perf_test.scene.

The performance scene is a wide open arena with the player at the world
origin, a broken ring of low walls around the spawn, and a scattered pillar
field, so thousands of stress-test Runners have real navigation topology to
path through instead of an empty plane.
"""
import io
import math
import os

ARENA_HALF_X = 1600
ARENA_HALF_Z = 1200
PERIMETER_THICKNESS = 40
PERIMETER_SPRITE_HEIGHT = 46

ROOM_HALF = 160          # inner low-wall ring around the player spawn
ROOM_THICKNESS = 24
LOW_WALL_SPRITE_HEIGHT = 26

PILLAR_SIZE = 60
PILLAR_SPACING = 400
PILLAR_CLEAR_RADIUS = 400
PILLAR_SPRITE_HEIGHT = 34

solids = []  # (name, x, z, width, depth, sprite_height, rgba)

PERIMETER_TINT = (108, 74, 62, 255)
LOW_WALL_TINT = (190, 92, 72, 255)
PILLAR_TINT = (142, 118, 96, 255)


def add(name, x, z, width, depth, sprite_height, tint):
    solids.append((name, x, z, width, depth, sprite_height, tint))


# Perimeter -----------------------------------------------------------------
add("perimeter-north", -ARENA_HALF_X, -ARENA_HALF_Z, 2 * ARENA_HALF_X,
    PERIMETER_THICKNESS, PERIMETER_SPRITE_HEIGHT, PERIMETER_TINT)
add("perimeter-south", -ARENA_HALF_X, ARENA_HALF_Z - PERIMETER_THICKNESS,
    2 * ARENA_HALF_X, PERIMETER_THICKNESS, PERIMETER_SPRITE_HEIGHT, PERIMETER_TINT)
add("perimeter-west", -ARENA_HALF_X, -ARENA_HALF_Z, PERIMETER_THICKNESS,
    2 * ARENA_HALF_Z, PERIMETER_SPRITE_HEIGHT, PERIMETER_TINT)
add("perimeter-east", ARENA_HALF_X - PERIMETER_THICKNESS, -ARENA_HALF_Z,
    PERIMETER_THICKNESS, 2 * ARENA_HALF_Z, PERIMETER_SPRITE_HEIGHT, PERIMETER_TINT)

# Sealed low-wall box around the spawn ---------------------------------------
# The ring is deliberately unbroken: the player observes the crowd from inside
# it, and no Runner has a route in. The side walls stop short of the north and
# south walls so the four solids meet exactly without overlapping.
inner = ROOM_HALF - ROOM_THICKNESS
add("wall-north", -ROOM_HALF, -ROOM_HALF, 2 * ROOM_HALF, ROOM_THICKNESS,
    LOW_WALL_SPRITE_HEIGHT, LOW_WALL_TINT)
add("wall-south", -ROOM_HALF, inner, 2 * ROOM_HALF, ROOM_THICKNESS,
    LOW_WALL_SPRITE_HEIGHT, LOW_WALL_TINT)
add("wall-west", -ROOM_HALF, -inner, ROOM_THICKNESS, 2 * inner,
    LOW_WALL_SPRITE_HEIGHT, LOW_WALL_TINT)
add("wall-east", inner, -inner, ROOM_THICKNESS, 2 * inner,
    LOW_WALL_SPRITE_HEIGHT, LOW_WALL_TINT)

# Pillar field --------------------------------------------------------------
pillar_index = 0
x = -ARENA_HALF_X + PILLAR_SPACING
while x < ARENA_HALF_X - PILLAR_SPACING * 0.5:
    z = -ARENA_HALF_Z + PILLAR_SPACING
    while z < ARENA_HALF_Z - PILLAR_SPACING * 0.5:
        if x * x + z * z >= PILLAR_CLEAR_RADIUS * PILLAR_CLEAR_RADIUS:
            add("pillar-%02d" % pillar_index, x - PILLAR_SIZE // 2,
                z - PILLAR_SIZE // 2, PILLAR_SIZE, PILLAR_SIZE,
                PILLAR_SPRITE_HEIGHT, PILLAR_TINT)
            pillar_index += 1
        z += PILLAR_SPACING
    x += PILLAR_SPACING


def number(value):
    if float(value) == int(value):
        return str(int(value))
    return ("%.2f" % value).rstrip("0").rstrip(".")


LOCOMOTION_STATES = [
    "idle_south", "idle_southwest", "idle_northwest", "idle_north",
    "idle_northeast", "idle_west", "idle_east", "idle_southeast",
    "move_south", "move_southwest", "move_northwest", "move_north",
    "move_northeast", "move_west", "move_east", "move_southeast",
]

PLAYER_ASSETS = [
    "player-v3-move-south", "player-v3-move-north", "player-v3-move-east",
    "player-v3-move-southeast", "player-v3-move-northeast",
    "player-v3-idle-south", "player-v3-idle-southwest", "player-v3-idle-west",
    "player-v3-idle-northwest", "player-v3-idle-north", "player-v3-idle-northeast",
    "player-v3-idle-east", "player-v3-idle-southeast",
    "player-v3-seated-south", "player-v3-seated-north",
    "player-v3-dodge-sidestep", "player-v3-dodge-roll", "player-v3-dodge-slide",
    "player-v3-dodge-back-hop-south", "player-v3-dodge-north",
    "player-v3-shoot-south", "player-v3-shoot-north", "player-v3-shoot-east",
]

PLAYER_BINDINGS = [
    ("idle_south", "player-v3-idle-south"),
    ("idle_southwest", "player-v3-idle-southwest"),
    ("idle_northwest", "player-v3-idle-northwest"),
    ("idle_north", "player-v3-idle-north"),
    ("idle_northeast", "player-v3-idle-northeast"),
    ("idle_west", "player-v3-idle-west"),
    ("idle_east", "player-v3-idle-east"),
    ("idle_southeast", "player-v3-idle-southeast"),
    ("move_south", "player-v3-move-south"),
    ("move_southwest", "player-v3-move-southwest"),
    ("move_northwest", "player-v3-move-northwest"),
    ("move_north", "player-v3-move-north"),
    ("move_northeast", "player-v3-move-northeast"),
    ("move_west", "player-v3-move-west"),
    ("move_east", "player-v3-move-east"),
    ("move_southeast", "player-v3-move-southeast"),
    ("dodge_south", "player-v3-dodge-back-hop-south"),
    ("dodge_southwest", "player-v3-dodge-slide-west"),
    ("dodge_northwest", "player-v3-dodge-roll-west"),
    ("dodge_north", "player-v3-dodge-north"),
    ("dodge_northeast", "player-v3-dodge-roll-east"),
    ("dodge_west", "player-v3-dodge-sidestep-west"),
    ("dodge_east", "player-v3-dodge-sidestep-east"),
    ("dodge_southeast", "player-v3-dodge-slide-east"),
    ("seated_south", "player-v3-seated-south"),
    ("seated_north", "player-v3-seated-north"),
    ("shoot_south", "player-v3-shoot-south"),
    ("shoot_west", "player-v3-shoot-west"),
    ("shoot_north", "player-v3-shoot-north"),
    ("shoot_east", "player-v3-shoot-east"),
]


def bindings(entity_id, clip_prefix):
    lines = []
    for index, state in enumerate(LOCOMOTION_STATES):
        clip = "%s-%s" % (clip_prefix, state.replace("_", "-"))
        lines.append("animation_binding=%s|%s|%s|%s" % (
            entity_id, state, clip, "true" if index == 0 else "false"))
    return lines


out = []
w = out.append
w("# IC_2DE performance test scene - regenerate with tools/generate-perf-scene.py")
w("schema=12")
w("id=perf_test")
w("world_space=x_y_z")
w("ground_plane=x_z")
w("elevation_axis=y")
w("")
w("walkable_bounds=%d|%d|%d|%d" % (-ARENA_HALF_X, -ARENA_HALF_Z,
                                   2 * ARENA_HALF_X, 2 * ARENA_HALF_Z))
w("max_step_height=24")
# Yaw zero is deliberate. Sprites are screen-axis-aligned quads, so with a
# yawed camera a wall running along Z projects to a steep diagonal that can
# only be approximated by a visible staircase of slices. At yaw zero an X wall
# projects perfectly horizontal and a Z wall perfectly vertical, so all four
# sides of a rectangular room read as straight walls.
w("camera=0|50|1|1")
w("physics=32|4|0|0|false|32")
w("ground_filter=1|22")
w("trigger_filter=8|22")
w("player_speed=130")
w("")
w("texture=soft-shadow|radial|16|16|4|8|12|145|4|8|12|0|smooth")
w("texture=test-crate|checker|8|8|2|48|92|145|255|74|145|205|255|pixel")
for player_asset in PLAYER_ASSETS:
    w("aseprite=%s|%s.json|pixel|60" % (player_asset, player_asset))
w("aseprite=npc-patchwork-atlas|npc-patchwork-atlas.json|pixel|60")
w("aseprite=npc-patchwork-diagonal-atlas|npc-patchwork-diagonal-atlas.json|pixel|60")
w("")
w("# kind|x|z|width|depth|elevation|tag")
for name, x, z, width, depth, _height, _tint in solids:
    w("ground_area=solid|%s|%s|%s|%s|0|0" % (
        number(x), number(z), number(width), number(depth)))
w("")
w("# id|role|motion|x|z|half-width|half-depth|category|mask|tag|sensor|"
  "fixed-rotation|linear-damping|angular-damping|density|friction")
w("physics_box=player|player|kinematic|0|0|9|5|2|29|10|false|true|0|0|1|0.6")
w("physics_box=dynamic-crate|primary_prop|dynamic|80|80|11|8|4|27|20|false|false|1.2|2|0.8|0.7")
w("physics_box=attacker|attacker|kinematic|600|0|10|6|16|15|31|false|true|0|0|2|0.9")
w("")
w("# entity=id|uuid|name|physics-binding|x|y|z|sprite-width|sprite-height|"
  "origin-x|origin-y|RGBA|layer|texture-id")
w("entity=player-shadow|1001|Player shadow|player|0|0|0|20|7|0.5|0.5|255|255|255|255|0|soft-shadow")
w("entity=player|1002|Player|player|0|0|0|48|48|0.5|0.833333|255|255|255|255|0|player-v3-idle-south")
# Shadows use the radial soft-shadow texture. An untextured sprite is drawn as
# a hard rectangle, which reads as a black box sitting under the actor.
w("entity=dynamic-crate-shadow|1003|Dynamic crate shadow|dynamic-crate|80|0|80|28|10|0.5|0.5|255|255|255|210|0|soft-shadow")
w("entity=dynamic-crate|1004|Dynamic crate|dynamic-crate|80|0|80|32|26|0.5|1|255|255|255|255|0|test-crate")
w("entity=attacker-shadow|1005|Runner shadow|attacker|600|0|0|22|8|0.5|0.5|255|255|255|210|0|soft-shadow")
w("entity=attacker|1006|Threadbound Runner|attacker|600|0|0|24|34|0.5|1|255|190|190|255|0|npc-patchwork-atlas")
w("")
w("# Static arena geometry, one entity per wall. A wall running along X sits at")
w("# a single depth and is one billboard. A wall running along Z spans a range")
w("# of depth, which one billboard cannot represent, so it carries a depth span")
w("# and the renderer resolves that into overlapping depth-sorted slices.")
uuid = 2000
for name, x, z, width, depth, height, tint in solids:
    center_x = x + width / 2.0
    center_z = z + depth / 2.0
    record = "entity=%s|%d|%s|-|%s|0|%s|%s|%d|0.5|1|%d|%d|%d|%d|0|-" % (
        name, uuid, name.replace("-", " ").capitalize(),
        number(center_x), number(center_z), number(width), height,
        tint[0], tint[1], tint[2], tint[3])
    if depth > width:
        record += "|%s" % number(depth)
    w(record)
    uuid += 1
w("")
w("# child-entity-id|parent-entity-id")
w("# A shadow belongs to the thing casting it, so it leaves play with it.")
w("parent=player-shadow|player")
w("parent=dynamic-crate-shadow|dynamic-crate")
w("parent=attacker-shadow|attacker")
w("")
w("# entity-id|locomotion-state|clip-id|initial")
for index, (state, clip) in enumerate(PLAYER_BINDINGS):
    w("animation_binding=player|%s|%s|%s" % (
        state, clip, "true" if index == 0 else "false"))
for line in bindings("attacker", "npc-patchwork"):
    w(line)
w("")

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
target = os.path.join(root, "game", "assets", "runtime", "perf_test.scene")
io.open(target, "w", encoding="utf-8", newline="\n").write("\n".join(out))
print("wrote %s (%d solids)" % (target, len(solids)))
