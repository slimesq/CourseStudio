#include "CrashHandler.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <signal.h>
#include <new.h>
#include <eh.h>

// 链接必要的 Windows 库
#pragma comment(lib, "DbgHelp.lib")
#pragma comment(lib, "User32.lib")

// 静态防重入标志：防止崩溃处理代码本身发生崩溃导致死循环
static bool volatile g_IsInCrashHandler = false;

void CrashHandler::init()
{
    // 1. Windows SEH 异常 (Access Violation, Stack Overflow...)
    SetUnhandledExceptionFilter(unhandledExceptionHandler);

    // 2. CRT 无效参数 (Invalid Parameter)
    _set_invalid_parameter_handler(invalidParameterHandler);

    // 3. 纯虚函数调用 (Pure Virtual Call)
    _set_purecall_handler(pureVirtualCallHandler);

    // 4. C++ 异常 (std::terminate)
    std::set_terminate(terminateHandler);

    // 5. 信号处理 (SIGSEGV, SIGABRT, SIGFPE...)
    signal(SIGABRT, signalHandler);
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGILL, signalHandler);

    qDebug() << "CrashHandler: Enterprise-grade hooks installed.";
}

// -------------------------------------------------------------------------
// 各类回调钩子实现
// -------------------------------------------------------------------------

LONG WINAPI CrashHandler::unhandledExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    handleCrash(pExceptionInfo, "Unhandled Exception (SEH)");
    return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::invalidParameterHandler(wchar_t const* expression,
                                           wchar_t const* function,
                                           wchar_t const* file,
                                           unsigned int line,
                                           uintptr_t pReserved)
{
    // 构造一个假的异常信息来生成 dump
    EXCEPTION_RECORD exceptionRecord;
    CONTEXT contextRecord;
    EXCEPTION_POINTERS exceptionInfo = {&exceptionRecord, &contextRecord};

    // 0xC000000D 是 STATUS_INVALID_PARAMETER
    getExceptionPointers(0xC000000D, &exceptionInfo);

    handleCrash(&exceptionInfo, "CRT Invalid Parameter");
}

void CrashHandler::pureVirtualCallHandler()
{
    EXCEPTION_RECORD exceptionRecord;
    CONTEXT contextRecord;
    EXCEPTION_POINTERS exceptionInfo = {&exceptionRecord, &contextRecord};

    // 自定义错误码
    getExceptionPointers(0xE06D7363, &exceptionInfo);

    handleCrash(&exceptionInfo, "Pure Virtual Function Call");
}

void CrashHandler::terminateHandler()
{
    EXCEPTION_RECORD exceptionRecord;
    CONTEXT contextRecord;
    EXCEPTION_POINTERS exceptionInfo = {&exceptionRecord, &contextRecord};

    getExceptionPointers(0xE06D7363, &exceptionInfo);  // C++ Exception Magic Number

    handleCrash(&exceptionInfo, "C++ std::terminate()");
}

void CrashHandler::signalHandler(int signum)
{
    EXCEPTION_RECORD exceptionRecord;
    CONTEXT contextRecord;
    EXCEPTION_POINTERS exceptionInfo = {&exceptionRecord, &contextRecord};

    getExceptionPointers(0xE0000000 + signum, &exceptionInfo);

    QString sigName = QString("Signal %1").arg(signum);
    handleCrash(&exceptionInfo, sigName);
}

// -------------------------------------------------------------------------
// 核心崩溃处理逻辑
// -------------------------------------------------------------------------

void CrashHandler::handleCrash(PEXCEPTION_POINTERS pExceptionInfo, QString const& errorType)
{
    // 防止递归：如果正在处理崩溃时又发生了崩溃，立即终止，防止栈溢出或死锁
    if (g_IsInCrashHandler)
    {
        TerminateProcess(GetCurrentProcess(), 1);
        return;
    }
    g_IsInCrashHandler = true;

    // 1. 准备目录 (使用 try-catch 防止 QDir 导致二次崩溃)
    QString dumpDir;
    QString dumpPath;
    QString reportPath;

    try
    {
        // 尝试获取 AppData 路径，如果失败则回退到当前 exe 目录
        dumpDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/CrashDumps";
        QDir dir(dumpDir);
        if (!dir.exists())
        {
            if (!dir.mkpath("."))
            {
                dumpDir = QCoreApplication::applicationDirPath() + "/CrashDumps";
                QDir().mkpath(dumpDir);
            }
        }

        QString timeStr = QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss");
        dumpPath = QString("%1/crash_%2.dmp").arg(dumpDir, timeStr);
        reportPath = QString("%1/crash_report_%2.txt").arg(dumpDir, timeStr);
    }
    catch (...)
    {
        // 如果 Qt 路径操作失败，回退到最基础的 C 风格路径 (保底措施)
        dumpPath = "crash_emergency.dmp";
        reportPath = "crash_report_emergency.txt";
    }

    // 2. 生成 Minidump (这是最重要的，只要有这个，就能在 VS 中还原现场)
    createMinidump(pExceptionInfo, dumpPath);

    // 3. 生成文本堆栈报告 (辅助分析)
    createStackReport(pExceptionInfo, reportPath);

    // 4. 暴力弹窗提示 (使用原生 WinAPI，不依赖 Qt GUI 线程)
    QString msg = QString("Fatal Error: %1\n\nDump saved to:\n%2").arg(errorType, dumpPath);
    MessageBoxW(NULL,
                (LPCWSTR)msg.utf16(),
                L"CourseStudio Crash Handler",
                MB_OK | MB_ICONERROR | MB_TOPMOST);

    // 5. 彻底杀死进程
    TerminateProcess(GetCurrentProcess(), 1);
}

void CrashHandler::createMinidump(PEXCEPTION_POINTERS pExceptionInfo, QString const& path)
{
    HANDLE hFile = CreateFileW(
        (LPCWSTR)path.utf16(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION mdei;
        mdei.ThreadId = GetCurrentThreadId();
        mdei.ExceptionPointers = pExceptionInfo;
        mdei.ClientPointers = FALSE;

        // MiniDumpWithFullMemory 体积很大但包含所有堆内存
        // MiniDumpNormal 体积小，包含栈和寄存器 (通常够用)
        // 这里为了完美分析，我们开启间接内存捕获
        MINIDUMP_TYPE dumpType =
            (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithIndirectlyReferencedMemory |
                            MiniDumpWithDataSegs);

        MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), hFile, dumpType, &mdei, NULL, NULL);
        CloseHandle(hFile);
    }
}

void CrashHandler::createStackReport(PEXCEPTION_POINTERS pExceptionInfo, QString const& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream out(&file);

    out << "=== CRASH REPORT ===\n";
    out << "Time: " << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << "\n";
    if (pExceptionInfo && pExceptionInfo->ExceptionRecord)
    {
        out << "Exception Code: "
            << QString::number(pExceptionInfo->ExceptionRecord->ExceptionCode, 16) << "\n";
        out << "Memory Address: "
            << QString::number((quintptr)pExceptionInfo->ExceptionRecord->ExceptionAddress, 16)
            << "\n\n";
    }
    else
    {
        out << "Exception Info: Not available (Signal or Terminate)\n\n";
    }

    out << "=== STACK TRACE ===\n";

    HANDLE hProcess = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();

    // 初始化符号处理器
    // TRUE 表示入侵式初始化，自动加载已加载模块的符号
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);

    // 关键修复：确保符号搜索路径包含当前 exe 所在目录
    QString exeDir = QCoreApplication::applicationDirPath();
    std::string searchPath = exeDir.toStdString();

    if (!SymInitialize(hProcess, searchPath.c_str(), TRUE))
    {
        out << "SymInitialize failed. Error: " << GetLastError() << "\n";
        // 尝试重新初始化（有时系统已经初始化过了）
        SymCleanup(hProcess);
        SymInitialize(hProcess, searchPath.c_str(), TRUE);
    }

    // 初始化栈帧
    STACKFRAME64 stackFrame;
    memset(&stackFrame, 0, sizeof(stackFrame));

    // 获取上下文
    CONTEXT context;
    if (pExceptionInfo && pExceptionInfo->ContextRecord)
    {
        context = *pExceptionInfo->ContextRecord;
    }
    else
    {
        // 如果没有上下文（例如在 signal handler 中），我们需要手动捕获当前上下文
        RtlCaptureContext(&context);
    }

    stackFrame.AddrPC.Offset = context.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 50; i++)
    {  // 打印 50 层
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64,
                         hProcess,
                         hThread,
                         &stackFrame,
                         &context,
                         NULL,
                         SymFunctionTableAccess64,
                         SymGetModuleBase64,
                         NULL))
        {
            break;
        }

        if (stackFrame.AddrPC.Offset == 0)
            break;

        // 获取函数名
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;

        QString lineInfo =
            QString("[%1] Address: 0x%2").arg(i).arg(QString::number(stackFrame.AddrPC.Offset, 16));

        if (SymFromAddr(hProcess, stackFrame.AddrPC.Offset, &displacement, pSymbol))
        {
            lineInfo += QString(" | %1").arg(pSymbol->Name);

            // 获取文件名和行号
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD displacementLine = 0;
            if (SymGetLineFromAddr64(hProcess, stackFrame.AddrPC.Offset, &displacementLine, &line))
            {
                lineInfo += QString(" | %1:%2").arg(line.FileName).arg(line.LineNumber);
            }
        }
        else
        {
            lineInfo += " | (Symbol not found - Make sure .pdb is next to .exe)";
        }

        out << lineInfo << "\n";
    }

    SymCleanup(hProcess);
    file.close();
}

void CrashHandler::getExceptionPointers(DWORD dwExceptionCode, PEXCEPTION_POINTERS pExceptionInfo)
{
    // 这是一个辅助函数，用于在没有 Context 的情况下（如 abort/signal）手动抓取 Context
    // 从而让 StackWalk64 能够工作
    if (!pExceptionInfo)
        return;

    // 分配静态存储，避免在崩溃时的堆分配
    static EXCEPTION_RECORD record;
    static CONTEXT context;

    memset(&record, 0, sizeof(record));
    memset(&context, 0, sizeof(context));

    record.ExceptionCode = dwExceptionCode;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;

    RtlCaptureContext(&context);

    pExceptionInfo->ExceptionRecord = &record;
    pExceptionInfo->ContextRecord = &context;
}
