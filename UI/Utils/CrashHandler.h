#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <windows.h>
#include <DbgHelp.h>
#include <QString>

class CrashHandler
{
public:
    // 初始化全方位崩溃捕获
    static void init();

private:
    // 统一的崩溃入口，所有钩子最终都调用这个
    static void handleCrash(PEXCEPTION_POINTERS pExceptionInfo, QString const& errorType);

    // 1. Windows SEH 异常回调 (访问违规等)
    static LONG WINAPI unhandledExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo);

    // 2. CRT 无效参数回调
    static void invalidParameterHandler(wchar_t const* expression,
                                        wchar_t const* function,
                                        wchar_t const* file,
                                        unsigned int line,
                                        uintptr_t pReserved);

    // 3. CRT 纯虚函数调用回调
    static void pureVirtualCallHandler();

    // 4. C++ Terminate/Unexpected 回调
    static void terminateHandler();

    // 5. POSIX 信号回调 (SIGSEGV, SIGABRT 等)
    static void signalHandler(int signum);

    // 核心功能：生成 Minidump (.dmp)
    static void createMinidump(PEXCEPTION_POINTERS pExceptionInfo, QString const& path);

    // 核心功能：生成 AI 可读堆栈报告 (.txt)
    static void createStackReport(PEXCEPTION_POINTERS pExceptionInfo, QString const& path);

    // 辅助：获取系统寄存器上下文 (用于非 SEH 崩溃)
    static void getExceptionPointers(DWORD dwExceptionCode, PEXCEPTION_POINTERS pExceptionInfo);
};

#endif  // CRASHHANDLER_H
