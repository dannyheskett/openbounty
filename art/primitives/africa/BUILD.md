# Africa terrain: built as art/primitives/galliae/BUILD.md has described

    python3 tools/romeart.py zone africa && python3 tools/romeart.py install africa

Jobs: art/jobs/africa_*. Forest crown 0 (olive): seamcheck 0. The mountain slots in
rock_slots.json have been the arrangement of the eight rocks with seamcheck 0 and the fewest
grass pixels. The desert has been the cream run in desert/.

Folders the build has not used: desert_miss (collapsed into the grass), desert_orange,
cobble_miss (a tile grid), cobble_miss2 (collapsed), cobble_ref3 (copied the reference image),
sea_turquoise, river_mud, river_teal.

A surface whose colour has been close to the grass has collapsed into the grass in a chained
set: word it in a colour far from the grass and check the plain tiles' mean colours.
