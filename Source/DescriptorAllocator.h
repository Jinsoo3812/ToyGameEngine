#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <atomic>
#include <stdexcept>
#include "../Common/d3dx12.h"

// Descriptor의 위치에 대한 정보(index, Handle)을 담는 구조체
struct DescriptorHandle
{
    uint32_t Index = 0;
    CD3DX12_CPU_DESCRIPTOR_HANDLE CPUHandle;
};

// 디스크립터 힙을 관리하고 빈 슬롯을 동적으로 나누어주는 클래스
class DescriptorAllocator
{
public:
    DescriptorAllocator(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
        : mMaxDescriptors(numDescriptors)
    {
        // 거대한 Heap 생성
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = numDescriptors;
        heapDesc.Type = type;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mHeap));

        // 현재 GPU의 Descriptor 1개 크기 캐싱
        mDescriptorSize = device->GetDescriptorHandleIncrementSize(type);
    }

    // 텍스처 등이 뷰를 만들기 위해 빈 슬롯을 요청할 때 호출
    DescriptorHandle Allocate()
    {
		// 고유 인덱스 발급
        uint32_t index = mCurrentOffset.fetch_add(1);

        if (index >= mMaxDescriptors)
        {
            // 힙 용량 초과 예외 처리
            throw std::runtime_error("Descriptor Heap이 꽉 찼습니다!");
        }

        DescriptorHandle handle;
        handle.Index = index;

        // CPU Handle 계산
        handle.CPUHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mHeap->GetCPUDescriptorHandleForHeapStart());
        handle.CPUHandle.Offset(index, mDescriptorSize);

        return handle;
    }

    ID3D12DescriptorHeap* GetHeap() const { return mHeap.Get(); }

private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    uint32_t mDescriptorSize = 0;
    uint32_t mMaxDescriptors = 0;

	// 여러 Worker Thread에게 index를 발급해줘야 하므로 Atomic (Lock-free)
    std::atomic<uint32_t> mCurrentOffset{ 0 };
};