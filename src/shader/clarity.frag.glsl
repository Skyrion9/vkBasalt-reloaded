// Clarity shader - Local Contrast Enhancement
// Based on ReShade clarity.fx optimized for single-pass sparse grid sampling
#version 450

layout(set=0, binding=0) uniform sampler2D img;

layout(constant_id = 0) const float strength = 0.4;
layout(constant_id = 1) const float radius = 2.5;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

#define textureLod0(img, coord) textureLod(img, coord, 0.0f)

// Vectorized Overlay blend mode for local contrast enhancement
vec3 blendOverlay(vec3 base, vec3 blend) {
    return mix(
        2.0 * base * blend,
        1.0 - 2.0 * (1.0 - base) * (1.0 - blend),
        step(0.5, base)
    );
}

void main()
{
    // Get center pixel color
    vec4 centerColor = textureLod0(img, textureCoord);
    vec3 color = centerColor.rgb;

    // Dynamically retrieve frame dimensions natively on the GPU
    ivec2 texSize = textureSize(img, 0);
    vec2 imgSize = vec2(1.0 / float(texSize.x), 1.0 / float(texSize.y));
    vec2 offset = imgSize * radius;

    // High-performance 9-tap texture sampling cross (sparse grid)
    // This approximates wide-radius local contrast enhancement in a single pass
    vec3 blur = color;
    blur += textureLod0(img, textureCoord + vec2(-offset.x, -offset.y)).rgb;
    blur += textureLod0(img, textureCoord + vec2( 0.0,      -offset.y)).rgb;
    blur += textureLod0(img, textureCoord + vec2( offset.x, -offset.y)).rgb;
    blur += textureLod0(img, textureCoord + vec2(-offset.x,  0.0)).rgb;
    blur += textureLod0(img, textureCoord + vec2( offset.x,  0.0)).rgb;
    blur += textureLod0(img, textureCoord + vec2(-offset.x,  offset.y)).rgb;
    blur += textureLod0(img, textureCoord + vec2( 0.0,       offset.y)).rgb;
    blur += textureLod0(img, textureCoord + vec2( offset.x,  offset.y)).rgb;
    blur /= 9.0;

    // Calculate details and apply local contrast overlay adjustment
    vec3 highPass = (color - blur) + 0.5;
    vec3 enhanced = blendOverlay(color, clamp(highPass, 0.0, 1.0));

    // Blend between original color and enhanced contrast
    fragColor = vec4(mix(color, enhanced, strength), centerColor.a);
}