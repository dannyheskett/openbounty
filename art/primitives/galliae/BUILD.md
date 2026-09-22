# Galliae terrain: how the tiles in assets/glory-of-rome/art/tiles/galliae/ have been built

    python3 tools/romeart.py zone galliae       # builds build/art/galliae_tiles/out
    python3 tools/romeart.py install galliae    # copies it into the pack, lists it in game.json

Primitives (PixelLab; jobs in art/jobs/galliae_*): grass, sea, cobble and river create-tileset
sets (sea, cobble and river chained to the grass, terrain id 83f91c7d), and the trees and rocks
sprite batches (POST /create-1-direction-object, size 96, view top-down, four
item_descriptions per call; tools/ has had no driver for those).

What `zone` has done with them, in order:

    grass.png          the grass set's plain lower tile, 32 px, laid 3x3
    grass_01..10       romeart.py grass, --count 10 --seed 9 --patch-rate 0.3 --patch-size 1;
                       grass_variant is grass_01
    water*             romeart.py stitch over the sea set, --seed 3
    forest*            romeart.py lattice over trees, crown 0 (copper beech): seamcheck 0
    mountain*          romeart.py lattice over rocks with rock_slots.json: rocks 0,1,3,4,5,6
                       (2 and 7 are cut off at their frame), the arrangement with seamcheck 0
                       and the fewest grass pixels showing (36)
    road_*, river_*    romeart.py sweep --rim 2 --rim-shade 0.8 over grass.png; the rivers
                       again over forest.png and mountain.png (river_forest_*, river_mountain_*)
    bridge_river_ns    sweep --fill the pack's bridge_v.png over river_ew.png, --rim 3
                       --rim-shade 0.7, taking road_ns; bridge_river_ew is bridge_h.png over
                       river_ns.png, taking road_ew
    river_mouth_e      romeart.py mouth over water_edge_02.png; river_mouth_w is its mirror

Galliae has had no desert, so the desert names have fallen back to the master set. The *_teal,
grass_deep and trees_green folders have been runs the build has not used.
