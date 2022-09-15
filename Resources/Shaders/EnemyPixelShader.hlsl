#include "EnemyHeader.hlsli"

Texture2D<float4> tex : register(t0);  // 0番スロットに設定されたテクスチャ
SamplerState smp : register(s0);      // 0番スロットに設定されたサンプラー

float4 main(VSOutput input) : SV_TARGET
{
	float3 light = normalize(float3(0,-1,-1));//右下奥
	float diffuse = saturate(dot(-light, input.normal));//diffuseを[0,1]の範囲にClampする
	float brightness = diffuse + 1.0f;//光源へのベクトルと法線ベクトルの内積
	float4 texcolor = float4(tex.Sample(smp, input.uv));
	return float4(texcolor.rgb * brightness, texcolor.a) * color;
	//return float4(1, 1, 1, 1);
}