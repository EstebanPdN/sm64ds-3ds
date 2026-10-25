"""Select actual KCL prism interiors; ARM queries, not this parser, certify hits."""
import struct

def triangles(blob):
    positions,normals,triangles,octree=struct.unpack_from('<4I',blob)
    # The header prism pointer is biased -16; its sentinel overlaps
    # the normal tail. The first real prism is index1 at base+16.
    triangles+=16
    assert positions<=normals<=triangles<=octree<=len(blob)
    def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    def normal(i):
        p=normals+i*6;assert p+6<=triangles
        return struct.unpack_from('<3h',blob,p)
    result=[]
    for p in range(triangles,octree,16):
        length,vi,fi,ai,bi,ci,attribute=struct.unpack_from('<i6H',blob,p)
        vp=positions+vi*12;assert vp+12<=normals
        origin=struct.unpack_from('<3i',blob,vp)
        face=normal(fi);a=cross(normal(ai),face);b=cross(face,normal(bi));edge=normal(ci)
        da,db=dot(a,edge),dot(b,edge)
        if not da or not db:continue
        # Interior barycenter, kept in raw KCL units until the original factor64
        # conversion into game Fixed12. Floats are fixture selection only.
        center=tuple(round((origin[i]+(a[i]*length/da+b[i]*length/db)/3)*64) for i in range(3))
        result.append({'prism':1+(p-triangles)//16,'center_fixed12':center,'normal_q10':face,'attribute':attribute})
    return result
