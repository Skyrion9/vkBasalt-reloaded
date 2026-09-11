#version 450
layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(set = 0, binding = 0, rgba16f) uniform image2D outImage;

layout(push_constant) uniform PC {
    float peakNits;
    float windowSize;  // Area percentage (e.g., 0.10 for 10%)
    int   patternType;
} pc;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(outImage);
    if (pos.x >= size.x || pos.y >= size.y) return;
    
    vec2 uv = vec2(pos) / vec2(size);
    
    // Calculate window dimensions for target APL area
    float halfW = sqrt(pc.windowSize) / 2.0;
    bool inWindow = abs(uv.x - 0.5) < halfW && abs(uv.y - 0.5) < halfW;
    
    float lumaNits = 0.0;
    
    if (pc.patternType == 0) { // 10% APL Pattern
        lumaNits = inWindow ? (pc.peakNits * 0.95) : (pc.peakNits * 0.05);
    } else if (pc.patternType == 1) { // Flat 100%
        lumaNits = pc.peakNits;
    } else if (pc.patternType == 2) { // Flat 5%
        lumaNits = pc.peakNits * 0.05;
    }
    
    // Convert nits to linear space (1.0 = 100 nits)
    float linearLuma = lumaNits / 100.0;
    
    // Output neutral gray in linear space
    imageStore(outImage, pos, vec4(vec3(linearLuma), 1.0));
}
