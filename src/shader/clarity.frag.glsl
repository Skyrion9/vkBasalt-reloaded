#version 450

// Clarity Shader - Optimized Single-Pass Cross-Convolution Contrast

layout(set = 0, binding = 0) uniform sampler2D img;

// Defaulted back to Overlay as it uses a proper S-curve that preserves the natural gamma curve.
layout(constant_id = 0) const float radius = 1.0;
layout(constant_id = 1) const float offset = 5.0;
layout(constant_id = 2) const float strength = 1.0;
layout(constant_id = 3) const int blendMode = 1; 
layout(constant_id = 4) const int blendIfDark = 40;
layout(constant_id = 5) const int blendIfLight = 220;
layout(constant_id = 6) const float darkIntensity = 0.160;
layout(constant_id = 7) const float lightIntensity = 0.0;

// Push Constant Block for inverse screen resolution
// Padded to 16 bytes (vec4) for SPIR-V alignment requirements.
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
    // The Vulkan compiler (SPIR-V) optimizes these branches because 'blendMode'
    // is a specialization constant. The dead code is stripped at pipeline creation.
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

    // Optimization: Passing 'texelSize' via Push Constants from the C++ host 
    // is faster than calling textureSize() every frame.
    //vec2 texSize = textureSize(img, 0);
    //vec2 texelSize = 1.0 / vec2(texSize);

    //Using Push Constants instead of textureSize()
    vec2 texelSize = pc.texelSize;

    // Optimization: Simplified step calculations (4.5 = 1.5 * 3)
    float baseOffset = 1.5 * radius * offset;
    vec2 step1 = baseOffset * texelSize;
    vec2 step2 = step1 * 3.0;

    float blurLuma = luma * 0.20;

    // Optimization: Fetch all taps first, then group additions to minimize MUL/ADD instructions
    float h1 = getLuma(textureLod(img, textureCoord + vec2(step1.x, 0.0), 0.0).rgb);
    float h2 = getLuma(textureLod(img, textureCoord - vec2(step1.x, 0.0), 0.0).rgb);
    float h3 = getLuma(textureLod(img, textureCoord + vec2(step2.x, 0.0), 0.0).rgb);
    float h4 = getLuma(textureLod(img, textureCoord - vec2(step2.x, 0.0), 0.0).rgb);
    
    float v1 = getLuma(textureLod(img, textureCoord + vec2(0.0, step1.y), 0.0).rgb);
    float v2 = getLuma(textureLod(img, textureCoord - vec2(0.0, step1.y), 0.0).rgb);
    float v3 = getLuma(textureLod(img, textureCoord + vec2(0.0, step2.y), 0.0).rgb);
    float v4 = getLuma(textureLod(img, textureCoord - vec2(0.0, step2.y), 0.0).rgb);

    blurLuma += (h1 + h2 + v1 + v2) * 0.16 + (h3 + h4 + v3 + v4) * 0.04;
    float diff = luma - blurLuma;

    // OPTIMIZATION: Branchless Halo Choke
    // Replaces the ternary operator to prevent GPU warp divergence. (Might be causing slight gamma shift?)
    // old implementation : diff *= (diff < 0.0) ? (1.0 - darkIntensity) : (1.0 - lightIntensity);
    float isLight = step(0.0, diff);
    float choke = mix(1.0 - darkIntensity, 1.0 - lightIntensity, isLight);
    diff *= choke;

    // Drops the contrast boost to 0.0 as the pixel approaches pure black or white. (Gamma fix)
    // This prevents the S-curve from artificially crushing shadows or blowing out highlights, preserving the natural gamma roll-off of the original image.
    float distFromMid = abs(luma - 0.5) * 2.0; 
    float extremesMask = clamp(1.0 - (distFromMid * distFromMid), 0.0, 1.0); 
    diff *= extremesMask;

    float sharp = luma + diff;
    sharp = applyBlendMode(luma, sharp);

    // BlendIf mid-tone masking, stripped at compile-time by SPIR-V based on specialization constants
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