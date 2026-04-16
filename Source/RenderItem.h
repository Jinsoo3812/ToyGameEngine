#pragma once
#include "../Common/MathHelper.h"

// 정점 구조체
struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

// 화면에 그려질 개별 Object의 정보를 담는 구조체.
struct RenderItem
{
	RenderItem() = default;
	
	// TODO
};