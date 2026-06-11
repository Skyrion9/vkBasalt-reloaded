#version 450

// Clarity Shader - Optimized Single-Pass Cross-Convolution Contrast
// Cleans up register pressure, eliminates unsafe chroma division, and removes branch stalls.

layout(set = 0, binding = 0) uniform sampler2D img;

layout(constant_id = 0) const float radius = 1.0;
layout(constant_id = 1) const float offset = 5.0;
layout(constant_id = 2) const float strength = 1.0;
layout(constant_id = 3) const int blendMode = 6;
layout(constant_id = 4) const int blendIfDark = 50;
layout(constant_id = 5) const int blendIfLight = 215;
layout(constant_id = 6) const float darkIntensity = 0.160;
layout(constant_id = 7) const float lightIntensity = 0.0;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

float getLuma(vec3 rgb) {
    return dot(rgb, vec3(0.32786885, 0.655737705, 0.0163934436));
}

float applyBlendMode(float luma, float sharp) {
    if (blendMode == 0) {
        return mix(2.0 * luma * sharp + luma * luma * (1.0 - 2.0 * sharp),
                   2.0 * luma * (1.0 - sharp) + sqrt(luma) * (2.0 * sharp - 1.0),
                   step(0.49, sharp));
    } else if (blendMode == 1) {
        return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, luma));
    } else if (blendMode == 2) {
        return mix(2.0 * luma * sharp, 1.0 - 2.0 * (1.0 - luma) * (1.0 - sharp), step(0.50, sharp));
    } else if (blendMode == 3) {
        return clamp(2.0 * luma * sharp, 0.0, 1.0);
    } else if (blendMode == 4) {
        return mix(2.0 * luma * sharp, luma / max(2.0 * (1.0 - sharp), 0.0001), step(0.50, sharp));
    } else if (blendMode == 5) {
        return luma + 2.0 * sharp - 1.0;
    } else {
        return clamp(luma + (sharp - 0.5), 0.0, 1.0);
    }
}

void main() {
    vec4 centerColor = textureLod(img, textureCoord, 0.0);
    vec3 orig = centerColor.rgb;

    float luma = getLuma(orig);

    if (luma <= 0.0001) {
        fragColor = centerColor;
        return;
    }

    vec2 texSize = textureSize(img, 0);
    vec2 texelSize = vec2(1.0 / float(texSize.x), 1.0 / float(texSize.y));

    float pixelOffset = radius * offset;

    float centerWeight = 0.20;
    float tap1Weight   = 0.16;
    float tap2Weight   = 0.04;

    float blurLuma = luma * centerWeight;

    // Pre-calculate clean step intervals for the cross layout
    vec2 step1 = vec2(1.5 * pixelOffset * texelSize.x, 1.5 * pixelOffset * texelSize.y);
    vec2 step2 = vec2(4.5 * pixelOffset * texelSize.x, 4.5 * pixelOffset * texelSize.y);

    // Horizontal Taps
    blurLuma += getLuma(textureLod(img, textureCoord + vec2(step1.x, 0.0), 0.0).rgb) * tap1Weight;
    blurLuma += getLuma(textureLod(img, textureCoord - vec2(step1.x, 0.0), 0.0).rgb) * tap1Weight;
    blurLuma += getLuma(textureLod(img, textureCoord + vec2(step2.x, 0.0), 0.0).rgb) * tap2Weight;
    blurLuma += getLuma(textureLod(img, textureCoord - vec2(step2.x, 0.0), 0.0).rgb) * tap2Weight;

    // Vertical Taps
    blurLuma += getLuma(textureLod(img, textureCoord + vec2(0.0, step1.y), 0.0).rgb) * tap1Weight;
    blurLuma += getLuma(textureLod(img, textureCoord - vec2(0.0, step1.y), 0.0).rgb) * tap1Weight;
    blurLuma += getLuma(textureLod(img, textureCoord + vec2(0.0, step2.y), 0.0).rgb) * tap2Weight;
    blurLuma += getLuma(textureLod(img, textureCoord - vec2(0.0, step2.y), 0.0).rgb) * tap2Weight;

    float diff = luma - blurLuma;

    // Flattened branch logic using ternary selection
    diff *= (diff < 0.0) ? (1.0 - darkIntensity) : (1.0 - lightIntensity);

    float sharp = luma + diff;
    sharp = applyBlendMode(luma, sharp);

    // BlendIf mid-tone masking using unweighted average to match original ReShade behavior
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

    // Scalar scaling factor avoids vec3 register pressure and division instability
    float lumaScale = mix(luma, sharp, strength) / max(luma, 0.0001);
    vec3 finalColor = orig * lumaScale;

    fragColor = vec4(clamp(finalColor, 0.0, 1.0), centerColor.a);
}