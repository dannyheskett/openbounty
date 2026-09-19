# Galliae terrain: how the 168 tiles in assets/glory-of-rome/art/tiles/galliae/ were built

Primitives (PixelLab, 2026-09-19; jobs in art/jobs/galliae_*): grass, sea, cobble and river
create-tileset sets (sea, cobble and river chained to the grass, terrain id 83f91c7d), and the
trees and rocks sprite batches (tools/ has no driver for those; the call is the Italia one:
POST /create-1-direction-object, size 96, view top-down, four item_descriptions per call).
The *_teal and grass_deep folders are rejected runs.

    S=build/art/galliae_tiles; O=$S/out
    grass.png          the set's plain lower tile, 32 px, laid 3x3
    python3 tools/grassvar.py art/primitives/galliae/grass $S/grassvar --count 10 --seed 9 --patch-rate 0.3 --patch-size 1
                       grass_01..10; grass_variant is a copy of grass_01 (as in Italia)
    python3 tools/stitch96.py art/primitives/galliae/sea water $S/water --seed 3
    python3 tools/forestlattice.py $S/forest.json --sprites art/primitives/galliae/trees --crown 0 --name forest
                       autumn run (art/jobs/galliae_o96_trees_autumn.json), crown 0 (copper beech): seamcheck 0. The green run (trees_green) used crown 6
    python3 tools/forestlattice.py $S/mountain.json --sprites art/primitives/galliae/rocks --terrain mountain --name mountain --slots art/primitives/galliae/rock_slots.json
                       slots found by trying every arrangement of rocks 0,1,3,4,5,6 (2 and 7 are cut
                       off at their frame): seamcheck 0, fewest grass pixels showing (36)
    (set each layout's "grass" to $O/grass.png, then tools/treetile.py <layout> <dir>)
    python3 tools/roadtile.py art/primitives/galliae/cobble $S/roads --sweep --rim 2 --rim-shade 0.8 --grass $O/grass.png
    python3 tools/roadtile.py art/primitives/galliae/river $S/rivers --sweep --prefix river --rim 2 --rim-shade 0.8 --grass $O/grass.png
    ... --prefix river_forest --grass $O/forest.png, and --prefix river_mountain --grass $O/mountain.png
    bridge_river_ns: roadtile.py cobble --sweep --fill <master bridge_v.png> --grass $S/rivers/river_ew.png --rim 3 --rim-shade 0.7 (road_ns)
    bridge_river_ew: the same with bridge_h.png over river_ns.png (road_ew)
    python3 tools/rivermouth.py $O/water_edge_02.png $O/river_ew.png $O/grass.png $O/water.png $O/river_mouth_e.png
                       river_mouth_w is river_mouth_e mirrored, as in Italia
No desert tiles: Galliae has no desert, so those names fall back to the master set.
