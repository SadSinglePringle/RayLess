import bpy
import os

blend_file = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom\classroom.blend"
output_glb = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom.glb"
classroom_root = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom"

print(f"Loading {blend_file} in Blender 5.1...")
bpy.ops.wm.open_mainfile(filepath=blend_file)

# 1. Make all linked objects, meshes, and materials local
print("Making all objects and materials local...")
try:
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.make_local(type='ALL')
except Exception as e:
    print("make_local notice:", e)

# 2. Delete all non-mesh helper objects and strip dangling modifiers
print("Removing armatures, lattices, empties, and clearing skeleton modifiers...")
for obj in list(bpy.data.objects):
    if obj.type in ('ARMATURE', 'LATTICE', 'CAMERA', 'LIGHT', 'SPEAKER'):
        bpy.data.objects.remove(obj, do_unlink=True)
    elif obj.type == 'MESH':
        obj.modifiers.clear()

# 3. Load all textures on disk into Blender image data
print("Scanning and loading all texture files from disk...")
disk_images = {}
for root, dirs, files in os.walk(classroom_root):
    for f in files:
        if f.lower().endswith(('.jpg', '.png', '.tga', '.hdr', '.jpeg')):
            full_path = os.path.join(root, f)
            name_key = os.path.splitext(f.lower())[0]
            try:
                img = bpy.data.images.load(full_path, check_existing=True)
                img.pack()
                disk_images[name_key] = img
                disk_images[f.lower()] = img
            except Exception as err:
                print("Image load error for", f, ":", err)

def find_best_image(name):
    name = name.lower()
    for k, img in disk_images.items():
        if k in name or name in k:
            return img
    return None

print(f"Loaded {len(disk_images)} textures from disk. Upgrading materials...")

# 4. Convert all materials to PBR Principled BSDF with textures wired
for mat in bpy.data.materials:
    mat.use_nodes = True
    nt = mat.node_tree
    nodes = nt.nodes
    links = nt.links
    
    m_name = mat.name.lower()
    
    # Check if there's already an existing image
    found_img = None
    for n in nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            found_img = n.image
            break
            
    # Heuristic mapping based on material name and textures
    if not found_img:
        if "floor" in m_name or "woodplank" in m_name:
            found_img = disk_images.get("woodplanks") or disk_images.get("base_woodfloor")
        elif "blackboard" in m_name or "board" in m_name:
            found_img = disk_images.get("blackboard")
        elif "cork" in m_name:
            found_img = disk_images.get("cork")
        elif "map" in m_name:
            found_img = disk_images.get("europemap")
        elif "clock" in m_name:
            found_img = disk_images.get("wallclock")
        elif "radiator" in m_name:
            found_img = disk_images.get("radiator_ao")
        elif "zap" in m_name or "book" in m_name:
            found_img = disk_images.get("zapbook") or disk_images.get("base_leather")
        elif "drawing" in m_name:
            for i in range(1, 9):
                key = f"childdrawing_0{i}" if i < 10 else f"childdrawing_{i}"
                if str(i) in m_name and key in disk_images:
                    found_img = disk_images[key]
                    break
            if not found_img:
                found_img = disk_images.get("childdrawing_01")
        elif "wall" in m_name or "plaster" in m_name or "ceiling" in m_name:
            found_img = disk_images.get("base_paintedplasterwall") or disk_images.get("base_wallpaint")
        elif "metal" in m_name or "pipe" in m_name:
            found_img = disk_images.get("base_baremetal") or disk_images.get("base_bluredmetal")
        elif "leather" in m_name:
            found_img = disk_images.get("base_leather")
        elif "wood" in m_name or "desk" in m_name or "chair" in m_name:
            found_img = disk_images.get("base_brownwood") or disk_images.get("base_darkwood") or disk_images.get("base_brightwood")
        elif "glass" in m_name or "window" in m_name:
            found_img = disk_images.get("glass") or disk_images.get("base_frostedglass")
        elif "paper" in m_name or "waste" in m_name or "feuille" in m_name:
            found_img = disk_images.get("crinkledpaper") or disk_images.get("base_paper_01")
        elif "pencil" in m_name or "pen" in m_name:
            found_img = disk_images.get("pencil_color") or disk_images.get("eraser")
            
    # Clear old nodes
    nodes.clear()
    
    output_node = nodes.new(type='ShaderNodeOutputMaterial')
    output_node.location = (350, 0)
    
    principled = nodes.new(type='ShaderNodeBsdfPrincipled')
    principled.location = (0, 0)
    links.new(principled.outputs['BSDF'], output_node.inputs['Surface'])
    
    # Wire Texture to Base Color
    if found_img:
        tex_node = nodes.new(type='ShaderNodeTexImage')
        tex_node.image = found_img
        tex_node.location = (-350, 0)
        links.new(tex_node.outputs['Color'], principled.inputs['Base Color'])
    else:
        # Natural fallback base colors
        if "wall" in m_name or "plaster" in m_name:
            principled.inputs['Base Color'].default_value = (0.75, 0.72, 0.68, 1.0)
        elif "wood" in m_name or "chair" in m_name or "desk" in m_name:
            principled.inputs['Base Color'].default_value = (0.42, 0.28, 0.18, 1.0)
        elif "metal" in m_name or "pipe" in m_name or "lamp" in m_name:
            principled.inputs['Base Color'].default_value = (0.35, 0.36, 0.38, 1.0)
        elif "blackboard" in m_name:
            principled.inputs['Base Color'].default_value = (0.12, 0.20, 0.15, 1.0)
        else:
            principled.inputs['Base Color'].default_value = (0.55, 0.52, 0.48, 1.0)
            
    # PBR Material parameter tuning
    if "metal" in m_name or "baremetal" in m_name or "pipe" in m_name or "lamp" in m_name:
        principled.inputs['Metallic'].default_value = 0.85
        principled.inputs['Roughness'].default_value = 0.28
    elif "glass" in m_name or "frosted" in m_name or "window" in m_name:
        principled.inputs['Roughness'].default_value = 0.05
        principled.inputs['Metallic'].default_value = 0.0
        # Transparent glass in glTF
        principled.inputs['Alpha'].default_value = 0.20
        mat.blend_method = 'BLEND'
    elif "wood" in m_name or "floor" in m_name or "plank" in m_name or "desk" in m_name or "chair" in m_name:
        principled.inputs['Roughness'].default_value = 0.45
        principled.inputs['Metallic'].default_value = 0.0
    elif "blackboard" in m_name:
        principled.inputs['Roughness'].default_value = 0.80
        principled.inputs['Metallic'].default_value = 0.0
    else:
        principled.inputs['Roughness'].default_value = 0.65
        principled.inputs['Metallic'].default_value = 0.0

print("Packing all loaded textures into Blender blend data...")
try:
    bpy.ops.file.pack_all()
except Exception as e:
    print("pack_all notice:", e)

print(f"Exporting clean static PBR GLB to {output_glb}...")
bpy.ops.export_scene.gltf(
    filepath=output_glb,
    export_format='GLB',
    use_selection=False,
    export_skins=False,
    export_morph=False,
    export_lights=False,
    export_cameras=False,
    export_materials='EXPORT',
    export_image_format='AUTO',
    export_apply=True,
    export_animations=False
)
print("✅ Clean Photorealistic PBR GLB export finished successfully!")
