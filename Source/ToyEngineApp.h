// Frank Luna의 저서 및 예제 프로젝트를 기반으로 Realtime Rendering 및 Multithreading을 공부하기 위한 프로젝트입니다.

#include "../Common/d3dApp.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/StringHelper.h"
#include "../Common/GeometryGenerator.h"
#include "RenderItem.h"
#include "FrameResource.h"
#include "Texture.h"
#include "DescriptorAllocator.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

// FrameResource의 개수
// CPU는 GPU보다 N frame을 앞서서 작업할 수 있다.
const int gNumFrameResources = 3;

class ToyEngineApp : public D3DApp
{
public:
	ToyEngineApp(HINSTANCE hinstance); 
	~ToyEngineApp();

	// EngineApp 고유의 초기화를 수행합니다.
	virtual bool Initialize() override;

private:
	/* Update Function */

	// 카메라의 위치와 방향을 갱신합니다.
	void UpdateCamera(const GameTimer& gt);
	// 창 크기가 바뀔 때 마다 수행됩니다. 창 크기 변경에 영향을 받는 자원들의 작업을 수행해야 합니다.
	virtual void OnResize()override;
	// 매 프레임 호출됩니다. 시간의 흐름에 따른 갱신 작업을 수행해야 합니다.
	virtual void Update(const GameTimer& gt)override;
	// 매 프레임 호출됩니다. BackBuffer를 그리고 교체하여 화면에 표시합니다.
	virtual void Draw(const GameTimer& gt)override;


	/* Keyboard & Mouse Input Function */

	// 마우스가 프로그램 창 위에서 눌렸을 때 호출
	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	// 마우스가 프로그램 창 위에서 떼졌을 때 호출
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	// 마우스가 프로그램 창 위에서 움직였을 때 호출.
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;
	// 키보드가 처음 눌릴 때 한 번 호출됩니다.
	virtual void OnKeyboardDown(WPARAM btnState)override;
	// 키보드의 상태를 매 프레임마다 체크하여 처리합니다.
	void OnKeyboardInput(const GameTimer& gt);
	

	/* Initialize Function */

	// RootSignature를 생성합니다.
	void BuildRootSignature();
	// View를 종류 별로 저장하는 Descriptor Heap을 생성합니다.
	void BuildDescriptorHeaps();
	// Texture를 로드하여 mTextures에 저장
	void LoadTextures();
	// HLSL Shader Compile 및 InputLayout 작성
	void BuildShadersAndInputLayout();
	// Map의 MeshGeometry를 생성하여 mGeometries에 저장
	void LoadMapGeometry();
	
	
	
	
	// Material을 생성하여 mMaterials에 저장
	void BuildMaterials();


	// RenderItem을 생성 및 저장
	void BuildRenderItems();

	// 상수 버퍼 값을 갱신합니다.
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);

	// RenderItem을 순회하며 Drawcall 호출
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	// Pipeline State Object을 생성합니다.
	void BuildPSOs();

	// FrameResource를 생성합니다.
	void BuildFrameResources();

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
private:
	// 셰이더가 사용하는 자원(상수 버퍼 등)과 셰이더의 연결을 정의하는 객체
	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	// Constant Buffer View를 위한 Descriptor Heap
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	// Shader Resource View를 위한 Descriptor Heap
	ComPtr<ID3D12DescriptorHeap> mSRVHeap = nullptr;


	// 화면에 그릴 object들의 MeshGeometries
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	// 화면에 그릴 object들의 Material (이름 string으로 조회)
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	// 화면에 그릴 object들의 Texture (이름 string으로 조회)
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	// HLSL 셰이더의 기계어를 저장하는 객체 (셰이더 이름 string으로 조회)
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;

	// 그래픽 파이프라인의 상태를 제어하는 여러 객체들(셰이더, InputLayout, RootSignature 등)을 묶어서 저장하는 객체
	ComPtr<ID3D12PipelineState> mOpaquePSO = nullptr;
	bool mIsWireframe = false; // Wireframe 모드 Flag

	// 정점 셰이더의 기계어
	ComPtr<ID3DBlob> mvsByteCode = nullptr;
	// 픽셀 셰이더의 기계어
	ComPtr<ID3DBlob> mpsByteCode = nullptr;

	// C++ 구조체의 각 성분과 HLSL 셰이더의 입력 간의 매핑을 정의하는 서술자
	// 일대일 대응이므로, C++ 구조체의 멤버 수만큼 필요하다.
	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// 모든 RenderItem
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	// 한 frame을 그릴 때 단위로 사용될 전역 상수 버퍼의 속성들
	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;
	
	DirectX::XMFLOAT4X4 mWorld = MathHelper::Identity4x4(); // Object > World 변환 행렬
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4(); // World > Camera 변환 행렬
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4(); // Camera > Projection 변환 행렬
	
	POINT mLastMousePos; // 마우스의 윈도우 창 위에서의 픽셀 좌표

	/* Initialize Member */
	UINT mNumSRVDescriptors = 100; // SRV Heap에 생성할 SRV의 개수
	std::unique_ptr<DescriptorAllocator> mSrvAllocator = nullptr; // SRV에게 Heap을 할당해주는 Allocator (Thread-safe)

	/* Camera Member */
	float mPitch = 0.0f; // x축을 기준으로 상하 회전
	float mYaw = 0.0f; // y축을 기준으로 좌우 회전 (반시계방향)
	float mRadius = 5.0f; // 카메라와 Target 사이의 거리 (카메라의 궤도 반지름)

	DirectX::XMFLOAT3 mCameraPos = { 0.0f, 0.0f, -5.0f }; // 카메라의 월드 좌표
	DirectX::XMFLOAT3 mCameraForward = { 0.0f, 0.0f, 1.0f }; // 카메라의 시선 방향 벡터
	DirectX::XMFLOAT3 mCameraTarget = { 0.0f, 0.0f, 0.0f }; // 카메라 시선 방향의 어느 한 점

	float mMouseRotationSensitivity = 0.1f;
	float mMouseOrbitalSensitivity = 0.25f;
	float mMouseZoomSensitivity = 0.005f;
	float mCameraMoveSpeed = 3.0f;
	float mCameraNearZ = 0.01f;

	/* Frame Resource */
	std::vector<std::unique_ptr<FrameResource>> mFrameResources; // FrameResources Container
	FrameResource* mCurrFrameResource = nullptr; // 현재 CPU가 작업 중인 FrameResource
	int mCurrFrameResourceIndex = 0; // CPU가 작업 중인 FrameResource의 index
	UINT mCbvSrvDescriptorSize = 0;
};