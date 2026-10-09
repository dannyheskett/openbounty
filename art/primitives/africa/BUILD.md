# Africa terrain: built as art/primitives/galliae/BUILD.md describes

    python3 tools/romeart.py zone africa && python3 tools/romeart.py install africa

Jobs: art/jobs/africa_*. Forest crown 0 (olive): seamcheck 0. The mountain slots in rock_slots.json are
the arrangement of the eight rocks with seamcheck 0 and the fewest grass pixels. The desert is the cream
run in desert/; with a desert, `zone` also builds the woods and ranges on sand (*_sand_edge_*) and the sand
shores (water_sand_edge_*, `shore`). pieces/fields_wheat.png is the irrigated farmland
(art/jobs/fields_africa_irrigated.json, run 2, laid 2x2), which Oriens shares.

A surface whose colour was close to the grass collapsed into the grass in a chained set: word it in a
colour far from the grass and check the plain tiles' mean colours.
