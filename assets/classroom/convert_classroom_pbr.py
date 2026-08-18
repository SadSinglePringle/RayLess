import bpy
import os

blend_file = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom\classroom.blend"
output_glb = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom.glb"

print(f"Loading {blend_file}...")
bpy.ops.wm.open_mainfile(filepath=blend_file)

# Find all texture file paths on disk for fallback mapping
textures_dir = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom\textures"
base_tex_dir = os.path.join(textures_dir, "_baseTextures")
child_tex_dir = os.path.join(textures_dir, "childDrawing")
assets_dir = r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom\assets"

def find_image(name_pattern):
    for root, dirs, files in os.walk(r"C:\Users\Brand\Documents\hermes\Rayless\assets\classroom\classroom"):
        for f in files:
            if name_pattern.lower() in f.lower() and f.lower().endswith(('.jpg', '.png', '.tga', '.hdr')):
                full_path = os.path.join(root, f)
                try:
                    return bpy.data.images.load(full_path, check_existing=True)
                except:
                    pass
    return None

print("Upgrading all materials to standard Principled BSDF PBR node trees...")
for mat in bpy.data.materials:
    if not mat.use_nodes:
        mat.use_nodes = True
        
    nt = mat.node_tree
    nodes = nt.nodes
    links = nt.links
    
    # Find existing Image Texture nodes
    img_nodes = [n for n in nodes if n.type == 'TEX_IMAGE']
    found_img = None
    if img_nodes and img_nodes[0].image:
        found_img = img_nodes[0].image
        
    # Search by material name if no image node present
    m_name = mat.name.lower()
    if not found_img:
        if "floor" in m_name or "wood" in m_name:
            found_img = find_image("base_woodFloor") or find_image("base_brightWood")
        elif "blackboard" in m_name or "black_board" in m_name:
            found_img = find_image("blackBoard")
        elif "map" in m_name:
            found_img = find_image("europeMap")
        elif "cork" in m_name:
            found_img = find_image("cork")
        elif "wall" in m_name or "plaster" in m_name:
            found_img = find_image("base_paintedPlasterWall") or find_image("base_wallPaint")
        elif "clock" in m_name:
            found_img = find_image("wallClock")
        elif "book" in m_name:
            found_img = find_image("zapBook")
        elif "radiator" in m_name:
            found_img = find_image("radiator_AO")
        elif "drawing" in m_name:
            found_img = find_image("childDrawing_01")
        elif "metal" in m_name:
            found_img = find_image("base_bareMetal")
        elif "glass" in m_name:
            found_img = find_image("glass")
            
    # Find or create Principled BSDF
    principled = None
    for n in nodes:
        if n.type == 'BSDF_PRINCIPLED':
            principled = n
            break
            
    if not principled:
        principled = nodes.new(type='ShaderNodeBsdfPrincipled')
        principled.location = (0, 0)
        
    # Find Output Material node
    output_node = None
    for n in nodes:
        if n.type == 'OUTPUT_MATERIAL':
            output_node = n
            break
    if not output_node:
        output_node = nodes.new(type='ShaderNodeOutputMaterial')
        output_node.location = (300, 0)
        
    # Connect Principled to Output Surface
    links.new(principled.outputs['BSDF'], output_node.inputs['Surface'])
    
    # If image texture exists, wire it to Base Color
    if found_img:
        tex_node = None
        for n in nodes:
            if n.type == 'TEX_IMAGE' and n.image == found_img:
                tex_node = n
                break
        if not tex_node:
            tex_node = nodes.new(type='ShaderNodeTexImage')
            tex_node.image = found_img
            tex_node.location = (-300, 0)
            
        links.new(tex_node.outputs['Color'], principled.inputs['Base Color'])
        
    # Material specific roughness / metallic defaults
    if "metal" in m_name:
        principled.inputs['Metallic'].default_value = 0.9
        principled.inputs['Roughness'].default_value = 0.25
    elif "glass" in m_name:
        principled.inputs['Roughness'].default_value = 0.05
        if 'Transmission Weight' in principled.inputs:
            principled.inputs['Transmission Weight'].default_value = 0.95
        elif 'Transmission' in principled.inputs:
            principled.inputs['Transmission'].default_value = 0.95
    elif "wood" in m_name or "floor" in m_name:
        principled.inputs['Roughness'].default_value = 0.4
    else:
        principled.inputs['Roughness'].default_value = 0.6

print("Packing and exporting GLB with full PBR materials and textures...")
bpy.ops.export_scene.gltf(
    filepath=output_glb,
    export_format='GLB',
    use_selection=False,
    export_cameras=False,
    export_lights=False,
    export_apply=True,
    export_materials='EXPORT',
    export_image_format='AUTO',
    export_animations=False
)
print("PBR GLB export finished successfully!")
