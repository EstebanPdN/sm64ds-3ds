"""Authored native2D bank fixture, with no original game resources."""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from native_renderer.bank import Mesh,Image,Material,Part,build
vertices=b''.join(struct.pack('<5f4B',*v,255,255,255,255) for v in [(0,0,0,0,0),(1,0,0,1,0),(1,1,0,1,1),(0,1,0,0,1)])
Path(sys.argv[1]).write_bytes(build([Mesh(vertices,struct.pack('<6H',0,1,2,0,2,3))],[Image(8,8,bytes(256))],
    [Material(texture=0,blend=1,cull=0,depth_test=False,depth_write=False)],[Part(0,0,0,0,6)],1,bytes(range(32))))
