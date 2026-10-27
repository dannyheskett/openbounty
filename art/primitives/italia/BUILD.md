# Italia: the master set's forest and mountain (#63, #67)

Italia's set is the master set (assets/glory-of-rome/art/tiles/): the zone sets start from its names, and
a zone takes from it what it does not draw itself. Its sea, desert, roads and rivers were stitched and swept
in #38-#67 from PixelLab corner sets that were not kept, so they are kept as made. Its forest and mountain
are lattices of the sprites kept here, their layouts committed, so `compose` rebuilds them exactly
(`provenance check --rebuild`):

    python3 tools/romeart.py compose art/layouts/forest96_italia.json build/art/italia_forest
    python3 tools/romeart.py compose art/layouts/mountain96.json build/art/italia_mountain

How the layouts were made:

    python3 tools/romeart.py lattice art/layouts/forest96_italia.json --sprites art/primitives/italia/trees --name forest --crown 1
    python3 tools/romeart.py slots art/primitives/italia/rocks art/primitives/italia/rock_slots.json --tries 500 --seed 3 --straddle 4567
    python3 tools/romeart.py lattice art/layouts/mountain96.json --sprites art/primitives/italia/rocks --terrain mountain --name mountain \
        --slots art/primitives/italia/rock_slots.json --ragged 4,20,34,0,28 --shadow 2,3,0.35

rocks: eight PixelLab sprites (art/jobs/primitives/italia_rocks.json), two calls of four (grey limestone, moss,
snow caps); 0-3 run to their frame edge, so the straddling slots take 4-7 (--straddle). The arrangement
kept is seamcheck 0 with 14 grass pixels showing in the plain tile. --ragged 4,20,34,0,28: rocks within 20
or 34 px of an open line taken out, alternately, the middle crag left out for a notch, the bottom crag set
back 28 px (the top one cannot move: it meets a north straddler); strips and spits kept whole.

trees: eight Retro Diffusion trees (art/jobs/primitives/italia_tree_00..07, rd_pro__topdown 96 px), their leaves
matched to the old forest by hand in hue, saturation and value and the trunks set brown (`colormatch` is
that step now); the forest is a lattice of tree 1.

The river_forest_* and river_mountain_* pieces kept their river bands (every pixel unlike the old
interior) laid on the new interiors with `rebank`; the old interiors were not kept, so those pieces are
kept as made. The set passes then run over all four sets:

    python3 tools/romeart.py fills; python3 tools/romeart.py interiors; python3 tools/romeart.py aprons
    python3 tools/romeart.py details; python3 tools/romeart.py edgevars; python3 tools/romeart.py shore
    python3 tools/romeart.py dock
    python3 tools/romeart.py edges assets/glory-of-rome '' --as fields_wheat=desert --as fields_plough=desert
