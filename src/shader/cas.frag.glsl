#version 450

// LICENSE
// =======
// Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
// -------
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
// -------
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
// Software.
// -------
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE

// AMD FidelityFX CAS - Algebraically Reduced & Optimized

layout(set=0, binding=0) uniform sampler2D img;
layout(constant_id = 0) const float sharpness = 0.4;

layout(location = 0) in vec2 textureCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    // fetch a 3x3 neighborhood around the pixel 'e',
    //  a b c
    //  d(e)f
    //  g h i
    vec4 inputColor = textureLod(img, textureCoord, 0.0);
    vec3 e = inputColor.rgb;

    // Hardware-accelerated offset fetches
    vec3 a = textureLodOffset(img, textureCoord, 0.0, ivec2(-1,-1)).rgb;
    vec3 b = textureLodOffset(img, textureCoord, 0.0, ivec2( 0,-1)).rgb;
    vec3 c = textureLodOffset(img, textureCoord, 0.0, ivec2( 1,-1)).rgb;
    vec3 d = textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 0)).rgb;
    vec3 f = textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 0)).rgb;
    vec3 g = textureLodOffset(img, textureCoord, 0.0, ivec2(-1, 1)).rgb;
    vec3 h = textureLodOffset(img, textureCoord, 0.0, ivec2( 0, 1)).rgb;
    vec3 i = textureLodOffset(img, textureCoord, 0.0, ivec2( 1, 1)).rgb;

    // AMD's intentional "soft min/max" bias. 
    // Soft min and max.
    //  a b c             b
    //  d e f * 0.5  +  d e f * 0.5
    //  g h i             h
    vec3 mnRGB  = min(min(min(d,e),min(f,b)),h);
    vec3 mnRGB2 = min(min(min(mnRGB,a),min(g,c)),i);
    mnRGB += mnRGB2; 

    vec3 mxRGB  = max(max(max(d,e),max(f,b)),h);
    vec3 mxRGB2 = max(max(max(mxRGB,a),max(g,c)),i);
    mxRGB += mxRGB2; 

    // Prevent divide-by-zero on pure black pixels
    vec3 ampRGB = clamp(min(mnRGB, 2.0 - mxRGB) / max(mxRGB, vec3(0.0001)), 0.0, 1.0);
    float peak = 8.0 - 3.0 * sharpness;

    //                          0 w 0
    //  Filter shape:           w 1 w
    //                          0 w 0  
    
    // Prevent divide-by-zero in inversesqrt
    vec3 invAmp = inversesqrt(max(ampRGB, vec3(0.0001)));
    
    // ALGEBRAIC REDUCTION:
    // P is always >= 5.0, so (P - 4.0) is always >= 1.0. Perfectly stable.
    vec3 P = invAmp * peak;
    vec3 window = (b + d) + (f + h);
    
    // P is always >= 5.0, so (P - 4.0) is always >= 1.0. No divide-by-zero possible.
    vec3 outColor = clamp((e * P - window) / (P - 4.0), 0.0, 1.0);

    fragColor = vec4(outColor, inputColor.a);
}