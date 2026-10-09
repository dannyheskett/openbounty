# Galliae terrain: how the tiles in assets/glory-of-rome/art/tiles/galliae/ are built

    python3 tools/romeart.py zone galliae       # builds build/art/galliae_tiles/out
    python3 tools/romeart.py install galliae    # copies what changed into the pack, lists it in game.json

`zone` builds every tile the set ships, the same pixels each time; `provenance check --rebuild`
(scripts/check_maps.sh, CI) rebuilds it and compares it with the pack.

Primitives (PixelLab, retired; jobs in art/jobs/primitives/galliae_*): grass, sea, cobble and river corner sets
(sea, cobble and river chained to the grass, terrain id 83f91c7d), the trees and rocks sprite batches,
and pieces/: the causeway road bridges over Galliae's water (bridge_ew, bridge_ns, drawn for the Sein in #218).

What `zone` does with them, in order:

    grass.png          the grass set's plain lower tile, 32 px, laid 3x3
    water*             stitch over the sea set, --seed 3
    forest*            the lattice over trees, crown 0 (copper beech): seamcheck 0
    mountain*          the lattice over rocks with rock_slots.json: rocks 0,1,3,4,5,6 (2 and 7 run to
                       their frame), the arrangement with seamcheck 0 and the fewest grass pixels (36);
                       ragged edges 6,20,34 with the side crags set back 12 and 28 px, and the contact
                       shadow (#63)
    road_*, river_*    sweep --rim 2 --rim-shade 0.8 over grass.png; the rivers again over forest.png and
                       mountain.png (river_forest_*, river_mountain_*)
    bridge_river_*     bridge: the road's own cobbles across the river between pale parapets, a shadow
                       on the water
    river_mouth_*      mouth over water_edge_sw.png (_ne); _nw its mirror; _es and _sw drawn the other way up
    fields_*           the master set's ploughed and wheat bases; their edges by edges --as ...=desert
    bridge_ew, _ns     pieces/
    the passes         fills, aprons, details, interiors, edgevars over the set's own sprites

Galliae has no desert, so the desert names fall back to the master set.
