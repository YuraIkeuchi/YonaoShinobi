#include"Basic.hlsli"

Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);

float4 main(VSOutput input) : SV_TARGET{
	float3 light = normalize(float3(0,-1,0));//右下奥
	float diffuse = saturate(dot(-light, input.normal));//diffuseを[0,1]の範囲にClampする
	float brightness = diffuse+1.0f;//光源へのベクトルと法線ベクトルの内積
	float4 texcolor = float4(tex.Sample(smp, input.uv));
	return float4(texcolor.rgb * brightness, texcolor.a) * color;
	//return float4(brightness,brightness,brightness,1);//輝度をRGBに代入して出力
}


//float4 main(VSOutput input) :SV_TARGET
//{
//	return float4(input.uv,1,1);
//	//return float4(tex.Sample(smp,input.uv));
//}
//cbuffer cbuff0:register(b0)
//{
//	float4 color;//色
//}
//
//float4 main() : SV_TARGET
//{
//	return color;
//}