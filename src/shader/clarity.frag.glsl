#version 450

// clarity inspired optimized single-pass cross-convolution contrast
// pure 9-tap architecture with gaussian bilateral weights and  smooth transitions

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float radius = 2.0;
layout(constant_id = 1) const float offset = 1.5;
layout(constant_id = 2) const float strength = 1.0;
layout(constant_id = 3) const int blendMode = 1; 
layout(constant_id = 4) const int blendIfDark = 40;
layout(constant_id = 5) const int blendIfLight = 220;
layout(constant_id = 6) const float edgeThreshLow = 0.05;
layout(constant_id = 7) const float edgeThreshHigh = 0.25;
layout(constant_id = 8) const int enableDithering = 1;

layout(push_constant) uniform PushConstants {
    vec2 step1;     
    vec2 step2;     
} pc;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

#define BILATERAL_WEIGHT(diff) (1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(diff)))
#define BILATERAL_DIFF(neighbor, weight) ((lE - neighbor) * BILATERAL_WEIGHT(lE - neighbor) * weight)

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
    vec3 e = centerColor.rgb;
    float lE = getLuma(e);

    if (lE <= 0.0001) {
        fragColor = centerColor;
        return;
    }

    // phase 1: clarity wide fetches (8 taps)
    float h1_raw = getLuma(textureLod(img, textureCoord + vec2(pc.step1.x, 0.0), 0.0).rgb);
    float h2_raw = getLuma(textureLod(img, textureCoord - vec2(pc.step1.x, 0.0), 0.0).rgb);
    float h3_raw = getLuma(textureLod(img, textureCoord + vec2(pc.step2.x, 0.0), 0.0).rgb);
    float h4_raw = getLuma(textureLod(img, textureCoord - vec2(pc.step2.x, 0.0), 0.0).rgb);
    
    float v1_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, pc.step1.y), 0.0).rgb);
    float v2_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, pc.step1.y), 0.0).rgb);
    float v3_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, pc.step2.y), 0.0).rgb);
    float v4_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, pc.step2.y), 0.0).rgb);

    // phase 2: bilateral delta accumulation
    float diff = (
        BILATERAL_DIFF(h1_raw, 0.16) + BILATERAL_DIFF(h2_raw, 0.16) + BILATERAL_DIFF(v1_raw, 0.16) + BILATERAL_DIFF(v2_raw, 0.16) +
        BILATERAL_DIFF(h3_raw, 0.04) + BILATERAL_DIFF(h4_raw, 0.04) + BILATERAL_DIFF(v3_raw, 0.04) + BILATERAL_DIFF(v4_raw, 0.04)
    );

    // phase 3: clarity gates and s-curve
    diff *= smoothstep(0.006, 0.018, abs(diff));

    float distFromMidSq = (lE - 0.5) * (lE - 0.5);
    float extremesMask = clamp(1.0 - distFromMidSq * 4.0, 0.0, 1.0); 
    diff *= extremesMask;

    diff = clamp(diff, -0.02, 0.15);

    float blendMask = clamp(0.5 + diff, 0.0, 1.0); 
    float sharpLuma = applyBlendMode(lE, blendMask);

    if (blendIfDark > 0 || blendIfLight < 255) {
        float blendIfD = (float(blendIfDark) / 255.0) + 0.0001;
        float blendIfL = (float(blendIfLight) / 255.0) - 0.0001;
        
        float mixVal = lE; 
        float mask = 1.0;
        if (blendIfDark > 0) mask = smoothstep(blendIfD * 0.8, blendIfD * 1.2, mixVal);
        if (blendIfLight < 255) mask *= 1.0 - smoothstep(blendIfL * 0.8, blendIfL * 1.2, mixVal);
        sharpLuma = mix(lE, sharpLuma, mask);
    }

    // phase 4: final composite and dithering
    float lumaScale = (lE > 0.0001) ? mix(lE, sharpLuma, strength) / lE : 1.0;
    vec3 finalColor = e * lumaScale;

    if (enableDithering == 1) {
        uint hash = uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u;
        hash = (hash ^ (hash >> 16)) * 2654435769u;
        
        float dither = float(hash & 255u) * 0.0039215686; 
        finalColor += (dither - 0.5) * 0.0039215686;
    }

    fragColor = vec4(clamp(finalColor, 0.0, 1.0), centerColor.a);
}
