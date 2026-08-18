// ==============================================================================
// HLSL COMPUTE SHADER: GPU-DRIVEN MASSIVE STATIONARY LIGHT ANIMATOR
// Animates up to 131,072 lights in parallel on GPU with zero PCIe transfer overhead.
// ==============================================================================

struct LightStatic {
    float pos_x, pos_y, pos_z;
    float range;
    float dir_x, dir_y, dir_z;
    uint type;
    float anim_phase;
    float anim_frequency;
    float base_hue;
    uint flags;
};

struct LightDynamic {
    float color_r;
    float color_g;
    float color_b;
    float intensity;
    uint enabled;
    uint generation;
    uint flags;
    uint pad1;
};

cbuffer AnimatorConstants : register(b0) {
    uint g_total_lights;
    float g_time_sec;
    uint g_anim_mode; // 0=Intensity, 1=RGB, 2=Toggle, 3=RGB+Intensity, 4=Full Chaos
    uint g_frame_index;
};

StructuredBuffer<LightStatic> g_light_static : register(t0);
RWStructuredBuffer<LightDynamic> g_light_dynamic : register(u0);

float3 HueToRGB(float h) {
    h = frac(h);
    float r = abs(h * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - abs(h * 6.0f - 2.0f);
    float b = 2.0f - abs(h * 6.0f - 4.0f);
    return saturate(float3(r, g, b));
}

uint Hash(uint a, uint b) {
    uint hash = a * 0x45d9f3b + b * 0x27d4eb2d;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = ((hash >> 16) ^ hash) * 0x45d9f3b;
    hash = (hash >> 16) ^ hash;
    return hash;
}

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint light_id = dispatchThreadId.x;
    if (light_id >= g_total_lights) {
        return;
    }

    LightStatic s = g_light_static[light_id];
    LightDynamic d = g_light_dynamic[light_id];

    float phase = s.anim_phase;
    float freq = s.anim_frequency;
    float base_hue = s.base_hue;

    float intensity = 5.0f;
    float3 color = float3(1.0f, 1.0f, 1.0f);
    uint enabled = 1;

    if (g_anim_mode == 0) {
        // Mode 0: Intensity Only
        intensity = 1.0f + 8.0f * (0.5f + 0.5f * sin(freq * g_time_sec + phase));
        color = HueToRGB(base_hue);
        enabled = 1;
    } else if (g_anim_mode == 1) {
        // Mode 1: RGB Only
        float current_hue = frac(base_hue + freq * 0.1f * g_time_sec);
        color = HueToRGB(current_hue);
        intensity = 5.0f;
        enabled = 1;
    } else if (g_anim_mode == 2) {
        // Mode 2: On/Off Toggle Only
        color = HueToRGB(base_hue);
        intensity = 5.0f;
        uint h = Hash(light_id, g_frame_index / 30);
        enabled = (h % 5 != 0) ? 1 : 0; // 80% on, 20% off
    } else if (g_anim_mode == 3) {
        // Mode 3: RGB + Intensity Combined
        intensity = 1.0f + 8.0f * (0.5f + 0.5f * sin(freq * g_time_sec + phase));
        float current_hue = frac(base_hue + freq * 0.15f * g_time_sec);
        color = HueToRGB(current_hue);
        enabled = 1;
    } else if (g_anim_mode == 4) {
        // Mode 4: Full Chaos (RGB + Intensity + Toggle)
        intensity = 0.5f + 9.5f * (0.5f + 0.5f * sin(freq * 1.5f * g_time_sec + phase));
        float current_hue = frac(base_hue + freq * 0.2f * g_time_sec + (float)light_id * 0.001f);
        color = HueToRGB(current_hue);
        uint h = Hash(light_id, g_frame_index / 20);
        enabled = (h % 10 != 0) ? 1 : 0; // 90% on, 10% off
    }

    d.color_r = color.r;
    d.color_g = color.g;
    d.color_b = color.b;
    d.intensity = intensity;
    d.enabled = enabled;
    d.generation = d.generation + 1;

    g_light_dynamic[light_id] = d;
}
