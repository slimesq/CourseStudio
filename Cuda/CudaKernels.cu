// CudaKernels.cu
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdio.h>

// ---------------------------------------------------------
// Helper: 错误检查
// ---------------------------------------------------------
#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            printf("CUDA Error: %s at line %d\n", cudaGetErrorString(err), __LINE__); \
        } \
    } while (0)

// ---------------------------------------------------------
// Kernel 1: 简单的图像处理 (例如：增加亮度/美颜预处理)
// 假设输入是 BGRA (DX11 标准格式)
// ---------------------------------------------------------
__global__ void processEffectKernel(uchar4* data, int width, int height, int pitch) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x < width && y < height) {
        // 计算像素在显存中的偏移量 (注意 pitch 是字节数)
        int offset = y * (pitch / 4) + x;

        uchar4 pixel = data[offset];

        // 示例：简单的反色效果 (或者在这里接入你的 OpenCV CUDA 算法)
        // pixel.x = 255 - pixel.x; // B
        // pixel.y = 255 - pixel.y; // G
        // pixel.z = 255 - pixel.z; // R

        // 示例：增加亮度
        pixel.x = min(255, pixel.x + 20);
        pixel.y = min(255, pixel.y + 20);
        pixel.z = min(255, pixel.z + 20);

        data[offset] = pixel;
    }
}

// ---------------------------------------------------------
// Kernel 2: RGB (BGRA) -> NV12 高性能转换
// ---------------------------------------------------------
__global__ void bgraToNv12Kernel(const uchar4* src, uint8_t* dstY, uint8_t* dstUV,
                                 int width, int height, int srcPitch, int dstPitchY, int dstPitchUV) {
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= width || y >= height) return;

    // UV 采样是 2x2，我们只在偶数坐标处理 UV
    // Y 分量每个像素都要

    int srcOffset = y * (srcPitch / 4) + x;
    uchar4 px = src[srcOffset]; // B, G, R, A

    // BT.601 标准系数
    // Y =  0.299R + 0.587G + 0.114B
    // U = -0.169R - 0.331G + 0.500B + 128
    // V =  0.500R - 0.419G - 0.081B + 128

    float R = px.z;
    float G = px.y;
    float B = px.x;

    uint8_t Y = (uint8_t)(0.299f * R + 0.587f * G + 0.114f * B);

    dstY[y * dstPitchY + x] = Y;

    // 处理 UV (每 2x2 像素一组)
    if (x % 2 == 0 && y % 2 == 0) {
        // 简单采样：取当前像素的 UV (更好的做法是 2x2 平均)
        uint8_t U = (uint8_t)(-0.1687f * R - 0.3313f * G + 0.5000f * B + 128.0f);
        uint8_t V = (uint8_t)( 0.5000f * R - 0.4187f * G - 0.0813f * B + 128.0f);

        int uvOffset = (y / 2) * dstPitchUV + (x); // NV12: U, V 交错
        dstUV[uvOffset] = U;
        dstUV[uvOffset + 1] = V;
    }
}

// ---------------------------------------------------------
// Host Wrappers (C++ 可调用的接口)
// ---------------------------------------------------------
extern "C" {

void launchEffectKernel(void* d_surface, int width, int height, int pitch, cudaStream_t stream) {
    dim3 blockSize(32, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                  (height + blockSize.y - 1) / blockSize.y);

    processEffectKernel<<<gridSize, blockSize, 0, stream>>>((uchar4*)d_surface, width, height, pitch);
}

void launchBgraToNv12(void* d_src, void* d_dstY, void* d_dstUV,
                      int width, int height,
                      int srcPitch, int dstPitchY, int dstPitchUV,
                      cudaStream_t stream) {
    dim3 blockSize(32, 16);
    dim3 gridSize((width + blockSize.x - 1) / blockSize.x,
                  (height + blockSize.y - 1) / blockSize.y);

    bgraToNv12Kernel<<<gridSize, blockSize, 0, stream>>>(
        (const uchar4*)d_src, (uint8_t*)d_dstY, (uint8_t*)d_dstUV,
        width, height, srcPitch, dstPitchY, dstPitchUV
    );
}

}