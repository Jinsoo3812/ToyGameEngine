//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Transforms and colors geometry.
//***************************************************************************************

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
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
};

// 정점 셰이더의 Input Signature
struct VertexIn
{
	float3 PosL  : POSITION;
    float4 Color : COLOR;
};

// 정점 셰이더의 Output Signature
struct VertexOut
{
	float4 PosH  : SV_POSITION; // SV_는 시스템 값을 나타내는 시맨틱. Rasterrizer는 이 시맨틱을 보고 위치를 계산해야 한다.
    float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
	
	// 정점의 world 좌표
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    
    // 미리 계산되어 넘어온 VP 행렬과 world 좌표의 곱
    vout.PosH = mul(posW, gViewProj);
	
	// 색깔은 그대로 사용
    vout.Color = vin.Color;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// Rasterizer를 거친 pixel의 색상을 그대로 출력
    return pin.Color;
}


