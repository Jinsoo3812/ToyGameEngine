#include "ToyEngineApp.h"
#include "Utility/Log.h" // 원하는 로그 출력을 도와주는 Utility
#include <DirectXColors.h>

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

	// Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
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