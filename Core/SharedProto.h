#ifndef SHAREDPROTO_H
#define SHAREDPROTO_H
#pragma once
#include <windows.h>

// [配置 1] 虚拟摄像头的唯一标识符 (GUID)
// 使用 VS 工具 -> 创建 GUID -> 复制 Registry Format 格式
// 例如: {291CDA2F-0893-4F5B-9B55-B94699C46E44}
static const GUID CLSID_MyVirtualCam = {
    0x291CDA2F, 0x0893, 0x4f5b, {0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44}};

// [配置 2] 共享内存名称
// 建议：开发阶段使用 "Local\\" 前缀，避免管理员权限问题
// 生产环境如果需要服务隔离，使用 "Global\\" 但需设置 DACL
#define VCAM_SHARED_MAP_NAME L"Local\\MFCam_CourseStudio_01"

// [配置 3] 视频参数 (必须两边一致，否则画面花屏)
#define VCAM_WIDTH 1280
#define VCAM_HEIGHT 720
#define VCAM_FPS 30
#endif  // SHAREDPROTO_H
