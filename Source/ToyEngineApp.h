// Frank Luna의 저서 및 예제 프로젝트를 기반으로 Realtime Rendering 및 Multithreading을 공부하기 위한 프로젝트입니다.

#include "../Common/d3dApp.h" // Windows 프로그래밍과 닿아있는 DX 초기화 등의 로직을 포함합니다.

class ToyEngineApp : public D3DApp
{
public:
	ToyEngineApp(HINSTANCE hinstance); 
	~ToyEngineApp();
};