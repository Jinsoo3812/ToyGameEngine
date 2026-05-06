#pragma once
#include "../Common/MathHelper.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

// 하나의 object를 그리는 데 필요한 속성들을 담는 구조체
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

    // Object > World 변환 행렬 (Object의 World Transform)
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Texture 변환 행렬 (0 ~ 1 범위인 UV 좌표에 적용되는 변환 행렬)
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    int NumFramesDirty = 3;

    // 상수 버퍼에서 이 renderItem의 index
    UINT ObjCBIndex = -1;

	// 이 object가 그릴 MeshGeometry
    MeshGeometry* Geo = nullptr;

	// 이 object가 그릴 때 사용할 Material
	Material* Mat = nullptr;

    // 이 object를 그릴 때 사용할 기본 도형
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced 호출 시 필요한 매개변수들
    UINT IndexCount = 0; // 이 RenderItem은 몇 개의 index로 이루어져 있는가?
    UINT StartIndexLocation = 0; // 이 RenderItem은 인덱스 버퍼의 어디부터 시작하는가?
    int BaseVertexLocation = 0; // 0번 인덱스는 정점 버퍼의 어디를 가리키는가?
};