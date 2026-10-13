#!/usr/bin/env python3
"""Scatter a render's own small clumps (pixels unlike its flat base: flowers, tufts) onto tiles that share
that base, --per clumps per tile at random, each clump whole with its own pixels, only onto base-colour
ground clear of the 4 px shared border.   scatter.py RENDER TILES_DIR --match NAME_PREFIX --per N [--pick WHAT]
--pick flower keeps only clumps holding a white or red pixel; --max-size S (px) drops big clumps."""
import argparse, os, random
import numpy as np
from PIL import Image

def clumps(a, base, maxs):
    d=(np.abs(a-base).sum(2)>0); H,W=d.shape; lab=np.zeros((H,W),int); n=0; out=[]
    for y0 in range(H):
        for x0 in range(W):
            if d[y0,x0] and lab[y0,x0]==0:
                n+=1; st=[(y0,x0)]; lab[y0,x0]=n; pts=[]
                while st:
                    y,x=st.pop(); pts.append((y,x))
                    for dy in (-1,0,1):
                        for dx in (-1,0,1):
                            ny,nx=y+dy,x+dx
                            if 0<=ny<H and 0<=nx<W and d[ny,nx] and lab[ny,nx]==0: lab[ny,nx]=n; st.append((ny,nx))
                ys=[p[0] for p in pts]; xs=[p[1] for p in pts]; y1,y2,x1,x2=min(ys),max(ys)+1,min(xs),max(xs)+1
                if y2-y1>maxs or x2-x1>maxs or len(pts)<4 or y1==0 or x1==0 or y2==H or x2==W: continue
                m=np.zeros((y2-y1,x2-x1),bool)
                for y,x in pts: m[y-y1,x-x1]=True
                out.append((a[y1:y2,x1:x2].copy(),m))
    return out

def main():
    ap=argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("render"); ap.add_argument("tiles_dir"); ap.add_argument("--match", required=True); ap.add_argument("--per", type=int, default=8)
    ap.add_argument("--pick", default="any"); ap.add_argument("--max-size", type=int, default=14); ap.add_argument("--seed", type=int, default=9)
    a=ap.parse_args(); R=np.array(Image.open(a.render).convert("RGB")).astype(int)
    u,c=np.unique(R.reshape(-1,3),axis=0,return_counts=True); base=u[c.argmax()]
    C=clumps(R,base,a.max_size)
    if a.pick=="flower":
        def fl(pic,m):
            p=pic[m]; return (((p.min(1)>=200)) | ((p[:,0]>150)&(p[:,1]<90)&(p[:,2]<90))).any()
        C=[(p,m) for p,m in C if fl(p,m)]
    print("clumps:", len(C)); rng=random.Random(a.seed); n=0
    for f in sorted(os.listdir(a.tiles_dir)):
        if not (f.endswith(".png") and f.startswith(a.match)): continue
        im=Image.open(os.path.join(a.tiles_dir,f)).convert("RGBA"); t=np.array(im).astype(int); rgb=t[...,:3]
        isbase=(np.abs(rgb-base).sum(2)==0); placed=[]
        for _ in range(400):
            if len(placed)>=a.per: break
            pic,m=rng.choice(C); h,w=m.shape; y,x=rng.randrange(5,96-h-5),rng.randrange(5,96-w-5)
            if not isbase[y:y+h,x:x+w][m].all(): continue
            if any(abs(y-py)<12 and abs(x-px)<12 for py,px in placed): continue
            reg=rgb[y:y+h,x:x+w]; reg[m]=pic[m]; placed.append((y,x))
        t[...,:3]=rgb; Image.fromarray(t.astype(np.uint8)).save(os.path.join(a.tiles_dir,f)); n+=1
    print("scatter:", n, "tiles")
if __name__=="__main__": main()
