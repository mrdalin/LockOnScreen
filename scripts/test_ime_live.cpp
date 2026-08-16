/* test_ime_live.cpp — 实时输入法状态诊断（ImTip/aardio 方式）
 * 每 300ms 打印当前前台窗口的输入法状态（opened / conv 十六进制 + 位判定）。
 * 用途：验证 WM_IME_CONTROL 通道在真实应用（Word/记事本等）中是否反映
 * 中英/全半角真实状态；观察「搜狗中文 → 微软英文」切换时 conv 如何变化。
 * 构建：g++ -O2 -o test_ime_live.exe test_ime_live.cpp -limm32 -lole32 -lmsctf
 * 运行：先启动本程序（控制台），再切到 Word 操作输入法，回来看输出。按 Ctrl+C 退出。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>
#include <stdio.h>
#include <wchar.h>

/* 查询方式 A：GetForegroundWindow + ImmGetDefaultIMEWnd */
static int QueryFg(BOOL *outOpened, int *outConv)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return -1;
    HWND imeWnd = ImmGetDefaultIMEWnd(fg);
    if (imeWnd) {
        LRESULT opened = SendMessageW(imeWnd, WM_IME_CONTROL, 0x005, 0);
        LRESULT conv   = SendMessageW(imeWnd, WM_IME_CONTROL, 0x001, 0);
        *outOpened = (opened != 0);
        *outConv   = (int)conv;
        return 0;
    }
    return -1;
}

/* 查询方式 B：前台线程的焦点窗口（GetGUIThreadInfo.hwndFocus）+ ImmGetDefaultIMEWnd */
static int QueryFocus(BOOL *outOpened, int *outConv)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return -1;
    DWORD tid = GetWindowThreadProcessId(fg, NULL);
    GUITHREADINFO gti;
    memset(&gti, 0, sizeof(gti));
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(tid, &gti) || !gti.hwndFocus)
        return -1;
    HWND imeWnd = ImmGetDefaultIMEWnd(gti.hwndFocus);
    if (imeWnd) {
        LRESULT opened = SendMessageW(imeWnd, WM_IME_CONTROL, 0x005, 0);
        LRESULT conv   = SendMessageW(imeWnd, WM_IME_CONTROL, 0x001, 0);
        *outOpened = (opened != 0);
        *outConv   = (int)conv;
        return 0;
    }
    return -1;
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);
    /* 同时写日志文件，方便复制粘贴（文件在 exe 同目录 test_ime_live.log） */
    FILE *log = fopen("test_ime_live.log", "w");
    if (!log) log = stderr;
    wprintf(L"实时输入法状态诊断（ImTip 方式）。切到目标窗口操作输入法后回来看输出。\n");
    wprintf(L"A=GetForegroundWindow  B=焦点窗口(GetGUIThreadInfo)  opened  conv(十六进制)\n");
    wprintf(L"ch = opened && !(conv&0x100) && (conv&3);  full = conv&8\n");
    wprintf(L"仅状态变化时输出一行；全部记录已写入 test_ime_live.log\n\n");
    fprintf(log, "LockOnScreen 输入法状态诊断日志\n");
    fprintf(log, "A=GetForegroundWindow  B=焦点窗口(GetGUIThreadInfo)\n");
    fprintf(log, "操作指引: 在目标窗口(如Word)内 1)按Shift切换中英 2)切输入法(搜狗->微软) 3)按Shift+空格切全半角 4)切窗口\n");
    fprintf(log, "格式: 经过毫秒 | 前台类名 标题 | A:opened conv ch full | B:opened conv ch full\n\n");
    fflush(log);

    DWORD t0 = GetTickCount();
    WCHAR lastCls[64] = L"";
    int lastA = -2, lastB = -2;  /* 上次打印的 conv */
    BOOL lastOA = FALSE, lastOB = FALSE;

    for (int i = 0; i < 600; i++) {  /* 最长约 3 分钟 */
        HWND fg = GetForegroundWindow();
        WCHAR cls[64] = L"?", title[128] = L"?";
        if (fg) {
            GetClassNameW(fg, cls, 64);
            GetWindowTextW(fg, title, 128);
        }
        BOOL oA = FALSE, oB = FALSE;
        int cA = -1, cB = -1;
        QueryFg(&oA, &cA);
        QueryFocus(&oB, &cB);

        int ms = (int)(GetTickCount() - t0);
        BOOL fgChanged = (wcscmp(cls, lastCls) != 0);
        BOOL aChanged = (cA != lastA) || (oA != lastOA);
        BOOL bChanged = (cB != lastB) || (oB != lastOB);
        BOOL any = fgChanged || aChanged || bChanged;

        if (any) {
            WCHAR lineA[96], lineB[96];
            if (cA >= 0) {
                BOOL ch  = oA && !(cA & 0x100) && (cA & 3);
                BOOL full = (cA & 8) != 0;
                wsprintfW(lineA, L"op=%d conv=0x%08X %ls%ls", oA ? 1 : 0, (unsigned)cA,
                          ch ? L"中" : L"英", full ? L"·全角" : L"·半角");
            } else wsprintfW(lineA, L"(读不到)");
            if (cB >= 0) {
                BOOL ch  = oB && !(cB & 0x100) && (cB & 3);
                BOOL full = (cB & 8) != 0;
                wsprintfW(lineB, L"op=%d conv=0x%08X %ls%ls", oB ? 1 : 0, (unsigned)cB,
                          ch ? L"中" : L"英", full ? L"·全角" : L"·半角");
            } else wsprintfW(lineB, L"(读不到)");

            if (fgChanged) {
                wprintf(L"== %6dms 前台窗口: %ls | %ls\n", ms, cls, title);
                fprintf(log, "== %6dms 前台窗口: %ls | %ls\n", ms, cls, title);
            }
            wprintf(L"   %6dms A[%ls]  B[%ls]\n", ms, lineA, lineB);
            fprintf(log,   "   %6dms A[%ls]  B[%ls]\n", ms, lineA, lineB);
            fflush(log);
            wcscpy(lastCls, cls);
            lastA = cA; lastOA = oA;
            lastB = cB; lastOB = oB;
        }
        Sleep(250);
    }
    wprintf(L"\n诊断结束，日志已保存到 test_ime_live.log\n");
    fprintf(log, "\n诊断结束\n");
    if (log != stderr) fclose(log);
    return 0;
}
