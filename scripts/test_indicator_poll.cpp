/*
 * IMEModeButton 名称轮询测试（持续 12 秒，用户切换中/英、全/半角观察变化）
 * 编译：g++ -O2 -o scripts/test_indicator_poll.exe scripts/test_indicator_poll.cpp -luiautomationcore -lole32 -loleaut32
 */
#define INITGUID
#include <windows.h>
#include <uiautomationclient.h>
#include <stdio.h>

static BOOL CALLBACK FindIndicator(HWND hwnd, LPARAM lp)
{
    WCHAR cls[64];
    GetClassNameW(hwnd, cls, 63);
    if (wcsstr(cls, L"TrayInputIndicator")) {
        *(HWND *)lp = hwnd;
        return FALSE;
    }
    return TRUE;
}

int main(void)
{
    CoInitialize(NULL);
    printf("=== IMEModeButton 轮询（12 秒，请切换中/英、全/半角） ===\n");
    HWND hShell = FindWindowW(L"Shell_TrayWnd", NULL);
    HWND hInd = NULL;
    EnumChildWindows(hShell, FindIndicator, (LPARAM)&hInd);
    if (!hInd) { printf("未找到指示器\n"); return 1; }

    IUIAutomation *pUia = NULL;
    CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER, IID_IUIAutomation, (void **)&pUia);
    if (!pUia) return 1;

    IUIAutomationElement *pEl = NULL;
    pUia->ElementFromHandle(hInd, &pEl);
    if (!pEl) { printf("ElementFromHandle 失败\n"); return 1; }

    /* 找到 IMEModeButton 子元素 */
    IUIAutomationTreeWalker *pWalker = NULL;
    pUia->get_RawViewWalker(&pWalker);
    IUIAutomationElement *pMode = NULL;
    if (pWalker) {
        IUIAutomationElement *pChild = NULL;
        if (SUCCEEDED(pWalker->GetFirstChildElement(pEl, &pChild)) && pChild) {
            pMode = pChild; /* 第一个子元素就是 IMEModeButton */
        }
    }

    if (!pMode) {
        printf("未找到 IMEModeButton\n");
        /* 兜底：直接读根元素 name */
        pMode = pEl;
    }

    for (int t = 0; t < 12; t++) {
        BSTR name = NULL, legacy = NULL;
        pMode->get_CurrentName(&name);
        /* 也试 LegacyIAccessible */
        IUIAutomationLegacyIAccessiblePattern *pLegacy = NULL;
        if (SUCCEEDED(pMode->GetCurrentPatternAs(UIA_LegacyIAccessiblePatternId,
                                                 IID_IUIAutomationLegacyIAccessiblePattern,
                                                 (void **)&pLegacy))) {
            if (pLegacy) {
                pLegacy->get_CurrentName(&legacy);
                pLegacy->Release();
            }
        }
        printf("[%d] name=\"%S\" legacy=\"%S\"\n", t,
               name ? name : L"(空)", legacy ? legacy : L"(空)");
        if (name) SysFreeString(name);
        if (legacy) SysFreeString(legacy);
        Sleep(1000);
    }

    if (pMode && pMode != pEl) pMode->Release();
    if (pWalker) pWalker->Release();
    if (pEl) pEl->Release();
    if (pUia) pUia->Release();
    printf("=== 完成 ===\n");
    CoUninitialize();
    return 0;
}