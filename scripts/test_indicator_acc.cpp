/*
 * TrayInputIndicatorWClass 无障碍名称读取测试
 * 编译：g++ -O2 -o scripts/test_indicator_acc.exe scripts/test_indicator_acc.cpp -loleacc -lole32
 * 运行：找到任务栏输入指示器窗口，用 IAccessible 读取其 accName。
 *       若输出 "中"/"英"/"全"/"半" 等，则该通道可用于检测输入法状态。
 */
#define INITGUID
#include <windows.h>
#include <oleacc.h>
#include <stdio.h>

static void DumpAccName(IAccessible *pAcc, int depth)
{
    VARIANT vChild;
    vChild.vt = VT_I4;
    vChild.lVal = CHILDID_SELF;
    BSTR bstrName = NULL;
    HRESULT hr = pAcc->get_accName(vChild, &bstrName);
    if (SUCCEEDED(hr) && bstrName && SysStringLen(bstrName) > 0) {
        printf("  [%d] accName=\"%S\" (len=%u)\n", depth, bstrName, (unsigned)SysStringLen(bstrName));
    }
    if (bstrName) SysFreeString(bstrName);

    /* 递归子元素 */
    long cnt = 0;
    pAcc->get_accChildCount(&cnt);
    for (long i = 1; i <= cnt; i++) {
        VARIANT v;
        v.vt = VT_I4;
        v.lVal = i;
        IDispatch *pDisp = NULL;
        if (SUCCEEDED(pAcc->get_accChild(v, &pDisp)) && pDisp) {
            IAccessible *pChild = NULL;
            if (SUCCEEDED(pDisp->QueryInterface(IID_IAccessible, (void **)&pChild)) && pChild) {
                DumpAccName(pChild, depth + 1);
                pChild->Release();
            }
            pDisp->Release();
        }
    }
}

static BOOL CALLBACK FindIndicator(HWND hwnd, LPARAM lp)
{
    WCHAR cls[64];
    GetClassNameW(hwnd, cls, 63);
    if (wcsstr(cls, L"TrayInputIndicator") || wcsstr(cls, L"ToolbarWindow32") ||
        wcsstr(cls, L"TrayNotify")) {
        *(HWND *)lp = hwnd;
        return FALSE;
    }
    return TRUE;
}

int main(void)
{
    CoInitialize(NULL);
    printf("=== TrayInputIndicatorWClass 无障碍名称 ===\n");
    HWND hShell = FindWindowW(L"Shell_TrayWnd", NULL);
    if (hShell) {
        HWND hInd = NULL;
        EnumChildWindows(hShell, FindIndicator, (LPARAM)&hInd);
        if (hInd) {
            printf("找到指示器窗口 %p cls=", (void *)hInd);
            WCHAR cls[64]; GetClassNameW(hInd, cls, 63);
            wprintf(L"%s\n", cls);
            IAccessible *pAcc = NULL;
            HRESULT hr = AccessibleObjectFromWindow(hInd, OBJID_CLIENT, IID_IAccessible, (void **)&pAcc);
            printf("AccessibleObjectFromWindow hr=0x%08lx %s\n", hr, SUCCEEDED(hr) ? "OK" : "FAIL");
            if (SUCCEEDED(hr) && pAcc) {
                DumpAccName(pAcc, 0);
                pAcc->Release();
            }
        } else {
            printf("未在任务栏找到指示器窗口！\n");
        }
    }
    printf("=== 完成 ===\n");
    CoUninitialize();
    return 0;
}