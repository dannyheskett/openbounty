# Italia mountain: how art/tiles/mountain*.png and river_mountain_* have been built (#67)
Italia's grass, sea, river and forest tiles have been the master set the zone builds
(Galliae/Africa/Oriens) start from. Its mountain has been built here from rock sprites, as the
other zones' have, rather than as a tile texture (#67).
    python3 tools/romeart.py sprites art/jobs/italia_o96_rocks.json art/primitives/italia/rocks --run
    python3 tools/romeart.py slots art/primitives/italia/rocks art/primitives/italia/rock_slots.json --tries 500 --seed 3 --straddle 4567
    python3 tools/romeart.py lattice art/layouts/mountain96.json --sprites art/primitives/italia/rocks --terrain mountain --name mountain --slots art/primitives/italia/rock_slots.json
    python3 tools/romeart.py compose art/layouts/mountain96.json build/art/italia_mountain/A
    python3 tools/romeart.py rebank <old mountain.png> <new mountain.png> assets/glory-of-rome/art/tiles river_mountain
rocks: eight PixelLab sprites, two calls of four (grey limestone, moss, snow caps); 0-3 run to
their frame edge, so the straddling slots take 4-7 (--straddle). The arrangement kept has been
formation A: seamcheck 0, 14 grass pixels showing in the interior tile (Galliae shipped with 36).
The river_mountain_* tiles keep their river bands (every pixel that differed from the old interior)
laid on the new interior, since Italia's river primitives were never kept either.
