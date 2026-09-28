# Field paintings

One picture per continent, cut into the 30 open-field cells of
`assets/glory-of-rome/art/combat/field/<zone>_<x>_<y>.png` (ART-PIPELINE,
"Field grid").

`italia.png` was supplied (2026-09-25, #64). `italia_calm.png` is that
painting with its boulders, ferns, clover, moss and earth patches painted
out from its own grass:

    python3 tools/fieldcalm.py art/fields/italia.png build/art/field_calm italia --level strong --fill patchmatch

(`build/art/field_calm/inpainted.png` copied here; the fill is not
deterministic, so the calmed file kept here is the one that shipped.)

`galliae.png`, `africa.png` and `oriens.png` are windows of the calmed
painting, flipped and colour-graded for each land (2026-09-28, #64). The
grade is deterministic; these calls reproduce the files exactly:

    python3 tools/romeart.py fieldgrade art/fields/italia_calm.png art/fields/galliae.png --window 208,268,696,580 --flip h   --hue 6   --sat 1.20 --val 0.86
    python3 tools/romeart.py fieldgrade art/fields/italia_calm.png art/fields/africa.png  --window 352,448,696,580 --flip v   --hue -12 --sat 0.90 --val 1.05 --tint 214,190,138,0.12
    python3 tools/romeart.py fieldgrade art/fields/italia_calm.png art/fields/oriens.png  --window 340,248,696,580 --flip 180 --hue -5  --sat 0.62 --val 1.02 --tint 190,178,158,0.10

The windows are the top-left-most, bottom-right-most and top-right-most
696 x 580 windows (6:5) whose whole edge is content rather than the white
margin, searched on a 4 px grid, so the three compositions differ from
Italia's centred one and from each other.

Cells, for any zone:

    python3 tools/siegeslice.py art/fields/<zone>.png assets/glory-of-rome/art/combat/field --field <zone>
