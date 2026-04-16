#include "ToyEngineApp.h"
#include "Utility/Log.h" // 원하는 로그 출력을 도와주는 Utility
#include <DirectXColors.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		ToyEngineApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ToyEngineApp::ToyEngineApp(HINSTANCE hinstance)
	: D3DApp(hinstance)
{
}

ToyEngineApp::~ToyEngineApp()
{
}

bool ToyEngineApp::Initialize()
{
	if (!D3DApp::Initialize()) {
		LOG_WARNING(L"ToyEngineApp 초기화 실패.");
		return false;
	}

	// CommandList를 Open하여 명령 입력을 준비
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildBoxGeometry();
	BuildPSO();

	// Build.. 함수들에서 작성한 CommandList를 닫은 후 Queue로 넘겨준다.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// 넘겨준 명령들이 모두 처리되기를 기다린다.
	FlushCommandQueue();

	LOG_INFO(L"ToyEngineApp 초기화 성공.");
	return true;
}

void ToyEngineApp::OnResize()
{
	D3DApp::OnResize();

	// 창 크기(화면 비율)가 바뀌었으므로 투영 행렬 P를 다시 계산
	DirectX::XMMATRIX P = DirectX::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ToyEngineApp::Update(const GameTimer& gt)
{
	// 카메라의 좌표를 구면좌표계를 통해 계산
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	float y = mRadius * cosf(mPhi);

	DirectX::XMVECTOR pos = DirectX::XMVectorSet(x, y, z, 1.0f); // 카메라의 위치
	DirectX::XMVECTOR target = DirectX::XMVectorZero(); // 카메라 시선방향의 어느 한 점
	DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // 카메라의 y 방향

	// View 행렬(World > Camera 변환)
	DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);

	// WVP 행렬 계산
	DirectX::XMMATRIX world = XMLoadFloat4x4(&mWorld);
	DirectX::XMMATRIX proj = XMLoadFloat4x4(&mProj);
	DirectX::XMMATRIX worldViewProj = world * view * proj;

	// WVP 행렬을 Constant Buffer에 복사
	ObjectConstants objConstants;
	XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
	mObjectCB->CopyData(0, objConstants);
}

void ToyEngineApp::Draw(const GameTimer& gt)
{
	// 이번 프레임의 Command를 기록하기 전에 CommandAllocator를 초기화
	// Draw의 끝에 있는 FlushCommandQueue가 CommandQueue를 비우기 때문에 안전하게 Allocator를 초기화할 수 있다.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// CommandList에 Allocator 및 PSO를 할당
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()));

	// Viewport와 ScissorRect 설정
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// 현재 BackBuffer의 상태 전이: Present(화면에 보여져서 수정 불가) -> RenderTarget(그리기 대상)
	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &transition);


	// BackBuffer와 DS Buffer 초기화
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// 출력 대상을 현재 BackBuffer와 DS Buffer로 지정
	auto dsv = DepthStencilView();
	auto cbbv = CurrentBackBufferView();
	mCommandList->OMSetRenderTargets(1, &cbbv, true, &dsv);

	// 셰이더가 View를 참조할 수 있도록 Descriptor Heap을 셰이더에 바인딩
	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() }; // 현재는 CBV 한 개
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	// RootSignature을 셰이더에 바인딩
	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	// IA에 Box의 정점 버퍼와 인덱스 버퍼를 바인딩하고 Primitive Topology을 설정
	auto vbv = mBoxGeo->VertexBufferView();
	auto ibv = mBoxGeo->IndexBufferView();
	mCommandList->IASetVertexBuffers(0, 1, &vbv);
	mCommandList->IASetIndexBuffer(&ibv);
	mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// RootSignature의 0번 슬롯에 CBV Heap을 바인딩
	mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

	// 그려!!!
	mCommandList->DrawIndexedInstanced(
		mBoxGeo->DrawArgs["box"].IndexCount,
		1, 0, 0, 0);

	// 그리기를 끝냈으니 PRESENT로 상태 전이
	transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &transition);

	// CommandList 작성 종료
	ThrowIfFailed(mCommandList->Close());

	// CommandQueue로 CommandList 전달
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// BackBuffer 교체
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// 현재까지의 작업이 모두 끝날 때 까지 CPU 대기
	FlushCommandQueue();
}

void ToyEngineApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ToyEngineApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ToyEngineApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.005 unit in the scene.
		float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ToyEngineApp::BuildShadersAndInputLayout()
{
	HRESULT hr = S_OK;

	// HLSL을 컴파일 (GPU를 위한 ByteCode로 변환)
	mvsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = d3dUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

	// InputLayout의 작성
	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void ToyEngineApp::BuildBoxGeometry()
{
	// Box를 구성하는 정점 배열 정의
	std::array<Vertex, 8> vertices =
	{
		Vertex({ DirectX::XMFLOAT3(-1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::White) }),
		Vertex({ DirectX::XMFLOAT3(-1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Black) }),
		Vertex({ DirectX::XMFLOAT3(+1.0f, +1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Red) }),
		Vertex({ DirectX::XMFLOAT3(+1.0f, -1.0f, -1.0f), DirectX::XMFLOAT4(DirectX::Colors::Green) }),
		Vertex({ DirectX::XMFLOAT3(-1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Blue) }),
		Vertex({ DirectX::XMFLOAT3(-1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Yellow) }),
		Vertex({ DirectX::XMFLOAT3(+1.0f, +1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Cyan) }),
		Vertex({ DirectX::XMFLOAT3(+1.0f, -1.0f, +1.0f), DirectX::XMFLOAT4(DirectX::Colors::Magenta) })
	};

	// Box를 구성하는 삼각형을 정점의 index로 정의 (시계 방향 나열이 기본)
	std::array<std::uint16_t, 36> indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7
	};

	// GPU로 전송할 정점 버퍼와 인덱스 버퍼의 크기를 계산
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	// Box의 MeshGeometry 객체 생성
	mBoxGeo = std::make_unique<MeshGeometry>();
	mBoxGeo->Name = "boxGeo";

	// 정점 버퍼를 CPU Memory에 백업하기 위한 Blob을 생성하고, vertices 배열의 데이터를 복사
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU));
	CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	// 인덱스 버퍼를 CPU Memory에 백업하기 위한 Blob을 생성하고, indices 배열의 데이터를 복사
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU));
	CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	// GPU의 Default Heap에 정점 버퍼를 생성 후 vertices 배열을 복사
	mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, mBoxGeo->VertexBufferUploader);

	// GPU의 Default Heap에 인덱스 버퍼를 생성 후 indices 배열을 복사
	mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, mBoxGeo->IndexBufferUploader);

	/* Uploader Buffer의 포인터는 데이터 복사가 완료된 후 해제하기 위해 반환받아 보관 */

	// 정점 버퍼와 인덱스 버퍼의 속성을 mBoxGeo에 기록
	mBoxGeo->VertexByteStride = sizeof(Vertex); // 정점 하나의 Byte 크기
	mBoxGeo->VertexBufferByteSize = vbByteSize; // 정점 버퍼 전체의 Byte 크기
	mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT; // 인덱스 하나의 Byte 크기
	mBoxGeo->IndexBufferByteSize = ibByteSize; // 인덱스 버퍼 전체의 Byte 크기

	/* SubMesh는 추후 공부 필요 */
	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	mBoxGeo->DrawArgs["box"] = submesh;
}

void ToyEngineApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc; // Descriptor Heap의 속성을 정의하는 서술자
	cbvHeapDesc.NumDescriptors = 1; // 이 Heap에 저장할 View의 개수 (현재 Box 하나)
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // Heap의 타입을 지정
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 셰이더가 Heap을 직접 참조할 수 있도록 상태 지정
	cbvHeapDesc.NodeMask = 0; // ?
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap))); // Descriptor Heap을 생성하여 mCbvHeap를 저장
}

void ToyEngineApp::BuildConstantBuffers()
{
	// 상수 버퍼(= UploadBuffer) 객체 할당 및 Upload Heap 할당(UploadBuffer의 기능)
	// 두 번째 인자는 Buffer에 저장할 요소의 개수 (현재 Box 하나이므로 1)
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

	// 상수 버퍼에 담을 ObjectConstants의 크기를 계산 (현재는 4x4 행렬 하나이므로 64 Byte)
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	// VRAM 상의 가상 주소 가져오기?
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

	// 추후 Box 이외의 Object가 추가되어 wObjectCB가 담을 상수 버퍼가 늘어났을 때를 대비한 offset
	int boxCBufIndex = 0;
	cbAddress += boxCBufIndex * objCBByteSize;

	// CBV의 속성을 작성하기 위한 서술자
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	// CBV 생성 및 저장
	md3dDevice->CreateConstantBufferView(
		&cbvDesc,
		mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void ToyEngineApp::BuildRootSignature()
{
	// Shader programs typically require resources as input (constant buffers,
	// textures, samplers).  The root signature defines the resources the shader
	// programs expect.  If we think of the shader programs as a function, and
	// the input resources as function parameters, then the root signature can be
	// thought of as defining the function signature.  

	// Root Parameter Slot의 개수 정의.
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];

	// Shader에게 View와 Resgister Slot의 연결을 알려주는 table 생성
	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0); // cbvTable은 CBV 매핑 하나를 담는 table이며, 그것은 register b0(base register 0)에 연결한다.
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	// Input Assembler 단계에서 InputLayout을 사용할 수 있도록 승인. (왜 여기서 하지 갑자기?)
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// rootSigDesc를 GPU가 읽을 수 있도록 직렬화
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	// VRAM에 Root Signature를 생성하고 mRootSignature에 저장
	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&mRootSignature)));
}

void ToyEngineApp::BuildPSO()
{
	// PSO의 속성을 정의하는 서술자
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() }; // InputLayout 포함
	psoDesc.pRootSignature = mRootSignature.Get(); // RootSignature 포함
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
		mvsByteCode->GetBufferSize()
	}; // 정점 셰이더의 기계어(ByteCode) 포함
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mpsByteCode->GetBufferSize()
	}; // 픽셀 셰이더의 기계어(ByteCode) 포함

	// 이하 기타 등등..
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = mDepthStencilFormat;
	
	// PSO 생성 시점에서 각 속성들의 유효성을 미리 검사하므로 Draw 호출 시점에 비용이 발생하지 않음.
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}