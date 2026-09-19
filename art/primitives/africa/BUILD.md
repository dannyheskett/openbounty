# Africa terrain: built as art/primitives/galliae/BUILD.md describes, in one script
Jobs: art/jobs/africa_*. Forest crown 0 (olive): seamcheck 0. Mountain slots in rock_slots.json
(every arrangement of the eight rocks tried; seamcheck 0, fewest grass pixels).
Rejected runs kept beside the approved ones: desert_miss (collapsed into the grass), desert_orange,
cobble_miss (tile grid), cobble_miss2 (collapsed), cobble_ref3 (copied the reference image),
sea_turquoise, river_mud, river_teal. The approved desert is the cream run.
Lesson (2026-09-19): a surface whose colour is close to the grass collapses into the grass in a
chained set; word it in a colour far from the grass and check the plain tiles' mean colours.
