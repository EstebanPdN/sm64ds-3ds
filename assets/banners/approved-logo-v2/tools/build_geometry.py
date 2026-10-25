"""Build actual letter volumes, ink borders and bevels. The reference is never a material."""
from pathlib import Path
import json, math
import cv2
import numpy as np
from PIL import Image
from shapely.geometry import Polygon
from shapely import constrained_delaunay_triangles

ROOT=Path(__file__).resolve().parents[1]
COLORS={'cyan':'#2daec8','blue':'#1255ec','green':'#28ba24','yellow':'#ffda25','red':'#fa281b','black':'#07090b',
        'card0':'#d78b35','card1':'#f7b65a','card2':'#f1a449','card3':'#c97c2d','card4':'#fbc66d','card5':'#e69b43',
        'silver':'#dce4e6','silver_edge':'#89979e','silver_bevel':'#eef7fa','gold':'#edb747','red_metal':'#eb2015'}

def trace_colored(name,im):
    r,g,b=[im[:,:,i].astype(float) for i in range(3)]
    if name.startswith('SUPER'):
        mask=(g>80)&(b>100)&(r<g*.70)&(r<b*.65)
        box={'S':(35,0,81,39),'U':(81,0,120,39),'P':(120,0,157,39),'E':(153,0,187,39),'R':(187,0,237,39)}[name[-1]]
    elif name=='MARIO_M':mask=(b>80)&(b>r*1.5)&(b>g*1.07);box=(0,42,94,111)
    elif name in ('MARIO_A','MARIO_O'):
        mask=(g>75)&(g>r*1.4)&(g>b*1.15);box=(83,43,144,108) if name.endswith('A') else (222,57,289,111)
    elif name=='MARIO_R':mask=(r>110)&(g>95)&(b<g*.65);box=(140,43,199,109)
    elif name=='MARIO_I':mask=(r>100)&(r>g*1.7)&(r>b*1.3);box=(197,44,229,108)
    else:mask=(r>110)&(r>g*1.5)&(r>b*1.4);box=(248,28,278,59) if name.endswith('6') else (275,28,300,59)
    x0,y0,x1,y1=box
    mask=(mask[y0*4:y1*4,x0*4:x1*4]*255).astype('uint8')
    contours,h=cv2.findContours(mask,cv2.RETR_CCOMP,cv2.CHAIN_APPROX_SIMPLE)
    top=max((i for i in range(len(contours)) if h[0][i][3]==-1),key=lambda i:cv2.contourArea(contours[i]))
    def pts(c):return (cv2.approxPolyDP(c,2.4,True).reshape(-1,2)/4+[x0,y0]).tolist()
    holes=[pts(c) for i,c in enumerate(contours) if h[0][i][3]==top and cv2.contourArea(c)>160]
    polygon=Polygon(pts(contours[top]),[x for x in holes if len(x)>2]).buffer(0)
    if polygon.geom_type=='MultiPolygon':polygon=max(polygon.geoms,key=lambda p:p.area)
    return polygon.simplify(.22,preserve_topology=True)

def curve_path(start,commands):
    pts=[start];p=np.array(start,dtype=float)
    for c in commands:
        if c[0]=='L':p=np.array(c[1:]);pts.append(p.tolist())
        else:
            a,b,q=[np.array(c[i:i+2]) for i in (1,3,5)]
            for k in range(1,13):
                t=k/12;pts.append(((1-t)**3*p+3*(1-t)**2*t*a+3*(1-t)*t*t*b+t**3*q).tolist())
            p=q
    return pts

def ds_shapes():
    # Hand-authored cubic outlines. Silver highlights come from 3D surfaces and lights.
    d=curve_path((48.1,108.5),[
        ('L',101.0,108.8),('C',123.7,108.8,140.1,113.7,140.1,127.4),
        ('C',140.1,141.3,126.8,146.3,103.1,146.3),('L',42.5,146.1)])
    dh=curve_path((76.0,114.1),[('L',98.9,114.2),('C',108.2,114.3,113.5,118.1,113.5,126.0),
        ('C',113.5,134.1,107.6,138.5,97.6,138.6),('L',72.6,138.7)])
    s=curve_path((177.0,109.4),[
        ('C',195.2,109.2,214.2,110.1,224.6,114.7),('C',227.3,116.1,228.8,118.5,229.2,120.9),
        ('L',201.3,120.7),('C',200.2,117.6,193.3,116.2,183.2,116.2),
        ('C',176.6,116.0,172.2,116.9,172.1,118.4),('C',171.8,120.1,177.4,121.5,187.4,122.8),
        ('L',208.7,125.8),('C',225.8,128.2,234.3,131.7,234.8,137.1),
        ('C',235.5,144.0,220.4,147.2,201.2,147.2),('L',175.0,147.0),
        ('C',155.8,147.0,145.8,144.0,143.1,138.0),('L',142.7,132.5),('L',171.6,132.5),
        ('C',172.8,136.6,179.5,139.0,189.3,139.0),('L',205.3,139.2),
        ('C',213.7,139.2,218.1,138.4,218.2,136.8),('C',218.2,135.1,212.4,134.1,202.3,132.7),
        ('L',178.3,129.1),('C',156.0,126.0,144.0,122.2,144.8,116.7),
        ('C',145.3,111.1,159.0,109.3,177.0,109.4)])
    return Polygon(d,[dh]).simplify(.12),Polygon(s).simplify(.12)

class Mesh:
    def __init__(self,name,kind):self.name=name;self.kind=kind;self.v=[];self.f=[];self.m=[];self.look={}
    def idx(self,x,y,z):
        co=((x-150)/20,(76-y)/20,z)
        key=tuple(round(float(v),6) for v in co)
        if key not in self.look:self.look[key]=len(self.v);self.v.append(list(key))
        return self.look[key]
    def triangle(self,pts,z,material):
        self.f.append([self.idx(x,y,z(x,y) if callable(z) else z) for x,y in pts]);self.m.append(material)
    def planar(self,poly,z,material,flip=False):
        for t in constrained_delaunay_triangles(poly).geoms:
            pts=list(t.exterior.coords)[:3]
            self.triangle(pts[::-1] if flip else pts,z,material)
    def walls(self,poly,z0,z1,material,back_shift0=0,back_shift1=0,flare0=0,flare1=0):
        for ring in [poly.exterior,*poly.interiors]:
            pts=list(ring.coords)[:-1]
            for i,(x,y) in enumerate(pts):
                xx,yy=pts[(i+1)%len(pts)]
                x0=150+(x-150)*(1+flare0);xx0=150+(xx-150)*(1+flare0)
                x1=150+(x-150)*(1+flare1);xx1=150+(xx-150)*(1+flare1)
                self.f.append([self.idx(x0,y+back_shift0,z0),self.idx(xx0,yy+back_shift0,z0),self.idx(xx1,yy+back_shift1,z1),self.idx(x1,y+back_shift1,z1)])
                self.m.append(material)
    def data(self):return dict(name=self.name,kind=self.kind,vertices=self.v,faces=self.f,materials=self.m)

def extruded(name,poly,front,depth,material,kind='border'):
    mesh=Mesh(name,kind);mesh.planar(poly,front,material);mesh.planar(poly,front-depth,material,True)
    mesh.walls(poly,front,front-depth,material)
    return mesh.data()

def cardboard(name,poly,depth,shift):
    mesh=Mesh(name+'_cardboard','cardboard');mesh.planar(poly,0,'card1')
    bands=6
    for k in range(bands):
        a,b=k/bands,(k+1)/bands
        mesh.walls(poly,-depth*a,-depth*b,'card'+str([1,2,4,0,1,3][k]),shift*a,shift*b,.045*a,.045*b)
    # Translate the final planar closure by the same amount as the final ring.
    from shapely.affinity import translate,scale
    back=translate(scale(poly,xfact=1.045,yfact=1,origin=(150,0)),yoff=shift)
    mesh.planar(back,-depth,'card3',True)
    return mesh.data()

def metal_body(name,poly,depth,material,front=.005):
    """Use exactly the same projected depth direction as the cardboard letters."""
    from shapely.affinity import translate,scale
    shift=7.0*depth;flare=.045
    mesh=Mesh(name,'metal');mesh.planar(poly,front,material)
    mesh.walls(poly,front,front-depth,material,0,shift,0,flare)
    back=translate(scale(poly,xfact=1+flare,yfact=1,origin=(150,0)),yoff=shift)
    mesh.planar(back,front-depth,material,True)
    return mesh.data()

def bevel_cap(name,poly,material,width=.8,outer_z=.075,inner_z=.15,kind='colored'):
    mesh=Mesh(name+'_face',kind)
    inner=poly.buffer(-width,join_style='mitre')
    assert inner.geom_type=='Polygon' and inner.is_valid and not inner.is_empty,name
    rim=poly.difference(inner)
    light=material+'_light' if kind=='colored' else 'silver_bevel'
    dark=material+'_dark' if kind=='colored' else 'silver_edge'
    for t in constrained_delaunay_triangles(rim).geoms:
        coords=list(t.exterior.coords)[:3]
        center=t.centroid
        # Top/left bevels catch the key light. This is a real sloped surface.
        edge=poly.boundary.interpolate(poly.boundary.project(center))
        choose=light if center.y>edge.y or center.x>edge.x else dark
        mesh.triangle(coords,lambda x,y: inner_z if inner.boundary.distance(__import__('shapely').Point(x,y))<1e-5 else outer_z,choose)
    mesh.planar(inner,inner_z,material)
    mesh.walls(poly,outer_z,.055,dark)
    mesh.planar(poly,.055,material,True)
    return mesh.data()

if __name__=='__main__':
    image=np.array(Image.open(ROOT/'source'/'owner-reference.png').convert('RGB').resize((1200,608),Image.Resampling.LANCZOS))
    parts=[];outlines=[]
    names=['SUPER_'+c for c in 'SUPER']+['MARIO_'+c for c in 'MARIO']+['NUMBER_6','NUMBER_4']
    for name in names:
        poly=trace_colored(name,image)
        assert poly.is_valid,name
        color='cyan' if name.startswith('SUPER') else ({'M':'blue','A':'green','R':'yellow','I':'red','O':'green'}[name[-1]] if name.startswith('MARIO') else 'red_metal')
        border=poly.buffer(2.0 if not name.startswith('NUMBER') else .8,join_style='mitre')
        if name.startswith('NUMBER'):
            parts.append(metal_body(name+'_gold',border.buffer(.25),.95,'gold',-.01))
        else:
            depth=1.55 if name.startswith('SUPER') else 2.10
            parts.append(cardboard(name,border.buffer(.35,join_style='mitre'),depth,7.0*depth))
        parts.append(extruded(name+'_ink',border,.035,.06,'gold' if name.startswith('NUMBER') else 'black'))
        parts.append(bevel_cap(name,poly,color,width=.85 if name.startswith('MARIO') else .6))
        outlines.append({'name':name,'color':COLORS[color],'outer':list(poly.exterior.coords),'holes':[list(h.coords) for h in poly.interiors]})
    for name,poly in zip(['DS_D','DS_S'],ds_shapes()):
        assert poly.is_valid,name
        parts.append(metal_body(name+'_chrome',poly.buffer(.75,join_style='round'),1.00,'silver_edge'))
        parts.append(bevel_cap(name,poly,'silver',width=1.3,outer_z=.07,inner_z=.21,kind='metal'))
        outlines.append({'name':name,'color':COLORS['silver'],'outer':list(poly.exterior.coords),'holes':[list(h.coords) for h in poly.interiors]})
    (ROOT/'source'/'geometry.json').write_text(json.dumps({'parts':parts,'colors':COLORS,'outlines':outlines},indent=2)+'\n')
    svg=['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 300 152">']
    for p in outlines:
        def path(r):return 'M '+' L '.join(f'{x:.3f},{y:.3f}' for x,y in r)+' Z'
        svg.append(f'<path id="{p["name"]}" fill="{p["color"]}" stroke="#08090b" stroke-width="1.7" fill-rule="evenodd" d="'+path(p['outer'])+' '+' '.join(path(h) for h in p['holes'])+'"/>')
    (ROOT/'source'/'letter-outlines.svg').write_text('\n'.join(svg+['</svg>']))
    print('Built',len(outlines),'letters as',len(parts),'closed component meshes; no reference-image surfaces.')
