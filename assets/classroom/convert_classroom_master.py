import bpy
import os

blend_file = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom\classroom.blend"
output_glb = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom.glb"

print(f"Loading {blend_file} in Blender 5.1...")
bpy.ops.wm.open_mainfile(filepath=blend_file)

# 1. Make all linked assets local
print("Making all objects, meshes, and materials local...")
try:
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.make_local(type='ALL')
except Exception as e:
    print("make_local notice:", e)

# 2. Remove all non-mesh helper objects (armatures, lattices, empties, curves, cameras, lights)
print("Removing armatures, hooks, lattices, and non-mesh objects...")
for obj in list(bpy.data.objects):
    if obj.type in ('ARMATURE', 'LATTICE', 'CAMERA', 'LIGHT', 'SPEAKER'):
        bpy.data.objects.remove(obj, do_unlink=True)
    elif obj.type == 'MESH':
        # Clear all skeleton, armature, hook, particle modifiers to produce clean static meshes
        obj.modifiers.clear()

# Texture cache
image_cache = {}
for img in bpy.data.images:
    image_cache[img.name.lower()] = img
    base = os.path.splitext(img.name.lower())[0]
    image_cache[base] = img

def get_image_matching(pattern):
    pattern = pattern.lower()
    for k, img in image_cache.items():
        if pattern in k and img.has_data:
            return img
    for img in bpy.data.images:
        if pattern in img.filepath.lower():
            return img
    return None

print("Upgrading all materials to standard Principled BSDF PBR node trees...")
for mat in bpy.data.materials:
    mat.use_nodes = True
    nt = mat.node_tree
    nodes = nt.nodes
    links = nt.links
    
    m_name = mat.name.lower()
    
    found_images = []
    for n in nodes:
        if n.type == 'TEX_IMAGE' and n.image:
            found_images.append(n.image)
            
    base_color_img = found_images[0] if found_images else None
    
    if not base_color_img:
        if "floor" in m_name or "woodplank" in m_name:
            base_color_img = get_image_matching("woodplanks") or get_image_matching("woodfloor")
        elif "blackboard" in m_name or "board" in m_name:
            base_color_img = get_image_matching("blackboard")
        elif "cork" in m_name:
            base_color_img = get_image_matching("cork")
        elif "map" in m_name:
            base_color_img = get_image_matching("europemap")
        elif "clock" in m_name:
            base_color_img = get_image_matching("wallclock")
        elif "radiator" in m_name:
            base_color_img = get_image_matching("radiator")
        elif "book" in m_name:
            base_color_img = get_image_matching("zapbook") or get_image_matching("leather")
        elif "drawing" in m_name:
            base_color_img = get_image_matching("childdrawing")
        elif "wall" in m_name or "plaster" in m_name:
            base_color_img = get_image_matching("paintedplaster") or get_image_matching("wallpaint")
        elif "metal" in m_name:
            base_color_img = get_image_matching("baremetal")
        elif "paper" in m_name or "waste" in m_name:
            base_color_img = get_image_matching("paper")
        elif "pencil" in m_name or "pen" in m_name:
            base_color_img = get_image_matching("pencil")
            
    nodes.clear()
    
    output_node = nodes.new(type='ShaderNodeOutputMaterial')
    output_node.location = (350, 0)
    
    principled = nodes.new(type='ShaderNodeBsdfPrincipled')
    principled.location = (0, 0)
    links.new(principled.outputs['BSDF'], output_node.inputs['Surface'])
    
    if base_color_img:
        tex_node = nodes.new(type='ShaderNodeTexImage')
        tex_node.image = base_color_img
        tex_node.location = (-350, 0)
        links.new(tex_node.outputs['Color'], principled.inputs['Base Color'])
    else:
        # Default natural albedo tones rather than stark 1.0 white
        if "wall" in m_name or "plaster" in m_name or "ceiling" in m_name:
            principled.inputs['Base Color'].default_value = (0.75, 0.73, 0.68, 1.0)
        elif "wood" in m_name or "chair" in m_name or "desk" in m_name:
            principled.inputs['Base Color'].default_value = (0.45, 0.32, 0.22, 1.0)
        elif "metal" in m_name or "pipe" in m_name:
            principled.inputs['Base Color'].default_value = (0.35, 0.36, 0.38, 1.0)
        elif "plastic" in m_name:
            principled.inputs['Base Color'].default_value = (0.50, 0.52, 0.55, 1.0)
        else:
            principled.inputs['Base Color'].default_value = (0.60, 0.58, 0.55, 1.0)
            
    if "metal" in m_name or "baremetal" in m_name or "pipe" in m_name:
        principled.inputs['Metallic'].default_value = 0.85
        principled.inputs['Roughness'].default_value = 0.30
    elif "glass" in m_name or "frosted" in m_name:
        principled.inputs['Roughness'].default_value = 0.05
        principled.inputs['Metallic'].default_value = 0.0
    elif "wood" in m_name or "floor" in m_name or "plank" in m_name or "desk" in m_name or "chair" in m_name:
        principled.inputs['Roughness'].default_value = 0.50
        principled.inputs['Metallic'].default_value = 0.0
    elif "blackboard" in m_name:
        principled.inputs['Roughness'].default_value = 0.80
        principled.inputs['Metallic'].default_value = 0.0
    else:
        principled.inputs['Roughness'].default_value = 0.65
        principled.inputs['Metallic'].default_value = 0.0

print("Packing all texture resources into GLB...")
try:
    bpy.ops.file.pack_all()
except Exception as e:
    print("Pack notice:", e)

print(f"Exporting clean static GLB to {output_glb}...")
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
print("✅ Pure Static PBR GLB export finished successfully!")
