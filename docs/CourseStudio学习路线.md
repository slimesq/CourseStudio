# CourseStudio 系统学习路线

> 目标：从“能运行和修改界面”走到能独立解释项目的控制流、数据流、线程与资源生命周期，并能在验证后安全修改一条完整业务链。
>
> 组织方式参考 [IOCP 项目的学习路线](https://github.com/slimesq/IOCP/blob/main/docs/IOCP学习路线.md)：每阶段都有目标、源码入口、动手验证和完成标准。以下内容根据 CourseStudio 当前源码编写，后续代码变化时应重新核对。

## 1. 最终要掌握什么

完成后，应能不看提示讲清楚：

1. 程序如何从 `main()` 进入登录、工作台、场景编辑和直播页面；Qt 信号与槽在哪里连接。
2. 一个场景、一个图层、一个资源分别由谁创建、持有、保存、加载和销毁；本地数据与云端数据如何交汇。
3. 摄像头、窗口/屏幕、图片和视频如何成为可供 D3D11 合成的纹理。
4. 一帧画面如何从资源经过 `SceneCompositor` 到达预览，再以 YUV/NV12 数据进入录制器。
5. 麦克风和系统声音如何采集、混合、编码，与视频时间戳如何对齐。
6. 点击开播、录制、下播时，UI、配置、HTTP、FFmpeg 和上传分别执行哪些步骤；失败时如何回退。
7. 各条线程、Qt 对象、`shared_ptr`、COM 对象和 FFmpeg 对象的所有权及停止顺序。
8. 在修改一项功能前，能指出受影响的文件、验证路径和可能的回归风险。

“理解”以**能解释、能用断点证明、能完成小修改并验证**为准，不以读完全部文件为准。

## 2. 项目地图与学习边界

| 位置 | 当前职责 | 建议何时读 |
| --- | --- | --- |
| [`main.cpp`](../main.cpp)、[`UI/Pattern/`](../UI/Pattern/) | 应用入口、工作台与页面路由 | 阶段一、二 |
| [`UI/Login/`](../UI/Login/)、[`Net/Http/`](../Net/Http/) | 登录、用户状态、HTTP 请求、资源与直播接口 | 阶段二、九 |
| [`Model/`](../Model/)、[`UI/Screen/`](../UI/Screen/) | 场景、资源、图层与编辑界面 | 阶段三、五 |
| [`Core/`](../Core/) | 采集、D3D11 纹理、合成、音频及虚拟摄像头接口 | 阶段四、五、七 |
| [`VFX/`](../VFX/) | 摄像头特效、人脸检测与效果配置 | 阶段六 |
| [`Record/`](../Record/)、[`UI/Live/`](../UI/Live/) | 编码、封装、推送、直播控制与辅助数据 | 阶段八、九 |
| [`Cuda/`](../Cuda/) | CUDA/D3D11 实验代码；有独立索引目标，未并入主程序 | 主链完成后 |
| [`CMakeLists.txt`](../CMakeLists.txt)、[`CMakePresets.json`](../CMakePresets.json) | 构建入口和公共预设 | 阶段零 |

先记住两条主线，再逐层展开：

```text
控制流：
main → LoginWidget → WorkSpaceWindow → WorkSpaceBusinessController
     → ScreenWindow（场景编辑 / 直播）→ SceneViewController / LiveController

画面流：
SceneManager / Scene / SceneItem → ISource / IVideoSource → 采集或文件源
     → SceneCompositor → frameReady → CanvasView（预览）
                       └→ yuvFrameReady → LiveController
                                          → RecordController → FFmpegRecorder
                                          → 文件或推流目标
```

这张图是阅读索引，不代表每个模式都会执行全部分支。例如编辑模式侧重预览；直播模式还会处理编码和网络。每条箭头都应在源码和调试器里再验证。

## 3. 学习方法与记录格式

每个阶段使用同一个闭环：**读接口 → 画调用链 → 跑一个最小场景 → 下断点 → 故意触发一个失败分支 → 用自己的话复述**。先理解正常路径，再看异常与关闭路径。

每次追踪函数，至少记录：

```text
用户动作 / 触发事件：
入口函数与文件：
调用者与下一步：
执行线程：
输入和输出的数据类型：
谁创建对象、谁负责释放：
是否跨线程或异步：
成功条件：
失败与取消路径：
我实际看到的断点/日志证据：
仍不确定的问题：
```

每阶段保留三项产物：一张调用时序图、一张对象/资源所有权图、一个可复现的验证记录。没有真实后端或硬件时，明确标注“只完成静态追踪”，不要把推测写成已验证事实。

## 4. 阶段零：建立可运行基线（3～5 小时）

**目标：**能说明哪些文件进入主程序，能配置 Debug 并编译，知道运行还依赖哪些外部环境。

**阅读顺序：**[`CMakeLists.txt`](../CMakeLists.txt) → [`CMakePresets.json`](../CMakePresets.json) → [`.vscode/settings.json`](../.vscode/settings.json) → [`main.cpp`](../main.cpp)。

**操作：**

1. 在 Windows + MSVC 开发环境中，用 VS Code 的 CMake Tools 选择 `local-msvc-debug`，配置并生成 `CourseStudio`。当前机器的 `local-msvc-debug` 位于未纳入 Git 的 `CMakeUserPresets.json`；新克隆项目时需自行指定 `CMAKE_PREFIX_PATH`（Qt 6）、`OpenCV_DIR` 和 `FFMPEG_ROOT`，或创建对应的本地预设。
2. 记录 `build/ninja-debug`、`compile_commands.json`、可执行文件的实际位置，以及运行时 Qt、OpenCV、FFmpeg DLL 的来源。
3. 看清主目标包含根目录和 `Core/Model/Net/Record/UI/VFX`，并排除了部分演示文件；`CourseStudioCudaIndex` 只用于可选 CUDA 源码索引。
4. 记录配置、编译、启动各自的结果。当前 CMake 没有注册 CTest；后续各阶段主要靠断点、受控手工场景和必要的小型验证。

**完成标准：**能解释公共预设与本地依赖预设的区别，并能在干净构建目录复现 Debug 构建。编译成功不等于登录、后端、摄像头和推流都已可用。

## 5. 阶段一：补齐阅读所需基础（6～9 小时）

只补项目马上会用到的知识：

- C++20：RAII、`unique_ptr`/`shared_ptr`/`weak_ptr`、移动语义、lambda 捕获、`mutex` 与 `atomic`。
- Qt：`QObject` 父子关系、信号与槽、`Qt::QueuedConnection`、事件循环、`QThread`、`deleteLater()`。
- Windows 媒体：COM/`ComPtr`、D3D11 device/context/texture/SRV、WGC 与 WASAPI 的角色。
- 音视频：BGRA、YUV/NV12、采样率、帧率、PTS、编码器、封装器与推流地址。

**对照源码：**[`main.cpp`](../main.cpp) 的 `deleteLater()`；[`UI/Screen/Scene.h`](../UI/Screen/Scene.h) 的共享所有权；[`Core/SceneCompositor.h`](../Core/SceneCompositor.h) 的锁和原子状态；[`Record/FFmpegRecorder.h`](../Record/FFmpegRecorder.h) 的线程与 FFmpeg 对象。

**完成标准：**能说出这些类型保护什么生命周期，并解释 Qt UI 对象为何不能任意从工作线程直接修改。

## 6. 阶段二：启动、登录与页面路由（6～9 小时）

**阅读顺序：**[`main.cpp`](../main.cpp) → [`UI/Login/LoginWidget.cpp`](../UI/Login/LoginWidget.cpp) → [`UI/Login/UserMgr.cpp`](../UI/Login/UserMgr.cpp) / [`Net/Http/AuthService.cpp`](../Net/Http/AuthService.cpp) → [`UI/Pattern/WorkSpaceWindow.cpp`](../UI/Pattern/WorkSpaceWindow.cpp) → [`UI/Pattern/WorkSpaceBusinessController.cpp`](../UI/Pattern/WorkSpaceBusinessController.cpp)。

**跟踪两个动作：**一次登录成功；点击“场景设置”或“直播课程”。找出 `loginCompleted`、`handleModeSelection()`、`requestViewSwitch`、`handleViewSwitchRequest()` 和 `handleLiveRoomEntry()` 的触发顺序。区分本地用户数据与 HTTP 登录路径，记录实际采用哪条。

**练习：**画出 `WorkSpaceWindow`、业务控制器、`ScreenWindow` 和课程选择页的创建/持有关系；在调试器中观察切页时旧页面的 `suspendRendering()` 与新页面的 `resumeRendering()`。

**完成标准：**不靠猜测，能说明一个按钮点击后为何进入对应页面，以及登录或请求失败时用户看到什么。

## 7. 阶段三：场景、图层、资源与持久化（10～14 小时）

**阅读顺序：**[`Model/Types.h`](../Model/Types.h) → [`Model/ISource.h`](../Model/ISource.h) → [`UI/Screen/Scene.h`](../UI/Screen/Scene.h) → [`UI/Screen/SceneItem.h`](../UI/Screen/SceneItem.h) → [`Model/SceneManager.cpp`](../Model/SceneManager.cpp) → [`UI/Utils/SceneRepository.cpp`](../UI/Utils/SceneRepository.cpp) → [`UI/Screen/SceneViewController.cpp`](../UI/Screen/SceneViewController.cpp)。

**追踪：**创建场景 → 添加一个图片资源 → 调整图层 → 保存 → 重新加载。记下 UUID、云端 ID、`SceneItem` 的变换数据和 `Scene::toJson()` 在哪一层产生；比较本地文件保存与 `ResourceService` 云端同步。

**练习：**手工画出 `Scene → SceneItem → ISource` 关系；用一个只含图片的场景检查序列化前后的字段。再跟踪 `removeSource()` 到资源回收信号。

**完成标准：**能说明场景所有权、图层顺序、资源替换和本地/云端合并的边界；能解释为什么不能在渲染仍使用纹理时立即释放源对象。

## 8. 阶段四：视频源与采集（10～14 小时）

**阅读顺序：**[`Core/IVideoSource.h`](../Core/IVideoSource.h) → [`Model/SourceFactory.h`](../Model/SourceFactory.h) → [`Model/SourceProxies.h`](../Model/SourceProxies.h) → [`Core/ImageFileSource.cpp`](../Core/ImageFileSource.cpp) / [`Core/VideoFileSource.cpp`](../Core/VideoFileSource.cpp) → [`Core/CameraCapture.cpp`](../Core/CameraCapture.cpp) / [`Core/WGCCapture.cpp`](../Core/WGCCapture.cpp) → [`Core/DX11Context.cpp`](../Core/DX11Context.cpp)。

先用图片源建立无硬件路径，再看视频文件、摄像头、窗口/显示器采集。逐一确认每种源何时 `start()`/`stop()`，怎样向合成器提供纹理，设备丢失或窗口消失时会怎样。

**练习：**为四类源各画一张“数据进入 → 纹理产生 → 纹理使用 → 停止释放”小图；用调试器记录至少一种源的启动与停止。

**完成标准：**能区分 CPU 图像、GPU 纹理和源元数据，指出 D3D11 device/context 的所有者。

## 9. 阶段五：编辑器、D3D11 合成与预览（12～18 小时）

**阅读顺序：**[`UI/Screen/ScreenWindow.cpp`](../UI/Screen/ScreenWindow.cpp) → [`UI/Screen/CanvasView.cpp`](../UI/Screen/CanvasView.cpp) → [`UI/Screen/VisualItemProxy.cpp`](../UI/Screen/VisualItemProxy.cpp) → [`Core/SceneCompositor.cpp`](../Core/SceneCompositor.cpp) → [`Core/TexturePool.cpp`](../Core/TexturePool.cpp) → [`Core/DX11Filters.h`](../Core/DX11Filters.h)。

**主链：**`SceneViewController::sceneSwitched` → `ScreenWindow` 创建/切换 `SceneCompositor` → `renderLoop()` → `frameReady` → `CanvasView::updateBackgroundImage()`。再追踪直播模式的 `yuvFrameReady` 与 `LiveController::handleYuvFrame()`。

**必须回答：**编辑与直播模式的帧率、分辨率和输出有何区别？`Qt::QueuedConnection` 保护哪条跨线程路径？切换场景时为何先停旧合成器？预览 generation 标记防止什么旧帧问题？

**练习：**仅添加一张静态图片，记录一帧的宽高、格式、时间戳和线程；切场景时检查旧合成器和纹理的释放顺序。

**完成标准：**能画出完整的视频帧数据流与资源所有权图，解释预览和编码输出为何走不同分支。

## 10. 阶段六：摄像头特效与模型资源（6～10 小时）

**阅读顺序：**[`Core/CameraCapture.cpp`](../Core/CameraCapture.cpp) → [`VFX/VideoEffectSystem.cpp`](../VFX/VideoEffectSystem.cpp) → [`VFX/ConcreteEffects.cpp`](../VFX/ConcreteEffects.cpp) → [`VFX/FaceTracker.cpp`](../VFX/FaceTracker.cpp) → [`VFX/CameraSettingsDialog.cpp`](../VFX/CameraSettingsDialog.cpp)。

**追踪：**特效管理器何时创建、特效怎样排序和启停、图像在哪个格式上处理、设置如何从 UI 传入。实际模型加载路径指向 `assets/models/face_detection_yunet_2023mar.onnx`；当前源码未直接引用根目录的 [`lbfmodel.yaml`](../lbfmodel.yaml)。把“仓库有这个文件”和“运行时确实使用它”分开验证。

**完成标准：**能说明一个特效开关从界面到处理函数的路径，并能判断模型缺失时哪条功能受影响。

## 11. 阶段七：音频采集与混音（5～8 小时）

**阅读顺序：**[`Core/WasapiCapturer.h`](../Core/WasapiCapturer.h) / [`Core/WasapiCapturer.cpp`](../Core/WasapiCapturer.cpp) → [`UI/Live/LiveController.cpp`](../UI/Live/LiveController.cpp) → [`Record/RecordController.h`](../Record/RecordController.h) → [`Record/FFmpegModules.h`](../Record/FFmpegModules.h) 中的 `AudioDSP` / `AudioEncoder`。

**追踪：**麦克风与系统 loopback 的设备选择、采样格式、`audioDataReady`、音量表、混音和编码。记录 PCM 样本数如何换算为时间，与视频 PTS 如何对齐。

**完成标准：**能解释静音、设备不可用和采样率不一致时该观察哪些状态；能说明下播时两个音频采集线程如何结束。

## 12. 阶段八：录制、编码、封装与推流（12～18 小时）

**阅读顺序：**[`Record/RecorderConfig.h`](../Record/RecorderConfig.h) → [`UI/Utils/AppConfig.cpp`](../UI/Utils/AppConfig.cpp) → [`Record/RecordController.h`](../Record/RecordController.h) → [`Record/FFmpegRecorder.cpp`](../Record/FFmpegRecorder.cpp) → [`Record/FFmpegModules.h`](../Record/FFmpegModules.h)。

先研究**只录制本地文件**的路径：`startRecord()`、`pushNV12Frame()`、`pushAudioData()`、`run()`、`VideoEncoder`/`AudioEncoder`、`FileMuxer`、`stopRecord()`。再加入 `StreamMuxer`、`PacketQueue`、`StreamSender` 和多个推流目标。

**练习：**为一帧视频和一段音频各做一张时间戳表；说明录制与推流共用哪些编码结果；追踪“目标地址失败”“队列堆积”“停止时 flush”三个分支。

**完成标准：**能从按钮动作走到输出文件或推流包，知道编码、封装和发送各自负责什么，并能说明停机时先停止输入还是先释放封装器。

## 13. 阶段九：直播业务、HTTP 与云端同步（10～14 小时）

**阅读顺序：**[`Net/Http/ApiConfig.h`](../Net/Http/ApiConfig.h) → [`Net/Http/HttpManager.cpp`](../Net/Http/HttpManager.cpp) → [`Net/Http/AuthService.cpp`](../Net/Http/AuthService.cpp) → [`Net/Http/ResourceService.cpp`](../Net/Http/ResourceService.cpp) / [`Model/LiveService.cpp`](../Model/LiveService.cpp) → [`UI/Live/LiveSelectionController.cpp`](../UI/Live/LiveSelectionController.cpp) → [`UI/Live/LiveController.cpp`](../UI/Live/LiveController.cpp)。

**主链：**选直播间 → 取得房间详情和场景 ID → `ScreenWindow::startLiveCourse()` → `LiveController::startLiveProcess()` → 配置校验/房间请求 → `realStartLive()` → 状态轮询 → `stopLive()` → 录制文件上传。注意只录制和启用推流时的启动分支不同。

**练习：**为每个 HTTP 请求列出 URL、方法、请求体、响应信号、调用者和失败反馈。使用受控测试账号和测试端点观察，不对真实直播间执行练习性开播、上传或删除。

**完成标准：**能解释网络响应如何回到 UI/控制器、云端推流地址与本地配置如何协调，以及异常状态轮询和下播上传的时序。

## 14. 阶段十：并发、失败与安全停机（8～12 小时）

重新审视 [`Core/HighPrecisionThread.h`](../Core/HighPrecisionThread.h)、[`Core/SceneCompositor.cpp`](../Core/SceneCompositor.cpp)、[`Core/WasapiCapturer.cpp`](../Core/WasapiCapturer.cpp)、[`Record/FFmpegRecorder.cpp`](../Record/FFmpegRecorder.cpp)、[`UI/Live/AIDataBridge.cpp`](../UI/Live/AIDataBridge.cpp) 和 [`UI/Pattern/WorkSpaceWindow.cpp`](../UI/Pattern/WorkSpaceWindow.cpp)。

为 UI 线程、渲染线程、音频线程、录制/发送线程画线程图。逐项验证：切页、切场景、采集设备断开、网络失败、下播和退出程序时，谁先停止生产数据，谁等待工作线程结束，谁最后释放纹理、COM 与 FFmpeg 对象。

**练习：**选择一条不涉及真实直播的失败路径，用日志和断点记录完整回收顺序。顺手检查 `main.cpp` 中 WebEngine 运行参数、默认账号和令牌日志等现有配置是否符合你计划的部署环境；这是独立审查题，不能仅凭注释认定安全。

**完成标准：**能给出至少五个资源的“创建者 → 使用者 → 释放者”表，并说明一种可能的悬空引用、竞态或重复停止风险。

## 15. 阶段十一：外围能力与第一次独立修改（6～10 小时）

最后再看 [`UI/Live/AIDataBridge.cpp`](../UI/Live/AIDataBridge.cpp)、[`Core/SharedMemoryPublisher.h`](../Core/SharedMemoryPublisher.h)、[`Core/IVirtualCamPublisher.h`](../Core/IVirtualCamPublisher.h) 和 [`Cuda/`](../Cuda/)。先确认这些功能在当前主目标中的接入状态；尤其不要把独立 CUDA 索引目标当成应用已经使用 CUDA 的证据。

选择一个小而完整的改动，例如：为场景保存添加一个可见的错误提示，或为本地录制路径增加校验。修改前写出调用链和预期结果，修改后重新构建、复现正常路径与失败路径，并画出修改影响范围。

**完成标准：**能独立说明“为什么改这里、还会影响哪里、如何证明没有破坏主链”。

## 16. 参考节奏

阶段用时合计约 **94～141 小时**。下表把跨周阶段的时间拆分到对应周；每周投入并不均匀，第十周任务较重。若每周固定学习 8～12 小时，可以把未完成的内容顺延，不必赶进度。

| 周次 | 主任务 | 预计投入（本周） | 当周产物 |
| --- | --- | --- | --- |
| 1 | 阶段零、一：构建基线、Qt/C++ 所有权 | 9～14 小时（零 3～5；一 6～9） | 环境记录、对象生命周期速查表 |
| 2 | 阶段二：登录与工作台路由 | 6～9 小时 | 从登录到场景页的时序图 |
| 3 | 阶段三：场景和图层模型 | 5～7 小时（阶段三上半） | Scene/SceneItem/ISource 所有权图 |
| 4 | 阶段三：本地保存和云端同步 | 5～7 小时（阶段三下半） | JSON 字段表、保存/加载断点记录 |
| 5 | 阶段四：图片/视频文件源 | 5～7 小时（阶段四上半） | 无硬件视频源路径图 |
| 6 | 阶段四、五：摄像头/WGC 与编辑器 | 11～16 小时（四 5～7；五 6～9） | 采集到预览的帧路径图 |
| 7 | 阶段五、六：合成、预览和特效 | 12～19 小时（五 6～9；六 6～10） | 编辑/直播模式差异表 |
| 8 | 阶段七：WASAPI 与音频时间线 | 5～8 小时 | 音频线程和样本时间戳图 |
| 9 | 阶段八：本地录制 | 6～9 小时（阶段八上半） | 帧到输出文件的调用链 |
| 10 | 阶段八、九：推流与直播接口 | 16～23 小时（八 6～9；九 10～14） | 请求/响应表、启动与下播时序图 |
| 11 | 阶段十：失败、取消与停机 | 8～12 小时 | 线程图、资源释放顺序表 |
| 12 | 阶段十一：外围能力和小修改 | 6～10 小时 | 变更说明、构建与回归记录 |

## 17. 最终验收清单

- [ ] 不看源码，能画出启动、登录、场景编辑、直播的主要对象关系。
- [ ] 能从 UI 点击追到 `SceneManager`、`Scene`、`SceneItem` 和保存/加载。
- [ ] 能追踪一帧图片从源到 D3D11 合成、预览、NV12、编码、封装的路径。
- [ ] 能追踪一段麦克风 PCM 到混音和音频包的路径。
- [ ] 能解释编辑与直播模式在帧率、分辨率、输出上的区别。
- [ ] 能区分本地录制、推流、直播间状态变更和录制文件上传。
- [ ] 能列出各工作线程和五类核心资源的停止/释放顺序。
- [ ] 能说出登录失败、设备丢失、网络失败、下播时的实际处理路径。
- [ ] 能指出 CUDA 实验代码与主程序的边界，判断模型文件是否被运行时引用。
- [ ] 能在不依赖助手的情况下完成一项小改动，并留下可复现的验证记录。

## 18. 第一轮学习：现在就做

1. 记录当前 Git 提交、CMake 预设和 Debug 构建结果。
2. 只读 `main.cpp`、`LoginWidget`、`WorkSpaceWindow`、`WorkSpaceBusinessController`，画出“登录成功 → 进入场景设置”的时序图。
3. 在 `loginCompleted`、`handleModeSelection()` 和 `handleViewSwitchRequest()` 下断点；若后端不可用，先完成静态调用链并标记未验证部分。
4. 用本文件的记录模板写第一页学习笔记。完成这条最短控制流后，再进入场景与媒体管线。
