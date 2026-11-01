#ifndef SHAREDMEMORYPUBLISHER_H
#define SHAREDMEMORYPUBLISHER_H
#include "IVirtualCamPublisher.h"
#include "Logger.h"
class SharedMemoryPublisher : public IVirtualCamPublisher
{
public:
    SharedMemoryPublisher() : m_hMapFile(NULL), m_pBuffer(NULL), m_hEvent(NULL)
    {
    }

    bool initialize(std::wstring const& deviceName, int w, int h) override
    {
        m_width = w;
        m_height = h;
        // 计算 NV12 大小：W*H (Y) + W*H/2 (UV)
        m_bufferSize = sizeof(MFCamFrameHeader) + (w * h * 3 / 2);

        // 1. 创建共享内存
        std::wstring mapName = L"Local\\MFCam_" + deviceName;
        m_hMapFile = CreateFileMapping(
            INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)m_bufferSize, mapName.c_str());
        if (!m_hMapFile)
            return false;

        m_pBuffer = MapViewOfFile(m_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, m_bufferSize);

        // 2. 创建同步事件通知驱动
        std::wstring eventName = mapName + L"_Event";
        m_hEvent = CreateEvent(NULL, FALSE, FALSE, eventName.c_str());

        return m_pBuffer != nullptr;
    }

    bool pushFrame(uint8_t const* data, uint32_t size, uint64_t timestamp) override
    {
        if (!m_pBuffer)
            return false;

        // 写入 Header
        MFCamFrameHeader* header = (MFCamFrameHeader*)m_pBuffer;
        header->width = m_width;
        header->height = m_height;
        header->timestamp = timestamp;
        header->dataSize = size;
        header->frameIndex++;

        // 写入像素数据
        uint8_t* pixelPtr = (uint8_t*)m_pBuffer + sizeof(MFCamFrameHeader);
        memcpy(pixelPtr, data, size);

        if (header->frameIndex % 300 == 0)
        {
            LOG_INFO("Virtual Camera Heartbeat: Pushed 300 frames to Shared Memory.");
        }

        // 通知 Media Foundation 驱动读取
        SetEvent(m_hEvent);
        return true;
    }

    void stop() override
    {
        if (m_pBuffer)
            UnmapViewOfFile(m_pBuffer);
        if (m_hMapFile)
            CloseHandle(m_hMapFile);
        if (m_hEvent)
            CloseHandle(m_hEvent);
    }

private:
    HANDLE m_hMapFile;
    HANDLE m_hEvent;
    void* m_pBuffer;
    uint32_t m_width, m_height, m_bufferSize;
};
#endif  // SHAREDMEMORYPUBLISHER_H
