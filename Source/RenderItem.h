#pragma once
#include "../Common/MathHelper.h"

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