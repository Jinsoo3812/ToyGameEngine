#include "ToyEngineApp.h"
#include "Utility/Log.h" // 원하는 로그 출력을 도와주는 Utility
#include <DirectXColors.h>
#include "AssetManager.h"
#include <filesystem>
#include "WICTextureLoader.h"
#include <mutex>
#include <future>
#include "ResourceUploadBatch.h"
#include <fstream>
#include <sstream>

std::mutex mTextureMapMutex;

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

    // CBV/SRV Descriptor 하나의 크기를 캐시
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// ThreadPool을 생성하여 멀티 스레딩을 활용할 준비
    unsigned int numThreads = std::thread::hardware_concurrency(); // OS를 통해 HW가 지원하는 최대 thread 수를 획득
    mThreadPool = std::make_unique<ThreadPool>(numThreads > 1 ? numThreads - 1 : 1); // Main Thread를 위해 -1
	OutputDebugString((L"ThreadPool created with " + std::to_wstring(numThreads) + L" threads.\n").c_str());

    BuildRootSignature();
    BuildDescriptorHeaps();
    LoadTextures();
    BuildShadersAndInputLayout();
    LoadMapGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

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
    DirectX::XMMATRIX P = DirectX::XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), mCameraNearZ, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void ToyEngineApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
    UpdateCamera(gt);
    
    // FrameResources 배열에 순환 접근
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // GPU가 현재 frame의 자원을 다 처리했는지 확인 및 대기 (N frame 전에 세워둔 fence)
    if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
    UpdateMaterialCBs(gt);
}

void ToyEngineApp::Draw(const GameTimer& gt)
{
    // 이번 frame에 사용할 command allocator를 가져와 Reset하여 재사용할 준비
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    // CommandList에 Allocator 및 PSO를 할당
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mOpaquePSO.Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
    auto transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &transition);

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    auto cbbv = CurrentBackBufferView();
    auto dsv = DepthStencilView();
    mCommandList->OMSetRenderTargets(1, &cbbv, true, &dsv);

    ID3D12DescriptorHeap* descriptorHeaps[] = { mSRVHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    // Indicate a state transition on the resource usage.
    transition = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &transition);

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
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
        float dx = DirectX::XMConvertToRadians(static_cast<float>(x - mLastMousePos.x));
        float dy = DirectX::XMConvertToRadians(static_cast<float>(y - mLastMousePos.y));

        // Alt 키 여부에 따라 회전 방향(부호)과 민감도 결정
        bool isAltPressed = (GetAsyncKeyState(VK_MENU) & 0x8000);
        float sign = isAltPressed ? 1.0f : -1.0f;
        float sensitivity = isAltPressed ? mMouseRotationSensitivity : mMouseOrbitalSensitivity;

        mYaw += sign * sensitivity * dx;
        mPitch += sign * sensitivity * dy;

        // 짐벌락 방지
        mPitch = MathHelper::Clamp(mPitch, -0.99f * DirectX::XM_PIDIV2, 0.99f * DirectX::XM_PIDIV2);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        float dx = static_cast<float>(x - mLastMousePos.x);
        float dy = static_cast<float>(y - mLastMousePos.y);
        mRadius += (dx - dy) * mMouseZoomSensitivity;
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

    // Yaw, Pitch를 이용해 회전행렬 R 도출 (Roll을 이용한 Z축 회전은 생략)
    DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(mPitch, mYaw, 0.0f);

    // Camera 로컬 방향 벡터 도출 (World 방향 벡터에 R을 적용한 후 정규화)
    DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), R);
    DirectX::XMVECTOR right = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), R);
    DirectX::XMVECTOR up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMVECTOR movement = DirectX::XMVectorZero();

    if (GetAsyncKeyState('W') & 0x8000) movement += forward;
    if (GetAsyncKeyState('S') & 0x8000) movement -= forward;
    if (GetAsyncKeyState('D') & 0x8000) movement += right;
    if (GetAsyncKeyState('A') & 0x8000) movement -= right;
    if (GetAsyncKeyState('Q') & 0x8000) movement += up;
    if (GetAsyncKeyState('E') & 0x8000) movement -= up;

    // 이동이 있을 경우에만 위치 갱신 처리
    if (!DirectX::XMVector3Equal(movement, DirectX::XMVectorZero()))
    {
        movement = DirectX::XMVector3Normalize(movement) * mCameraMoveSpeed * dt;

        // 이동 시 카메라 위치와 Target 위치가 동시에 이동해야 함
        DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&mCameraPos);
        DirectX::XMVECTOR target = DirectX::XMLoadFloat3(&mCameraTarget);

        pos += movement;
        target += movement;

        DirectX::XMStoreFloat3(&mCameraPos, pos);
        DirectX::XMStoreFloat3(&mCameraTarget, target);
    }

    mIsWireframe = bool(GetAsyncKeyState('1') & 0x8000);
}

void ToyEngineApp::UpdateCamera(const GameTimer& gt)
{
    // Pitch와 Yaw를 이용해 회전 행렬 및 방향 벡터 도출
    DirectX::XMMATRIX R = DirectX::XMMatrixRotationRollPitchYaw(mPitch, mYaw, 0.0f);
    DirectX::XMVECTOR forward = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), R);
    DirectX::XMVECTOR up = DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), R);

    DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&mCameraPos);
    DirectX::XMVECTOR target;

    // Alt 키를 누른 상태: 카메라의 회전 (Target을 이동)
    if (GetAsyncKeyState(VK_MENU) & 0x8000)
    {
        target = pos + forward * mRadius;
        DirectX::XMStoreFloat3(&mCameraTarget, target);
    }
    // Alt 키를 누르지 않은 상태: 카메라의 궤도 운동 (Camera 이동)
    else
    {
        target = DirectX::XMLoadFloat3(&mCameraTarget);
        pos = target - forward * mRadius; // Forward를 뒤집어서 카메라의 위치를 구함
        DirectX::XMStoreFloat3(&mCameraPos, pos);
    }

    // View 행렬(World > Camera 변환)
    DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void ToyEngineApp::LoadTextures()
{
    // Texture 모여있는 대상 폴더 경로
    std::wstring directory = LR"(C:\Graphic Programming\ToyGameEngine\Textures\San_Miguel\PNG)";

    std::vector<std::future<bool>> threadFutures; // Worker Thread의 완료 상태를 담을 Container
    std::vector<std::future<void>> uploadFutures; // Worker Thread 내부의 GPU Upload 명령의 완료 상태를 담을 Container
	std::mutex uploadMutex; // uploadFutures에 대한 Thread-safe한 접근을 위한 Mutex

    for (const auto& entry : std::filesystem::directory_iterator(directory))
    {
        if (entry.path().extension() == L".png")
        {
            // Worker Thread를 하나 만들어서 Texture Loading 작업을 비동기로 맡긴다.
            threadFutures.push_back(mThreadPool->Enqueue(
                [this, path = entry.path(), &uploadFutures, &uploadMutex]()
                {
                    // Texture 객체 생성
                    auto tex = std::make_unique<Texture>();
                    tex->Name = path.stem().string();
                    tex->Filename = path.wstring();

                    // Upload를 위한 임시 CommandList를 연다. (개별 Thread마다 소유하므로 Thread safe)
                    DirectX::ResourceUploadBatch uploadBatch(md3dDevice.Get());
                    uploadBatch.Begin();

                    // Texture Resource 생성 및 UploadBuffer로의 upload 명령
                    ThrowIfFailed(DirectX::CreateWICTextureFromFile(
                        md3dDevice.Get(),
                        uploadBatch,
                        tex->Filename.c_str(),
                        tex->Resource.ReleaseAndGetAddressOf()
                    ));

                    // UploadBuffer로의 upload 종료 및 GPU에 복사 명령 제출 및 대기
                    auto uploadOperation = uploadBatch.End(mCommandQueue.Get());

					// uploadFutures에 uploadOperation의 완료 상태 저장 (Thread-safe by Mutex)
                    {
                        std::lock_guard<std::mutex> lock(uploadMutex);
                        uploadFutures.push_back(std::move(uploadOperation));
                    }

                    // Descriptor 할당 (Thread-safe)
                    DescriptorHandle allocatedHandle = mSrvAllocator->Allocate();

                    // Texture 객체에 필요한 정보 저장 (나중에 렌더링 시 필요할 수 있음)
                    tex->SrvHeapIndex = allocatedHandle.Index;

                    // CPU Handle을 이용해 SRV 생성
                    tex->BuildSRV(md3dDevice.Get(), allocatedHandle.CPUHandle);

                    // mTextures map에 저장 (Thread-safe by Mutex)
                    {
                        std::lock_guard<std::mutex> lock(mTextureMapMutex);
                        mTextures[tex->Name] = std::move(tex);
                    }

                    return true;
                }));
        }
    }

    // --- 여기부터는 다시 메인 스레드 영역 ---

    std::vector<ID3D12CommandList*> cmdListsToExecute;

    // 모든 Worker Thread의 작업 완료를 대기 (초기 맵 로딩이므로 Main Thread Block)
    for (auto& f : threadFutures){ f.get(); }

    // 제출된 모든 GPU 업로드(VRAM 복사) 작업이 완전히 끝날 때까지 대기
    for (auto& uf : uploadFutures) { uf.wait(); }
}

void ToyEngineApp::BuildRootSignature()
{
    // SRV(Texture)를 바인딩하기 위한 Descriptor Table
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // 사용할 모든 Root Parameter의 배열
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // 성능 Tip: 가장 자주 사용되는 자원부터 할당
    slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL); // Texture를 가리키는 SRV를 가리키는 Descriptor table
    slotRootParameter[1].InitAsConstantBufferView(0); // 
    slotRootParameter[2].InitAsConstantBufferView(1); //
    slotRootParameter[3].InitAsConstantBufferView(2); // 

    auto staticSamplers = GetStaticSamplers();

    // Root Signature 생성을 위한 서술자
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
        (UINT)staticSamplers.size(), staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // Root Signature 생성을 위한 직렬화
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    // Root Signature 생성
    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void ToyEngineApp::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;

    // HLSL Shader Compile 및 저장
    mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_0");

    // Input Assembler가 Vertex Buffer 데이터를 어떻게 읽을지 지정하는 Input Layout
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void ToyEngineApp::LoadMapGeometry()
{
    // Geometry의 외부 바이너리 파일 로드
    std::string filePath = R"(C:\Graphic Programming\ToyGameEngine\Asset\san-miguel-low-poly.toygeom)";
    LR"(C:\Graphic Programming\ToyGameEngine\Textures\San_Miguel\PNG)";
    auto geo = AssetManager::LoadBinaryModel("SanMiguelGeo", filePath, md3dDevice.Get(), mCommandList.Get());

    if (geo != nullptr)
    {
        mGeometries[geo->Name] = std::move(geo);
    }
}

void ToyEngineApp::BuildMaterials()
{
    // 1. mtl 파일 열기
    std::ifstream fin("C:\\Graphic Programming\\ToyGameEngine\\Asset\\san-miguel.mtl");
    if (!fin.is_open())
    {
        LOG_WARNING(L"MTL 파일을 열 수 없습니다.");
        return;
    }

    std::string line;
    Material* currentMat = nullptr;
    UINT matCBIndex = 0;

    // 2. 한 줄씩 파싱
    while (std::getline(fin, line))
    {
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "newmtl")
        {
            std::string matName;
            iss >> matName;

            auto mat = std::make_unique<Material>();
            mat->Name = matName;
            mat->MatCBIndex = matCBIndex++;

            // 기본값 (PBR 렌더러 기준 적당한 초기값)
            mat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
            mat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
            mat->Roughness = 0.5f;
            mat->DiffuseSrvHeapIndex = 0; // 기본 텍스처 인덱스 (에러 방지용)

            currentMat = mat.get();
            mMaterials[matName] = std::move(mat);
        }
        else if (prefix == "Kd" && currentMat)
        {
            iss >> currentMat->DiffuseAlbedo.x >> currentMat->DiffuseAlbedo.y >> currentMat->DiffuseAlbedo.z;
        }
        else if (prefix == "map_Kd" && currentMat)
        {
            std::string texFilename;
            iss >> texFilename;

            // 확장자(.png)를 떼어내고 이름만 추출하여 키값으로 사용 (예: "leaf_diff.png" -> "leaf_diff")
            std::filesystem::path texPath(texFilename);
            std::string texName = texPath.stem().string();

            // LoadTextures()에서 미리 로드해둔 mTextures 맵에서 검색
            std::lock_guard<std::mutex> lock(mTextureMapMutex); // 스레드 안전성 확인
            if (mTextures.find(texName) != mTextures.end())
            {
                // 찾았다면 해당 텍스처의 SRV 인덱스를 머티리얼에 바인딩!
                currentMat->DiffuseSrvHeapIndex = mTextures[texName]->SrvHeapIndex;
            }
            else
            {
                // 텍스처 로드가 누락되었거나 이름이 불일치하는 경우
                std::stringstream ss;
                ss << "Warning: Texture not found for material " << currentMat->Name << " : " << texName << "\n";
                OutputDebugStringA(ss.str().c_str());
            }
        }
        // 기타 속성(Ks, Ns, map_bump 등)도 필요에 따라 파싱...
    }
}

void ToyEngineApp::BuildDescriptorHeaps()
{
    // SRV Heap 생성
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = mNumSRVDescriptors;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSRVHeap)));

    // SRV DescriptorAllocator 생성
    mSrvAllocator = std::make_unique<DescriptorAllocator>(
        md3dDevice.Get(),
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        mNumSRVDescriptors
    );
}


void ToyEngineApp::BuildRenderItems()
{
    auto geo = mGeometries["SanMiguelGeo"].get();
    UINT objCBIndex = 0;

    // 로드된 모든 서브메쉬에 대해 RenderItem 동적 생성
    for (auto const& [submeshName, submeshGeo] : geo->DrawArgs)
    {
        auto ritem = std::make_unique<RenderItem>();
        ritem->ObjCBIndex = objCBIndex++;
        ritem->Mat = mMaterials["woodCrate"].get(); // 임시 재질 적용
        ritem->Geo = geo;
        ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST; // D3D11 -> D3D12 표기로 수정
        ritem->IndexCount = submeshGeo.IndexCount;
        ritem->StartIndexLocation = submeshGeo.StartIndexLocation;
        ritem->BaseVertexLocation = submeshGeo.BaseVertexLocation;

        mAllRitems.push_back(std::move(ritem));
    }

    for (auto& e : mAllRitems)
        mOpaqueRitems.push_back(e.get());
}

void ToyEngineApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();
    for (auto& e : mAllRitems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void ToyEngineApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = XMLoadFloat4x4(&mView);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);

    XMMATRIX viewProj = XMMatrixMultiply(view, proj);

    auto viewDet = XMMatrixDeterminant(view);
    auto projDet = XMMatrixDeterminant(proj);
    XMMATRIX invView = XMMatrixInverse(&viewDet, view);
    XMMATRIX invProj = XMMatrixInverse(&projDet, proj);
    auto viewProjDet = XMMatrixDeterminant(viewProj);
    XMMATRIX invViewProj = XMMatrixInverse(&viewProjDet, viewProj);

    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    mMainPassCB.EyePosW = mCameraPos;
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.NearZ = 0.01f;
    mMainPassCB.FarZ = 1000.0f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void ToyEngineApp::UpdateMaterialCBs(const GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
    for (auto& e : mMaterials)
    {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }
}

void ToyEngineApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList,
    const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        auto vbv = ri->Geo->VertexBufferView();
        auto ibv = ri->Geo->IndexBufferView();
        cmdList->IASetVertexBuffers(0, 1, &vbv);
        cmdList->IASetIndexBuffer(&ibv);
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSRVHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

        cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void ToyEngineApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

    //
    // PSO for opaque objects.
    //
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    opaquePsoDesc.pRootSignature = mRootSignature.Get();
    opaquePsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };
    opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
    opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
    opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mOpaquePSO)));
}

void ToyEngineApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> ToyEngineApp::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp };
}