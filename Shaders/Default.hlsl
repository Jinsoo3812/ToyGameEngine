//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Default shader, currently supports lighting.
//***************************************************************************************

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    
    Light gLights[MaxLights];
};

// 정점 셰이더의 Input Signature
struct VertexIn
{
    float3 PosL : POSITION; // 로컬 좌표
    float3 NormalL : NORMAL; // 로컬 법선 벡터
};

// 정점 셰이더의 Output Signature
struct VertexOut
{
    float4 PosH : SV_POSITION; // 동차 클립 공간 좌표
    float3 PosW : POSITION; // 월드 좌표
    float3 NormalW : NORMAL; // 월드 법선 벡터
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	
	// 정점의 world 좌표
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    
    // 비균등 비례 변환이 없다고 가정한 법선 벡터의 월드 좌표 변환 (회전과 이동만 적용)
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);

    // 동차 클립 공강 좌표로 변환
    vout.PosH = mul(posW, gViewProj);

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// 법선 벡터 정규화
    pin.NormalW = normalize(pin.NormalW);

    // 표면에서 카메라로의 정규 벡터
    float3 toEyeW = normalize(gEyePosW - pin.PosW);

	// 주변광 계산 (주변광 세기 * 난반사율)
    float4 ambient = gAmbientLight * gDiffuseAlbedo;

    // 직접광 계산
    const float shininess = 1.0f - gRoughness;
    Material mat = { gDiffuseAlbedo, gFresnelR0, shininess };
    float3 shadowFactor = 1.0f;
    float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
        pin.NormalW, toEyeW, shadowFactor);

    float4 litColor = ambient + directLight;

    // 일반적으로, 알파값은 Material의 난반사율에서 가져온다.
    litColor.a = gDiffuseAlbedo.a;

    return litColor;
}


