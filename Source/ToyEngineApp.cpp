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

	BuildShadersAndInputLayout();
	BuildBoxGeometry();
	BuildRenderItems();
	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
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
	OnKeyboardInput(gt);

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

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ToyEngineApp::Draw(const GameTimer& gt)
{
	// 이번 프레임의 Command를 기록하기 전에 CommandAllocator를 초기화
	// Draw의 끝에 있는 FlushCommandQueue가 CommandQueue를 비우기 때문에 안전하게 Allocator를 초기화할 수 있다.
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	// CommandList에 Allocator 및 PSO를 할당
	std::string PSO_type = mIsWireframe ? "opaque_wireframe" : "opaque";
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSOs[PSO_type].Get()));

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

	auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
		mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	cbvHandle.Offset(mObjectCount, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, cbvHandle);

	std::vector<RenderItem*> rItems;
	for (auto& e : mAllRitems)
		rItems.push_back(e.get());
	DrawRenderItems(mCommandList.Get(), rItems);

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

void ToyEngineApp::OnKeyboardDown(WPARAM btnState) {
	std::stringstream ss;
	ss << "[OnKeyboardDown]: " << char(btnState) << std::endl;
	OutputDebugStringA(ss.str().c_str());
}

void ToyEngineApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();
	float vel = 1.f;
	if (GetAsyncKeyState('W') & 0x8000)
		mPos.y += vel * dt;
	if (GetAsyncKeyState('S') & 0x8000)
		mPos.y -= vel * dt;
	if (GetAsyncKeyState('A') & 0x8000)
		mPos.x -= vel * dt;
	if (GetAsyncKeyState('D') & 0x8000)
		mPos.x += vel * dt;

	mIsWireframe = bool(GetAsyncKeyState('1') & 0x8000);
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
	cbvHeapDesc.NumDescriptors = mObjectCount + 1; // 이 Heap에 저장할 View의 개수 (PassCV를 위해 + 1)
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // Heap의 타입을 지정
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // 셰이더가 Heap을 직접 참조할 수 있도록 상태 지정
	cbvHeapDesc.NodeMask = 0; // ?
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap))); // Descriptor Heap을 생성하여 mCbvHeap를 저장
}

void ToyEngineApp::BuildConstantBuffers()
{
	// 상수 버퍼(= UploadBuffer) 객체 할당 및 Upload Heap 할당
	mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), mObjectCount, true); // Object의 개수 만큼
	mPassCB = std::make_unique<UploadBuffer<PassConstants>>(md3dDevice.Get(), 1, true); // 모든 Object가 공유하므로 1

	// 상수 버퍼에 담을 ObjectConstants의 크기를 계산
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	// Object의 수만큼 CBV를 생성
	for (int i = 0; i < mObjectCount; i++) {
		// VRAM 상의 가상 주소 가져오기
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
		cbAddress += (UINT64)i * objCBByteSize; // i 번째 object가 위치한 offset

		// CBV의 속성을 작성하기 위한 서술자
		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = objCBByteSize;

		// CBV 생성 및 저장
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(i, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateConstantBufferView(
			&cbvDesc, handle);
	}

	// Pass 상수 버퍼의 생성
	D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mPassCB->Resource()->GetGPUVirtualAddress();

	// CBV의 속성을 작성하기 위한 서술자
	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = cbAddress;
	cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Pass CBV 생성 및 저장
	auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
	handle.Offset(mObjectCount, mCbvSrvUavDescriptorSize); // ObjectCBV의 다음 index임에 주의할 것
	md3dDevice->CreateConstantBufferView(
		&cbvDesc, handle);
}

void ToyEngineApp::BuildRootSignature()
{
	// Shader programs typically require resources as input (constant buffers,
	// textures, samplers).  The root signature defines the resources the shader
	// programs expect.  If we think of the shader programs as a function, and
	// the input resources as function parameters, then the root signature can be
	// thought of as defining the function signature.  

	// Shader에게 View와 Resgister Slot의 연결을 알려주는 Descriptor Table 생성
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);   // b0: per-object World
	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);   // b1: per-pass data

	// Root Parameter Slot과 Descriptor Table 연결
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	// Input Assembler 단계에서 InputLayout을 사용할 수 있도록 승인.
	// 정점/색인 버퍼는 RootSignature가 아닌 Input Slot에 의해 IA 단계에 직접 묶이고, 이곳에서는 그 권한만 승인함
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
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

void ToyEngineApp::BuildRenderItems()
{
	// 상수 버퍼 index
	UINT objCBIndex = 0;

	// Box 하나의 RenderItem 객체를 생성
	auto boxRitem = std::make_unique<RenderItem>();
	boxRitem->World = MathHelper::Identity4x4();
	boxRitem->ObjCBIndex = objCBIndex++;
	//boxRitem->Geo = mGeometries["boxGeo"].get();
	boxRitem->Geo = mBoxGeo.get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	//boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	//boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	//boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	// 두 번째 Box는 위치만 옮겨서 생성
	auto boxRitem2 = std::make_unique<RenderItem>(boxRitem.get());
	DirectX::XMMATRIX world = DirectX::XMMatrixTranslation(-2.5f, 0.0f, 0.0f);
	XMStoreFloat4x4(&boxRitem2->World, world);
	boxRitem2->ObjCBIndex = objCBIndex++; // 상수 버퍼 index 변경
	mAllRitems.push_back(std::move(boxRitem2));

	// 세 번째 Box도 위치만 옮겨서 생성
	auto boxRitem3 = std::make_unique<RenderItem>(boxRitem.get());
	world = DirectX::XMMatrixTranslation(+2.5f, 0.0f, 0.0f);
	XMStoreFloat4x4(&boxRitem3->World, world);
	boxRitem3->ObjCBIndex = objCBIndex++; // 상수 버퍼 index 변경
	mAllRitems.push_back(std::move(boxRitem3));

	mAllRitems.push_back(std::move(boxRitem));

	mMovingObjIndex = mAllRitems.size() - 1; /*리팩토링 필요. 추후 Object가 늘 때마다 수정해야 함.*/
	mObjectCount = objCBIndex;
}

void ToyEngineApp::UpdateObjectCBs(const GameTimer& gt)
{
	// Object의 위치 변경에 따른 World 행렬 갱신
	DirectX::XMMATRIX world = DirectX::XMMatrixTranslation(mPos.x, mPos.y, mPos.z);
	XMStoreFloat4x4(&(mAllRitems[mMovingObjIndex]->World), world);

	ObjectConstants objConstants;

	// 모든 Render Item을 순회하며 상수 버퍼 업데이트 (최적화 필요)
	for (size_t i = 0; i < mAllRitems.size(); i++) {
		world = DirectX::XMLoadFloat4x4(&(mAllRitems[i]->World));
		std::stringstream ss;
		ss << "UpdateObjectCBs: world[" << i << "] = " << world << std::endl;
		OutputDebugStringA(ss.str().c_str());
		XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
		mObjectCB->CopyData(mAllRitems[i]->ObjCBIndex, objConstants);
	}
}

void ToyEngineApp::UpdateMainPassCB(const GameTimer& gt)
{
	// 현재 카메라에 대한 View와 Proj 행렬 로드
	DirectX::XMMATRIX view = XMLoadFloat4x4(&mView);
	DirectX::XMMATRIX proj = XMLoadFloat4x4(&mProj);

	// 상수 버퍼에 저장
	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	mPassCB->CopyData(0, mMainPassCB);
}

void ToyEngineApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList,
	const std::vector<RenderItem*>& ritems)
{
	for (size_t i = 0; i < ritems.size(); i++)
	{
		auto ri = ritems[i];

		auto vbv = ri->Geo->VertexBufferView();
		auto ibv = ri->Geo->IndexBufferView();

		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(&ibv);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		UINT cbvIndex = ri->ObjCBIndex;

		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
			mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

		cmdList->DrawIndexedInstanced(ri->IndexCount,
			1, 0, 0, 0);
	}
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
	
	// 기본 PSO를 opaque로 설정
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// Wireframe PSO는 opaque_wireframe로 설정
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePsoDesc = psoDesc;
	wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME; // 기존 PSO에서 모드만 wireframe으로 변경
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&wireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}