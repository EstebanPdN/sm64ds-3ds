#!/usr/bin/env python3
"""Visualize captured native Player geometry on the HOST, never as game proof.

The port does not call this rasterizer. It is an inspection artifact for the
original body's/head's posed geometry, materials and texture coordinates.
"""
import argparse
import json
from pathlib import Path
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont


def font(size):
    path='/System/Library/Fonts/Supplemental/Arial.ttf'
    return ImageFont.truetype(path,size) if Path(path).exists() else ImageFont.load_default()


def render(folder,width=400,height=320):
    geometry=json.loads((folder/'geometry.json').read_text())
    triangles=geometry['triangles']
    textures={}
    for path in folder.glob('texture-*.rgba'):
        data=path.read_bytes();w,h=struct.unpack_from('<II',data)
        argb=np.frombuffer(data[8:],dtype='<u4').reshape(h,w)
        textures[int(path.stem[8:])]=np.stack(((argb>>16)&255,(argb>>8)&255,argb&255,argb>>24),axis=-1)
    positions=np.array([v[:3] for t in triangles for v in t['vertices']],dtype=float)
    # A fixed inspection view of each pose. Centering is for inspection only;
    # original world-space displacement is verified in execution.json.
    yaw=np.deg2rad(-25);pitch=np.deg2rad(-12)
    rot_y=np.array([[np.cos(yaw),0,-np.sin(yaw)],[0,1,0],[np.sin(yaw),0,np.cos(yaw)]])
    rot_x=np.array([[1,0,0],[0,np.cos(pitch),np.sin(pitch)],[0,-np.sin(pitch),np.cos(pitch)]])
    rotation=rot_y@rot_x
    center=(positions.min(axis=0)+positions.max(axis=0))/2
    transformed=(positions-center)@rotation
    low=transformed.min(axis=0);high=transformed.max(axis=0)
    scale=min((width-64)/(high[0]-low[0]),(height-50)/(high[1]-low[1]))
    frame=np.full((height,width,3),[222,229,235],dtype=float)
    depth=np.full((height,width),-np.inf)
    for t in triangles:
        verts=np.array(t['vertices'],dtype=float)
        p=(verts[:,:3]-center)@rotation
        p[:,0]=width/2+p[:,0]*scale;p[:,1]=height/2-p[:,1]*scale
        x0=max(0,int(np.floor(p[:,0].min())));x1=min(width-1,int(np.ceil(p[:,0].max())))
        y0=max(0,int(np.floor(p[:,1].min())));y1=min(height-1,int(np.ceil(p[:,1].max())))
        if x1<x0 or y1<y0:continue
        yy,xx=np.mgrid[y0:y1+1,x0:x1+1];xx=xx+.5;yy=yy+.5
        a,b,c=p
        denominator=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(denominator)<1e-9:continue
        wa=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/denominator
        wb=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/denominator
        wc=1-wa-wb;weights=np.stack((wa,wb,wc),axis=-1)
        z=weights@p[:,2];mask=(wa>=0)&(wb>=0)&(wc>=0)&(z>depth[y0:y1+1,x0:x1+1])
        if not mask.any():continue
        colors=verts[:,5].astype(np.uint32)
        rgb=np.stack(((colors>>16)&255,(colors>>8)&255,colors&255),axis=-1)
        color=weights@rgb;alpha=np.full(z.shape,t['alpha']/31)
        if t['texture']:
            tex=textures[t['texture']];uv=weights@verts[:,3:5]
            coords=[]
            for axis,length in ((0,tex.shape[1]),(1,tex.shape[0])):
                q=np.floor(uv[:,:,axis]).astype(np.int64)
                if t['wrap']&(1<<axis):
                    if t['wrap']&(4<<axis):
                        q=q%(length*2);q=np.where(q<length,q,length*2-1-q)
                    else:q=q%length
                else:q=np.clip(q,0,length-1)
                coords.append(q)
            sample=tex[coords[1],coords[0]]
            color=color*sample[:,:,:3]/255;alpha=alpha*sample[:,:,3]/255
        mask&=alpha>0
        target=frame[y0:y1+1,x0:x1+1]
        target[mask]=color[mask]*alpha[mask,None]+target[mask]*(1-alpha[mask,None])
        depth[y0:y1+1,x0:x1+1][mask]=z[mask]
    return Image.fromarray(np.clip(frame,0,255).astype(np.uint8))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    samples=[(0,'Reposo'),(90,'Caminando'),(179,'Tras frenar')]
    sheet=Image.new('RGB',(1200,426),'#edf1f5');draw=ImageDraw.Draw(sheet)
    draw.text((20,12),'Geometría original de Yoshi · diagnóstico en host',fill='#142437',font=font(23))
    draw.text((20,44),'CPU ARM11 simulada. Encuadre centrado de cada pose. No es Azahar ni una consola 3DS.',fill='#415369',font=font(16))
    for index,(tick,label) in enumerate(samples):
        image=render(args.directory/f'frame-{tick:03d}')
        sheet.paste(image,(index*400,76))
        draw.text((index*400+20,399),f'Ciclo {tick}: {label}',fill='#142437',font=font(17))
    path=args.directory/'player-poses.png';sheet.save(path);print(path.resolve())


if __name__=='__main__':main()
