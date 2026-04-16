// 로그와 관련된 Utility 들이 정의되어 있습니다.

#include <windows.h>
#include <string>

// 일반적 상황에서 문자열 로그를 출력하고 싶을 때
#define LOG_INFO(msg) \
	{ \
		std::wstring finalMsg = L"[INFO] [" + std::wstring(__FUNCTIONW__) + L":" + std::to_wstring(__LINE__) + L"] " + msg + L"\n"; \
		OutputDebugString(finalMsg.c_str()); \
	}

// 문제 상황에서 문자열 로그를 출력하고 싶을 때
#define LOG_WARNING(msg) \
	{ \
		std::wstring finalMsg = L"[WARNING] [" + std::wstring(__FUNCTIONW__) + L":" + std::to_wstring(__LINE__) + L"] " + msg + L"\n"; \
		OutputDebugString(finalMsg.c_str()); \
	}