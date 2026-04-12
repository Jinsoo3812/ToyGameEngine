// Frank Luna의 저서 및 예제 프로젝트를 기반으로 Realtime Rendering 및 Multithreading을 공부하기 위한 프로젝트입니다.

#include "../Common/d3dApp.h" // Windows 프로그래밍과 닿아있는 DX 초기화 등의 로직을 포함합니다.
#include "../Common/MathHelper.h"

// 정점 구조체
struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

class ToyEngineApp : public D3DApp
{
public:
	ToyEngineApp(HINSTANCE hinstance); 
	~ToyEngineApp();

	// EngineApp 고유의 초기화를 수행합니다.
	virtual bool Initialize() override;

private:
	// 창 크기가 바뀔 때 마다 수행됩니다. 창 크기 변경에 영향을 받는 자원들의 작업을 수행해야 합니다.
	virtual void OnResize()override;
	// 매 프레임 호출됩니다. 시간의 흐름에 따른 갱신 작업을 수행해야 합니다.
	virtual void Update(const GameTimer& gt)override;
	// 매 프레임 호출됩니다. BackBuffer를 그리고 교체하여 화면에 표시합니다.
	virtual void Draw(const GameTimer& gt)override;

	// HLSL 셰이더를 로드하고 그 명세서를 정의합니다.
	// Text로 된 HLSL 파일을 읽어 ByteCode로 컴파일하고 InputLayout을 작성합니다.
	void BuildShadersAndInputLayout();

private:
	// 정점 셰이더의 기계어
	Microsoft::WRL::ComPtr<ID3DBlob> mvsByteCode = nullptr;
	// 픽셀 셰이더의 기계어
	Microsoft::WRL::ComPtr<ID3DBlob> mpsByteCode = nullptr;

	// 정점 구조체의 각 성분과 대응되어, 그것으로 무엇을 해야 하는지 DX3D에게 알려주는 서술자
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
};