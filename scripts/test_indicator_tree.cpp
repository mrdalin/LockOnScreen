/*
 * 完整 UIA 树打印：任务栏 TrayInputIndicator 全子树，含 ControlType/Name/AutomationId
 * 编译：g++ -O2 -o scripts/test_indicator_tree.exe scripts/test_indicator_tree.cpp -luiautomationcore -lole32 -loleaut32
 */
#define INITGUID
#include <windows.h>
#include <uiautomationclient.h>
#include <stdio.h>

static const wchar_t *CtrlTypeName(int id)
{
    switch (id) {
    case UIA_ButtonControlTypeId: return L"Button";
    case UIA_ToolBarControlTypeId: return L"ToolBar";
    case UIA_TextControlTypeId: return L"Text";
    case UIA_ImageControlTypeId: return L"Image";
    case UIA_MenuItemControlTypeId: return L"MenuItem";
    case UIA_PaneControlTypeId: return L"Pane";
    case UIA_StatusBarControlTypeId: return L"StatusBar";
    case UIA_GroupControlTypeId: return L"Group";
    default: return L"?";
    }
}

static void DumpTree(IUIAutomation *pUia, IUIAutomationElement *pEl, int depth)
{
    if (!pEl || depth > 10) return;
    BSTR name = NULL, aid = NULL, cls = NULL;
    int ctrlType = 0;
    pEl->get_CurrentName(&name);
    pEl->get_CurrentAutomationId(&aid);
    pEl->get_CurrentClassName(&cls);
    pEl->get_CurrentControlType(&ctrlType);
    for (int i = 0; i < depth; i++) printf("  ");
    printf("type=%-9S cls=%-24S aid=%-6S name=\"%S\"\n",
           CtrlTypeName(ctrlType), cls ? cls : L"?", aid ? aid : L"?", name ? name : L"");
    if (name) SysFreeString(name);
    if (aid) SysFreeString(aid);
    if (cls) SysFreeString(cls);

    IUIAutomationTreeWalker *pWalker = NULL;
    if (SUCCEEDED(pUia->get_RawViewWalker(&pWalker)) && pWalker) {
        IUIAutomationElement *pChild = NULL;
        if (SUCCEEDED(pWalker->GetFirstChildElement(pEl, &pChild)) && pChild) {
            DumpTree(pUia, pChild, depth + 1);
            pChild->Release();
        }
        pWalker->Release();
    }
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
    printf("=== TrayInputIndicator UIA 完整树 ===\n");
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
    DumpTree(pUia, pEl, 0);
    pEl->Release();
    pUia->Release();
    printf("=== 完成 ===\n");
    CoUninitialize();
    return 0;
}