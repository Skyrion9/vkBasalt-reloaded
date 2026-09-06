#version 450

layout(location = 0) out vec2 textureCoord;

void main() {
    // Branchless fullscreen triangle clipspace positions, generates: (-1, -1), (3, -1), (-1, 3)
    vec2 pos = vec2(float((gl_VertexIndex & 1) << 2) - 1.0, 
                    float((gl_VertexIndex & 2) << 1) - 1.0);

    gl_Position = vec4(pos, 0.0, 1.0);

    // Derive UVs directly from clip space positions, maps: (-1 -> 0.0), (3 -> 2.0)
    textureCoord = pos * 0.5 + 0.5;
}
