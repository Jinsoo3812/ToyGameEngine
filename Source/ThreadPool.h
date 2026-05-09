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
        mWorkers.emplace_back([this] {
            // [중요 최적화] 스레드가 생성될 때 한 번만 COM을 초기화합니다.
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool isComInitialized = SUCCEEDED(hr);

            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->mQueueMutex);
                    this->mCondition.wait(lock, [this] {
                        return this->mStop || !this->mTasks.empty();
                        });

                    if (this->mStop && this->mTasks.empty())
                        break;

                    task = std::move(this->mTasks.front());
                    this->mTasks.pop();
                }
                task(); // 작업 실행
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
        std::unique_lock<std::mutex> lock(mQueueMutex);
        mStop = true;
    }
    mCondition.notify_all();
    for (std::thread& worker : mWorkers) {
        worker.join();
    }
}

template<class F, class... Args>
auto ThreadPool::Enqueue(F&& f, Args&&... args)
-> std::future<std::invoke_result_t<F, Args...>>
{
    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        if (mStop) throw std::runtime_error("enqueue on stopped ThreadPool");
        mTasks.emplace([task]() { (*task)(); });
    }
    mCondition.notify_one();
    return res;
}