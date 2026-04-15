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

	LOG_INFO(L"ToyEngineApp 초기화 성공.");
	return true;
}

void ToyEngineApp::OnResize()
{
	D3DApp::OnResize();
}

void ToyEngineApp::Update(const GameTimer& gt)
{
}

void ToyEngineApp::Draw(const GameTimer& gt)
{
	// 이번 프레임의 Command를 기록하기 전에 CommandAllocator를 초기화
	// Draw의 끝에 있는 FlushCommandQueue가 CommandQueue를 비우기 때문에 안전하게 Allocator를 초기화할 수 있다.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// CommandList에 Allocator를 할당하며 초기화
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// 현재 BackBuffer의 상태 전이: Present(화면에 보여져서 수정 불가) -> RenderTarget(그리기 대상)
	auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	// CommandAlloc에 Transition 명령을 기록
	mCommandList->ResourceBarrier(1, &transition);

	// NDC 공간을 Viewport와 ScisserRect로 변환
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	auto dsv = DepthStencilView();
	auto cbbv = CurrentBackBufferView();
	mCommandList->OMSetRenderTargets(1, &cbbv, true, &dsv);

	// Indicate a state transition on the resource usage.
	transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &transition);

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Wait until frame commands are complete.  This waiting is inefficient and is
	// done for simplicity.  Later we will show how to organize our rendering code
	// so we do not have to wait per frame.
	FlushCommandQueue();
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