#[compute]
#version 450

// ==============================================================================
// ASTG GPU PROBE EVALUATOR COMPUTE SHADER (RenderingDevice GLSL)
// Evaluates live radiant transfer from all 128,000 stationary lights to all probes
// in parallel on the GPU every frame without CPU bottlenecks.
// ==============================================================================

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(push_constant) uniform Constants {
    uint probe_count;
    uint total_lights;
    float time_sec;
    uint anim_mode;
} u_const;

// 1. Light Descriptors (Stored directly in GPU VRAM)
struct LightDesc {
    vec4 pos_range;   // xyz = position, w = range
    vec4 color_param; // xyz = base_rgb, w = anim_freq
    vec4 anim_data;   // x = anim_phase, y = base_intensity, z = type, w = is_occluded
};

layout(set = 0, binding = 0, std430) readonly buffer LightDescBuffer {
    LightDesc g_lights[];
};

// 2. Probe Descriptors (Stored directly in GPU VRAM)
struct ProbeDesc {
    vec4 pos;    // xyz = position, w = padding
    vec4 normal; // xyz = normal, w = padding
};

layout(set = 0, binding = 1, std430) readonly buffer ProbeDescBuffer {
    ProbeDesc g_probes[];
};

// 3. Live Evaluated Probe Irradiance Output
layout(set = 0, binding = 2, std430) writeonly buffer ProbeIrradianceBuffer {
    vec4 g_probe_irradiance[]; // xyz = RGB irradiance, w = scalar magnitude
};

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    uint probe_id = gl_GlobalInvocationID.x;
    if (probe_id >= u_const.probe_count) {
        return;
    }

    vec3 p_pos = g_probes[probe_id].pos.xyz;
    vec3 p_norm = g_probes[probe_id].normal.xyz;

    vec3 total_irradiance = vec3(0.0);
    float t = u_const.time_sec;
    uint mode = u_const.anim_mode;

    // Evaluates all stationary lights across the entire scene on the GPU
    for (uint i = 0; i < u_const.total_lights; ++i) {
        LightDesc ld = g_lights[i];
        if (ld.anim_data.w > 0.5) {
            // Occluded
            continue;
        }

        vec3 l_pos = ld.pos_range.xyz;
        float l_range = max(0.01, ld.pos_range.w);
        vec3 to_light = l_pos - p_pos;
        float dist_sq = dot(to_light, to_light);
        float r_sq = l_range * l_range;

        if (dist_sq > r_sq) {
            continue;
        }

        float dist = sqrt(dist_sq);
        vec3 l_dir = to_light / max(0.0001, dist);
        float ndotl = max(0.0, dot(p_norm, l_dir));
        if (ndotl < 0.05) {
            continue;
        }

        // Live Animated Light State evaluated directly in compute register
        vec3 live_color = ld.color_param.xyz;
        float freq = ld.color_param.w;
        float phase = ld.anim_data.x;
        float base_intensity = ld.anim_data.y;
        float live_intensity = base_intensity;

        if (mode == 0) {
            // Static Gold
            live_color = ld.color_param.xyz;
            live_intensity = base_intensity * 1.2;
        } else if (mode == 1) {
            // Intensity Waves
            live_intensity *= (0.2 + 0.8 * sin(freq * 2.5 * t + phase));
        } else if (mode == 2) {
            // RGB Rainbow Waves
            float h = fract(phase * 0.15915 + freq * 0.2 * t);
            live_color = hsv2rgb(vec3(h, 0.85, 1.0));
        } else if (mode == 3) {
            // Strobe Toggling
            bool enabled = ((i + uint(t * 15.0)) % 4) != 0;
            live_intensity = enabled ? (base_intensity * 1.5) : 0.0;
        } else if (mode == 4) {
            // Full Hyperspace Chaos
            float h = fract(phase * 0.15915 + freq * 0.35 * t + float(i) * 0.0002);
            live_color = hsv2rgb(vec3(h, 0.9, 1.0));
            float inten = 0.2 + 0.8 * sin(freq * 3.0 * t + phase);
            bool enabled = ((i + uint(t * 20.0)) % 5) != 0;
            live_intensity = enabled ? (base_intensity * inten * 1.6) : 0.05;
        }

        float atten = (1.0 - (dist / l_range)) * ndotl / (dist_sq + 1.0);
        total_irradiance += live_color * (live_intensity * atten);
    }

    g_probe_irradiance[probe_id] = vec4(total_irradiance, length(total_irradiance));
}
