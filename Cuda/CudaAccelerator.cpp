// CudaAccelerator.cpp
#include "CudaAccelerator.h"
#include "Logger.h"

CudaAccelerator& CudaAccelerator::instance()
{
    static CudaAccelerator s;
    return s;
}

CudaAccelerator::~CudaAccelerator()
{
    if (m_stream)
        cudaStreamDestroy(m_stream);
}

bool CudaAccelerator::init(ID3D11Device* device)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_isInit)
        return true;

    // 关键：注册 DX11 设备给 CUDA
    cudaError_t err = cudaD3D11SetDirect3DDevice(device);
    if (err != cudaSuccess)
    {
        LOG_ERROR("CUDA: Failed to set D3D11 device: " << cudaGetErrorString(err));
        return false;
    }

    cudaStreamCreate(&m_stream);
    m_isInit = true;
    LOG_INFO("CUDA: Initialized with D3D11 Interop.");
    return true;
}

bool CudaAccelerator::processTextureInPlace(ID3D11Texture2D* texture)
{
    if (!texture || !m_isInit)
        return false;
    std::lock_guard<std::mutex> lock(m_mutex);

    // 1. 注册资源 (通常建议缓存注册过的资源，这里为了简化演示每次注册)
    // 生产环境中，应该维护一个 map<ID3D11Texture2D*, cudaGraphicsResource_t> 的缓存
    cudaGraphicsResource_t cudaResource;
    cudaError_t err =
        cudaGraphicsD3D11RegisterResource(&cudaResource, texture, cudaGraphicsRegisterFlagsNone);
    if (err != cudaSuccess)
        return false;

    // 2. 映射资源 (Lock)
    err = cudaGraphicsMapResources(1, &cudaResource, m_stream);
    if (err != cudaSuccess)
    {
        cudaGraphicsUnregisterResource(cudaResource);
        return false;
    }

    // 3. 获取 CUDA 数组指针
    cudaArray_t cuArray;
    err = cudaGraphicsSubResourceGetMappedArray(&cuArray, cudaResource, 0, 0);

    // 由于我们是在修改纹理，通常使用 Surface Object 或直接指针
    // 注意：cudaGraphicsSubResourceGetMappedArray 拿到的是 Array，
    // 若要直接读写像素，建议在 CreateTexture2D 时不要用 Array，或者这里用 Surface Memory
    // 简便起见，这里假设 texture 是 D3D11_USAGE_DEFAULT，我们可以映射为 DevicePointer
    // (注意：这需要 RegisterFlagsSurfaceLoadStore，且 DX11 纹理需要特定 Flag)

    // **修正方案**：最通用的做法是映射为 cudaArray，然后绑定 SurfaceObject
    cudaResourceDesc resDesc = {};
    resDesc.resType = cudaResourceTypeArray;
    resDesc.res.array.array = cuArray;

    cudaSurfaceObject_t surface;
    cudaCreateSurfaceObject(&surface, &resDesc);

    // 调用 Kernel (这里为了演示方便，假设你修改 Kernel 接受 SurfaceObject)
    // launchEffectKernelSurface(surface, ...);

    // 4. 解除映射
    cudaDestroySurfaceObject(surface);
    cudaGraphicsUnmapResources(1, &cudaResource, m_stream);
    cudaGraphicsUnregisterResource(cudaResource);

    return true;
}
