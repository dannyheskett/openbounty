# Italia mountain: how art/tiles/mountain*.png and river_mountain_* have been built (#67)
Italia's grass, sea, river and forest tiles have been the master set the zone builds
(Galliae/Africa/Oriens) start from. Its mountain has been built here from rock sprites, as the
other zones' have, rather than as a tile texture (#67).
    python3 tools/romeart.py sprites art/jobs/italia_o96_rocks.json art/primitives/italia/rocks --run
    python3 tools/romeart.py slots art/primitives/italia/rocks art/primitives/italia/rock_slots.json --tries 500 --seed 3 --straddle 4567
    python3 tools/romeart.py lattice art/layouts/mountain96.json --sprites art/primitives/italia/rocks --terrain mountain --name mountain --slots art/primitives/italia/rock_slots.json --ragged 4,20,34
    python3 tools/romeart.py compose art/layouts/mountain96.json build/art/italia_mountain/A
    python3 tools/romeart.py rebank <old mountain.png> <new mountain.png> assets/glory-of-rome/art/tiles river_mountain
rocks: eight PixelLab sprites, two calls of four (grey limestone, moss, snow caps); 0-3 run to
their frame edge, so the straddling slots take 4-7 (--straddle). The arrangement kept has been
formation A: seamcheck 0, 14 grass pixels showing in the interior tile (Galliae shipped with 36).
The river_mountain_* tiles keep their river bands (every pixel that differed from the old interior)
laid on the new interior, since Italia's river primitives were never kept either.

--ragged 4,20,34 (#63): the edge pieces' terminal sides end raggedly -- rocks within 20 or 34 px of an
open line taken out, alternately, the middle edge crag left out for a notch, strips and spits kept
whole -- so a range no longer stops in a straight line. The plain tile is unchanged.

# Italia forest: tree sprites and lattice (#63, plan step 3)

Italia's forest had been a texture tile with no sprites kept, so its edges could not be rebuilt the
way the other sets' are. Eight trees were generated on Retro Diffusion (art/jobs/italia_tree_00..07,
rd_pro__topdown 96 px, $0.18 each; 00 and 04 run twice), their leaves matched to the old forest.png
in hue, saturation and value (trunks set brown), and kept here as trees/tile_00..07. The forest is
now a lattice of tree 1, like the other sets' woods:

    python3 tools/romeart.py lattice art/layouts/forest96_italia.json --sprites art/primitives/italia/trees --name forest --crown 1
    python3 tools/romeart.py compose art/layouts/forest96_italia.json build/art/italia_forest
    python3 tools/romeart.py rebank <old forest.png> <new forest.png> assets/glory-of-rome/art/tiles river_forest
    python3 tools/romeart.py fills; python3 tools/romeart.py interiors; python3 tools/romeart.py aprons; python3 tools/romeart.py details

The mountain edges are composed with a soft contact shadow (romeart MOUNTAIN_SHADOW) and the
side crags set back (--ragged 4,20,34,0,28: the top crag cannot move, it meets a north straddler);
`romeart.py edgevars` then rebuilds the mountain side variants the same way.
