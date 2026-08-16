/*
 * TSF + IMM 输入法状态检测验证工具
 * 独立编译：g++ -o scripts/test_tsf.exe scripts/test_tsf.cpp -lole32 -lmsctf -limm32
 */
#define WIN32_LEAN_AND_MEAN
#define INITGUID            /* 必须在 windows.h 前定义，DEFINE_GUID 才生成定义 */
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#undef INITGUID
#include <stdio.h>

/* 从 LockOnScreen main.c 复制的 TSF sink 和查询函数 */
static ITfThreadMgr *g_pTfThreadMgr = NULL;
static ITfSource     *g_pTfSource = NULL;
static ITfInputProcessorProfiles *g_pTfProf = NULL;
static DWORD g_tfCookie = 0;
static volatile int g_langChanged = 0;

/* MinGW msctf.h 仅声明 CLSID_TF_*，需手动定义 */
EXTERN_C const CLSID CLSID_TF_ThreadMgr = {0x529A9E6B,0x6587,0x4F23,{0xAB,0x9E,0x9C,0x7D,0x68,0x3E,0x3C,0x50}};
EXTERN_C const CLSID CLSID_TF_InputProcessorProfiles = {0x33C53A50,0xF456,0x4884,{0xB0,0x49,0x85,0xFD,0x64,0x3E,0xCF,0xED}};
EXTERN_C const IID  IID_IUnknown = {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

class CTsfSink : public ITfActiveLanguageProfileNotifySink
{
public:
    CTsfSink() : m_ref(1) {}
    virtual ~CTsfSink() {}
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink)) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --m_ref; if (r == 0) delete this; return r; }
    STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated) override {
        printf("[TSF] OnActivated (activated=%d)\n", fActivated);
        g_langChanged = 1;
        return S_OK;
    }
private:
    ULONG m_ref;
};

static void TsfInit(void)
{
    CTsfSink *pSink = new CTsfSink();
    HRESULT hr;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { printf("CoInitializeEx failed: 0x%08lx\n", hr); return; }
    hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&g_pTfThreadMgr);
    if (FAILED(hr)) { printf("CoCreateInstance CLSID_TF_ThreadMgr failed: 0x%08lx\n", hr); return; }
    TfClientId tid;
    g_pTfThreadMgr->Activate(&tid);
    if (g_pTfThreadMgr->QueryInterface(IID_ITfSource, (void**)&g_pTfSource) == S_OK) {
        g_pTfSource->AdviseSink(IID_ITfActiveLanguageProfileNotifySink, pSink, &g_tfCookie);
        printf("[TSF] Sink registered (cookie=%lu)\n", g_tfCookie);
    }
    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL, CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles, (void**)&g_pTfProf);
    if (FAILED(hr)) printf("[TSF] GetInputProcessorProfiles failed: 0x%08lx\n", hr);
    printf("[TSF] Init OK\n");
}

static void TsfShutdown(void)
{
    if (g_pTfSource) { if (g_tfCookie) g_pTfSource->UnadviseSink(g_tfCookie); g_pTfSource->Release(); }
    if (g_pTfProf) { g_pTfProf->Release(); }
    if (g_pTfThreadMgr) { g_pTfThreadMgr->Deactivate(); g_pTfThreadMgr->Release(); }
    CoUninitialize();
}

int main()
{
    printf("=== TSF + IMM 输入法状态检测验证 ===\n");
    printf("请尝试切换输入法（Ctrl+Space/Alt+Shift/Win+Space），\n");
    printf("并查看屏幕右下角的状态提示。运行 15 秒自动退出。\n\n");

    TsfInit();

    /* 立即查询一次当前状态 */
    LANGID lang = 0;
    if (g_pTfProf && SUCCEEDED(g_pTfProf->GetCurrentLanguage(&lang)))
        printf("[INITIAL] TSF lang: 0x%04X (%S)\n", lang, lang == 0x0804 ? L"中文" : lang == 0x0409 ? L"英文" : L"其他");

    /* IMM 查询 */
    HWND fg = GetForegroundWindow();
    HIMC himc = fg ? ImmGetContext(fg) : NULL;
    if (himc) {
        DWORD conv = 0, sent = 0;
        if (ImmGetConversionStatus(himc, &conv, &sent)) {
            printf("[INITIAL] IMM conv=0x%08lX (Native=%d FullShape=%d)\n",
                   conv, !!(conv & IME_CMODE_NATIVE), !!(conv & IME_CMODE_FULLSHAPE));
        } else {
            printf("[INITIAL] ImmGetConversionStatus failed\n");
        }
        ImmReleaseContext(fg, himc);
    } else {
        printf("[INITIAL] ImmGetContext returned NULL (no IME context)\n");
    }
    /* IMM 方法2：WM_IME_CONTROL */
    if (fg) {
        HWND imeWnd = ImmGetDefaultIMEWnd(fg);
        if (imeWnd) {
            LRESULT r = SendMessageW(imeWnd, WM_IME_CONTROL, 0x001, 0); /* IMC_GETCONVERSIONMODE */
            printf("[INITIAL] WM_IME_CONTROL conv=0x%08lX (Native=%d FullShape=%d)\n",
                   (DWORD)r, !!(r & IME_CMODE_NATIVE), !!(r & IME_CMODE_FULLSHAPE));
        } else {
            printf("[INITIAL] ImmGetDefaultIMEWnd returned NULL\n");
        }
    }

    /* 轮询 15 秒 */
    for (int i = 0; i < 15; i++) {
        Sleep(1000);
        if (g_langChanged) {
            /* TSF 事件触发后重新查询 */
            if (g_pTfProf && SUCCEEDED(g_pTfProf->GetCurrentLanguage(&lang)))
                printf("[T=%d] TSF lang: 0x%04X\n", i, lang);
            g_langChanged = 0;
        }
        /* 每轮也做 IMM 查询 */
        HWND fg2 = GetForegroundWindow();
        HIMC himc2 = fg2 ? ImmGetContext(fg2) : NULL;
        if (himc2) {
            DWORD conv2 = 0, sent2 = 0;
            if (ImmGetConversionStatus(himc2, &conv2, &sent2))
                printf("[T=%d] IMM conv=0x%08lX (Native=%d FullShape=%d)\n", i, conv2,
                       !!(conv2 & IME_CMODE_NATIVE), !!(conv2 & IME_CMODE_FULLSHAPE));
            ImmReleaseContext(fg2, himc2);
        }
    }

    TsfShutdown();
    printf("\n=== 测试完成 ===\n");
    return 0;
}