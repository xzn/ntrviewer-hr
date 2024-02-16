
// COPIED from official FSR sample
//
// FidelityFX Super Resolution Sample
//
// Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

layout(binding=0) uniform const_buffer
{
    uvec4 Const0;
    uvec4 Const1;
    uvec4 Const2;
    uvec4 Const3;
    uvec4 Const0RCAS;
    uvec4 Extents; // elecro custom (input.width, input.height, output.width, output.height);
    uvec4 Sample;
};

#define A_GPU 1
#define A_GLSL 1

#define SAMPLE_SLOW_FALLBACK 1
//#define SAMPLE_EASU 1

#if SAMPLE_SLOW_FALLBACK
    // GL: removed sampler
    layout(binding=1) uniform sampler2D InputTexture;
    layout(binding=2,rgba32f) uniform highp writeonly image2D OutputTexture;


    //layout(binding=1) uniform texture2D InputTexture;
    //layout(binding=2,rgba32f) uniform image2D OutputTexture;
    //layout(binding=3) uniform sampler InputSampler;
    #if SAMPLE_EASU
        #define FSR_EASU_F 1
        AF4 FsrEasuRF(AF2 p) { AF4 res = textureGather(InputTexture, p, 0); return res; }
        AF4 FsrEasuGF(AF2 p) { AF4 res = textureGather(InputTexture, p, 1); return res; }
        AF4 FsrEasuBF(AF2 p) { AF4 res = textureGather(InputTexture, p, 2); return res; }
        /*
        AF4 FsrEasuRF(AF2 p) { AF4 res = textureGather(sampler2D(InputTexture,InputSampler), p, 0); return res; }
        AF4 FsrEasuGF(AF2 p) { AF4 res = textureGather(sampler2D(InputTexture,InputSampler), p, 1); return res; }
        AF4 FsrEasuBF(AF2 p) { AF4 res = textureGather(sampler2D(InputTexture,InputSampler), p, 2); return res; }
        */
    #endif
    #if SAMPLE_RCAS
        //#define FSR_RCAS_F
        AF4 FsrRcasLoadF(ASU2 p) { return texelFetch(InputTexture, ASU2(p), 0); }
        //AF4 FsrRcasLoadF(ASU2 p) { return texelFetch(sampler2D(InputTexture,InputSampler), ASU2(p), 0); }
        void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}
    #endif
#else
    #define A_HALF
    layout(binding=1) uniform texture2D InputTexture;
    layout(binding=2,rgba16f) uniform image2D OutputTexture;
    layout(binding=3) uniform sampler InputSampler;
    #if SAMPLE_EASU
        #define FSR_EASU_H 1
        AH4 FsrEasuRH(AF2 p) { AH4 res = AH4(textureGather(sampler2D(InputTexture,InputSampler), p, 0)); return res; }
        AH4 FsrEasuGH(AF2 p) { AH4 res = AH4(textureGather(sampler2D(InputTexture,InputSampler), p, 1)); return res; }
        AH4 FsrEasuBH(AF2 p) { AH4 res = AH4(textureGather(sampler2D(InputTexture,InputSampler), p, 2)); return res; }  
    #endif
    #if SAMPLE_RCAS
        #define FSR_RCAS_H
        AH4 FsrRcasLoadH(ASW2 p) { return AH4(texelFetch(sampler2D(InputTexture,InputSampler), ASU2(p), 0)); }
        void FsrRcasInputH(inout AH1 r,inout AH1 g,inout AH1 b){}
    #endif
#endif

//#include "ffx_fsr1.h"
//#include "ffx_a.h"

void CurrFilter(AU2 pos)
{
#if SAMPLE_BILINEAR
    AF2 pp = (AF2(pos) * AF2_AU2(Const0.xy) + AF2_AU2(Const0.zw)) * AF2_AU2(Const1.xy) + AF2(0.5, -0.5) * AF2_AU2(Const1.zw);
    imageStore(OutputTexture, ASU2(pos), textureLod(InputTexture, pp, 0.0));
#endif
#if SAMPLE_EASU
    #if SAMPLE_SLOW_FALLBACK
        AF3 c;
        FsrEasuF(c, pos, Const0, Const1, Const2, Const3);
        if( Sample.x == 1u )
            c *= c;
        imageStore(OutputTexture, ASU2(pos), AF4(c, 1));
    #else
        AH3 c;
        FsrEasuH(c, pos, Const0, Const1, Const2, Const3);
        if( Sample.x == 1 )
            c *= c;
        imageStore(OutputTexture, ASU2(pos), AH4(c, 1));
    #endif
#endif
#if SAMPLE_RCAS
    #if SAMPLE_SLOW_FALLBACK
        AF3 c;
        FsrRcasF(c.r, c.g, c.b, pos, Const0RCAS);
        if( Sample.x == 1u )
            c *= c;
        imageStore(OutputTexture, ASU2(pos), AF4(c, 1));
    #else
        AH3 c;
        FsrRcasH(c.r, c.g, c.b, pos, Const0);
        if( Sample.x == 1 )
            c *= c;
        imageStore(OutputTexture, ASU2(pos), AH4(c, 1));
    #endif
#endif
}

layout(local_size_x=64) in;
void main()
{
    // Do remapping of local xy in workgroup for a more PS-like swizzle pattern.
    AU2 gxy = ARmp8x8(gl_LocalInvocationID.x) + AU2(gl_WorkGroupID.x << 4u, gl_WorkGroupID.y << 4u);
    CurrFilter(gxy);
    gxy.x += 8u;
    CurrFilter(gxy);
    gxy.y += 8u;
    CurrFilter(gxy);
    gxy.x -= 8u;
    CurrFilter(gxy);
}

