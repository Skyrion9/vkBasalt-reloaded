#version 450

// Minimal validation compute shader for the SimpleComputePass infrastructure. Accumulates a 256-bin luminance histogram from the input image.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 0) uniform sampler2D inputImage;
layout(set = 0, binding = 1) buffer HistogramBuffer { uint bins[256]; };

layout(push_constant) uniform PushConstants {
    uint width;
    uint height;
} pc;

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    if (pixel.x >= pc.width || pixel.y >= pc.height) return;

    vec2 uv = (vec2(pixel) + 0.5) / vec2(pc.width, pc.height);
    vec4 color = textureLod(inputImage, uv, 0.0);
    float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    uint bin = uint(clamp(luma, 0.0, 1.0) * 255.0);
    atomicAdd(bins[bin], 1);
}
