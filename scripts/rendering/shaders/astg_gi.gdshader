shader_type spatial;
render_mode blend_mix, depth_draw_opaque, cull_back, diffuse_burley, specular_schlick_ggx;

// PBR Base Parameters
uniform vec4 albedo : source_color = vec4(1.0, 1.0, 1.0, 1.0);
uniform sampler2D texture_albedo : source_color, filter_linear_mipmap, repeat_enable;
uniform bool use_texture_albedo = false;

uniform float roughness : hint_range(0.0, 1.0) = 0.6;
uniform sampler2D texture_roughness : filter_linear_mipmap, repeat_enable;
uniform bool use_texture_roughness = false;

uniform float metallic : hint_range(0.0, 1.0) = 0.0;
uniform sampler2D texture_metallic : filter_linear_mipmap, repeat_enable;
uniform bool use_texture_metallic = false;

uniform sampler2D texture_normal : hint_normal, filter_linear_mipmap, repeat_enable;
uniform bool use_texture_normal = false;
uniform float normal_scale : hint_range(-16.0, 16.0) = 1.0;

uniform sampler2D texture_orm : filter_linear_mipmap, repeat_enable;
uniform bool use_texture_orm = false;

uniform vec3 uv1_scale = vec3(1.0, 1.0, 1.0);
uniform vec3 uv1_offset = vec3(0.0, 0.0, 0.0);

// ASTG Volumetric GI Parameters
uniform sampler3D gi_volume_texture : filter_linear, repeat_disable;
uniform vec3 volume_min = vec3(-7.5, 0.0, -5.5);
uniform vec3 volume_size = vec3(15.0, 4.0, 11.0);
uniform float gi_intensity : hint_range(0.0, 5.0) = 1.2;
uniform bool gi_enabled = true;

varying vec3 v_world_pos;
varying vec3 v_world_normal;

void vertex() {
	v_world_pos = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
	v_world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
}

void fragment() {
	vec2 base_uv = UV * uv1_scale.xy + uv1_offset.xy;
	
	// 1. Albedo evaluation
	vec3 base_albedo = albedo.rgb;
	if (use_texture_albedo) {
		vec4 albedo_tex = texture(texture_albedo, base_uv);
		base_albedo *= albedo_tex.rgb;
	}
	ALBEDO = base_albedo;
	
	// 2. ORM or individual Roughness/Metallic evaluation
	if (use_texture_orm) {
		vec4 orm_tex = texture(texture_orm, base_uv);
		// glTF standard: R = Occlusion, G = Roughness, B = Metallic
		ROUGHNESS = orm_tex.g * roughness;
		METALLIC = orm_tex.b * metallic;
		AO = orm_tex.r;
	} else {
		if (use_texture_roughness) {
			ROUGHNESS = texture(texture_roughness, base_uv).r * roughness;
		} else {
			ROUGHNESS = roughness;
		}
		
		if (use_texture_metallic) {
			METALLIC = texture(texture_metallic, base_uv).r * metallic;
		} else {
			METALLIC = metallic;
		}
	}
	
	// 3. Normal mapping
	if (use_texture_normal) {
		NORMAL_MAP = texture(texture_normal, base_uv).rgb;
		NORMAL_MAP_DEPTH = normal_scale;
	}
	
	// 4. ASTG / DDGI / GT Indirect Diffuse GI Injection
	if (gi_enabled) {
		vec3 sample_pos = v_world_pos + v_world_normal * 0.12;
		vec3 uvw = clamp((sample_pos - volume_min) / volume_size, vec3(0.001), vec3(0.999));
		vec3 indirect_radiance = texture(gi_volume_texture, uvw).rgb;
		
		// Indirect diffuse bounce modulated by surface albedo
		EMISSION = base_albedo * indirect_radiance * gi_intensity;
	} else {
		EMISSION = vec3(0.0);
	}
}
