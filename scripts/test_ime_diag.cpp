/*
 * 输入法状态实时诊断工具
 * 编译：g++ -o scripts/test_ime_diag.exe scripts/test_ime_diag.cpp -lole32 -lmsctf -limm32
 * 运行：启动后每秒打印当前 IMM/TSF 检测值。运行期间切换中/英文、全/半角，
 *       观察哪一行数值随切换变化，帮助定位检测通道问题。
 */
#include <windows.h>
#define INITGUID            /* 必须在 windows.h 前定义，DEFINE_GUID 才生成定义 */
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#undef INITGUID
#include <stdio.h>

EXTERN_C const CLSID CLSID_TF_ThreadMgr = {0x529A9E6B,0x6587,0x4F23,{0xAB,0x9E,0x9C,0x7D,0x68,0x3E,0x3C,0x50}};
EXTERN_C const CLSID CLSID_TF_InputProcessorProfiles = {0x33C53A50,0xF456,0x4884,{0xB0,0x49,0x85,0xFD,0x64,0x3E,0xCF,0xED}};
EXTERN_C const IID  IID_IUnknown = {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
EXTERN_C const IID  IID_ITfInputProcessorProfiles = {0x1f02b6c5,0x7842,0x4ee6,{0x8a,0x0b,0x9a,0x24,0x18,0x3a,0x95,0xca}};

static ITfInputProcessorProfiles *g_pTfProf = NULL;

int main()
{
    HRESULT hr;
    LANGID lang = 0;
    printf("=== 输入法状态诊断（每秒刷新，10秒后退出） ===\n");
    printf("运行期间请切换中/英文、全/半角，观察哪一行变化\n\n");

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITfInputProcessorProfiles, (void**)&g_pTfProf);
    if (FAILED(hr)) printf("[TSF] profiles 创建失败: 0x%08lx\n", hr);

    for (int t = 0; t < 10; t++) {
        HWND fg = GetForegroundWindow();
        const wchar_t *fgName = L"(null)";
        wchar_t buf[64] = L"";
        HIMC himc = NULL;
        DWORD convImm = 0;
        BOOL immOk = FALSE;
        LRESULT convCtrl = -1;

        if (fg) {
            GetWindowTextW(fg, buf, 63);
            fgName = buf;
            /* IMM 法1：ImmGetContext */
            himc = ImmGetContext(fg);
            if (himc) {
                DWORD sent = 0;
                immOk = ImmGetConversionStatus(himc, &convImm, &sent);
                ImmReleaseContext(fg, himc);
            }
            /* IMM 法2：WM_IME_CONTROL */
            {
                HWND imeWnd = ImmGetDefaultIMEWnd(fg);
                if (imeWnd)
                    convCtrl = SendMessageW(imeWnd, WM_IME_CONTROL, 0x001, 0); /* IMC_GETCONVERSIONMODE */
            }
        }

        if (g_pTfProf && SUCCEEDED(g_pTfProf->GetCurrentLanguage(&lang))) {
            printf("[T=%d] 前台=%-20S | TSF lang=0x%04X(%s) | "
                   "ImmGetConversionStatus=%s(0x%08lX Native=%d Full=%d) | "
                   "WM_IME_CONTROL=0x%08lX(Native=%d Full=%d)\n",
                   t, fgName, lang,
                   (lang == 0x0804) ? "\xE4\xB8\xAD\xE6\x96\x87" : "\xE8\x8B\xB1\xE6\x96\x87",
                   immOk ? "OK" : "FAIL", convImm,
                   immOk ? !!(convImm & IME_CMODE_NATIVE) : -1,
                   immOk ? !!(convImm & IME_CMODE_FULLSHAPE) : -1,
                   (DWORD)convCtrl,
                   convCtrl >= 0 ? !!(convCtrl & IME_CMODE_NATIVE) : -1,
                   convCtrl >= 0 ? !!(convCtrl & IME_CMODE_FULLSHAPE) : -1);
        } else {
            printf("[T=%d] 前台=%-20S | TSF 不可用 | "
                   "ImmGetConversionStatus=%s(0x%08lX Native=%d Full=%d) | "
                   "WM_IME_CONTROL=0x%08lX(Native=%d Full=%d)\n",
                   t, fgName,
                   immOk ? "OK" : "FAIL", convImm,
                   immOk ? !!(convImm & IME_CMODE_NATIVE) : -1,
                   immOk ? !!(convImm & IME_CMODE_FULLSHAPE) : -1,
                   (DWORD)convCtrl,
                   convCtrl >= 0 ? !!(convCtrl & IME_CMODE_NATIVE) : -1,
                   convCtrl >= 0 ? !!(convCtrl & IME_CMODE_FULLSHAPE) : -1);
        }
        Sleep(1000);
    }

    if (g_pTfProf) g_pTfProf->Release();
    CoUninitialize();
    printf("\n=== 诊断完成 ===\n");
    return 0;
}