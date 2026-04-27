#pragma once

#include "../Common/d3dUtil.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"

struct ObjectConstants
{
    DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
};

struct PassConstants
{
    DirectX::XMFLOAT4X4 View = MathHelper::Identity4x4();
    DirectX::XMFLOAT4X4 Proj = MathHelper::Identity4x4();
};

struct Vertex
{
    DirectX::XMFLOAT3 Pos;
    DirectX::XMFLOAT4 Color;
};

// CPU가 한 frame의 command list를 구축하는 데 필요한 resource들을 대표하는 클래스.
struct FrameResource
{
public:
    
    FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    // 각 frame이 개별 command allocator를 소유.
    // 다른 frame이 그려지고 있는 중에도 CPU는 다음 frame의 command list를 작성할 수 있다.
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    // 각 frame은 시간의 흐름에 따른 CB의 복제본을 소유.
    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

    // CPU가 다시 해당 frame을 작업할 차례가 다가왔을 때 확인할 fence
    UINT64 Fence = 0;
};