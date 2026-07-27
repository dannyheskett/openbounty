# Italia: 40 x 64. Po valley north, Apennine spine down the peninsula,
# NW->SE tilt, Sicilia / Sardinia / Corsica as islands. One connected sea.
W,H = 40,64
G,V,F,M,S,D = '.',',','F','^','~','D'
g=[[S]*W for _ in range(H)]

def land(y, x0, x1, c=G):
    for x in range(max(0,x0), min(W,x1+1)): g[y][x]=c

# --- Alpine arc + Po valley (rows 0..11) -------------------------------------
for y in range(0,4):                      # Alps: blocking northern wall
    land(y, 6+ (y//2), 33-(y//2), M)
for y in range(4,8):                      # Po plain, wide and flat
    land(y, 5, 34)
for y in range(8,12):                     # narrowing toward the peninsula neck
    land(y, 7+(y-8), 33-(y-8))

# --- Peninsula: drifts SE as it descends (rows 12..52) -----------------------
for y in range(12,53):
    t=(y-12)/40.0
    cx=int(13 + 14*t)                     # centre marches east
    half=int(9 - 3.2*t)                   # and narrows
    land(y, cx-half, cx+half)
    # Apennine spine, offset west of centre, with passes every 7th row
    if y%7 not in (3,):
        sx=cx-max(1,half//3)
        for x in range(sx, sx+2):
            if 0<=x<W: g[y][x]=M

# --- Heel and toe (rows 53..58) ----------------------------------------------
land(53, 24, 33); land(54, 25, 33); land(55, 26, 32)     # Apulian heel
land(53, 20, 23); land(54, 20, 23); land(55, 20, 22)     # Calabrian toe
land(56, 20, 22); land(57, 20, 21)

# --- Sicilia (island, rows 59..62) ------------------------------------------
land(59, 15, 21); land(60, 14, 22); land(61, 15, 21); land(62, 17, 19)
g[60][18]=M                                # Etna

# --- Sardinia + Corsica (islands, west) -------------------------------------
for y in range(28,35): land(y, 3, 7)       # Sardinia
for y in range(22,27): land(y, 4, 7)       # Corsica
for y in (24,30,32): g[y][5]=M

# --- Woodland: Po valley and the Tuscan hills -------------------------------
for y in range(5,8):
    for x in range(8,30,3): g[y][x]=F
for y in range(16,26):
    for x in range(12,20,4):
        if g[y][x]==G: g[y][x]=F
# grass variant speckle for texture
for y in range(H):
    for x in range(W):
        if g[y][x]==G and (x*7+y*13)%23==0: g[y][x]=V

out='\n'.join(''.join(r) for r in g)+'\n'
open('/home/danheskett/personal/openbounty/assets/glory-of-rome/maps/italia.dat','w').write(
"# Italia -- 40x64. Home zone.\n"
"# Alps north, Po valley, Apennine peninsula tilting SE, heel and toe,\n"
"# Sicilia / Sardinia / Corsica offshore. Single connected sea (no lakes).\n"+out)
print("wrote", W, "x", H)
