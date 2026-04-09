//***************************************************************************************
// d3dApp.h by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

// 창 생성, 메시지 처리 및 루프, DX3D 초기화 등의 기능을 제공합니다. (166p 참조)
class D3DApp
{
protected:

	D3DApp(HINSTANCE hInstance); // HINSTANCE: WINMAIN과 연결되는 고유 ID
	D3DApp(const D3DApp& rhs) = delete;
	D3DApp& operator=(const D3DApp& rhs) = delete;
	virtual ~D3DApp(); // COM을 해제하고 CommandList를 비웁니다. (GPU가 참조하는 자원들을 안전하게 파괴하기 위해)

public:

	static D3DApp* GetApp();
	
	HINSTANCE AppInst()const;
	HWND      MainWnd()const;
	float     AspectRatio()const;

	bool Get4xMsaaState()const;
	void Set4xMsaaState(bool value);

	int Run();

	/*
	* 자원 할당, Scene Object 초기화, 광원 설정 등 EngineApp 고유의 초기화를 수행합니다.
	* 내부적으로 InitMainWindow과 InitDirect3D, 그리고 최초의 OnResize를 호출합니다.
	*/
	virtual bool Initialize();
	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
	virtual void CreateRtvAndDsvDescriptorHeaps(); // RTV와 DSV Descriptor Heap을 생성합니다.
	virtual void OnResize(); // 창 크기가 변경될 때 호출됩니다. 창 크기의 변경에 따른 Buffer 크기 조정 등을 수행합니다.
	virtual void Update(const GameTimer& gt)=0;
	virtual void Draw(const GameTimer& gt)=0;

	// Convenience overrides for handling mouse input.
	virtual void OnMouseDown(WPARAM btnState, int x, int y){ }
	virtual void OnMouseUp(WPARAM btnState, int x, int y)  { }
	virtual void OnMouseMove(WPARAM btnState, int x, int y){ }

protected:
	bool InitMainWindow(); // EngineApp을 위한 창을 생성합니다.
	bool InitDirect3D(); // Direct3D를 초기화합니다. (138p 참조)
	void CreateCommandObjects(); // CommandQueue, CommandAllocator, CommandList를 생성합니다.
	void CreateSwapChain(); // SwapChain을 초기화하고 새로 생성합니다.
	void FlushCommandQueue(); // CommandQueue에 남아있는 명령이 모두 처리될 때까지 CPU가 기다리는 함수(Fence)

	ID3D12Resource* CurrentBackBuffer()const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView()const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()const;

	void CalculateFrameStats();

	void LogAdapters();
	void LogAdapterOutputs(IDXGIAdapter* adapter);
	void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);

protected:

	static D3DApp* mApp;

	HINSTANCE mhAppInst = nullptr; // application instance handle
	HWND      mhMainWnd = nullptr; // main window handle
	bool      mAppPaused = false;  // is the application paused?
	bool      mMinimized = false;  // is the application minimized?
	bool      mMaximized = false;  // is the application maximized?
	bool      mResizing = false;   // are the resize bars being dragged?
	bool      mFullscreenState = false;// fullscreen enabled

	// Set true to use 4X MSAA (�4.1.8).  The default is false.
	bool      m4xMsaaState = false;    // 4X MSAA enabled
	UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

	// Used to keep track of the �delta-time� and game time (�4.4).
	GameTimer mTimer;
	
	Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory; // DXGI 객체의 생성 및 관리
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain; // SwapChain 객체: Front/Back Buffer의 생성 및 관리
	Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice; // GPU 객체 (GPU interface)

	Microsoft::WRL::ComPtr<ID3D12Fence> mFence; // Fence 객체
	UINT64 mCurrentFence = 0; // Fence가 사용하는 counter. GPU의 처리 경과 확인 용
	
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue; // CommandQueue 객체: GPU가 처리할 명령 queue
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc; // CommandAllocator 객체: CommandList가 메모리에 올려놓은 명령 목록
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList; // CommandList 객체: GPU에게 내릴 명령을 작성하는 도구

	static const int SwapChainBufferCount = 2; // Number of Front/Back Buffer
	int mCurrBackBuffer = 0; // 현재 Back Buffer의 index (0 또는 1)
	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount]; // SwapChaine이 들고 있는 Front/Back Buffer resouce
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer; // Depth/Stencil Buffer resource. 한 개로 충분

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap; // RTV Descriptor의 저장 배열. Memory Heap의 연속 공간
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap; // DSV Descriptor의 저장 배열. Memory Heap의 연속 공간

	D3D12_VIEWPORT mScreenViewport; 
	D3D12_RECT mScissorRect;

	UINT mRtvDescriptorSize = 0;
	UINT mDsvDescriptorSize = 0;
	UINT mCbvSrvUavDescriptorSize = 0;

	// Derived class should set these in derived constructor to customize starting values.
	std::wstring mMainWndCaption = L"d3d App";
	D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int mClientWidth = 800;
	int mClientHeight = 600;
};

