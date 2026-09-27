# Italia mountain: how art/tiles/mountain*.png and river_mountain_* have been built (2026-09-27, #67)
Italia's set predates the zone builds (Galliae/Africa/Oriens); its grass, sea, river and forest
tiles are the master set and stay. Only the mountain has been rebuilt here, after the tester
called the old interior (a 48 px Retro Diffusion texture of rounded domes, art/jobs/mountain.json,
now deleted) a split cup. The rocks the old edges were composed from had never been kept.
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
