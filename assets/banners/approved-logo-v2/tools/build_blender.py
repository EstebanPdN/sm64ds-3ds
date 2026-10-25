"""Render a logo-only rebuild with real borders, bevels and curved DS geometry."""
import bpy,bmesh,json,math
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[1]
data=json.loads((ROOT/'source'/'geometry.json').read_text())
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
scene=bpy.context.scene;model=bpy.data.collections.get('Collection');model.name='LOGO - real letter components'
studio=bpy.data.collections.new('STUDIO - cameras and lights');scene.collection.children.link(studio)

def linear(v):return v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4
def rgb(hex):return tuple(linear(int(hex[i:i+2],16)/255) for i in (1,3,5))
materials={}
for name,hex in data['colors'].items():
    variants=[''] if name not in ('cyan','blue','green','yellow','red','red_metal') else ['','_light','_dark']
    for variant in variants:
        m=bpy.data.materials.new(name+variant);m.use_nodes=True;m.use_backface_culling=True
        color=rgb(hex)
        if variant=='_light':color=tuple(min(1,v*1.30+.025) for v in color)
        if variant=='_dark':color=tuple(v*.58 for v in color)
        p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*color,1)
        p.inputs['Roughness'].default_value=.62 if name.startswith('card') else (.22 if name.startswith(('silver','red_metal')) or name=='gold' else .48)
        p.inputs['Metallic'].default_value=.92 if name.startswith(('silver','red_metal')) or name=='gold' else 0
        p.inputs['Specular IOR Level'].default_value=.32
        if name=='black':p.inputs['Roughness'].default_value=1;p.inputs['Specular IOR Level'].default_value=0
        materials[name+variant]=m

meshes=[];audit={};scale=2.25
for part in data['parts']:
    mesh=bpy.data.meshes.new(part['name']);mesh.from_pydata(part['vertices'],[],part['faces']);mesh.update()
    ob=bpy.data.objects.new(part['name'],mesh);model.objects.link(ob)
    slots=list(dict.fromkeys(part['materials']))
    for key in slots:mesh.materials.append(materials[key])
    for poly,key in zip(mesh.polygons,part['materials']):poly.material_index=slots.index(key)
    bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
    bad=sum(not e.is_manifold for e in bm.edges);audit[ob.name]=bad;assert bad==0,(ob.name,bad)
    bm.to_mesh(mesh);bm.free()
    for v in mesh.vertices:v.co*=scale;v.co.y+=1.0
    bpy.context.view_layer.objects.active=ob;ob.select_set(True)
    mod=ob.modifiers.new('Native triangulation','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
    ob.select_set(False);meshes.append(ob)

def aim(ob,target=(0,1,-.2)):
    z=(ob.location-Vector(target)).normalized()
    x=Vector((0,1,0)).cross(z).normalized();y=z.cross(x)
    ob.rotation_euler=Matrix((x,y,z)).transposed().to_euler()
def camera(name,pos):
    d=bpy.data.cameras.new(name);o=bpy.data.objects.new(name,d);studio.objects.link(o);o.location=pos;aim(o)
    d.type='PERSP';d.sensor_fit='VERTICAL';d.sensor_height=24;d.lens=24/(2*math.tan(math.pi/12));d.clip_start=10;d.clip_end=1000
    return o
front=camera('Front - HOME camera',(0,1,44.786));volume=camera('Three quarter - inspect real volume',(10,5,44.786))
side=camera('Stronger side view',(21,7,42))
for name,pos,power,size,color in [('Large white key',(-14,20,28),7000,18,(1,.98,.94)),('Broad silver reflection',(15,4,20),5500,16,(.86,.93,1)),('Lower reflection',(-2,-15,19),2200,12,(1,.85,.65))]:
    d=bpy.data.lights.new(name,'AREA');d.energy=power;d.shape='RECTANGLE';d.size=size;d.size_y=size*.5;d.color=color
    o=bpy.data.objects.new(name,d);studio.objects.link(o);o.location=pos;aim(o)
scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs['Color'].default_value=(.65,.68,.72,1)
scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=.65
scene.render.engine='CYCLES';scene.cycles.samples=48;scene.cycles.use_denoising=True
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.render.film_transparent=True
scene.camera=front;scene.render.resolution_x=1500;scene.render.resolution_y=900;scene.render.resolution_percentage=100

# Keep individual closed solids in Blender; export a temporary combined runtime mesh.
bpy.ops.object.select_all(action='DESELECT');copies=[]
for o in meshes:
    c=o.copy();c.data=o.data.copy();model.objects.link(c);c.select_set(True);copies.append(c)
bpy.context.view_layer.objects.active=copies[0];bpy.ops.object.join();runtime=bpy.context.object;runtime.name='Logo';runtime.data.name='Logo'
bpy.ops.export_scene.gltf(filepath=str(ROOT/'ready-to-use'/'sm64ds-logo.glb'),export_format='GLB',use_selection=True,export_yup=False,export_animations=False,export_cameras=False,export_lights=False)
bpy.data.objects.remove(runtime,do_unlink=True)
stats={'design':'Logo only; no star. True closed letter solids, ink borders, sloped bevels and hand-authored cubic DS outlines.','triangles':sum(len(o.data.polygons) for o in meshes),'geometric_vertices':sum(len(o.data.vertices) for o in meshes),'components':len(meshes),'letters':14,'source_image_used_as_texture':False,'textures':0,'depth':{'SUPER':1.55,'MARIO':2.1,'64':.95,'DS':1.0,'previous_SUPER':.8,'previous_MARIO':1.16,'common_projected_depth_vector':'Y -0.35 per unit Z depth; outward X flare 4.5%'},'metallic':{'64_face':.92,'64_body':.92,'DS_face':.92,'DS_body':.92},'manifold_edges_by_component':audit,'hardware_tested':False,'previews':'Blender renders, not hardware captures'}
(ROOT/'ready-to-use'/'validation.json').write_text(json.dumps(stats,indent=2)+'\n')
for screen in bpy.data.screens:
    for a in screen.areas:
        if a.type=='VIEW_3D':a.spaces.active.region_3d.view_perspective='CAMERA';a.spaces.active.shading.type='MATERIAL'
bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'sm64ds-logo.blend'))
for name,cam in [('front',front),('three-quarter',volume),('side',side)]:
    scene.camera=cam;scene.render.filepath=str(ROOT/'previews'/f'{name}.png');bpy.ops.render.render(write_still=True)
scene.camera=front;scene.render.resolution_x=400;scene.render.resolution_y=240;scene.render.filepath=str(ROOT/'previews'/'HOME-400x240.png');bpy.ops.render.render(write_still=True)
print('LOGO ONLY REBUILD',json.dumps(stats))
