/*
 * 任务栏输入法指示器探测工具
 * 编译：g++ -O2 -o scripts/test_tray_diag.exe scripts/test_tray_diag.cpp
 * 运行：枚举 Shell_TrayWnd 子窗口，打印所有含 中/英/全/半/CH/EN/Full/Half 文本的窗口。
 *       再打印输入法状态窗口（类名含 IME/Input/Toolbar 等）的文本，帮助确认检测通道。
 */
#include <windows.h>
#include <stdio.h>

static void DumpWindow(HWND hwnd, int depth)
{
    WCHAR cls[64], txt[128];
    GetClassNameW(hwnd, cls, 63);
    int n = GetWindowTextW(hwnd, txt, 127);
    if (n > 0) {
        printf("  [%d] cls=%-28S text=\"%S\" (len=%d)\n", depth, cls, txt, n);
    }
}

static BOOL CALLBACK EnumChild(HWND hwnd, LPARAM lp)
{
    DumpWindow(hwnd, (int)lp);
    return TRUE;
}

int main(void)
{
    printf("=== 任务栏 Shell_TrayWnd 子窗口文本探测 ===\n");
    HWND hShell = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!hShell) {
        printf("未找到 Shell_TrayWnd！\n");
        return 1;
    }
    printf("Shell_TrayWnd = %p\n", (void *)hShell);
    EnumChildWindows(hShell, EnumChild, (LPARAM)0);

    printf("\n=== 含 IME/输入法 关键词的顶层窗口 ===\n");
    struct { WCHAR *kw; int cnt; } kws[] = {
        { (WCHAR *)L"IME", 0 }, { (WCHAR *)L"Input", 0 },
        { (WCHAR *)L"\u8F93\u5165", 0 }, { (WCHAR *)L"\u4E2D", 0 }, { (WCHAR *)L"\u82F1", 0 },
    };
    for (HWND h = FindWindowW(NULL, NULL); h; h = GetNextWindow(h, GW_HWNDNEXT)) {
        WCHAR cls[64], txt[128];
        GetClassNameW(h, cls, 63);
        int n = GetWindowTextW(h, txt, 127);
        if (n <= 0) continue;
        for (int i = 0; i < 5; i++) {
            if (wcsstr(txt, kws[i].kw)) {
                printf("  cls=%-24S text=\"%S\"\n", cls, txt);
                break;
            }
        }
    }
    printf("=== 完成 ===\n");
    return 0;
}