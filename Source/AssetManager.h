#pragma once
#include <string>
#include <vector>
#include <memory>
#include "FrameResource.h" // Vertex 구조체와 MeshGeometry가 정의된 헤더 포함

class AssetManager
{
public:
    // 바이너리 파일을 읽어 단일 MeshGeometry로 병합 후 반환하는 함수
    static std::unique_ptr<MeshGeometry> LoadBinaryModel(
        const std::string& geoName,
        const std::string& filePath,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList);
};