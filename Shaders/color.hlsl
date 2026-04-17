//***************************************************************************************
// color.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//
// Transforms and colors geometry.
//***************************************************************************************

cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj; 
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
	
	// 입력 받은 3차원 좌표(vin.PosL)을 동차 좌표로 변환
	// 이후 WVP 행렬과 곱하여 동차 클립 공간에서의 좌표로 변환
	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);
	
	// 색상은 그대로 출력
    vout.Color = vin.Color;
    
    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	// Rasterizer를 거친 pixel의 색상을 그대로 출력
    return pin.Color;
}


