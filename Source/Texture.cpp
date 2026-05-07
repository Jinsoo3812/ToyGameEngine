#include "Texture.h"
#include "Utility/Log.h"

void Texture::BuildSRV(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor)
{
	// 텍스처 자원이 아직 로드되지 않았다면 뷰를 만들 수 없으므로 방어 코드 작성
	if (Resource == nullptr)
	{
		LOG_WARNING(L"Texture 자원이 아직 로드되지 않았습니다. SRV를 생성할 수 없습니다.");
		return;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = Resource->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = Resource->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	// 전달받은 디스크립터 핸들 위치에 SRV 규격서를 작성
	device->CreateShaderResourceView(Resource.Get(), &srvDesc, hDescriptor);
}