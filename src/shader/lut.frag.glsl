#version 450

layout(set=0, binding=0) uniform sampler2D img;
layout(set=1, binding=0) uniform sampler3D lut;

//Only works with cubes not with cuboids
layout(constant_id = 0) const int lutSize = 32;
layout(constant_id = 1) const int flipGB = 0;
layout(constant_id = 2) const int hdrMode = 0;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

#define textureLod0Offset(img, coord, offset) textureLodOffset(img, coord, 0.0f, offset)
#define textureLod0(img, coord) textureLod(img, coord, 0.0f)

void main()
{
    vec4 color;
    if(flipGB != 0)
    {
        color = textureLod0(img,textureCoord).rbga;
    }
    else
    {
        color = textureLod0(img,textureCoord);
    }

    //see https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter24.html
    vec3 scale = (vec3(lutSize) - 1.0) / vec3(lutSize);
    vec3 offset = 1.0 / (2.0 * vec3(lutSize));
    vec3 lutInput = color.rgb;
    vec3 excess = vec3(0.0);

    if (hdrMode == 1) {
        // LUTs map [0,1] -> [0,1]. Clamp input for sampling, preserve HDR excess.
        excess = max(color.rgb - 1.0, 0.0);
        lutInput = min(color.rgb, 1.0);
    }

    vec3 lutColor = textureLod0(lut, scale * lutInput + offset).rgb;

    // Add back HDR excess to preserve highlight intensity
    vec3 finalColor = lutColor + excess;
    fragColor = vec4(finalColor, color.a);
}
