#include "AssetManager.h"
#include <fstream>
#include <iostream>

std::unique_ptr<MeshGeometry> AssetManager::LoadBinaryModel(
    const std::string& geoName, const std::string& filePath,
    ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    std::ifstream fin(filePath, std::ios::binary);
    if (!fin) {
        // [문제 해결] 파일 경로 오류 시 즉시 파악 가능하도록 예외 또는 로그 처리
        OutputDebugStringA(("파일 개방 실패: " + filePath + "\n").c_str());
        return nullptr;
    }

    uint32_t numMeshes = 0;
    fin.read(reinterpret_cast<char*>(&numMeshes), sizeof(uint32_t));

    // 전체 데이터를 담을 하나의 거대한 벡터 생성
    std::vector<Vertex> totalVertices;
    std::vector<std::uint32_t> totalIndices; // 32비트 인덱스 사용

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = geoName;

    // 1722개의 메쉬를 순회하며 데이터 병합
    for (uint32_t i = 0; i < numMeshes; ++i)
    {
        uint32_t numVertices = 0;
        uint32_t numIndices = 0;

        fin.read(reinterpret_cast<char*>(&numVertices), sizeof(uint32_t));
        fin.read(reinterpret_cast<char*>(&numIndices), sizeof(uint32_t));

        // 파일에서 읽어올 임시 버퍼 생성
        std::vector<Vertex> tempVertices(numVertices);
        std::vector<uint32_t> tempIndices(numIndices);

        fin.read(reinterpret_cast<char*>(tempVertices.data()), numVertices * sizeof(Vertex));
        fin.read(reinterpret_cast<char*>(tempIndices.data()), numIndices * sizeof(uint32_t));

        // SubmeshGeometry 설정 (현재까지 누적된 정점/인덱스 개수가 시작점이 됨)
        SubmeshGeometry submesh;
        submesh.IndexCount = numIndices;
        submesh.StartIndexLocation = (UINT)totalIndices.size();
        submesh.BaseVertexLocation = (int)totalVertices.size();

        // 맵에 저장 (예: "Submesh_0", "Submesh_1" ...)
        std::string submeshName = "Submesh_" + std::to_string(i);
        geo->DrawArgs[submeshName] = submesh;

        // 전체 벡터에 병합 (데이터 이어 붙이기)
        totalVertices.insert(totalVertices.end(), tempVertices.begin(), tempVertices.end());
        totalIndices.insert(totalIndices.end(), tempIndices.begin(), tempIndices.end());
    }

    fin.close();

    // GPU 버퍼 생성 (d3dUtil::CreateDefaultBuffer 활용)
    const UINT vbByteSize = (UINT)totalVertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)totalIndices.size() * sizeof(std::uint32_t);

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), totalVertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), totalIndices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, totalVertices.data(), vbByteSize, geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, totalIndices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;

    // [중요] 32비트 인덱스 포맷으로 지정
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    return geo;
}