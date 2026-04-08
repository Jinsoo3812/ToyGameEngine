// Frank Luna의 저서 및 예제 프로젝트를 기반으로 Realtime Rendering 및 Multithreading을 공부하기 위한 프로젝트입니다.

#include "../Common/d3dApp.h" // Windows 프로그래밍과 닿아있는 DX 초기화 등의 로직을 포함합니다.

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
};

