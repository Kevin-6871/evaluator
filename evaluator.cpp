// evaluator.cpp - Win32 UI 评测器 (支持调整窗口大小)
#ifndef UNICODE
#define UNICODE
#endif

#include "md.hpp"            // 内含 <windows.h>
#include <commdlg.h>
#include <string>
#include <cstdio>
#include <cstdlib>

// ==================== 全局变量 ====================
static std::wstring g_sourcePath;
static std::wstring g_exeDir;
static std::wstring g_testDir;
static HWND g_hEditResult;
static HWND g_hBtnRun;
static HWND g_hStaticFile;
static HWND g_hBtnSelect;       // 选择文件按钮
static HFONT g_hFont;           // 字体句柄（用于自适应）

// ==================== 前向声明 ====================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
void SelectFile(HWND hwnd);
void PrepareTestDir();
void RunEvaluation(HWND hwnd);
void AppendResult(const wchar_t* text);
void OnSize(HWND hwnd, int width, int height);

// ==================== 获取评测器所在目录 ====================
std::wstring GetExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring full(path);
    return full.substr(0, full.find_last_of(L"\\/") + 1);
}

// ==================== WinMain 入口 ====================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    g_exeDir = GetExeDir();
    g_testDir = g_exeDir + L"test\\";

    // 启动时一次性校准
    myd::OJTimer::getInstance().doCalibrate(2);

    const wchar_t CLASS_NAME[] = L"MB_Evaluator_Window";

    WNDCLASS wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    // 允许最大化、调整大小（去掉之前的限制）
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, L"MB 评测器",
        WS_OVERLAPPEDWINDOW,           // 标准可调窗口
        CW_USEDEFAULT, CW_USEDEFAULT, 650, 480,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

// ==================== 窗口过程 ====================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        // 创建控件（使用固定位置，后续通过 WM_SIZE 调整编辑框）
        g_hBtnSelect = CreateWindow(L"BUTTON", L"选择 .cpp 文件",
                     WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                     20, 20, 160, 30, hwnd, (HMENU)1, NULL, NULL);

        g_hStaticFile = CreateWindow(L"STATIC", L"未选择文件",
                                     WS_VISIBLE | WS_CHILD | SS_LEFT,
                                     190, 25, 430, 20, hwnd, NULL, NULL, NULL);

        g_hBtnRun = CreateWindow(L"BUTTON", L"开始评测",
                                 WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
                                 20, 60, 160, 30, hwnd, (HMENU)2, NULL, NULL);

        g_hEditResult = CreateWindow(L"EDIT", L"",
                                     WS_VISIBLE | WS_CHILD | WS_BORDER |
                                     ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                                     20, 100, 590, 330, hwnd, NULL, NULL, NULL);

        g_hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
        SendMessage(g_hEditResult, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        break;
    }
    case WM_SIZE: {
        int width = LOWORD(lp);
        int height = HIWORD(lp);
        OnSize(hwnd, width, height);
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case 1: SelectFile(hwnd); break;
        case 2: RunEvaluation(hwnd); break;
        }
        break;
    }
    case WM_DESTROY:
        DeleteObject(g_hFont);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}

// ==================== 窗口尺寸变化处理 ====================
void OnSize(HWND hwnd, int width, int height) {
    if (!g_hEditResult) return;
    // 按钮位置不变，只调整编辑框大小
    int editX = 20;
    int editY = 100;
    int editWidth = width - 40;
    int editHeight = height - 120;
    if (editWidth < 200) editWidth = 200;
    if (editHeight < 100) editHeight = 100;
    MoveWindow(g_hEditResult, editX, editY, editWidth, editHeight, TRUE);
}

// ==================== 文件选择对话框 ====================
void SelectFile(HWND hwnd) {
    OPENFILENAMEW ofn = {};
    wchar_t szFile[260] = {};

    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = hwnd;
    ofn.lpstrFile       = szFile;
    ofn.nMaxFile        = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter     = L"C++ 源文件\0*.cpp\0所有文件\0*.*\0";
    ofn.nFilterIndex    = 1;
    ofn.lpstrFileTitle  = NULL;
    ofn.nMaxFileTitle   = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags           = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&ofn)) {
        g_sourcePath = szFile;
        SetWindowText(g_hStaticFile, szFile);
        EnableWindow(g_hBtnRun, TRUE);
        SetWindowText(g_hEditResult, L"");
    }
}

// ==================== 准备测试目录并拷贝文件 ====================
void PrepareTestDir() {
    CreateDirectoryW(g_testDir.c_str(), NULL);

    std::wstring fileName = g_sourcePath.substr(g_sourcePath.find_last_of(L"\\/") + 1);
    std::wstring baseName = fileName.substr(0, fileName.find_last_of(L'.'));
    std::wstring srcDir   = g_sourcePath.substr(0, g_sourcePath.find_last_of(L"\\/") + 1);

    std::wstring cmd;

    cmd = L"copy /Y \"" + g_sourcePath + L"\" \"" + g_testDir + L"source.cpp\" > nul";
    _wsystem(cmd.c_str());

    std::wstring inFile = srcDir + baseName + L".in";
    if (GetFileAttributesW(inFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        cmd = L"copy /Y \"" + inFile + L"\" \"" + g_testDir + L"source.in\" > nul";
        _wsystem(cmd.c_str());
    }

    std::wstring outFile = srcDir + baseName + L".out";
    if (GetFileAttributesW(outFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        cmd = L"copy /Y \"" + outFile + L"\" \"" + g_testDir + L"source.ans\" > nul";
        _wsystem(cmd.c_str());
    }
}

// ==================== 添加结果文本 ====================
void AppendResult(const wchar_t* text) {
    int len = GetWindowTextLength(g_hEditResult);
    SendMessage(g_hEditResult, EM_SETSEL, len, len);
    SendMessage(g_hEditResult, EM_REPLACESEL, FALSE, (LPARAM)text);
}

// ==================== 运行评测 ====================
void RunEvaluation(HWND hwnd) {
    if (g_sourcePath.empty()) return;

    EnableWindow(g_hBtnRun, FALSE);
    SetWindowText(g_hEditResult, L"");
    AppendResult(L"========== 评测开始 ==========\r\n");

    // 1. 准备测试目录
    PrepareTestDir();
    AppendResult(L"文件已拷贝到 test/ 目录\r\n");

    // 2. 编译被评测程序
    std::wstring compileCmd = L"g++ -std=c++17 -O2 -Wall \"" +
                              g_testDir + L"source.cpp\" -o \"" +
                              g_testDir + L"source.exe\" 2>&1";
    AppendResult(L"编译命令: ");
    AppendResult(compileCmd.c_str());
    AppendResult(L"\r\n");

    FILE* pipe = _wpopen(compileCmd.c_str(), L"r");
    if (!pipe) {
        AppendResult(L"编译失败：无法执行编译器\r\n");
        EnableWindow(g_hBtnRun, TRUE);
        return;
    }

    char buffer[256];
    bool compileOK = true;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        AppendResult((std::wstring(buffer, buffer + strlen(buffer)) + L"\r\n").c_str());
        compileOK = false;
    }
    _pclose(pipe);

    if (!compileOK) {
        AppendResult(L"编译错误 (CE)\r\n");
        EnableWindow(g_hBtnRun, TRUE);
        return;
    }
    AppendResult(L"编译成功\r\n\r\n");

    // 3. 显示速度因子
    double speedFactor = myd::OJTimer::getInstance().getSpeedFactor();
    wchar_t bufCal[128];
    swprintf(bufCal, 128, L"速度因子: %.3f\r\n", speedFactor);
    AppendResult(bufCal);

    // 4. 生成包装程序
    std::string wrapperCode = R"=====(#include <windows.h>
#include <cstdio>
#include <psapi.h>

int main() {
    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char cmd[] = "source.exe";

    if (!CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        FILE* f = fopen("time_result.txt", "w");
        if (f) { fprintf(f, "0.000 0"); fclose(f); }
        return 1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    FILETIME creation, exit, kernel, user;
    GetProcessTimes(pi.hProcess, &creation, &exit, &kernel, &user);

    ULARGE_INTEGER ul;
    ul.LowPart = user.dwLowDateTime;
    ul.HighPart = user.dwHighDateTime;
    double cpuTimeMs = ul.QuadPart / 10000.0;

    PROCESS_MEMORY_COUNTERS_EX pmc;
    SIZE_T peakPrivateBytes = 0;
    if (GetProcessMemoryInfo(pi.hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        peakPrivateBytes = pmc.PrivateUsage;
    }

    FILE* f = fopen("time_result.txt", "w");
    if (f) {
        fprintf(f, "%.3f %llu", cpuTimeMs, (unsigned long long)peakPrivateBytes);
        fclose(f);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
}
)=====";

    {
        FILE* f = _wfopen((g_testDir + L"wrapper.cpp").c_str(), L"w");
        if (f) { fputs(wrapperCode.c_str(), f); fclose(f); }
    }

    // 5. 编译包装程序
    std::wstring compileWrapper = L"g++ -std=c++17 -O2 -Wall \"" +
                                  g_testDir + L"wrapper.cpp\" -o \"" +
                                  g_testDir + L"wrapper.exe\" -lpsapi 2>&1";
    AppendResult(L"编译包装程序...\r\n");
    FILE* pipe2 = _wpopen(compileWrapper.c_str(), L"r");
    if (pipe2) {
        char buf[256]; bool ok = true;
        while (fgets(buf, sizeof(buf), pipe2)) {
            AppendResult((std::wstring(buf, buf + strlen(buf)) + L"\r\n").c_str());
            ok = false;
        }
        _pclose(pipe2);
        if (!ok) { AppendResult(L"包装程序编译错误\r\n"); EnableWindow(g_hBtnRun, TRUE); return; }
    }
    AppendResult(L"包装程序编译成功\r\n\r\n");

    // 6. 清理旧输出
    DeleteFileW((g_testDir + L"source.out").c_str());
    DeleteFileW((g_testDir + L"time_result.txt").c_str());

    // 7. 运行包装程序
    std::wstring runCmd = L"cd /D \"" + g_testDir + L"\" && wrapper.exe 2>&1";
    int exitCode = _wsystem(runCmd.c_str());

    // 8. 读取结果
    double localTime = 0.0;
    size_t peakMem = 0;
    {
        FILE* f = _wfopen((g_testDir + L"time_result.txt").c_str(), L"r");
        if (f) {
            unsigned long long memVal = 0;
            fscanf(f, "%lf %llu", &localTime, &memVal);
            peakMem = (size_t)memVal;
            fclose(f);
        }
    }

    double ojTime = myd::OJTimer::getInstance().toOJTime(localTime);

    wchar_t buf[256];
    swprintf(buf, 256, L"退出代码: %d\r\n", exitCode); AppendResult(buf);
    swprintf(buf, 256, L"用户态CPU时间: %.3f ms\r\n", localTime); AppendResult(buf);
    swprintf(buf, 256, L"标准OJ环境预估用时: %.3f ms\r\n", ojTime); AppendResult(buf);
    swprintf(buf, 256, L"峰值专用内存 (Private Bytes): %.2f MB\r\n", peakMem / (1024.0 * 1024.0)); AppendResult(buf);

    // 9. 比较输出
    AppendResult(L"\r\n========== 结果比对 ==========\r\n");
    std::wstring actualOut = g_testDir + L"source.out";
    std::wstring expectedOut = g_testDir + L"source.ans";

    if (GetFileAttributesW(actualOut.c_str()) == INVALID_FILE_ATTRIBUTES) {
        AppendResult(L"警告：程序未生成输出文件\r\n");
    } else {
        std::wstring cmpCmd = L"fc /W \"" + actualOut + L"\" \"" + expectedOut + L"\" > nul 2>&1";
        int cmpResult = _wsystem(cmpCmd.c_str());
        if (cmpResult == 0) AppendResult(L"答案正确 (AC)\r\n");
        else AppendResult(L"答案错误 (WA) - 查看 test/source.out 和 test/source.ans\r\n");
    }

    AppendResult(L"========== 评测结束 ==========\r\n");
    EnableWindow(g_hBtnRun, TRUE);
}