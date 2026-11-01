#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <windows.h>
#include <mutex>

class HighPrecisionThread
{
public:
    using Task = std::function<void()>;

    HighPrecisionThread() : m_running(false)
    {
        timeBeginPeriod(1);
    }

    ~HighPrecisionThread()
    {
        stop();
        timeEndPeriod(1);
    }

    void start(int fps, Task task)
    {
        if (m_running)
            return;
        m_running = true;
        updateInterval(fps);
        m_task = task;
        m_thread = std::thread(&HighPrecisionThread::run, this);
    }

    void stop()
    {
        m_running = false;
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    // 动态更新帧率
    void updateInterval(int fps)
    {
        if (fps <= 0)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_intervalNs = (int64_t)(1000000000.0 / fps);
    }

private:
    void run()
    {
        // 设置线程优先级：最高，但非实时，避免饿死UI
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

        auto next_frame = std::chrono::steady_clock::now();

        while (m_running)
        {
            // 1. 执行任务
            if (m_task)
                m_task();

            // 2. 计算下一帧时间
            int64_t currentInterval = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                currentInterval = m_intervalNs;
            }
            next_frame += std::chrono::nanoseconds(currentInterval);

            auto now = std::chrono::steady_clock::now();

            // 3. 等待策略
            if (now < next_frame)
            {
                // [Optimization] 低负载模式优化
                // 如果目标帧率 <= 30FPS (间隔 >= 33ms)，说明是编辑模式或低功耗模式
                // 此时不需要极致的微秒级精度，直接 sleep 即可，大幅降低 CPU 占用
                if (currentInterval >= 33000000)
                {
                    std::this_thread::sleep_until(next_frame);
                }
                else
                {
                    // [High Performance] 直播/录制模式
                    // 使用混合等待策略：先 sleep 到临近点，最后 1ms 使用忙等待 (Spin Lock) 以保证精度
                    auto remaining =
                        std::chrono::duration_cast<std::chrono::milliseconds>(next_frame - now)
                            .count();
                    if (remaining > 2)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(remaining - 1));
                    }

                    // 微秒级忙等待 (Busy Wait)
                    // 仅在最后 1ms 内自旋，减少抖动
                    while (std::chrono::steady_clock::now() < next_frame)
                    {
                        std::this_thread::yield();
                    }
                }
            }
            else
            {
                // 如果掉帧（任务执行时间超过了间隔），重置基准时间，防止连续追赶导致 CPU 100%
                // 但为了保持平滑，只有当落后超过 1 帧时才重置
                if (now > next_frame + std::chrono::nanoseconds(currentInterval))
                {
                    next_frame = std::chrono::steady_clock::now();
                }
            }
        }
    }

    std::atomic<bool> m_running;
    std::thread m_thread;
    int64_t m_intervalNs = 0;
    std::mutex m_mutex;
    Task m_task;
};
