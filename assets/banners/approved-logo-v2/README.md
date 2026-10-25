# Super Mario 64 DS — logo-only 3D revision

## Current scope

This is the logo-only revision requested after the owner rejected the first projected-image model. The star is intentionally absent at this stage. This folder contains an editable Blender scene, native-axis GLB and a structurally checked CGFX candidate. It does not contain an installed or physically accepted HOME Menu banner.

## Latest requested changes

- Increased SUPER body depth from 0.80 to 1.55 scene units, a 93.75% increase.
- Increased MARIO body depth from 1.16 to 2.10 scene units, an 81.03% increase.
- Gave the 64 a red metallic face, gold metallic bevel and gold metallic body, with 0.95 units of depth.
- Gave DS metallic silver faces, curved bevels and a metallic body, with 1.00 unit of depth.
- Aligned all body extrusions to the same projected direction: 7 reference pixels of downward displacement per unit of depth, plus 4.5% outward X flare about the composition center.
- Retained a separate matte black border around the colored SUPER and MARIO faces.

## Modeling method

The colored letter outlines are traced from the supplied reference into polygons. The D and S use independently authored cubic outlines, including a real counter in the D. Colored caps, sloping bevels, borders and deep side walls are actual geometry. The cartoon amber side bands are material regions on closed side walls. The reference image is not projected onto any surface. There are no image textures in this revision.

The metallic materials use Blender's metallic shading with a metallic value of 0.92. Their highlights are produced by the scene lighting and modeled bevels. The CGFX converter's material model differs from Blender's renderer; the preview is not proof of identical reflections on a 3DS.

## Deliverables

- `sm64ds-logo.blend`: individual editable letter components, named cameras and studio lights.
- `source/letter-outlines.svg`: deterministic editable outlines.
- `source/geometry.json`: generated closed component geometry.
- `source/owner-reference.png`: the owner's reference, used for tracing and comparison only.
- `ready-to-use/sm64ds-logo.glb`: combined runtime mesh, native axes, no animations or image textures.
- `ready-to-use/sm64ds-logo.cgfx`: checked native model candidate, 452,008 bytes.
- `ready-to-use/validation.json`: geometry, depth, materials, serialized dictionary checks and SHA-256.
- `previews/front.png`, `previews/three-quarter.png`, `previews/side.png`: Blender renders.
- `previews/HOME-400x240.png`: Blender approximation at the target display size.

## Validation and remaining work

The model has 6,400 triangles, 3,234 geometric vertices and 40 closed component meshes. Every component passed the manifold-edge check. The exported GLB contains no image resources. The CGFX is below the 524,288-byte budget with 72,280 bytes of remaining space. The serialized Patricia dictionaries and the direct Logo mesh-node/billboard bindings passed their checks.

These checks establish geometry and resource structure only. The current scope still requires visual review of this logo. A later complete banner would add the independently rotating star, final CBMD/CWAV packaging and testing in HOME Menu on physical hardware. No game source, existing game package, ROM, save or release was modified by this work.

## Rebuilding

Use Python with Pillow, NumPy, OpenCV, Shapely 2.1+ and gltflib, then Blender 5.2 or compatible. Run `tools/build_geometry.py`, run Blender in background mode with `tools/build_blender.py`, and run `tools/convert_cgfx.py` after the GLB export completes. The converter uses the existing local pycgfx copy under the sibling Minish Cap project and the existing Star Fox serialized dictionary validator; it does not modify either tool.

The reference artwork belongs to its original rights holder. This private project revision does not establish redistribution rights for Nintendo artwork or authorize a public release.
