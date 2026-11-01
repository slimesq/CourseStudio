// CudaAccelerator.h
#pragma once
#include <d3d11.h>
#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>
#include <memory>
#include <mutex>
#include "Logger.h"

// 声明 CUDA Kernel 包装器
extern "C" void launchEffectKernel(
    void* d_surface, int width, int height, int pitch, cudaStream_t stream);
extern "C" void launchBgraToNv12(void* d_src,
                                 void* d_dstY,
                                 void* d_dstUV,
                                 int w,
                                 int h,
                                 int sp,
                                 int dpY,
                                 int dpUV,
                                 cudaStream_t s);

class CudaAccelerator
{
public:
    static CudaAccelerator& instance();

    // 1. 初始化：绑定 DX11 设备
    bool init(ID3D11Device* device);

    // 2. 核心功能：原地处理 DX11 纹理 (替代 CPU OpenCV)
    // 直接传入 Camera 的 DX11 Texture，无需 CPU 拷贝
    bool processTextureInPlace(ID3D11Texture2D* texture);

    // 3. 核心功能：RGB 转 NV12 (输出到 CUDA 内存，用于快速下载或编码)
    // 这里我们演示将结果写回 DX11 纹理（如果是用于显示）或者写到 CPU 内存
    bool convertToNV12(ID3D11Texture2D* inputTex, ID3D11Texture2D* outputNV12Tex);

private:
    CudaAccelerator() = default;
    ~CudaAccelerator();

    bool m_isInit = false;
    cudaStream_t m_stream = nullptr;
    std::mutex m_mutex;
};
