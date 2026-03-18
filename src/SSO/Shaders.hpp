#pragma once

namespace SSO::Shaders
{
    static const char *const ps_solid
    {
        R"(
        float4 color : register(c0);

        struct PS_INPUT {
            float4 clr : COLOR0;
            float2 tex : TEXCOORD0;
        };

        float4 main(PS_INPUT i) : COLOR {
            return color;
        }
        )"
    };

    static const char *const ps_blur_outline
    {
        R"(
        sampler2D tex_sampler : register(s0);
        float2    blur_dir    : register(c1);
        float2    params      : register(c2);

        struct PS_INPUT {
            float4 clr : COLOR0;
            float2 tex : TEXCOORD0;
        };

        float4 main(PS_INPUT i) : COLOR
        {
            float4 sum = 0;
            float2 uv  = i.tex;

            sum += tex2D(tex_sampler, uv - 6.0 * blur_dir) * 0.012224;
            sum += tex2D(tex_sampler, uv - 5.0 * blur_dir) * 0.025222;
            sum += tex2D(tex_sampler, uv - 4.0 * blur_dir) * 0.046827;
            sum += tex2D(tex_sampler, uv - 3.0 * blur_dir) * 0.078207;
            sum += tex2D(tex_sampler, uv - 2.0 * blur_dir) * 0.117618;
            sum += tex2D(tex_sampler, uv - 1.0 * blur_dir) * 0.159253;
            sum += tex2D(tex_sampler, uv                 ) * 0.176950;
            sum += tex2D(tex_sampler, uv + 1.0 * blur_dir) * 0.159253;
            sum += tex2D(tex_sampler, uv + 2.0 * blur_dir) * 0.117618;
            sum += tex2D(tex_sampler, uv + 3.0 * blur_dir) * 0.078207;
            sum += tex2D(tex_sampler, uv + 4.0 * blur_dir) * 0.046827;
            sum += tex2D(tex_sampler, uv + 5.0 * blur_dir) * 0.025222;
            sum += tex2D(tex_sampler, uv + 6.0 * blur_dir) * 0.012224;

            if (params.y > 0.5 && sum.a > 0.005) {
                sum.rgb /= sum.a;
            }

            sum.a = saturate(sum.a * params.x);

            return sum;
        }
        )"
    };

    static const char *const ps_solid_outline
    {
        R"(
        sampler2D tex_sampler : register(s0);
        float2    texel_size  : register(c1);
        float4    params      : register(c2);

        struct PS_INPUT {
            float4 clr : COLOR0;
            float2 tex : TEXCOORD0;
        };

        float4 main(PS_INPUT i) : COLOR
        {
            float2 uv   = i.tex;
            float4 best = 0;

            float4 t;

            t = tex2D(tex_sampler, uv + float2(-1,  0) * texel_size); if (t.a > best.a) best = t;
            t = tex2D(tex_sampler, uv + float2( 1,  0) * texel_size); if (t.a > best.a) best = t;
            t = tex2D(tex_sampler, uv + float2( 0, -1) * texel_size); if (t.a > best.a) best = t;
            t = tex2D(tex_sampler, uv + float2( 0,  1) * texel_size); if (t.a > best.a) best = t;
            t = tex2D(tex_sampler, uv + float2(-1, -1) * texel_size); if (t.a > best.a) best = t;
            t = tex2D(tex_sampler, uv + float2( 1, -1) * texel_size); if (t.a > best.a) best = t;
            t = tex2D(tex_sampler, uv + float2(-1,  1) * texel_size); if (t.a > best.a) best = t;
            t = tex2D(tex_sampler, uv + float2( 1,  1) * texel_size); if (t.a > best.a) best = t;

            best.a = saturate(best.a * params.x);

            return best;
        }
        )"
    };
}