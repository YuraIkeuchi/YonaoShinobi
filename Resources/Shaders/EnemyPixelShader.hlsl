#include "EnemyHeader.hlsli"

Texture2D<float4> tex : register(t0);  // 0�ԃX���b�g�ɐݒ肳�ꂽ�e�N�X�`��
SamplerState smp : register(s0);      // 0�ԃX���b�g�ɐݒ肳�ꂽ�T���v���[

float4 main(VSOutput input) : SV_TARGET
{
	float3 light = normalize(float3(0,-1,-1));//�E����
	float diffuse = saturate(dot(-light, input.normal));//diffuse��[0,1]�͈̔͂�Clamp����
	float brightness = diffuse + 1.0f;//�����ւ̃x�N�g���Ɩ@���x�N�g���̓���
	float4 texcolor = float4(tex.Sample(smp, input.uv));
	return float4(texcolor.rgb * brightness, texcolor.a) * color;
	//return float4(1, 1, 1, 1);
}