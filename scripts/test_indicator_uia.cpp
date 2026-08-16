/*
 * TrayInputIndicatorWClass UIA 名称读取测试（微软 Q&A 推荐方案）
 * 编译：g++ -O2 -o scripts/test_indicator_uia.exe scripts/test_indicator_uia.cpp -luiautomationcore -lole32
 * 运行：用 IUIAutomation 读取任务栏输入指示器的可访问名称，观察是否含 中/英/全/半。
 */
#define INITGUID
#include <windows.h>
#include <uiautomationclient.h>
#include <stdio.h>

static void DumpUiaElement(IUIAutomationElement *pEl, int depth)
{
    if (!pEl || depth > 6) return;
    BSTR name = NULL, cls = NULL, aid = NULL;
    pEl->get_CurrentName(&name);
    pEl->get_CurrentClassName(&cls);
    pEl->get_CurrentAutomationId(&aid);
    if ((name && SysStringLen(name) > 0) || (aid && SysStringLen(aid) > 0)) {
        printf("  [%d] cls=%S aid=%S name=\"%S\"\n", depth,
               cls ? cls : L"?", aid ? aid : L"?", name ? name : L"");
    }
    if (name) SysFreeString(name);
    if (cls) SysFreeString(cls);
    if (aid) SysFreeString(aid);
}

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
    printf("=== TrayInputIndicator UIA 名称 ===\n");
    HWND hShell = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!hShell) { printf("无任务栏\n"); return 1; }
    HWND hInd = NULL;
    EnumChildWindows(hShell, FindIndicator, (LPARAM)&hInd);
    if (!hInd) { printf("未找到 TrayInputIndicatorWClass\n"); return 1; }

    IUIAutomation *pUia = NULL;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation, (void **)&pUia);
    printf("CoCreateInstance UIA: hr=0x%08lx %s\n", hr, SUCCEEDED(hr) ? "OK" : "FAIL");
    if (FAILED(hr) || !pUia) return 1;

    IUIAutomationElement *pEl = NULL;
    hr = pUia->ElementFromHandle(hInd, &pEl);
    printf("ElementFromHandle: hr=0x%08lx %s\n", hr, SUCCEEDED(hr) ? "OK" : "FAIL");
    if (SUCCEEDED(hr) && pEl) {
        DumpUiaElement(pEl, 0);

        /* 递归遍历后代 */
        IUIAutomationTreeWalker *pWalker = NULL;
        if (SUCCEEDED(pUia->get_RawViewWalker(&pWalker)) && pWalker) {
            IUIAutomationElement *pChild = NULL;
            if (SUCCEEDED(pWalker->GetFirstChildElement(pEl, &pChild)) && pChild) {
                DumpUiaElement(pChild, 1);
                pChild->Release();
            }
            pWalker->Release();
        }
        pEl->Release();
    }
    pUia->Release();
    printf("=== 完成 ===\n");
    CoUninitialize();
    return 0;
}