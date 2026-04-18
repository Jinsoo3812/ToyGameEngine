#pragma once
#include "../Common/MathHelper.h"

// 정점 구조체
struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

// Object가 개별적으로 갖는 상수 버퍼의 속성 구조체
struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
};

// 모든 Object가 공유하여 한 번에 pass에 한 번만 넘어가는 상수 버퍼의 속성 구조체
struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
};

// 화면에 그려질 개별 Object의 정보를 담는 구조체.
struct RenderItem
{
    RenderItem() = default;
    RenderItem(const RenderItem* ritem) {
        World = ritem->World;
        ObjCBIndex = -1;
        Geo = ritem->Geo;
        PrimitiveType = ritem->PrimitiveType;
        IndexCount = ritem->IndexCount;
    }

    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();

    // Index into the per-frame ObjectCB.
    UINT ObjCBIndex = -1;

    MeshGeometry* Geo = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT IndexCount = 0;
};