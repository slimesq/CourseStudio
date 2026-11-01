#ifndef IVIRTUALCAMPUBLISHER_H
#define IVIRTUALCAMPUBLISHER_H
#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 视频帧元数据
struct MFCamFrameHeader
{
    uint32_t width;
    uint32_t height;
    uint32_t pixelFormat;  // 如 DXGI_FORMAT_NV12
    uint64_t timestamp;    // QPC 高精度时间戳
    uint32_t frameIndex;   // 帧序号，用于丢帧检测
    uint32_t dataSize;     // 像素数据大小
};

// 虚拟摄像头抽象接口
class IVirtualCamPublisher
{
public:
    virtual ~IVirtualCamPublisher() = default;

    // 初始化：设置设备名称、分辨率和格式
    virtual bool initialize(std::wstring const& deviceName, int w, int h) = 0;

    // 推送数据：由 SceneCompositor 调用
    // data 应当是 NV12 格式的二进制数据
    virtual bool pushFrame(uint8_t const* data, uint32_t size, uint64_t timestamp) = 0;

    // 停止发布
    virtual void stop() = 0;
};
#endif  // IVIRTUALCAMPUBLISHER_H
