# Field paintings

One picture per continent, cut into the 30 open-field cells of
`assets/glory-of-rome/art/combat/field/<zone>_<x>_<y>.png` (ART-PIPELINE,
"Field grid").

`italia.png` has been the supplied painting (#64). `italia_calm.png` is that
painting with its boulders, ferns, clover, moss and earth patches painted
out from its own grass:

    python3 tools/romeart.py fieldcalm art/fields/italia.png build/art/field_calm italia --level strong --fill patchmatch

(`build/art/field_calm/inpainted.png` copied here; the fill is not
deterministic, so the calmed file kept here is the one that shipped.)

`galliae.png`, `africa.png` and `oriens.png` are windows of the calmed
painting, flipped and colour-graded for each land (#64); the grades are
`FIELD_GRADES` in tools/romeart.py, and deterministic. One command grades the
three and cuts every zone's 30 field cells into the pack:

    python3 tools/romeart.py fields

The windows are the top-left-most, bottom-right-most and top-right-most
696 x 580 windows (6:5) whose whole edge is content rather than the white
margin, searched on a 4 px grid, so the three compositions differ from
Italia's centred one and from each other. A single grade by hand:
`romeart.py fieldgrade <src> <out> --window x,y,w,h [--flip] [--hue] [--sat] [--val] [--tint]`.
