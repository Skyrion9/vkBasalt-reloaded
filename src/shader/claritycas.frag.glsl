#version 450

// Clarity CAS is a hybrid shader that takes best of both worlds as AMD's Contrast Adaptive Sharpening excels at bringing out small details and Clarity at cinematic or macro details while giving image a certain pop.
// CAS excels at micro detail enhancement but misses macro details that Clarity can highlight well albeit at a high performance cost especially at higher resolutions.
// Meticulously optimized, single pass. Without intermediary texture or downsampling like Clarity's original implementation has which is really expensive.
// Brings out detail no matter the distance, can easily help you spot enemies at a distance or under difficult lighting conditions thanks to its contrast aware sampling.
// Enemies as tiny as a few pixels will be more noticable. Foggy, stormy zones have nothing on this shader as they'll be more easily perceived.
// Virtually artifact-free despite using maximum sharpness values. If you're sensitive to any artifacts turning them down to 0.6 should remove artifacts entirely.
// I implemented PushConstants to vkBasalt just to extract more performance for this. Normally I'd layer CAS (vkBasalt's) ontop of Clarity.fx (Reshade) which cost me about 20 watts on rdna2. This costs about ~5 watts and has superior quality.
// Absolutely great at restoring detail lost to TAA and FXAA. I haven't experimented for upscaling but CAS is great for that and I kept those aspects of it intact. 
// Clarity helps with depth perception and sharpens uniformly so this can restore some detail for upscaling purposes, bringing clarity to objects CAS would've ignored (Larger and less fine.)

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float radius = 2.0;
layout(constant_id = 1) const float offset = 1.5;
layout(constant_id = 2) const float clarityStrength = 1.0;
layout(constant_id = 3) const int blendMode = 1;
layout(constant_id = 4) const int blendIfDark =40;
layout(constant_id = 5) const int blendIfLight = 220;
layout(constant_id = 6) const float casSharpness = 1.0; 
layout(constant_id = 7) const float casStrength = 1.0; 

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
    else return clamp(sharp, 0.0, 1.0); 
}

void main() {
    vec4 centerColor = textureLod(img, textureCoord, 0.0);
    vec3 e = centerColor.rgb;
    float lE = getLuma(e);

    if (lE <= 0.0001) {
        fragColor = centerColor;
        return;
    }

    // =====================================================================
    // 1. SHARED 3x3 FETCHES (8 Fetches)
    // =====================================================================
    vec3 a = textureLodOffset(img, textureCoord, 0.0, ivec2(-1,-1)).rgb;
    vec3 b = textureLodOffset(img, textureCoord, 0.0, ivec2( 0,-1)).rgb;
    vec3 c = textureLodOffset(img, textureCoord, 0.0, ivec2( 1,-1)).rgb;
    vec3 d = textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 0)).rgb;
    vec3 f = textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 0)).rgb;
    vec3 g = textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 1)).rgb;
    vec3 h = textureLodOffset(img, textureCoord, 0.0, ivec2( 0, 1)).rgb;
    vec3 i = textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 1)).rgb;

    // =====================================================================
    // 2. NATIVE RGB CAS MATH
    // =====================================================================
    vec3 mnRGB  = min(min(min(d,e),min(f,b)),h);
    vec3 mnRGB2 = min(min(min(mnRGB,a),min(g,c)),i);
    mnRGB = mnRGB2; // Removes corrupt ADD instruction

    vec3 mxRGB  = max(max(max(d,e),max(f,b)),h);
    vec3 mxRGB2 = max(max(max(mxRGB,a),max(g,c)),i);
    mxRGB = mxRGB2; // Removes corrupt ADD instruction

    vec3 ampRGB = clamp(min(mnRGB, 2.0 - mxRGB) / max(mxRGB, 0.0001), 0.0, 1.0);
    float peak = 8.0 - 3.0 * casSharpness;
    
    vec3 invAmp = inversesqrt(max(ampRGB, 0.0001));
    vec3 den = 4.0 - peak * invAmp;
    vec3 W_RGB = clamp(vec3(1.0) / den, 0.0, 1.0);

    vec3 tightWindow = (b + d) + (f + h);
    vec3 casDeltaRGB = W_RGB * (tightWindow - 4.0 * e);

    // =====================================================================
    // 3. CLARITY WIDE FETCHES (8 Fetches - Must be individual for non-adjacent taps)
    // =====================================================================
    vec2 myTexelSize = pc.texelSize;
    float baseOffset = 1.5 * radius * offset;
    vec2 step1 = baseOffset * myTexelSize;
    vec2 step2 = step1 * 3.0;

    float h1_raw = getLuma(textureLod(img, textureCoord + vec2(step1.x, 0.0), 0.0).rgb);
    float h2_raw = getLuma(textureLod(img, textureCoord - vec2(step1.x, 0.0), 0.0).rgb);
    float h3_raw = getLuma(textureLod(img, textureCoord + vec2(step2.x, 0.0), 0.0).rgb);
    float h4_raw = getLuma(textureLod(img, textureCoord - vec2(step2.x, 0.0), 0.0).rgb);
    
    float v1_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, step1.y), 0.0).rgb);
    float v2_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, step1.y), 0.0).rgb);
    float v3_raw = getLuma(textureLod(img, textureCoord + vec2(0.0, step2.y), 0.0).rgb);
    float v4_raw = getLuma(textureLod(img, textureCoord - vec2(0.0, step2.y), 0.0).rgb);

    // =====================================================================
    // 4. BILATERAL DELTA ACCUMULATION (Optimized ALU)
    // =====================================================================
    // We calculate the delta for each tap and weight it directly. 
    // This avoids reconstructing the blurred image in memory.
    float edgeThreshLow = 0.05;
    float edgeThreshHigh = 0.25;

    float d_h1 = lE - h1_raw; float w_h1 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h1));
    float d_h2 = lE - h2_raw; float w_h2 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h2));
    float d_h3 = lE - h3_raw; float w_h3 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h3));
    float d_h4 = lE - h4_raw; float w_h4 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_h4));
    
    float d_v1 = lE - v1_raw; float w_v1 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v1));
    float d_v2 = lE - v2_raw; float w_v2 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v2));
    float d_v3 = lE - v3_raw; float w_v3 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v3));
    float d_v4 = lE - v4_raw; float w_v4 = 1.0 - smoothstep(edgeThreshLow, edgeThreshHigh, abs(d_v4));

    // Sum the weighted deltas directly! (The center pixel delta is 0, so we ignore its 0.20 weight).
    float diff = (d_h1 * w_h1 + d_h2 * w_h2 + d_v1 * w_v1 + d_v2 * w_v2) * 0.16 + 
                 (d_h3 * w_h3 + d_h4 * w_h4 + d_v3 * w_v3 + d_v4 * w_v4) * 0.04;

    // Extremes Stopper (Fast parabola, no smoothstep needed for global multiplier)
    float distFromMid = abs(lE - 0.5) * 2.0; 
    float extremesMask = clamp(1.0 - (distFromMid * distFromMid), 0.0, 1.0); 

    diff *= extremesMask;

    float sharpLuma = lE + diff;
    sharpLuma = applyBlendMode(lE, sharpLuma);

    if (blendIfDark > 0 || blendIfLight < 255) {
        float blendIfD = (float(blendIfDark) / 255.0) + 0.0001;
        float blendIfL = (float(blendIfLight) / 255.0) - 0.0001;
        float mixVal = dot(e, vec3(0.33333333));
        float mask = 1.0;
        if (blendIfDark > 0) mask = smoothstep(blendIfD * 0.8, blendIfD * 1.2, mixVal);
        if (blendIfLight < 255) mask *= 1.0 - smoothstep(blendIfL * 0.8, blendIfL * 1.2, mixVal);
        sharpLuma = mix(lE, sharpLuma, mask);
    }

    // =====================================================================
    // 5. FINAL COMPOSITE
    // =====================================================================
    float lumaScale = mix(lE, sharpLuma, clarityStrength) / lE;
    vec3 clarityColor = e * lumaScale;

    vec3 finalColor = clarityColor + (casDeltaRGB * casStrength);

    fragColor = vec4(clamp(finalColor, 0.0, 1.0), centerColor.a);
}