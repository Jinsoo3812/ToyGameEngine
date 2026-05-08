#pragma once

#include <string>
#include <d3d12.h>
#include <wrl.h>
#include "../Common/d3dx12.h"

class Texture
{
public:
	Texture() = default;
	~Texture() = default;

	// SRV를 생성하고 Descriptor Heap에 등록하는 함수
	void BuildSRV(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor);

public:
	std::string Name; // Texture 조회에 사용되는 이름
	std::wstring Filename; // Texture 파일 이름

	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr; // VRAM의 Texture Resource
	Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr; // GPU가 Texture Resource로 데이터를 복사할 때 사용하는 Upload Heap
	UINT SrvHeapIndex = -1; // SRV Heap에서 이 Texture의 SRV가 위치한 인덱스
};