// Frank Luna의 저서 및 예제 프로젝트를 기반으로 Realtime Rendering 및 Multithreading을 공부하기 위한 프로젝트입니다.

#include "../Common/d3dApp.h" // Windows 프로그래밍과 닿아있는 DX 초기화 등의 로직
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "RenderItem.h" // 정점 구조체 및 RenderItem 구조체를 포함

// Object가 갖는 상수값들을 담는 구조체. (현재는 WVP 행렬만 포함)
// 위치 이동 필요
struct ObjectConstants
{
	DirectX::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
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

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	// HLSL 셰이더를 로드하고 그 명세서를 정의합니다.
	// Text로 된 HLSL 파일을 읽어 ByteCode로 컴파일하고 InputLayout을 작성합니다.
	void BuildShadersAndInputLayout();

	/* 리팩토링 필요 */
	// Box의 형태를 정의하고 정점 버퍼와 인덱스 버퍼를 생성합니다.
	void BuildBoxGeometry();

	// View를 종류 별로 저장하는 Descriptor Heap을 생성합니다.
	void BuildDescriptorHeaps();

	// ConstantBuffer를 UploadBuffer 객체로 생성합니다.
	void BuildConstantBuffers();

	// RootSignature를 생성합니다.
	void BuildRootSignature();

	// Pipeline State Object을 생성합니다.
	void BuildPSO();

private:
	//
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	// Constant Buffer View를 위한 Descriptor Heap
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	// ObjectConstants를 담는 실제 버퍼인 UploadBuffer 객체
	// Constant Buffer의 256byte의 배수 할당 규칙 및 UploadHeap만 사용한다는 특징으로 인해 이러한 래퍼 클래스를 사용한다.
	std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;

	// Box의 Mesh를 정의하는 MeshGeometry 객체
	std::unique_ptr<MeshGeometry> mBoxGeo = nullptr;

	// 정점 셰이더의 기계어
	Microsoft::WRL::ComPtr<ID3DBlob> mvsByteCode = nullptr;
	// 픽셀 셰이더의 기계어
	Microsoft::WRL::ComPtr<ID3DBlob> mpsByteCode = nullptr;

	// C++ 구조체의 각 성분과 HLSL 셰이더의 입력 간의 매핑을 정의하는 서술자
	// 일대일 대응이므로, C++ 구조체의 멤버 수만큼 필요하다.
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// 그래픽 파이프라인의 상태를 제어하는 여러 객체들(셰이더, InputLayout, RootSignature 등)을 묶어서 저장하는 객체
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mPSO = nullptr;

	DirectX::XMFLOAT4X4 mWorld = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = DirectX::XM_PIDIV4;
	float mRadius = 5.0f;

	POINT mLastMousePos;
};