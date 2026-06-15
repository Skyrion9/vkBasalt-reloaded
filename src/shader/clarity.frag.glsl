#version 450

// Clarity Shader - Optimized Single-Pass Cross-Convolution Contrast (Bilateral Edge-Aware)

layout(set = 0, binding = 0) uniform sampler2D img;

// 6 Specialization Constants (darkIntensity and lightIntensity removed due to bilateral edge-stopping)
layout(constant_id = 0) const float radius = 1.0;
layout(constant_id = 1) const float offset = 5.0;
layout(constant_id = 2) const float strength = 1.0;
layout(constant_id = 3) const int blendMode = 1; 
layout(constant_id = 4) const int blendIfDark = 40;
layout(constant_id = 5) const int blendIfLight = 220;

layout(push_constant) uniform PushConstants {
    vec2 texelSize;
    vec2 _padding;
} pc;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

float getLuma(vec3 rgb) {
    return dot(rgb, vec3(0.32786885, 0.655737705, 0.0163934436));
}

float applyBlendMode(float luma, float sharp) {
    if (blendMode == 0) return mix(2.0 * luma * sharp + luma * luma * (1.0 - 2.0 * sharp), 2.0 * luma * (1.0 - sharp) + sqrt(luma) * (2.0 * sharp - 1.0), step(0.49, sharp));
    else if (blendMode == 1) return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, luma));
    else if (blendMode == 2) return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, sharp));
    else if (blendMode == 3) return clamp(2.0 * luma * sharp, 0.0, 1.0);
    else if (blendMode == 4) return mix(2.0 * luma * sharp, luma / max(2.0 * (1.0 - sharp), 0.0001), step(0.50, sharp));
    else if (blendMode == 5) return luma + 2.0 * sharp - 1.0;
    else return clamp(luma + (sharp - 0.5), 0.0, 1.0);
}

void main() {
    vec4 centerColor = textureLod(img, textureCoord, 0.0);
    vec3 orig = centerColor.rgb;
    float luma = getLuma(orig);

    // Early exit for near-black pixels
    if (luma <= 0.0001) {
        fragColor = centerColor;
        return;
    }

    vec2 texelSize = pc.texelSize;
    float baseOffset = 1.5 * radius * offset;
    vec2 step1 = baseOffset * texelSize;
    vec2 step2 = step1 * 3.0;

    // Raw samples for bilateral delta accumulation
    float h1 = getLuma(textureLod(img, textureCoord + vec2(step1.x, 0.0), 0.0).rgb);
    float h2 = getLuma(textureLod(img, textureCoord - vec2(step1.x, 0.0), 0.0).rgb);
    float h3 = getLuma(textureLod(img, textureCoord + vec2(step2.x, 0.0), 0.0).rgb);
    float h4 = getLuma(textureLod(img, textureCoord - vec2(step2.x, 0.0), 0.0).rgb);
    
    float v1 = getLuma(textureLod(img, textureCoord + vec2(0.0, step1.y), 0.0).rgb);
    float v2 = getLuma(textureLod(img, textureCoord - vec2(0.0, step1.y), 0.0).rgb);
    float v3 = getLuma(textureLod(img, textureCoord + vec2(0.0, step2.y), 0.0).rgb);
    float v4 = getLuma(textureLod(img, textureCoord - vec2(0.0, step2.y), 0.0).rgb);

    // =====================================================================
    // BILATERAL DELTA ACCUMULATION (Edge-Aware)
    // =====================================================================
    float edgeThreshLow = 0.05;
    float edgeThreshHigh = 0.25;

    float d_h1 = luma - h1; float w_h1 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h1));
    float d_h2 = luma - h2; float w_h2 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h2));
    float d_h3 = luma - h3; float w_h3 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h3));
    float d_h4 = luma - h4; float w_h4 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h4));
    
    float d_v1 = luma - v1; float w_v1 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v1));
    float d_v2 = luma - v2; float w_v2 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v2));
    float d_v3 = luma - v3; float w_v3 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v3));
    float d_v4 = luma - v4; float w_v4 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v4));

    float diff = (d_h1 * w_h1 + d_h2 * w_h2 + d_v1 * w_v1 + d_v2 * w_v2) * 0.16 + 
                 (d_h3 * w_h3 + d_h4 * w_h4 + d_v3 * w_v3 + d_v4 * w_v4) * 0.04;

    // Extremes Stopper (Gamma fix)
    float distFromMid = abs(luma - 0.5) * 2.0; 
    float extremesMask = clamp(1.0 - (distFromMid * distFromMid), 0.0, 1.0); 
    diff *= extremesMask;

    float sharp = luma + diff;
    sharp = applyBlendMode(luma, sharp);

    // BlendIf mid-tone masking
    if (blendIfDark > 0 || blendIfLight < 255) {
        float blendIfD = (float(blendIfDark) / 255.0) + 0.0001;
        float blendIfL = (float(blendIfLight) / 255.0) - 0.0001;
        float mixVal = dot(orig, vec3(0.33333333));
        float mask = 1.0;

        if (blendIfDark > 0) mask = smoothstep(blendIfD * 0.8, blendIfD * 1.2, mixVal);
        if (blendIfLight < 255) mask *= 1.0 - smoothstep(blendIfL * 0.8, blendIfL * 1.2, mixVal);

        sharp = mix(luma, sharp, mask);
    }

    float lumaScale = mix(luma, sharp, strength) / luma;
    vec3 finalColor = orig * lumaScale;

    fragColor = vec4(clamp(finalColor, 0.0, 1.0), centerColor.a);
}