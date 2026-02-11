Shader "Unlit/MetaballsWhite"
{
    Properties
    {
        _UseExternal ("Use External (0 Demo / 1 External)", Float) = 1

      
        _ExternalTex ("External Texture", 2D) = "white" {}

        _Threshold ("Threshold", Float) = 0.12
        _BoundsPad ("Bounds Pad", Float) = 0.02

        _Color ("Ball Color", Color) = (1,1,1,1)
        _Ambient ("Ambient", Range(0,1)) = 0.35
        _LightDir ("Light Dir (xyz)", Vector) = (0.3, 0.8, 0.2, 0)
        _Specular ("Specular", Range(0,2)) = 0.35
        _Shininess ("Shininess", Range(8,256)) = 64
    }

    SubShader
    {
        Tags
        {
            "Queue"="Transparent"
            "RenderType"="Transparent"
            "RenderPipeline"="UniversalPipeline"
        }

        Pass
        {
            Blend SrcAlpha OneMinusSrcAlpha
            ZWrite Off
            Cull Off

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            struct appdata
            {
                float4 vertex : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct v2f
            {
                float4 vertex : SV_POSITION;
                float4 screenPos : TEXCOORD0;
            };

            v2f vert (appdata v)
            {
                v2f o;
                o.vertex = TransformObjectToHClip(v.vertex.xyz);
                o.screenPos = ComputeScreenPos(o.vertex);
                return o;
            }

            float _UseExternal;
            float _Threshold;
            float _BoundsPad;

            float4 _Color;
            float _Ambient;
            float4 _LightDir;
            float _Specular;
            float _Shininess;

            #define samples 4
            #define MAX_BLOBS 64

            int _BlobCount;
            float4 _Blobs[MAX_BLOBS];

          
            TEXTURE2D(_ExternalTex);
            SAMPLER(sampler_ExternalTex);

            float sdMetaBalls(float3 pos)
            {
                float m = 0.0;
                float p = 0.0;
                float dmin = 1e20;
                float hLip = 1.0;

                int count = clamp(_BlobCount, 0, MAX_BLOBS);

                for (int i = 0; i < MAX_BLOBS; i++)
                {
                    if (i >= count) break;

                    float4 b = _Blobs[i];
                    float db = length(b.xyz - pos);

                    if (db < b.w)
                    {
                        float x = db / b.w;
                        p += 1.0 - x * x*x*(x*(x*6.0 - 15.0) + 10.0);
                        m += 1.0;
                        hLip = max(hLip, 0.5333 * b.w);
                    }
                    else
                    {
                        dmin = min(dmin, db - b.w);
                    }
                }

                float d = dmin + _BoundsPad;
                if (m > 0.5)
                {
                    d = hLip * (_Threshold - p);
                }

                return d;
            }

            float3 norMetaBalls(float3 pos)
            {
                float3 nor = float3(0.0, 0.0001, 0.0);

                int count = clamp(_BlobCount, 0, MAX_BLOBS);

                for (int i = 0; i < MAX_BLOBS; i++)
                {
                    if (i >= count) break;

                    float4 b = _Blobs[i];

                    float db = length(b.xyz - pos);
                    float x = clamp(db / b.w, 0.0, 1.0);
                    float pp = x * x * (30.0 * x * x - 60.0 * x + 30.0);
                    nor += normalize(pos - b.xyz) * pp / b.w;
                }

                return normalize(nor);
            }

            static const float precis = 0.01;

            float map(float3 p)
            {
                return sdMetaBalls(p);
            }

            float2 intersect(float3 ro, float3 rd)
            {
                float maxd = 15.0;
                float h = precis * 2.0;
                float t = 0.0;
                float mHit = 1.0;

                for (int i = 0; i < 75; i++)
                {
                    if (h < precis || t > maxd) continue;
                    t += h;
                    h = map(ro + rd * t);
                }

                if (t > maxd) mHit = -1.0;
                return float2(t, mHit);
            }

            float4 frag (v2f i) : SV_Target
            {
                if (_UseExternal < 0.5)
                    return float4(0,0,0,0);

                float2 q = (i.screenPos.xy / i.screenPos.w); // 0..1

                float msamples = sqrt((float)samples);
                float3 tot = 0;
                float aTot = 0;

                for (int a = 0; a < samples; a++)
                {
                    float2 poff = float2(fmod((float)a, msamples), floor((float)a / msamples)) / msamples;

                    float3 ro = float3(0.0, 0.0, -8.0);
                    float3 ta = float3(0.0, 0.0, 0.0);

                    float2 p = -1.0 + 2.0 * (q * _ScreenParams.xy + poff) / _ScreenParams.xy;
                    p.x *= _ScreenParams.x / _ScreenParams.y;
                    p.x *= -1.0;

                    float3 ww = normalize(ta - ro);
                    float3 uu = normalize(cross(ww, float3(0.0, 1.0, 0.0)));
                    float3 vv = normalize(cross(uu, ww));

                    float3 rd = normalize(p.x * uu + p.y * vv + 2.0 * ww);

                    float3 col = 0;
                    float alpha = 0;

                    float2 tmat = intersect(ro, rd);
                    if (tmat.y > -0.5)
                    {
                        float3 pos = ro + tmat.x * rd;
                        float3 nor = norMetaBalls(pos);

                        float3 V = normalize(-rd);
                        float ndv = saturate(dot(nor, V));

                        
                        float shellRaw = pow(1.0 - ndv, 2.6);

                        float shellSoft = smoothstep(0.02, 0.60, shellRaw);
                        shellSoft = saturate(shellSoft);

                        float shellAlpha = smoothstep(0.06, 0.92, shellRaw);
                        shellAlpha = pow(shellAlpha, 0.90);

                       
                      
                        float refrStrength = 0.050;
                        float2 refrUV = q + nor.xy * refrStrength * shellSoft;

                        float3 bgRefr = SAMPLE_TEXTURE2D(_ExternalTex, sampler_ExternalTex, refrUV).rgb;
                        float3 bg     = SAMPLE_TEXTURE2D(_ExternalTex, sampler_ExternalTex, q).rgb;

                     
                        float fres = pow(1.0 - ndv, 6.0);

                        float3 L = normalize(_LightDir.xyz);
                        float3 H = normalize(L + V);

                        float tightPow1 = max(320.0, _Shininess * 3.6);
                        float tightPow2 = max(900.0, _Shininess * 7.5);

                        float spec1 = pow(saturate(dot(nor, H)), tightPow1) * (_Specular * 3.2);
                        float spec2 = pow(saturate(dot(nor, H)), tightPow2) * (_Specular * 1.6);

                       
                        float3 R = reflect(-V, nor);
                        float2 reflUV = q + R.xy * (0.030 * shellSoft);
                        float3 bgRefl = SAMPLE_TEXTURE2D(_ExternalTex, sampler_ExternalTex, reflUV).rgb;

                        float3 tint = lerp(1.0.xxx, float3(0.97, 0.985, 1.0), 0.35);

                        float3 shellCol = (bgRefr * tint);
                        shellCol = lerp(shellCol, bgRefl, 0.25 * fres);

                        shellCol += 1.0.xxx * (fres * 1.10);
                        shellCol += 1.0.xxx * ((spec1 + spec2) * shellSoft);

                      
                        float rim = pow(1.0 - ndv, 5.0);
                        float rimSoft = smoothstep(0.15, 0.85, rim);
                        float rimAbsorb = 0.30 * rimSoft;

                        shellCol = lerp(shellCol, shellCol * 0.62, rimAbsorb * shellSoft);

                     
                        col = lerp(bg, shellCol, shellSoft);
                        alpha = saturate(shellAlpha * 0.92);
                    }

                    tot += col;
                    aTot += alpha;
                }

                tot /= (float)samples;
                float alphaFinal = saturate(aTot / (float)samples);

                tot = pow(clamp(tot, 0.0, 1.0), float3(0.45, 0.45, 0.45));
                return float4(tot, alphaFinal);
            }
            ENDHLSL
        }
    }
}
