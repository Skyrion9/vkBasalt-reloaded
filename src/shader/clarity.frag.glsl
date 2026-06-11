// Enhanced Clarity Shader - Single-pass with all ReShade features
// Implements local contrast enhancement with proper blend modes and BlendIf masking

#version 450

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float strength = 0.4;
layout(constant_id = 1) const float radius = 1.0;
layout(constant_id = 2) const float offset = 1.0;
layout(constant_id = 3) const int blendMode = 6;
layout(constant_id = 4) const int blendIfDark = 50;
layout(constant_id = 5) const int blendIfLight = 215;
layout(constant_id = 6) const float darkIntensity = 0.4;
layout(constant_id = 7) const float lightIntensity = 0.0;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

// Fixed: Ioxa's true custom Clarity coefficients (sums to exactly 1.0)
float getLuma(vec3 rgb) {
    return dot(rgb, vec3(0.32786885, 0.655737705, 0.0163934436));
}

// Optimized 8-tap ring sampler that reuses the pre-fetched center luma value
float sparseBlur(float centerLuma, vec2 blurOffset) {
    float lumaSum = getLuma(textureLod(img, textureCoord + vec2(-blurOffset.x, -blurOffset.y), 0.0).rgb);
    lumaSum += getLuma(textureLod(img, textureCoord + vec2(0.0, -blurOffset.y), 0.0).rgb);
    lumaSum += getLuma(textureLod(img, textureCoord + vec2(blurOffset.x, -blurOffset.y), 0.0).rgb);
    lumaSum += getLuma(textureLod(img, textureCoord + vec2(-blurOffset.x, 0.0), 0.0).rgb);
    lumaSum += getLuma(textureLod(img, textureCoord + vec2(blurOffset.x, 0.0), 0.0).rgb);
    lumaSum += getLuma(textureLod(img, textureCoord + vec2(-blurOffset.x, blurOffset.y), 0.0).rgb);
    lumaSum += getLuma(textureLod(img, textureCoord + vec2(0.0, blurOffset.y), 0.0).rgb);
    lumaSum += getLuma(textureLod(img, textureCoord + vec2(blurOffset.x, blurOffset.y), 0.0).rgb);
    
    return (lumaSum + centerLuma) * 0.11111111; // Efficient 1/9 averaging
}

// All blend mode implementations matching ReShade exactly
float applyBlendMode(float luma, float sharp) {
    if (blendMode == 0) {
        // Soft Light
        return mix(2.0 * luma * sharp + luma * luma * (1.0 - 2.0 * sharp), 
                   2.0 * luma * (1.0 - sharp) + sqrt(luma) * (2.0 * sharp - 1.0), 
                   step(0.49, sharp));
    } else if (blendMode == 1) {
        // Overlay
        return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, luma));
    } else if (blendMode == 2) {
        // Hard Light
        return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, sharp));
    } else if (blendMode == 3) {
        // Multiply
        return clamp(2.0 * luma * sharp, 0.0, 1.0);
    } else if (blendMode == 4) {
        // Vivid Light
        return mix(2.0 * luma * sharp, luma / max(2.0 * (1.0 - sharp), 0.0001), step(0.50, sharp));
    } else if (blendMode == 5) {
        // Linear Light
        return luma + 2.0 * sharp - 1.0;
    } else {
        // Addition (blendMode == 6)
        return clamp(luma + (sharp - 0.5), 0.0, 1.0);
    }
}

void main() {
    vec4 centerColor = textureLod(img, textureCoord, 0.0);
    vec3 orig = centerColor.rgb;
    
    // Get luma from original image
    float luma = getLuma(orig);
    
    // Preserve chroma for later reconstruction (prevents color shift)
    vec3 chroma = orig / max(luma, 0.0001);
    
    // Calculate sample metrics once
    vec2 texSize = textureSize(img, 0);
    vec2 imgSize = vec2(1.0 / float(texSize.x), 1.0 / float(texSize.y));
    vec2 blurOffset = imgSize * radius * offset;
    
    // Get blurred luma using sparse grid sampling (reuses center luma)
    float blurLuma = sparseBlur(luma, blurOffset);
    
    // Calculate sharpened value matching ReShade's formula
    float sharp = 1.0 - blurLuma;
    sharp = (luma + sharp) * 0.5;
    
    // Apply dark/light intensity adjustments (matching original ReShade behavior)
    float sharpMin = mix(0.0, 1.0, smoothstep(0.0, 1.0, sharp));
    float sharpMax = sharpMin;
    sharpMin = mix(sharp, sharpMin, darkIntensity);
    sharpMax = mix(sharp, sharpMax, lightIntensity);
    
    // Select based on whether sharp is darker or lighter than 0.5
    sharp = mix(sharpMin, sharpMax, step(0.5, sharp));
    
    // Apply blend mode
    sharp = applyBlendMode(luma, sharp);
    
    // BlendIf masking using unweighted average to match original ReShade behavior
    if (blendIfDark > 0 || blendIfLight < 255) {
        float blendIfD = (float(blendIfDark) / 255.0) + 0.0001;
        float blendIfL = (float(blendIfLight) / 255.0) - 0.0001;
        float mixVal = dot(orig, vec3(0.33333333));
        float mask = 1.0;
        
        if (blendIfDark > 0) {
            mask = mix(0.0, 1.0, smoothstep(blendIfD - (blendIfD * 0.2), blendIfD + (blendIfD * 0.2), mixVal));
        }
        
        if (blendIfLight < 255) {
            mask = mix(mask, 0.0, smoothstep(blendIfL - (blendIfL * 0.2), blendIfL + (blendIfL * 0.2), mixVal));
        }
        
        sharp = mix(luma, sharp, mask);
    }
    
    // Blend and reconstruct with chroma
    orig = mix(vec3(luma), vec3(sharp), strength) * chroma;
    
    fragColor = vec4(clamp(orig, 0.0, 1.0), centerColor.a);
}