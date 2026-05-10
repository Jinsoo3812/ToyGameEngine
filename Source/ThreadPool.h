#pragma once
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <objbase.h> // for CoInitializeEx

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    
    // 호출할 함수와 그 인자들을 받아서 작업 큐에 넣고, 그것의 future 객체를 반환합니다.
    template<class F, class... Args> // F: 함수 타입, Args: 함수 인자 타입들
    auto Enqueue(F&& f, Args&&... args) // &&: 보편 참조로서 Rvalue와 Lvalue 모두 받을 수 있게 함
        -> std::future<std::invoke_result_t<F, Args...>>; // 후위 반환: 매개변수 f, args를 이용해야만 반환 타입을 결정할 수 있을 때 사용
                            // invoke_result: F와 Args로부터 반환 타입을 추론
private:
    std::vector<std::thread> mWorkers;
    std::queue<std::function<void()>> mTasks;

    std::mutex mQueueMutex;
    std::condition_variable mCondition;
    bool mStop;
};

// 템플릿 구현부 (헤더에 위치해야 함)
inline ThreadPool::ThreadPool(size_t numThreads) : mStop(false) {
    for (size_t i = 0; i < numThreads; ++i) {
        // mWorkers에 다음 람다를 실행하는 thread를 생성하여 추가
        mWorkers.emplace_back([this] {
            // Thread 내부에서 COM을 사용하기 위한 초기화
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool isComInitialized = SUCCEEDED(hr);

            while (true) {
                std::function<void()> task;
                { // Mutex 범위 지정
                    // mQueueMutex 잠금 (lock 으로 관리)
                    std::unique_lock<std::mutex> lock(this->mQueueMutex);
                    // lock을 해제함과 동시에 람다 조건식을 걸고 Thread 수면
                    this->mCondition.wait(lock, [this] {
                        // ThreadPool이 종료되었거나, 처리할 작업이 있을 때 깨워짐
						// 기상하며 다시 lock을 획득
						return this->mStop || !this->mTasks.empty();
                        });

                    // ThreadPool이 종료되고, 처리할 작업이 없으면 While 탈출
                    if (this->mStop && this->mTasks.empty())
                        break;

					// (처리할 작업이 있음이 보장되므로) 작업 큐에서 하나의 작업을 꺼내서 task에 저장 
                    task = std::move(this->mTasks.front());
                    this->mTasks.pop();
                } // lock이 범위를 벗어나면서 자동으로 mQueueMutex가 unlock

                // 작업 실행
                task();
            }

            // 스레드 종료 시 COM 해제
            if (isComInitialized) {
                CoUninitialize();
            }
        });
    }
}

inline ThreadPool::~ThreadPool() {
    {
        // ThreadPool이 종료
        std::unique_lock<std::mutex> lock(mQueueMutex);
        mStop = true;
    }
    // 모든 worker thread를 깨움
    mCondition.notify_all();
    for (std::thread& worker : mWorkers) {
        // 각 worker thread가 작업을 마치고 종료되는 것을 대기
        worker.join();
    }
}

template<class F, class... Args>
auto ThreadPool::Enqueue(F&& f, Args&&... args)
-> std::future<std::invoke_result_t<F, Args...>>
{
    // 함수 f에 인자 args를 넣었을 때 반환되는 타입을 컴파일 타임에 추론하여 저장
    using return_type = std::invoke_result_t<F, Args...>;

    // 호출 가능한 객체를의 실행결과가 future로 반환되로록 패키지
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...) // bind를 이용해 함수와 인자를 묶어 하나의 호출 가능한 객체로 승격
    );

    // 패키지된 작업의 future 객체 획득
    std::future<return_type> res = task->get_future();
    { // Mutex 범위 지정
        // mQueueMutex 잠금 (lock 으로 관리)
        std::unique_lock<std::mutex> lock(mQueueMutex);
        if (mStop) throw std::runtime_error("enqueue on stopped ThreadPool");
        mTasks.emplace([task]() { (*task)(); });
	} // lock이 범위를 벗어나면서 자동으로 mQueueMutex가 unlock

    // 새 작업이 들어왔다고 worker thread를 깨움
    mCondition.notify_one();
    return res;
}