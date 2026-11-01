INCLUDEPATH += $$PWD
# =============================================================
# CUDA Build Configuration for qmake (Windows)
# 适配显卡: RTX 2060 (Compute Capability 7.5)
# =============================================================

# 1. 查找 CUDA 安装路径
# 优先读取系统环境变量 CUDA_PATH
CUDA_DIR = $$(CUDA_PATH)
# 如果环境变量没读到，则使用默认安装路径 (根据你刚才安装的版本 v12.6)
isEmpty(CUDA_DIR): CUDA_DIR = "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6"

message("CUDA Path detected: $$CUDA_DIR")

# 2. 设置架构参数
# RTX 2060 属于 Turing 架构，算力为 7.5，对应 sm_75
CUDA_ARCH = -arch=sm_75

# 3. 添加头文件和库路径
INCLUDEPATH += "$$CUDA_DIR/include"
QMAKE_LIBDIR += "$$CUDA_DIR/lib/x64"

# 4. 链接必要的库
# cudart: CUDA 运行时库 (必须)
# d3d11, dxgi: 因为你要做 DX11 互操作 (必须)
LIBS += -lcudart -ld3d11 -ldxgi

# 5. 定义 NVCC 编译器规则
# 这是让 Qt 能够编译 .cu 文件的关键魔法
cuda.input = CUDA_SOURCES
cuda.output = $$OBJECTS_DIR/${QMAKE_FILE_BASE}.obj
cuda.dependency_type = TYPE_C

# 编译命令详解:
# -c: 编译
# -m64: 64位模式
# -O3: 开启最高优化
# -Xcompiler /MD: 将选项传递给 MSVC 宿主编译器 (匹配 Qt 默认的动态链接运行时)
cuda.commands = "$$CUDA_DIR/bin/nvcc.exe" -c $$CUDA_ARCH -m64 -O3 -Xcompiler /MD $$I ${QMAKE_FILE_NAME} -o ${QMAKE_FILE_OUT}

# 将规则添加到构建系统
QMAKE_EXTRA_COMPILERS += cuda

# =============================================================
# END CUDA Configuration
# =============================================================

HEADERS += \
    $$PWD/CudaAccelerator.h

SOURCES += \
    $$PWD/CudaAccelerator.cpp

DISTFILES += \
    $$PWD/CudaKernels.cu
