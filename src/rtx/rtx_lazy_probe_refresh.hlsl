// ==============================================================================
// HLSL COMPUTE SHADER: GPU LAZY PROBE REFRESH (LATE-BOUND LIGHTING EVALUATION)
// Combines persistent sparse geometric transfer coefficients with current LightState.
// ==============================================================================

struct LightState {
    float color_r;
    float color_g;
    float color_b;
    float intensity;
    uint enabled;
    uint generation;
    uint flags;
    uint pad1;
};

struct ProbeLightContribution {
    uint light_id;
    float transfer_r;
    float transfer_g;
    float transfer_b;
};

struct ProbeCacheEntry {
    float irradiance_r;
    float irradiance_g;
    float irradiance_b;
    uint last_eval_signature;
};

cbuffer RefreshConstants : register(b0) {
    uint g_requested_count;
    uint g_total_lights;
    uint g_pad0;
    uint g_pad1;
};

StructuredBuffer<LightState> g_lights : register(t0);
StructuredBuffer<ProbeLightContribution> g_contributions : register(t1);
StructuredBuffer<uint> g_probe_offsets : register(t2);
StructuredBuffer<uint> g_probe_counts : register(t3);
StructuredBuffer<uint> g_requested_probe_ids : register(t4);

RWStructuredBuffer<ProbeCacheEntry> g_probe_cache : register(u0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID) {
    uint idx = dispatchThreadId.x;
    if (idx >= g_requested_count) {
        return;
    }

    uint probe_id = g_requested_probe_ids[idx];
    uint offset = g_probe_offsets[probe_id];
    uint count = g_probe_counts[probe_id];

    float3 total_irradiance = float3(0.0f, 0.0f, 0.0f);
    uint signature = 0;

    for (uint i = 0; i < count; ++i) {
        ProbeLightContribution c = g_contributions[offset + i];
        if (c.light_id < g_total_lights) {
            LightState L = g_lights[c.light_id];
            signature = signature * 31 + L.generation;
            if (L.enabled != 0 && L.intensity > 0.0001f) {
                float3 emission = float3(L.color_r, L.color_g, L.color_b) * L.intensity;
                total_irradiance += emission * float3(c.transfer_r, c.transfer_g, c.transfer_b);
            }
        }
    }

    ProbeCacheEntry entry;
    entry.irradiance_r = total_irradiance.x;
    entry.irradiance_g = total_irradiance.y;
    entry.irradiance_b = total_irradiance.z;
    entry.last_eval_signature = signature;

    g_probe_cache[probe_id] = entry;
}
