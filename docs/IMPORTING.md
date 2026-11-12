# Bring in your own models

Click **Import model** at the top, press **Ctrl+O** while the viewport has focus, or drag **one file** from Explorer into the application. This replaces the study model after the entire file has been parsed successfully. The model can then be moved, rotated, colored and lit like the built-in head. Export multiple objects together if you want them treated as a single study model.

| Format | Supported |
|---|---|
| Wavefront OBJ | Positions, positive/negative face indices, triangle and convex polygon faces |
| STL | ASCII and binary, including binary headers beginning with `solid` |
| PLY | ASCII, binary little-endian and binary big-endian; vertex and face properties |
| Blender `.blend` | Export from Blender to OBJ, STL or PLY first |
| FBX, GLB/glTF, USD, other project files | Export or convert to OBJ/STL/PLY first |

## From Blender

1. Open the project and select the mesh objects you want to study.
2. Use **File → Export → Wavefront OBJ**. STL and PLY are also supported alternatives.
3. Enable **Selection Only** if you want to exclude other objects from the file.
4. Apply the desired modifiers and export evaluated mesh geometry. Triangulate concave polygons (for example with Blender's Triangulate modifier).
5. For OBJ, choose **Y Up**. This application uses Y as the vertical axis. If another exporter writes a sideways model, adjust its pitch/yaw/roll in the Object tab; you may need to raise it above the floor afterward. **F** frames the imported model.
6. Save the exported file and drag it into Shading Help.

The app automatically centers the mesh, puts its lowest Y point on the floor, and scales its largest dimension to 2.8 scene units. Original Blender world size is not preserved. The imported model rotates around its center.

## Color, shading and posing

Imports are **geometry only**. All faces use geometric flat normals, and the Surface tab's material color applies to the entire model. Texture maps, OBJ/PLY vertex colors, MTL files, Blender material nodes, authored smooth normals, skeletons and animations are not imported. Export a posed mesh from Blender to study that pose; imported meshes are not articulated by the built-in mannequin controls.

## File limits and saving

- Maximum file size: 64 MB.
- Maximum OBJ/PLY vertex count: 300,000.
- Maximum resulting triangle count: 200,000.
- Polygon faces must have at most 512 indices. Triangulate concave faces before importing.
- A PLY point cloud needs to be converted into a mesh with faces first.
- Scene files reference the original model by its absolute path. They do not embed or copy the asset. Keep the exported model, or import it again if you move it.
- Failed or unsupported imports report the problem and preserve the existing scene.

Use Blender's Decimate modifier to reduce a dense sculpt before export. Smaller meshes reduce import time, acceleration-structure work and GPU rendering cost. File import and scene preparation remain CPU tasks; the GPU handles ray tracing, shadows and bounce calculations once geometry is uploaded.

Official Blender documentation: [Importing and exporting](https://docs.blender.org/manual/en/5.0/files/import_export/) and [supported pipeline formats](https://www.blender.org/features/pipeline/).
