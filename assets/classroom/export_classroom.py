import bpy
import os

blend_file = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom\classroom.blend"
output_glb = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom.glb"

print(f"Loading {blend_file}...")
bpy.ops.wm.open_mainfile(filepath=blend_file)

print("Configuring objects for export...")
# Ensure all mesh objects are visible and selectable
for obj in bpy.data.objects:
    obj.hide_viewport = False
    obj.hide_render = False

print(f"Exporting to GLB: {output_glb}...")
bpy.ops.export_scene.gltf(
    filepath=output_glb,
    export_format='GLB',
    use_selection=False,
    export_cameras=True,
    export_lights=True,
    export_apply=True,
    export_materials='EXPORT',
    export_image_format='AUTO'
)
print("Export completed successfully!")
