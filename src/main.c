/*
 * LockOnScreen v2 - 键盘切换键状态提示 (Caps / Num / Scroll) + 输入法状态
 * 纯 Win32 API + GDI+ 抗锯齿绘制，32 位单文件，兼容 Windows 7/10/11
 *
 * v2 新增（M1）：
 *   - 气泡定位可配置：跟随文本光标/鼠标、屏幕底部居中、屏幕角落、贴边
 *   - 透明度百分比、显示时长可调（右键菜单）
 *   - 缓动渐入动画（上弹 + ease-in-out）
 *   - 完全自绘右键菜单：圆角 + 阴影 + 多级级联 + 单选/勾选
 *   - 配置持久化：同目录 LockOnScreen.ini 优先，HKCU 注册表兜底，写入双写
 */
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define INITGUID            /* 必须保持到 msctf.h 之后（MinGW 的 DEFINE_GUID 依赖 INITGUID） */
#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <math.h>
#include <objidl.h>
#include <imm.h>
#include <msctf.h>
#undef INITGUID
#include <gdiplus.h>
using namespace Gdiplus;

#define KEY_COUNT   3
#define POLL_MS     30      /* 轮询间隔 */
#define SHOW_MS     800     /* 默认 OSD 显示时长(ms)后开始淡出（可由配置覆盖） */
#define STARTUP_MS  1200    /* 启动提示显示时长 */
#define FADE_MS     30      /* 淡出步进间隔 */
#define FADE_STEP   60      /* 每步 alpha 减少量（快速淡出） */
#define OSD_ALPHA   200     /* 初始不透明度（100% 透明度时的 alpha 上限） */
#define EDGE_GAP    16      /* 角落/贴边时，OSD 距工作区边缘的距离 */
#define ENTER_MS    200     /* 渐入动画时长 */
#define ENTER_FRAME 15      /* 渐入动画帧间隔 */
#define ENTER_RISE  12      /* 渐入上弹距离(px) */
#define OSD_CORNER  14      /* 圆角半径（Win11 风格大圆角） */
#define OSD_H       58      /* OSD 内容区高度（不含阴影边距） */
#define OSD_TEXT_SIZE 17    /* 文字字号(px) */
#define OSD_ICON_SIZE 24    /* 图标字号(px) */
#define OSD_GAP     10      /* 统一间距：图标-左/图标-文字/文字-右 三边相等 */
#define OSD_ICON_W  34      /* 蓝色图标方块尺寸(正方形) */
#define OSD_SHADOW  14      /* 阴影边距：窗口四周预留的柔和投影区域 */
#define OSD_ICON_BLUE_R 45  /* 主色蓝（#2D6AFF） */
#define OSD_ICON_BLUE_G 106
#define OSD_ICON_BLUE_B 255

enum { POS_FOLLOW = 0,    /* 跟随文本光标/鼠标右下 */
       POS_CENTER,         /* 屏幕正中（鼠标所在屏） */
       POS_CORNER,        /* 屏幕角落 */
       POS_EDGE };        /* 贴边 */
/* 角落位置 */
enum { CORNER_TL = 0, CORNER_TR, CORNER_BL, CORNER_BR };
/* 贴边位置 */
enum { EDGE_TOP = 0, EDGE_LEFT, EDGE_RIGHT, EDGE_BOTTOM };

/* 右键菜单命令 ID */
enum {
    IDM_AUTOSTART = 1,
    IDM_POS_FOLLOW, IDM_POS_CENTER,
    IDM_POS_CORNER_TL, IDM_POS_CORNER_TR, IDM_POS_CORNER_BL, IDM_POS_CORNER_BR,
    IDM_POS_EDGE_TOP, IDM_POS_EDGE_LEFT, IDM_POS_EDGE_RIGHT, IDM_POS_EDGE_BOTTOM,
    IDM_ALPHA_40, IDM_ALPHA_50, IDM_ALPHA_60, IDM_ALPHA_75, IDM_ALPHA_90, IDM_ALPHA_100,
    IDM_SHOW_300, IDM_SHOW_500, IDM_SHOW_800, IDM_SHOW_1000, IDM_SHOW_1500, IDM_SHOW_2000,
    IDM_DISP_CAPS, IDM_DISP_NUM, IDM_DISP_SCROLL, IDM_DISP_IME,
    IDM_SETTINGS,
    IDM_RESET,
    IDM_EXIT,
};

/* 自绘菜单（Win11 Fluent 风格） */
#define MENU_ITEM_H    26      /* 菜单项高度（紧凑） */
#define MENU_PAD_X     14      /* 文字左右留白 */
#define MENU_CHECK_W   24      /* 勾选/单选标记区宽度 */
#define MENU_ARROW_W   18      /* 子菜单箭头区宽度 */
#define MENU_CORNER    8       /* 菜单窗口圆角半径 */
#define MENU_MAX_DEPTH 5       /* 级联最大深度 */

/* 配置键名（ini section=General，注册表 HKCU\Software\LockOnScreen） */
#define CFG_INI_NAME  L"LockOnScreen.ini"
#define CFG_INI_SECT  L"General"
#define CFG_REG_PATH  L"Software\\LockOnScreen"
#define CFG_KEY_POSMODE   L"posMode"
#define CFG_KEY_POSCORNER L"posCorner"
#define CFG_KEY_POSEDGE   L"posEdge"
#define CFG_KEY_ALPHAPCT  L"alphaPct"
#define CFG_KEY_SHOWMS    L"showMs"
#define CFG_KEY_SHOWCAPS  L"showCaps"
#define CFG_KEY_SHOWNUM   L"showNum"
#define CFG_KEY_SHOWSCROLL L"showScroll"
#define CFG_KEY_SHOWIME    L"showIme"

/* 三个切换键 */
static const UINT g_vk[KEY_COUNT]  = { VK_CAPITAL, VK_NUMLOCK, VK_SCROLL };
static const WCHAR *g_iconOn[KEY_COUNT]  = { L"A", L"N", L"S" };
static const WCHAR *g_iconOff[KEY_COUNT] = { L"a", L"N", L"S" };  /* Num/Scroll 关：×标记在 OsdRender 中绘制 */
static const WCHAR *g_textOn[KEY_COUNT]  = { L"\u5927\u5199\u5F00",  /* 大写开 */
                                             L"\u6570\u5B57\u5F00",  /* 数字开 */
                                             L"\u6EDA\u52A8\u5F00" }; /* 滚动开 */
static const WCHAR *g_textOff[KEY_COUNT] = { L"\u5927\u5199\u5173",  /* 大写关 */
                                             L"\u6570\u5B57\u5173",  /* 数字关 */
                                             L"\u6EDA\u52A8\u5173" }; /* 滚动关 */

/* 三键专属主题色（开）：Caps=黄(对应托盘黄A)、Num=蓝、Scroll=红(传统指示灯)
 * 关状态统一用中性灰（熄灭感），并叠加 × 标记 */
static const BYTE g_keyColor[KEY_COUNT][3] = {
    { 255, 184, 0 },   /* 黄  #FFB800  Caps */
    { 45, 106, 255 },  /* 蓝  #2D6AFF  Num  */
    { 224, 57, 46 },   /* 红  #E0392E  Scroll */
};
static const BYTE g_keyColorHi[KEY_COUNT][3] = { /* 渐变顶部亮色 */
    { 255, 205, 51 },
    { 84, 130, 255 },
    { 235, 90, 80 },
};
static const BYTE g_keyTextColor[KEY_COUNT][3] = { /* 文字开色（加深版） */
    { 178, 110, 0 },   /* 深黄 #B26E00 */
    { 27, 79, 216 },   /* 深蓝 #1B4FD8 */
    { 179, 38, 30 },   /* 深红 #B3261E */
};

static BOOL  g_last[KEY_COUNT];      /* 上一轮状态 */
static int   g_showKey = -1;         /* 当前显示的键 */
static BOOL  g_showOn  = FALSE;      /* 当前显示的状态 */
static HWND  g_hOsd    = NULL;       /* OSD 窗口 */
static HWND  g_hMain   = NULL;       /* 隐藏主窗口(消息循环/托盘) */
static HINSTANCE g_hInst = NULL;
static BOOL  g_fading  = FALSE;      /* 是否在淡出 */
static BOOL  g_entering = FALSE;     /* 是否在渐入动画 */
static int   g_alpha   = 0;          /* 当前整体 alpha */
static int   g_alphaMax = 0;         /* 本次显示的目标 alpha（按透明度配置计算） */
static int   g_enterT  = 0;          /* 渐入动画已走帧数 */
static int   g_enterX  = 0, g_enterY0 = 0, g_enterY1 = 0; /* 渐入起点/终点 */
static UINT  g_enterShowMs = 0;      /* 渐入结束后使用的显示时长 */
static BOOL  g_trayAdded = FALSE;
static NOTIFYICONDATAW g_nid;
static ULONG_PTR g_gdiplusToken = 0; /* GDI+ 初始化令牌 */
static HDC  g_osdDC  = NULL;         /* OSD 内存 DC（DIB section） */
static HBITMAP g_osdBmp = NULL;      /* OSD DIB section */
static void *g_osdBits = NULL;       /* DIB 像素指针 */
static int  g_osdW = 0, g_osdH = 0;  /* OSD 尺寸 */
static const WCHAR g_mutexName[] = L"LockOnScreen_Singleton";
static const WCHAR g_runValue[]  = L"LockOnScreen";
static const WCHAR g_runOldValue[] = L"CapsLockIndicator"; /* 旧版自启值，用于清理残留 */
static const WCHAR g_runPath[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

/* ---------------- 配置（ini 优先 + 注册表兜底，写入双写） ---------------- */

typedef struct {
    int  posMode;      /* POS_* */
    int  posCorner;    /* CORNER_* */
    int  posEdge;      /* EDGE_* */
    int  alphaPct;     /* 40..100 */
    int  showMs;       /* 切换提示显示时长(ms) */
    BOOL showCaps;     /* 是否提示大写 */
    BOOL showNum;      /* 是否提示数字 */
    BOOL showScroll;   /* 是否提示滚动 */
    BOOL showIme;      /* 是否提示输入法状态 */
} LOConfig;

static LOConfig g_cfg;
static WCHAR g_iniPath[MAX_PATH] = L"";

/* ---------------- 自绘右键菜单（圆角+阴影+级联） ---------------- */

typedef struct MenuItem {
    const WCHAR *text;               /* NULL = 分隔线 */
    UINT id;                         /* 命令 ID；0 = 仅子菜单容器 */
    BOOL checked;                    /* 勾选/单选状态 */
    struct MenuItem *children;       /* 子菜单 */
    int childCount;
} MenuItem;

static MenuItem SubAlpha[] = {
    { L"40%",  IDM_ALPHA_40,  FALSE, NULL, 0 },
    { L"50%",  IDM_ALPHA_50,  FALSE, NULL, 0 },
    { L"60%",  IDM_ALPHA_60,  FALSE, NULL, 0 },
    { L"75%",  IDM_ALPHA_75,  FALSE, NULL, 0 },
    { L"90%",  IDM_ALPHA_90,  FALSE, NULL, 0 },
    { L"100%", IDM_ALPHA_100, FALSE, NULL, 0 },
};
static MenuItem SubTime[] = {
    { L"0.3s", IDM_SHOW_300,  FALSE, NULL, 0 },
    { L"0.5s", IDM_SHOW_500,  FALSE, NULL, 0 },
    { L"0.8s", IDM_SHOW_800,  FALSE, NULL, 0 },
    { L"1.0s", IDM_SHOW_1000, FALSE, NULL, 0 },
    { L"1.5s", IDM_SHOW_1500, FALSE, NULL, 0 },
    { L"2.0s", IDM_SHOW_2000, FALSE, NULL, 0 },
};
/* 显示位置/显示项已移入「显示设置」对话框，不再出现在右键菜单 */
static MenuItem MainMenu[] = {
    { L"\u5F00\u673A\u81EA\u542F", IDM_AUTOSTART, FALSE, NULL, 0 },   /* 开机自启 */
    { NULL, 0, FALSE, NULL, 0 },
    { L"\u663E\u793A\u65F6\u957F", 0, FALSE, SubTime,  6 },           /* 显示时长 */
    { L"\u900F\u660E\u5EA6",       0, FALSE, SubAlpha, 6 },           /* 透明度 */
    { NULL, 0, FALSE, NULL, 0 },
    { L"\u663E\u793A\u8BBE\u7F6E", IDM_SETTINGS, FALSE, NULL, 0 },    /* 显示设置 */
    { NULL, 0, FALSE, NULL, 0 },
    { L"\u9000\u51FA", IDM_EXIT, FALSE, NULL, 0 },                   /* 退出 */
};

/* 打开的菜单帧（级联栈） */
typedef struct {
    HWND hwnd;
    const MenuItem *items;
    int count;
    int hover;
    int width;
    HWND child;    /* 已打开的直接子菜单窗口 */
} MenuFrame;
static MenuFrame g_menuStack[MENU_MAX_DEPTH];
static int  g_menuDepth = 0;
static HFONT g_menuFont = NULL; /* 菜单/卡片标题字体 */
static HFONT g_menuFontBold = NULL; /* 菜单选中项加粗字体 */
static HFONT g_cfgFont  = NULL; /* 设置界面控件字体（更大号） */
static HFONT g_titleFont = NULL; /* 设置界面卡片标题字体（大号加粗） */

/* ---------------- 输入法状态（TSF 事件通知 + IMM 轮询兜底） ---------------- */

static BOOL  g_imeChinese = FALSE;   /* 当前输入法：中文模式 */
static BOOL  g_imeFullShape = FALSE; /* 当前输入法：全角 */
static BOOL  g_imeDirty = TRUE;      /* 启动时强制初始化（TSF 事件亦触发） */
static ITfThreadMgr *g_pTfThreadMgr = NULL;
static ITfSource     *g_pTfSource = NULL;
static ITfInputProcessorProfiles *g_pTfProf = NULL;
static TfClientId    g_tfClientId = 0;
static DWORD         g_tfCookie = 0;
static BOOL          g_tfInit = FALSE;

/* MinGW msctf.h 仅声明 CLSID_TF_*，需手动定义（DEFINE_GUID 只作用 IID） */
EXTERN_C const CLSID CLSID_TF_ThreadMgr          = {0x529A9E6B,0x6587,0x4F23,{0xAB,0x9E,0x9C,0x7D,0x68,0x3E,0x3C,0x50}};
EXTERN_C const CLSID CLSID_TF_InputProcessorProfiles = {0x33C53A50,0xF456,0x4884,{0xB0,0x49,0x85,0xFD,0x64,0x3E,0xCF,0xED}};
/* IID_IUnknown 在部分 MinGW 版本中未定义 */
EXTERN_C const IID  IID_IUnknown                  = {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

/* TSF 活动语言 profile 通知 sink（纯 C++ 继承实现） */
class CTsfLangSink : public ITfActiveLanguageProfileNotifySink
{
public:
    CTsfLangSink() : m_ref(1) { }
    virtual ~CTsfLangSink() { }
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ITfActiveLanguageProfileNotifySink)) {
            *ppv = this;
            AddRef();
            return S_OK;
        }
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP OnActivated(REFCLSID clsid, REFGUID guidProfile, BOOL fActivated) override
    {
        (void)clsid; (void)guidProfile; (void)fActivated;
        g_imeDirty = TRUE; /* 通知主线程轮询立即检测（避免跨线程直接操作 UI） */
        return S_OK;
    }
private:
    ULONG m_ref;
};

enum { TMR_POLL = 1, TMR_FADE = 2, TMR_SHOW = 3, TMR_ENTER = 4 };

/* ---------------- 工具函数 ---------------- */

/* 切换键(Caps/Num/Scroll)的开关状态：GetKeyState 的 bit 0 是稳态 toggle 状态。
 * 注意不能改用 GetAsyncKeyState & 1 —— 那是"最近被按下过"标志，读一次即清零，
 * 轮询时会一次按键误报两次翻转。 */
static BOOL IsKeyOn(UINT vk)
{
    return (GetKeyState((int)vk) & 1) != 0;
}

static BOOL IsElevated(void)
{
    HANDLE hTok = NULL;
    TOKEN_ELEVATION te;
    DWORD sz = 0;
    BOOL ok = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTok))
        return FALSE;
    if (GetTokenInformation(hTok, TokenElevation, &te, sizeof(te), &sz))
        ok = (te.TokenIsElevated != 0);
    CloseHandle(hTok);
    return ok;
}

/* 确保开机自启：管理员写 HKLM(所有用户)，普通写 HKCU(当前用户) */
static void EnsureAutoStart(void)
{
    HKEY root = IsElevated() ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    WCHAR path[MAX_PATH];
    WCHAR cmd[MAX_PATH + 3];
    HKEY hk = NULL;
    DWORD dw = 0;
    if (GetModuleFileNameW(NULL, path, MAX_PATH) == 0)
        return;
    wsprintfW(cmd, L"\"%s\"", path);
    if (RegCreateKeyExW(root, g_runPath, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &hk, &dw) != ERROR_SUCCESS)
        return;
    RegSetValueExW(hk, g_runValue, 0, REG_SZ, (const BYTE *)cmd,
                   (DWORD)((wcslen(cmd) + 1) * sizeof(WCHAR)));
    RegCloseKey(hk);
}

/* 取消开机自启（两个 hive 都删，含旧版自启值名 CapsLockIndicator） */
static void DisableAutoStart(void)
{
    HKEY hk = NULL;
    const WCHAR *names[] = { g_runValue, g_runOldValue };
    const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    int r, n;
    for (r = 0; r < 2; r++) {
        if (RegOpenKeyExW(roots[r], g_runPath, 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
            continue;
        for (n = 0; n < 2; n++)
            RegDeleteValueW(hk, names[n]);
        RegCloseKey(hk);
    }
}

/* 当前是否已设自启（任一 hive 有值即视为已开启） */
static BOOL AutoStartEnabled(void)
{
    HKEY hk = NULL;
    const HKEY roots[2] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    int i;
    for (i = 0; i < 2; i++) {
        if (RegOpenKeyExW(roots[i], g_runPath, 0, KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
            LONG r = RegQueryValueExW(hk, g_runValue, NULL, NULL, NULL, NULL);
            RegCloseKey(hk);
            if (r == ERROR_SUCCESS)
                return TRUE;
        }
    }
    return FALSE;
}

static void GdiplusInit(void)
{
    GdiplusStartupInput in;
    in.GdiplusVersion = 1;
    in.DebugEventCallback = NULL;
    in.SuppressBackgroundThread = FALSE;
    in.SuppressExternalCodecs = FALSE;
    GdiplusStartup(&g_gdiplusToken, &in, NULL);
}

/* ---------------- 配置管理（LockOnScreen.ini 优先，HKCU 注册表兜底；写入双写） ---------------- */

/* 把默认值填入指定配置结构（不修改 g_cfg） */
static void ConfigFillDefault(LOConfig *cfg)
{
    cfg->posMode    = POS_FOLLOW; /* 默认跟随鼠标 */
    cfg->posCorner  = CORNER_BR;
    cfg->posEdge    = EDGE_BOTTOM;
    cfg->alphaPct   = 100;      /* 默认透明度 100% */
    cfg->showMs     = SHOW_MS;  /* 默认显示时长 0.8s（SHOW_MS=800） */
    cfg->showCaps   = TRUE;
    cfg->showNum    = TRUE;
    cfg->showScroll = TRUE;
    cfg->showIme    = TRUE;
}

static void ConfigSetDefault(void)
{
    ConfigFillDefault(&g_cfg);
}

/* 读整数：INI 优先，注册表兜底；均无则返回默认值 */
static int ConfigReadInt(const WCHAR *key, int defVal)
{
    WCHAR buf[16];
    DWORD n;
    /* INI 优先：用 GetPrivateProfileStringW 判断键是否存在（GetPrivateProfileIntW
     * 返回 UINT 无法可靠区分“未找到”，且会忽略注册表兜底） */
    n = GetPrivateProfileStringW(CFG_INI_SECT, key, L"__MISS__", buf, 16, g_iniPath);
    (void)n;
    if (wcscmp(buf, L"__MISS__") != 0) {
        return GetPrivateProfileIntW(CFG_INI_SECT, key, defVal, g_iniPath);
    }
    /* 注册表兜底 */
    {
        HKEY hk = NULL;
        DWORD type = 0, size = sizeof(DWORD);
        DWORD dw = 0;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, CFG_REG_PATH, 0,
                          KEY_QUERY_VALUE, &hk) == ERROR_SUCCESS) {
            LONG r = RegQueryValueExW(hk, key, NULL, &type, (BYTE *)&dw, &size);
            RegCloseKey(hk);
            if (r == ERROR_SUCCESS && type == REG_DWORD)
                return (int)dw;
        }
    }
    return defVal;
}

/* 读布尔（存为 0/1） */
static BOOL ConfigReadBool(const WCHAR *key, BOOL defVal)
{
    return ConfigReadInt(key, defVal ? 1 : 0) != 0;
}

static void ConfigWriteIniInt(const WCHAR *key, int v)
{
    WCHAR buf[16];
    wsprintfW(buf, L"%d", v);
    WritePrivateProfileStringW(CFG_INI_SECT, key, buf, g_iniPath);
}

static void ConfigWriteRegInt(const WCHAR *key, int v)
{
    HKEY hk = NULL;
    DWORD dw = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CFG_REG_PATH, 0, NULL, 0,
                        KEY_SET_VALUE, NULL, &hk, &dw) != ERROR_SUCCESS)
        return;
    RegSetValueExW(hk, key, 0, REG_DWORD, (const BYTE *)&v, sizeof(v));
    RegCloseKey(hk);
}

/* 写入时双写：ini + 注册表同步 */
static void ConfigWriteInt(const WCHAR *key, int v)
{
    ConfigWriteIniInt(key, v);
    ConfigWriteRegInt(key, v);
}

static void ConfigLoad(void)
{
    /* ini 路径：exe 同目录 */
    GetModuleFileNameW(NULL, g_iniPath, MAX_PATH);
    {
        WCHAR *slash = wcsrchr(g_iniPath, L'\\');
        if (slash) *(slash + 1) = L'\0';
        lstrcatW(g_iniPath, CFG_INI_NAME);
    }
    g_cfg.posMode    = ConfigReadInt(CFG_KEY_POSMODE, POS_CENTER);
    g_cfg.posCorner  = ConfigReadInt(CFG_KEY_POSCORNER, CORNER_BR);
    g_cfg.posEdge    = ConfigReadInt(CFG_KEY_POSEDGE, EDGE_BOTTOM);
    g_cfg.alphaPct   = ConfigReadInt(CFG_KEY_ALPHAPCT, 100);
    g_cfg.showMs     = ConfigReadInt(CFG_KEY_SHOWMS, SHOW_MS);
    g_cfg.showCaps   = ConfigReadBool(CFG_KEY_SHOWCAPS, TRUE);
    g_cfg.showNum    = ConfigReadBool(CFG_KEY_SHOWNUM, TRUE);
    g_cfg.showScroll = ConfigReadBool(CFG_KEY_SHOWSCROLL, TRUE);
    g_cfg.showIme    = ConfigReadBool(CFG_KEY_SHOWIME, TRUE);
    /* 数值越界保护 */
    if (g_cfg.posMode < POS_FOLLOW || g_cfg.posMode > POS_EDGE) g_cfg.posMode = POS_CENTER;
    if (g_cfg.posCorner < CORNER_TL || g_cfg.posCorner > CORNER_BR) g_cfg.posCorner = CORNER_BR;
    if (g_cfg.posEdge < EDGE_TOP || g_cfg.posEdge > EDGE_BOTTOM) g_cfg.posEdge = EDGE_BOTTOM;
    if (g_cfg.alphaPct < 40 || g_cfg.alphaPct > 100) g_cfg.alphaPct = 100;
    if (g_cfg.showMs < 200) g_cfg.showMs = SHOW_MS;
    if (g_cfg.showMs > 3000) g_cfg.showMs = 3000;
}

static void ConfigSave(void)
{
    ConfigWriteInt(CFG_KEY_POSMODE,   g_cfg.posMode);
    ConfigWriteInt(CFG_KEY_POSCORNER, g_cfg.posCorner);
    ConfigWriteInt(CFG_KEY_POSEDGE,   g_cfg.posEdge);
    ConfigWriteInt(CFG_KEY_ALPHAPCT,  g_cfg.alphaPct);
    ConfigWriteInt(CFG_KEY_SHOWMS,    g_cfg.showMs);
    ConfigWriteInt(CFG_KEY_SHOWCAPS,  g_cfg.showCaps ? 1 : 0);
    ConfigWriteInt(CFG_KEY_SHOWNUM,   g_cfg.showNum ? 1 : 0);
    ConfigWriteInt(CFG_KEY_SHOWSCROLL, g_cfg.showScroll ? 1 : 0);
    ConfigWriteInt(CFG_KEY_SHOWIME,   g_cfg.showIme ? 1 : 0);
}

/* 获取前台窗口文本光标（插入符）的屏幕坐标；无光标返回 FALSE */
static BOOL CaretGetPos(POINT *pt)
{
    GUITHREADINFO gti;
    HWND fg;
    DWORD tid;
    fg = GetForegroundWindow();
    if (!fg)
        return FALSE;
    tid = GetWindowThreadProcessId(fg, NULL);
    if (tid == 0)
        return FALSE;
    memset(&gti, 0, sizeof(gti));
    gti.cbSize = sizeof(GUITHREADINFO);
    if (!GetGUIThreadInfo(tid, &gti))
        return FALSE;
    if (!gti.hwndCaret)
        return FALSE;
    pt->x = gti.rcCaret.left;
    pt->y = gti.rcCaret.top;
    return ClientToScreen(gti.hwndCaret, pt);
}

/* 前台窗口是否处于文本输入场景（存在输入光标 caret）
 * 用于过滤非输入窗口（解压软件/看图器/文件管理器等）的输入法状态提示：
 * 这类窗口无文本输入需求，切到它们时不弹输入法气泡，避免频繁打扰；
 * 进入真实输入位置（Word/记事本/输入框等有 caret）后恢复正常提示 */
static BOOL ImeTargetHasCaret(void)
{
    GUITHREADINFO gti;
    HWND fg = GetForegroundWindow();
    DWORD tid;
    if (!fg)
        return FALSE;
    tid = GetWindowThreadProcessId(fg, NULL);
    if (tid == 0)
        return FALSE;
    memset(&gti, 0, sizeof(gti));
    gti.cbSize = sizeof(GUITHREADINFO);
    if (!GetGUIThreadInfo(tid, &gti))
        return FALSE;
    return gti.hwndCaret != NULL;
}

/* ---------------- 输入法状态查询（TSF 事件通知 + IMM 轮询兜底） ---------------- */

/* 输入法转换状态检测（ImTip/aardio 方式：按位判断，跨进程）
 * 返回 0 成功并输出 opened（输入法是否打开，诊断用）与 conv（转换模式位掩码）；
 * 返回 -1 表示两个通道都不可用。
 * 位规则（实测校准，参考 aardio imeState 文档）：
 *   - 中英：conv & 3（IME_CMODE_NATIVE=1 / CHINESE=2）任一非 0 为中文；
 *           且 conv & 0x100（IME_CMODE_NOCONVERSION）时即使 opened=1 也算英文。
 *           注意：opened 在切换/按键瞬间会抖动到 0（IMM 层缺陷），
 *           不能作为中英必要条件——仅以 conv 位判定（ImTip 同样只看 conv）
 *   - 全角：conv & 8（IME_CMODE_FULLSHAPE）
 * 通道1（首选）：WM_IME_CONTROL 到前台窗口的 IME 窗口（LangIndicator/ImTip 验证过的
 *               可靠通道，对微软拼音/搜狗/QQ 等 TSF 输入法有效）
 * 通道2（兜底）：ImmGetContext + ImmGetConversionStatus（传统 IMM） */
static int ImeGetConversionStatus(BOOL *outOpened, int *outConv)
{
    HWND fg = GetForegroundWindow();
    if (!fg)
        return -1;

    /* 方法1（首选）：WM_IME_CONTROL */
    {
        HWND imeWnd = ImmGetDefaultIMEWnd(fg);
        if (imeWnd) {
            LRESULT opened = SendMessageW(imeWnd, WM_IME_CONTROL, 0x005, 0); /* IMC_GETOPENSTATUS */
            LRESULT conv   = SendMessageW(imeWnd, WM_IME_CONTROL, 0x001, 0); /* IMC_GETCONVERSIONMODE */
            *outOpened = (opened != 0);
            *outConv   = (int)conv;
            return 0;
        }
    }
    /* 方法2（兜底）：ImmGetContext */
    {
        HIMC himc = ImmGetContext(fg);
        if (himc) {
            DWORD conv = 0, sent = 0;
            BOOL ok = ImmGetConversionStatus(himc, &conv, &sent);
            ImmReleaseContext(fg, himc);
            if (ok) {
                *outOpened = TRUE;
                *outConv   = (int)conv;
                return 0;
            }
        }
    }
    return -1;
}

/* TSF 初始化：ITfThreadMgr + 活动语言 sink + ITfInputProcessorProfiles */
static void TsfInit(void)
{
    HRESULT hr;
    static CTsfLangSink gSink; /* 单例，生命周期与进程一致 */

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return;

    hr = CoCreateInstance(CLSID_TF_ThreadMgr, NULL, CLSCTX_INPROC_SERVER,
                          IID_ITfThreadMgr, (void **)&g_pTfThreadMgr);
    if (FAILED(hr) || !g_pTfThreadMgr)
        return;

    g_pTfThreadMgr->Activate(&g_tfClientId);

    if (g_pTfThreadMgr->QueryInterface(IID_ITfSource, (void **)&g_pTfSource) == S_OK) {
        g_pTfSource->AdviseSink(IID_ITfActiveLanguageProfileNotifySink,
                                &gSink, &g_tfCookie);
    }

    hr = CoCreateInstance(CLSID_TF_InputProcessorProfiles, NULL,
                          CLSCTX_INPROC_SERVER, IID_ITfInputProcessorProfiles,
                          (void **)&g_pTfProf);
    if (FAILED(hr))
        g_pTfProf = NULL;

    g_tfInit = TRUE;
}

static void TsfShutdown(void)
{
    if (!g_tfInit) return;
    if (g_pTfSource) {
        if (g_tfCookie)
            g_pTfSource->UnadviseSink(g_tfCookie);
        g_pTfSource->Release();
        g_pTfSource = NULL;
    }
    if (g_pTfProf) {
        g_pTfProf->Release();
        g_pTfProf = NULL;
    }
    if (g_pTfThreadMgr) {
        g_pTfThreadMgr->Deactivate();
        g_pTfThreadMgr->Release();
        g_pTfThreadMgr = NULL;
    }
    CoUninitialize();
    g_tfInit = FALSE;
}

/* TSF 查询当前语言：返回 LANGID（如 0x0804=中文）或 -1 */
static int TsfGetLang(void)
{
    LANGID lang = 0;
    if (!g_pTfProf)
        return -1;
    if (FAILED(g_pTfProf->GetCurrentLanguage(&lang)))
        return -1;
    return (int)lang;
}

/* ---------------- 任务栏输入法指示器检测（替代 IMM/UIA 的跨进程可靠方案） ----------------
 * 原理：枚举 Shell_TrayWnd 任务栏的子窗口，找到显示输入法状态的控件。
 * 微软拼音/搜狗/QQ/百度等输入法都会在任务栏同步绘制状态文字（中/英、全/半）。
 * 微软 Q&A 与实测表明：现代 Windows 应用已绕过 IMM WM_IME_CONTROL，TSF 转换模式
 * compartment 又是 per-thread 无法跨进程监听——任务栏指示器是唯一全局实时同步源。
 * 兼容：Win7 起 Shell_TrayWnd 结构一致。返回 0 成功(填 chinese/full)；-1 不可用。 */

static HWND  g_hImeTrayWnd = NULL; /* 缓存的指示器窗口句柄 */
static DWORD g_lastTrayTick = 0;   /* 上次查找时间节流 */

/* EnumChildWindows 回调：寻找文本含中/英/全/半 状态字符的子窗口 */
static BOOL CALLBACK EnumImeTrayChild(HWND hwnd, LPARAM lParam)
{
    WCHAR buf[64];
    int len = GetWindowTextW(hwnd, buf, 64);
    if (len > 0) {
        for (int i = 0; i < len; i++) {
            WCHAR c = buf[i];
            if (c == L'\u4E2D' || c == L'\u82F1' || c == L'\u5168' || c == L'\u534A' ||
                c == L'C' || c == L'E' || c == L'F' || c == L'H' ||
                c == L'c' || c == L'e' || c == L'f' || c == L'h') {
                *(HWND *)lParam = hwnd;
                return FALSE;
            }
        }
    }
    return TRUE;
}

static int ImeGetTaskbarMode(BOOL *outChinese, BOOL *outFull)
{
    HWND hTray;
    DWORD now = GetTickCount();

    if (!g_hImeTrayWnd || (now - g_lastTrayTick >= 2000)) {
        /* 首次或长期未更新：重新查找指示器窗口 */
        HWND hShell = FindWindowW(L"Shell_TrayWnd", NULL);
        HWND hFound = NULL;
        if (hShell)
            EnumChildWindows(hShell, EnumImeTrayChild, (LPARAM)&hFound);
        g_hImeTrayWnd = hFound;
        g_lastTrayTick = now;
    }
    hTray = g_hImeTrayWnd;
    if (!hTray)
        return -1;

    if (!IsWindow(hTray))
        return -1;

    WCHAR text[64];
    int len = GetWindowTextW(hTray, text, 64);
    if (len <= 0)
        return -1;

    BOOL chinese = FALSE, full = FALSE;
    for (int i = 0; i < len; i++) {
        WCHAR c = text[i];
        if (c == L'\u4E2D' || c == L'C' || c == L'c')
            chinese = TRUE;
        else if (c == L'\u82F1' || c == L'E' || c == L'e')
            chinese = FALSE;
        else if (c == L'\u5168' || c == L'F' || c == L'f')
            full = TRUE;
        else if (c == L'\u534A' || c == L'H' || c == L'h')
            full = FALSE;
    }
    *outChinese = chinese;
    *outFull    = full;
    return 0;
}

/* ---------------- 低层键盘钩子：检测输入法切换热键 ----------------
 * 背景：现代 Windows 应用已绕过 IMM 兼容层，TSF 转换模式 compartment 又是
 * per-thread 的，后台进程无法通过 IMM/UIA 可靠读到中/英、全/半角切换。
 * 方案：WH_KEYBOARD_LL 全局低层钩子捕捉切换热键（单独 Shift、Ctrl+Space、
 * Shift+Space、Win+Space），触发时让 ImePoll 强制刷新并弹气泡。
 * 单独 Shift 需「按下→释放且无其他键」才判定为中英切换，避免打字误报。 */
static HHOOK  g_hKbHook = NULL;
static DWORD  g_shiftDownTick = 0;  /* 单独 Shift 按下时间戳 */
static BOOL   g_shiftCandidate = FALSE; /* 等待确认的单独 Shift */
static BOOL   g_shiftOtherKey = FALSE;  /* Shift 按下期间出现其他键 */
/* 热键类型：用于 IMM/任务栏都读不到状态时，按热键语义翻转中/英或全/半角 */
enum { HK_NONE = 0, HK_SHIFT = 1, HK_CTRLSPACE = 2, HK_SHIFTSPACE = 3, HK_WINSPACE = 4 };
static int    g_hotkeyType = HK_NONE;

static LRESULT CALLBACK KbHookProc(int nCode, WPARAM wp, LPARAM lp)
{
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pkh = (KBDLLHOOKSTRUCT *)lp;
        BOOL down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);

        if (down) {
            UINT vk = pkh->vkCode;
            BOOL ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            BOOL shift = (GetAsyncKeyState(VK_SHIFT)    & 0x8000) != 0;
            BOOL win   = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
            BOOL alt   = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;

            if (vk == VK_SPACE) {
                /* 组合热键：Ctrl+Space(中英) / Shift+Space(全半角) / Win+Space(语言) */
                if (shift && !ctrl && !win && !alt) {
                    g_hotkeyType = HK_SHIFTSPACE;
                } else if (ctrl && !shift && !win && !alt) {
                    g_hotkeyType = HK_CTRLSPACE;
                } else if (win && !shift && !ctrl && !alt) {
                    g_hotkeyType = HK_WINSPACE;
                }
            } else if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT) {
                /* 单独 Shift：记下时间，等待确认（无其他键同按） */
                g_shiftDownTick = GetTickCount();
                g_shiftCandidate = TRUE;
                g_shiftOtherKey = FALSE;
            } else if (vk == VK_LWIN || vk == VK_RWIN) {
                /* Win 键组合（Win+Space 等）已在 SPACE 分支处理 */
            } else if (g_shiftCandidate) {
                /* Shift 按下期间出现其他键：不是中英切换 */
                g_shiftOtherKey = TRUE;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wp, lp);
}

/* 在 TMR_POLL 中调用：确认单独 Shift 是否为中英切换（按下 ≥160ms 且无其他键）。
 * 160ms 阈值同时充当「按下时长」与「确认窗口」：快速轻点（<160ms 按放，往往
 * 是普通 Shift 击键而非切中英）不会误判，避免状态漂移导致气泡与实际相反。 */
static void KbCheckShiftCandidate(void)
{
    if (!g_shiftCandidate)
        return;
    DWORD now = GetTickCount();
    if (now - g_shiftDownTick < 160)
        return; /* 等待确认窗口 */

    BOOL shiftStillDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    if (shiftStillDown)
        return; /* 还按着，继续等待释放 */

    /* Shift 已释放且期间无其他键 → 中英切换 */
    if (!g_shiftOtherKey) {
        g_hotkeyType = HK_SHIFT;
    }
    g_shiftCandidate = FALSE;
}

/* 输入法状态气泡（在 ImePoll 前声明；定义见 OSD 区） */
static void OsdShowIme(BOOL chinese, BOOL fullshape);
static HWND g_hCfgDlg; /* 前向声明（定义见设置对话框区）：配置窗口打开期间暂停输入法轮询 */

/* 输入法轮询（融合 WM_IME_CONTROL/IMM + TSF 语言 + 任务栏指示器）：并入主轮询循环
 * 判定优先级（ImTip/aardio 方式：实时读取优先）：
 *   1. WM_IME_CONTROL(opened+conv) 有效 → 直接用按位判定（中英/全角），权威
 *   2. 前台窗口变化 / TSF 语言切换事件 → 强制重读并采用（修复「切输入法/切窗口后
 *      状态不更新」——用户实测切到新输入法或新窗口后需要重新检测才能恢复正确）
 *   3. 热键触发 → 按语义翻转（IME 内部中英/全半角切换，仅当读不到真实状态时兜底）
 *   4. TSF 语言变化 → 按语言重新推断（Win+Space 切键盘布局） */
static void ImePoll(void)
{
    static int sLastConv = -1;
    static int sLastLang = -1;
    static int sLastTrayState = -1; /* 0..3=中英(bit0)+全半角(bit1) 组合；-1 不可用 */
    static HWND sLastFg = NULL;    /* 上次前台窗口：变化时强制重读 */
    static BOOL sInited = FALSE;   /* 首次静默初始化，不弹气泡 */
    static DWORD sLastFlipTick = 0;/* 最近一次热键翻转时间（防强制刷新覆盖） */
    BOOL chinese, full;
    BOOL imeOpened = FALSE;
    int conv = -1;
    BOOL trayChinese = FALSE, trayFull = FALSE;
    int trayState = -1;

    /* 菜单打开期间跳过：右键菜单会 SetForegroundWindow 抢前台，
     * 导致 IMM 查询结果/上下文变化而误触发气泡 */
    if (g_menuDepth > 0)
        return;
    /* 配置窗口打开期间跳过：避免切换前台到设置窗口时弹出输入法状态气泡 */
    if (g_hCfgDlg)
        return;

    /* 通道1（首选）：任务栏输入法指示器（跨进程可靠，对微软拼音/搜狗/QQ 均同步） */
    if (ImeGetTaskbarMode(&trayChinese, &trayFull) == 0)
        trayState = (trayChinese ? 1 : 0) | (trayFull ? 2 : 0);

    /* 通道2：WM_IME_CONTROL / IMM（ImTip 验证的可靠通道） */
    int imeOk = ImeGetConversionStatus(&imeOpened, &conv);
    /* 通道3：TSF 语言（仅中/英，无全半角） */
    int lang = (g_tfInit) ? TsfGetLang() : -1;

    /* 热键触发：无论状态是否变化都弹气泡（用户按下切换键即期待反馈） */
    int hotkey = g_hotkeyType;
    g_hotkeyType = HK_NONE;
    BOOL tsFired = g_imeDirty;  /* TSF 语言/输入法切换事件（保留清除前） */

    /* 前台窗口变化 / TSF 事件 → 强制重读真实状态（ImTip「切输入位置即提醒」机制，
     * 解决「同一窗口切换输入法后提示相反」：新输入法刚激活时 conv 值即新状态） */
    HWND fgNow = GetForegroundWindow();
    BOOL fgChanged = (fgNow != sLastFg);
    sLastFg = fgNow;
    BOOL forceRefresh = tsFired || fgChanged;

    /* 各通道值真实变化 */
    BOOL convChanged = (conv != sLastConv);
    BOOL langChanged = (lang != sLastLang);
    BOOL trayChanged = (trayState != sLastTrayState);

    /* 无任何变化且无热键/强制刷新 → 直接返回 */
    if (!hotkey && !forceRefresh && !convChanged && !langChanged && !trayChanged)
        return;
    sLastConv = conv;
    sLastLang = lang;
    sLastTrayState = trayState;
    g_imeDirty = FALSE;

    /* 融合判定：任务栏 > IME(opened+conv) > TSF语言 > 热键翻转 */
    if (trayState >= 0 && trayChanged) {
        chinese = trayChinese;
        full    = trayFull;
    } else if (imeOk == 0 &&
               (convChanged || (forceRefresh && GetTickCount() - sLastFlipTick > 500))) {
        /* ImTip/aardio 按位规则（实测校准）：
         *   chinese = !(conv&0x100 NOCONVERSION) && (conv&3)!=0
         *   full    = conv & 8（IME_CMODE_FULLSHAPE）
         * 实测（用户诊断日志）：opened 通道在切换/按键瞬间频繁抖动到 0，
         * 不能作为中英必要条件（否则中文状态会误判英文——即“识别反了”
         * 的根因）；conv 位稳定可靠，仅按 conv 判定。
         * 500ms 内刚热键翻转过则不覆盖（防恒定假值在切窗口时覆盖翻转结果） */
        BOOL noConv = (conv & 0x100) != 0; /* IME_CMODE_NOCONVERSION */
        chinese = !noConv && (conv & 3) != 0;
        full    = (conv & IME_CMODE_FULLSHAPE) != 0;
    } else if (langChanged && lang >= 0) {
        /* 系统输入语言变化（Win+Space 切键盘布局）：重新推断中英 */
        chinese = (lang == 0x0804); /* 简体中文 */
        full    = FALSE;
    } else if (hotkey) {
        /* 读不到真实状态（或值恒定不反映切换）→ 按热键语义翻转。
         * 注意：HK_WINSPACE 切换的是系统输入语言布局（如 中文键盘→英文键盘），
         * 不是输入法内部中英模式，不能翻转推断——lang 变化已在上面的
         * langChanged 分支处理；走到这里说明 lang 未变/不可用 → 保持现状。 */
        chinese = g_imeChinese;
        full    = g_imeFullShape;
        if (hotkey == HK_SHIFT || hotkey == HK_CTRLSPACE) {
            chinese = !chinese;           /* 中/英切换（同一输入法内部模式） */
            sLastFlipTick = GetTickCount();
        } else if (hotkey == HK_SHIFTSPACE) {
            full = !full;                 /* 全/半角切换 */
            sLastFlipTick = GetTickCount();
        }
    } else {
        /* 强制刷新（切应用/TSF 事件）但无可靠通道：保持，不弹 */
        chinese = g_imeChinese;
        full    = g_imeFullShape;
        if (!forceRefresh)
            return;
    }

    BOOL changed = (chinese != g_imeChinese || full != g_imeFullShape);
    g_imeChinese = chinese;
    g_imeFullShape = full;
    /* 提示过滤：
     * - 首次静默初始化（不弹气泡）
     * - 被动确认（切窗口/TSF 事件/指示器变化等）仅在文本输入场景提示
     *   （前台窗口有输入光标 caret）；非输入窗口（解压/看图/文件管理器等）
     *   不弹输入法状态，避免频繁打扰
     * - 主动热键（Shift/Ctrl+Space/Shift+Space）是用户明确操作，任何场景都反馈 */
    if (sInited && (hotkey || changed) && g_cfg.showIme &&
        (hotkey || ImeTargetHasCaret()))
        OsdShowIme(chinese, full);
    sInited = TRUE;
}

/* ease-in-out 三次缓动（等价 WPF CircleEase 的平滑感） */
static double EaseInOut(double t)
{
    if (t < 0.5)
        return 4 * t * t * t;
    return 1 - pow(-2 * t + 2, 3) / 2;
}

/* ---------------- OSD 窗口（GDI+ 抗锯齿绘制 + UpdateLayeredWindow） ---------------- */

/* 将 DIB 像素从 straight alpha 转为 premultiplied alpha（UpdateLayeredWindow+AC_SRC_ALPHA 要求） */
static void OsdPremultiply(void)
{
    int n = g_osdW * g_osdH;
    BYTE *p = (BYTE *)g_osdBits;
    int i;
    for (i = 0; i < n; i++, p += 4) {
        BYTE a = p[3];
        if (a == 0) {
            p[0] = p[1] = p[2] = 0;
        } else if (a < 255) {
            p[0] = (BYTE)((p[0] * a) / 255);
            p[1] = (BYTE)((p[1] * a) / 255);
            p[2] = (BYTE)((p[2] * a) / 255);
        }
    }
}

/* GDI+ 绘制 OSD 内容到 DIB：现代亮色气泡（柔和多层阴影 + 白色渐变 + 主色图标） */
static void OsdRender(void)
{
    if (!g_osdBits || g_showKey < 0 || g_showKey > KEY_COUNT)
        return;
    Bitmap bmp(g_osdW, g_osdH, g_osdW * 4, PixelFormat32bppARGB, (BYTE *)g_osdBits);
    Graphics g(&bmp);
    WCHAR iconBuf[8], textBuf[32];
    BOOL isIme = (g_showKey >= KEY_COUNT);
    /* 内容区左上角（阴影边距偏移） */
    INT cx = OSD_SHADOW, cy = OSD_SHADOW;
    INT cw = g_osdW - OSD_SHADOW * 2, chh = g_osdH - OSD_SHADOW * 2;
    /* 蓝色图标方块：内容区内垂直居中，水平位于左侧 GAP 处 */
    REAL boxX = (REAL)(cx + OSD_GAP);
    REAL boxY = (REAL)(cy + (chh - OSD_ICON_W) / 2);
    REAL textX  = boxX + (REAL)OSD_ICON_W + (REAL)OSD_GAP;
    RectF layout, bounds;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0)); /* 全透明底 */

    /* 柔和多层投影：3 层半透明黑从内容边缘逐层外扩（Win11 阴影质感） */
    {
        static const int  exp[3] = { 4, 8, 12 };
        static const BYTE alp[3] = { 30, 18, 8 };
        INT d = OSD_CORNER * 2;
        for (int i = 0; i < 3; i++) {
            GraphicsPath sp;
            INT e = exp[i];
            INT sx = cx - e, sy = cy - e, sw = cw + e * 2, sh = chh + e * 2;
            sp.AddArc(sx, sy, d + e * 2, d + e * 2, 180, 90);
            sp.AddArc(sx + sw - d - e * 2, sy, d + e * 2, d + e * 2, 270, 90);
            sp.AddArc(sx + sw - d - e * 2, sy + sh - d - e * 2, d + e * 2, d + e * 2, 0, 90);
            sp.AddArc(sx, sy + sh - d - e * 2, d + e * 2, d + e * 2, 90, 90);
            sp.CloseFigure();
            SolidBrush shBr(Color(alp[i], 0, 0, 0));
            g.FillPath(&shBr, &sp);
        }
    }

    /* 内容区圆角矩形路径 */
    GraphicsPath path;
    {
        INT d = OSD_CORNER * 2;
        path.AddArc(cx, cy, d, d, 180, 90);
        path.AddArc(cx + cw - d, cy, d, d, 270, 90);
        path.AddArc(cx + cw - d, cy + chh - d, d, d, 0, 90);
        path.AddArc(cx, cy + chh - d, d, d, 90, 90);
        path.CloseFigure();
    }

    /* 背景：白色 → 浅蓝白 垂直渐变（明亮 Fluent 风格） */
    {
        LinearGradientBrush bgBrush(Point(cx, cy), Point(cx, cy + chh),
                                    Color(255, 255, 255, 255), Color(255, 241, 246, 253));
        g.FillPath(&bgBrush, &path);
    }

    /* 柔和浅描边 */
    {
        Pen edgePen(Color(255, 227, 232, 240), 1.0f);
        g.DrawPath(&edgePen, &path);
    }

    /* 图标字符：三键用 A/N/⇅，输入法用 中/英 */
    if (isIme)
        lstrcpyW(iconBuf, g_showOn ? L"\u4E2D" : L"\u82F1");   /* 中 / 英 */
    else
        lstrcpyW(iconBuf, g_showOn ? g_iconOn[g_showKey] : g_iconOff[g_showKey]);

    {
        /* 图标圆角方块（渐变）：三键用专属色（开）或中性灰（关）；输入法恒用主蓝 */
        BYTE cHiR = isIme ? 84  : g_showOn ? g_keyColorHi[g_showKey][0] : 154;
        BYTE cHiG = isIme ? 130 : g_showOn ? g_keyColorHi[g_showKey][1] : 158;
        BYTE cHiB = isIme ? 255 : g_showOn ? g_keyColorHi[g_showKey][2] : 168;
        BYTE cBoR = isIme ? OSD_ICON_BLUE_R : g_showOn ? g_keyColor[g_showKey][0] : 138;
        BYTE cBoG = isIme ? OSD_ICON_BLUE_G : g_showOn ? g_keyColor[g_showKey][1] : 142;
        BYTE cBoB = isIme ? OSD_ICON_BLUE_B : g_showOn ? g_keyColor[g_showKey][2] : 152;
        GraphicsPath ib;
        {
            INT bs = OSD_ICON_W, d = 8;
            ib.AddArc((INT)boxX, (INT)boxY, d, d, 180, 90);
            ib.AddArc((INT)boxX + bs - d, (INT)boxY, d, d, 270, 90);
            ib.AddArc((INT)boxX + bs - d, (INT)boxY + bs - d, d, d, 0, 90);
            ib.AddArc((INT)boxX, (INT)boxY + bs - d, d, d, 90, 90);
            ib.CloseFigure();
        }
        LinearGradientBrush ibBrush(Point((INT)boxX, (INT)boxY),
                                    Point((INT)boxX, (INT)(boxY + OSD_ICON_W)),
                                    Color(255, cHiR, cHiG, cHiB),
                                    Color(255, cBoR, cBoG, cBoB));
        g.FillPath(&ibBrush, &ib);
        {
            Pen ibEdge(Color(70, 255, 255, 255), 1.0f);
            g.DrawPath(&ibEdge, &ib);
        }

        /* 白色加粗字符，方块内手动垂直居中（测量后偏移，消除字体 leading 偏差） */
        FontFamily ffIcon(L"Microsoft YaHei");
        Font fIcon(&ffIcon, (REAL)OSD_ICON_SIZE, FontStyleBold, UnitPixel);
        StringFormat sfIcon;
        sfIcon.SetAlignment(StringAlignmentNear);
        sfIcon.SetLineAlignment(StringAlignmentNear);
        RectF mb;
        g.MeasureString(iconBuf, -1, &fIcon, PointF(0, 0), &sfIcon, &mb);
        REAL iconX = boxX + (OSD_ICON_W - mb.Width) / 2.0f - mb.X;
        REAL iconY = boxY + (OSD_ICON_W - mb.Height) / 2.0f - mb.Y;
        SolidBrush fgBrush(Color(255, 255, 255, 255));
        g.DrawString(iconBuf, -1, &fIcon, PointF(iconX, iconY), &fgBrush);

        /* 关状态：Num/Scroll 画白色 ×（交叉标记，避免与字形斜线重合） */
        if (!g_showOn && !isIme && (g_showKey == 1 || g_showKey == 2)) {
            Pen strike(Color(255, 255, 255, 255), 2.0f);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            REAL m1 = 6.0f, m2 = OSD_ICON_W - 6.0f;
            g.DrawLine(&strike, boxX + m1, boxY + m1, boxX + m2, boxY + m2);
            g.DrawLine(&strike, boxX + m2, boxY + m1, boxX + m1, boxY + m2);
        }
    }

    /* 文字（右侧，垂直居中；加粗）；开=主题深蓝，关=中性灰 */
    if (isIme) {
        /* 输入法状态：中文输入/英文输入 + ·全角/·半角 */
        wsprintfW(textBuf, L"%s%s",
                  g_showOn ? L"\u4E2D\u6587\u8F93\u5165" : L"\u82F1\u6587\u8F93\u5165",
                  g_imeFullShape ? L"\u00B7\u5168\u89D2" : L"\u00B7\u534A\u89D2");
    } else {
        lstrcpyW(textBuf, g_showOn ? g_textOn[g_showKey] : g_textOff[g_showKey]);
    }
    {
        FontFamily ffText(L"Microsoft YaHei");
        Font fText(&ffText, (REAL)OSD_TEXT_SIZE, FontStyleBold, UnitPixel);
        /* 文字颜色：输入法恒深蓝/灰；三键开=键色加深版，关=中性灰 */
        Color txtColor(Color(255, 120, 130, 145)); /* 默认关=灰 */
        if (g_showOn) {
            if (isIme)
                txtColor = Color(255, 27, 79, 216);
            else
                txtColor = Color(255, g_keyTextColor[g_showKey][0],
                                      g_keyTextColor[g_showKey][1],
                                      g_keyTextColor[g_showKey][2]);
        }
        SolidBrush fgBrush(txtColor);
        layout.X = textX; layout.Y = 0;
        layout.Width = (REAL)(cw - (textX - cx) - OSD_GAP); layout.Height = (REAL)chh;
        g.MeasureString(textBuf, -1, &fText, layout, &bounds);
        g.DrawString(textBuf, -1, &fText,
                     PointF(textX, (REAL)cy + chh / 2 - bounds.Height / 2 - bounds.Y),
                     &fgBrush);
    }

    OsdPremultiply();
}

/* 定位：按配置模式计算位置。
 * POS_FOLLOW        ：优先文本光标、否则鼠标位置，气泡出现在其右下方（LangIndicator 风格）
 * POS_CENTER        ：鼠标所在显示器工作区正中
 * POS_CORNER        ：鼠标所在屏的四个角之一
 * POS_EDGE          ：鼠标所在屏的贴边（顶/左/右/底居中）
 */
static void OsdPosition(HWND hwnd)
{
    RECT rc;
    POINT pt;
    HMONITOR mon;
    MONITORINFO mi;
    RECT work;
    int w, h, x, y;

    GetWindowRect(hwnd, &rc);
    GetCursorPos(&pt);
    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(mon, &mi))
        return;
    work = mi.rcWork; /* 工作区（排除任务栏） */
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;

    switch (g_cfg.posMode) {
    case POS_FOLLOW: {
        POINT cpt;
        if (!CaretGetPos(&cpt))
            cpt = pt; /* 无文本光标则跟随鼠标 */
        x = cpt.x + 12;          /* 光标右下方 */
        y = cpt.y + 16;
        /* 防止超出工作区 */
        if (x + w > work.right)  x = work.right - w - EDGE_GAP;
        if (y + h > work.bottom) y = work.bottom - h - EDGE_GAP;
        if (x < work.left)       x = work.left + EDGE_GAP;
        if (y < work.top)        y = work.top + EDGE_GAP;
        break;
    }
    case POS_CORNER:
        switch (g_cfg.posCorner) {
        case CORNER_TL: x = work.left + EDGE_GAP;  y = work.top + EDGE_GAP;  break;
        case CORNER_TR: x = work.right - w - EDGE_GAP; y = work.top + EDGE_GAP; break;
        case CORNER_BL: x = work.left + EDGE_GAP;  y = work.bottom - h - EDGE_GAP; break;
        default:        x = work.right - w - EDGE_GAP; y = work.bottom - h - EDGE_GAP; break;
        }
        break;
    case POS_EDGE:
        switch (g_cfg.posEdge) {
        case EDGE_TOP:    x = work.left + (work.right - work.left) / 2 - w / 2;
                          y = work.top + EDGE_GAP; break;
        case EDGE_LEFT:   x = work.left + EDGE_GAP;
                          y = work.top + (work.bottom - work.top) / 2 - h / 2; break;
        case EDGE_RIGHT:  x = work.right - w - EDGE_GAP;
                          y = work.top + (work.bottom - work.top) / 2 - h / 2; break;
        default:          x = work.left + (work.right - work.left) / 2 - w / 2;
                          y = work.bottom - h - EDGE_GAP; break;
        }
        break;
    case POS_CENTER: {
        x = work.left + (work.right - work.left - w) / 2;
        y = work.top + (work.bottom - work.top - h) / 2;
        break;
    }
    default:
        x = work.left + (work.right - work.left - w) / 2;
        y = work.top + (work.bottom - work.top - h) / 2;
        break;
    }
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

/* 提交 DIB 为分层窗口显示；alpha 为整体不透明度（淡出只需改此值） */
static void OsdCommit(int alpha)
{
    if (!g_hOsd || !g_osdDC)
        return;
    RECT rc;
    POINT ptDst, ptSrc = { 0, 0 };
    SIZE sz = { g_osdW, g_osdH };
    BLENDFUNCTION bf;
    HDC hdcScreen = GetDC(NULL);
    GetWindowRect(g_hOsd, &rc);
    ptDst.x = rc.left; ptDst.y = rc.top;
    memset(&bf, 0, sizeof(bf));
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = (BYTE)alpha;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(g_hOsd, hdcScreen, &ptDst, &sz, g_osdDC, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(NULL, hdcScreen);
}

static LRESULT CALLBACK OsdWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OsdCreate(void)
{
    WNDCLASSW wc;
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC memDC;
    int w, h;

    if (g_hOsd)
        return;

    wc.style         = 0;
    wc.lpfnWndProc   = OsdWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = g_hInst;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = L"CapsOsdClass";
    RegisterClassW(&wc);

    /* 初始最小宽度（后续 OsdShow 按实际内容自适应）：
     * 用「大写关」作为三键文本基线，测量其宽度 */
    {
        Bitmap tmp(1, 1, PixelFormat32bppARGB);
        Graphics g(&tmp);
        FontFamily ffText(L"Microsoft YaHei");
        Font fText(&ffText, (REAL)OSD_TEXT_SIZE, FontStyleBold, UnitPixel);
        RectF mlayout(0, 0, 1000, 100), mbounds;
        g.MeasureString(L"\u82F1\u6587\u8F93\u5165\u00B7\u534A\u89D2", -1, &fText, mlayout, &mbounds);
        int textW = (int)(mbounds.Width + 2);
        h = OSD_H + OSD_SHADOW * 2;
        w = OSD_GAP + OSD_ICON_W + OSD_GAP + textW + OSD_GAP + OSD_SHADOW * 2;
    }

    g_hOsd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED
                             | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
                             L"CapsOsdClass", L"",
                             WS_POPUP,
                             0, 0, w, h, NULL, NULL, g_hInst, NULL);
    if (!g_hOsd)
        return;

    /* 创建 32bpp DIB（top-down, BGRA）作为 GDI+ 绘图表面 + 分层窗口源 */
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* top-down */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_osdBmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!g_osdBmp)
        return;
    memDC = CreateCompatibleDC(NULL);
    if (!memDC) {
        DeleteObject(g_osdBmp);
        g_osdBmp = NULL;
        return;
    }
    SelectObject(memDC, g_osdBmp);
    g_osdDC = memDC;
    g_osdBits = bits;
    g_osdW = w;
    g_osdH = h;

    OsdPosition(g_hOsd);
}

/* 测量当前显示内容文字宽度（与绘制相同字体 17px 加粗雅黑） */
static int OsdTextWidth(void)
{
    Bitmap tmp(1, 1, PixelFormat32bppARGB);
    Graphics g(&tmp);
    FontFamily ffText(L"Microsoft YaHei");
    Font fText(&ffText, (REAL)OSD_TEXT_SIZE, FontStyleBold, UnitPixel);
    RectF mlayout(0, 0, 1000, 100), mbounds;
    WCHAR buf[32];

    if (g_showKey >= KEY_COUNT) {
        wsprintfW(buf, L"%s%s",
                  g_showOn ? L"\u4E2D\u6587\u8F93\u5165" : L"\u82F1\u6587\u8F93\u5165",
                  g_imeFullShape ? L"\u00B7\u5168\u89D2" : L"\u00B7\u534A\u89D2");
    } else {
        lstrcpyW(buf, g_showOn ? g_textOn[g_showKey] : g_textOff[g_showKey]);
    }
    g.MeasureString(buf, -1, &fText, mlayout, &mbounds);
    return (int)(mbounds.Width + 2);
}

/* 重建 OSD 绘图表面（尺寸变化时），并同步调整窗口大小 */
static void OsdResize(int w, int h)
{
    BITMAPINFO bmi;
    void *bits = NULL;
    HDC memDC;

    if (w == g_osdW && h == g_osdH)
        return;

    if (g_osdBmp) { DeleteObject(g_osdBmp); g_osdBmp = NULL; }
    if (g_osdDC)   { DeleteDC(g_osdDC);     g_osdDC = NULL; }
    g_osdBits = NULL;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    g_osdBmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!g_osdBmp)
        return;
    memDC = CreateCompatibleDC(NULL);
    if (!memDC) {
        DeleteObject(g_osdBmp);
        g_osdBmp = NULL;
        return;
    }
    SelectObject(memDC, g_osdBmp);
    g_osdDC = memDC;
    g_osdBits = bits;
    g_osdW = w;
    g_osdH = h;

    SetWindowPos(g_hOsd, NULL, 0, 0, w, h,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void OsdShow(int key, BOOL on, UINT showMs)
{
    g_showKey = key;
    g_showOn  = on;
    if (!g_hOsd)
        OsdCreate();
    if (!g_hOsd)
        return;

    /* 取消正在进行的淡出/渐入/计时 */
    if (g_fading) {
        KillTimer(g_hMain, TMR_FADE);
        g_fading = FALSE;
    }
    if (g_entering) {
        KillTimer(g_hMain, TMR_ENTER);
        g_entering = FALSE;
    }
    KillTimer(g_hMain, TMR_SHOW);

    /* 目标 alpha 由透明度配置计算 */
    g_alphaMax = OSD_ALPHA * g_cfg.alphaPct / 100;
    if (g_alphaMax < 20) g_alphaMax = 20;

    /* 按当前内容自适应宽度（输入法长文本 vs 三键短文本） */
    {
        int tw = OsdTextWidth();
        int nw = OSD_SHADOW * 2 + OSD_GAP + OSD_ICON_W + OSD_GAP + tw + OSD_GAP;
        int nh = OSD_H + OSD_SHADOW * 2;
        OsdResize(nw, nh);
    }

    OsdRender();      /* 重绘内容（抗锯齿圆角/文字） */
    OsdPosition(g_hOsd);

    /* 渐入动画参数：透明度 0→目标，位置从下方 ENTER_RISE 上弹到目标 */
    {
        RECT rc;
        GetWindowRect(g_hOsd, &rc);
        g_enterX = rc.left;
        g_enterY0 = rc.top + ENTER_RISE;
        g_enterY1 = rc.top;
    }
    g_enterT = 0;
    g_enterShowMs = showMs;
    g_entering = TRUE;
    g_alpha = 0;
    ShowWindow(g_hOsd, SW_SHOWNA); /* 确保可见（UpdateLayeredWindow 不改变可见性） */
    OsdCommit(0);
    SetTimer(g_hMain, TMR_ENTER, ENTER_FRAME, NULL); /* 渐入动画，结束后再计时显示 */
}

/* 输入法状态气泡：key 用 KEY_COUNT 表示 IME 特例 */
static void OsdShowIme(BOOL chinese, BOOL fullshape)
{
    g_imeFullShape = fullshape;
    OsdShow(KEY_COUNT, chinese, (UINT)g_cfg.showMs);
}

/* 渐入动画一步（TMR_ENTER）：缓动插值透明度 + 位置上弹 */
static void OsdEnterStep(void)
{
    double t, e;
    int alpha, y;
    g_enterT += ENTER_FRAME;
    t = (double)g_enterT / ENTER_MS;
    if (t >= 1.0) {
        t = 1.0;
        g_entering = FALSE;
        KillTimer(g_hMain, TMR_ENTER);
        SetTimer(g_hMain, TMR_SHOW, g_enterShowMs, NULL); /* 显示计时，到时开始淡出 */
    }
    e = EaseInOut(t);
    alpha = (int)(g_alphaMax * e + 0.5);
    y = g_enterY0 + (int)((g_enterY1 - g_enterY0) * e + 0.5);
    if (!g_entering)
        y = g_enterY1;
    SetWindowPos(g_hOsd, HWND_TOPMOST, g_enterX, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    g_alpha = alpha;
    OsdCommit(alpha);
}

/* 开始淡出并最终隐藏 */
static void OsdStartFade(void)
{
    if (!g_hOsd || !IsWindowVisible(g_hOsd) || g_fading)
        return;
    g_fading = TRUE;
    SetTimer(g_hMain, TMR_FADE, FADE_MS, NULL);
}

/* ---------------- 主窗口（隐藏，负责消息循环/托盘） ---------------- */

/* ---------------- 自绘右键菜单（圆角 + 阴影 + 多级级联） ---------------- */

static const WCHAR g_menuClassName[] = L"LockOnMenuClass";

/* 主菜单「显示时长/透明度」的动态文本（显示当前值） */
static WCHAR s_menuTimeText[32], s_menuAlphaText[32];

/* 刷新菜单树中所有 checked 状态（打开菜单前调用） */
static void MenuRefreshChecked(void)
{
    MainMenu[0].checked = AutoStartEnabled();
    SubAlpha[0].checked = (g_cfg.alphaPct == 40);
    SubAlpha[1].checked = (g_cfg.alphaPct == 50);
    SubAlpha[2].checked = (g_cfg.alphaPct == 60);
    SubAlpha[3].checked = (g_cfg.alphaPct == 75);
    SubAlpha[4].checked = (g_cfg.alphaPct == 90);
    SubAlpha[5].checked = (g_cfg.alphaPct == 100);
    SubTime[0].checked = (g_cfg.showMs == 300);
    SubTime[1].checked = (g_cfg.showMs == 500);
    SubTime[2].checked = (g_cfg.showMs == 800);
    SubTime[3].checked = (g_cfg.showMs == 1000);
    SubTime[4].checked = (g_cfg.showMs == 1500);
    SubTime[5].checked = (g_cfg.showMs == 2000);
    /* 主菜单项显示当前值：显示时长 (1.0s) / 透明度 (100%) */
    {
        int ti = 0, i;
        for (i = 0; i < 6; i++)
            if (SubTime[i].checked) { ti = i; break; }
        wsprintfW(s_menuTimeText,  L"\u663E\u793A\u65F6\u957F (%ls)", SubTime[ti].text); /* 显示时长 */
        wsprintfW(s_menuAlphaText, L"\u900F\u660E\u5EA6 (%d%%)", g_cfg.alphaPct);      /* 透明度 */
        MainMenu[2].text = s_menuTimeText;
        MainMenu[3].text = s_menuAlphaText;
    }
}

/* 菜单命令处理 */
static void ConfigShowDialog(void); /* 前向声明（定义见设置对话框区） */
static void MenuHandleCommand(UINT id)
{
    switch (id) {
    case IDM_AUTOSTART:
        if (AutoStartEnabled())
            DisableAutoStart();
        else
            EnsureAutoStart();
        break;
    case IDM_POS_FOLLOW:          g_cfg.posMode = POS_FOLLOW;        ConfigSave(); break;
    case IDM_POS_CENTER:          g_cfg.posMode = POS_CENTER;        ConfigSave(); break;
    case IDM_POS_CORNER_TL:       g_cfg.posMode = POS_CORNER; g_cfg.posCorner = CORNER_TL; ConfigSave(); break;
    case IDM_POS_CORNER_TR:       g_cfg.posMode = POS_CORNER; g_cfg.posCorner = CORNER_TR; ConfigSave(); break;
    case IDM_POS_CORNER_BL:       g_cfg.posMode = POS_CORNER; g_cfg.posCorner = CORNER_BL; ConfigSave(); break;
    case IDM_POS_CORNER_BR:       g_cfg.posMode = POS_CORNER; g_cfg.posCorner = CORNER_BR; ConfigSave(); break;
    case IDM_POS_EDGE_TOP:        g_cfg.posMode = POS_EDGE; g_cfg.posEdge = EDGE_TOP;    ConfigSave(); break;
    case IDM_POS_EDGE_LEFT:       g_cfg.posMode = POS_EDGE; g_cfg.posEdge = EDGE_LEFT;   ConfigSave(); break;
    case IDM_POS_EDGE_RIGHT:      g_cfg.posMode = POS_EDGE; g_cfg.posEdge = EDGE_RIGHT;  ConfigSave(); break;
    case IDM_POS_EDGE_BOTTOM:     g_cfg.posMode = POS_EDGE; g_cfg.posEdge = EDGE_BOTTOM; ConfigSave(); break;
    case IDM_ALPHA_40:  g_cfg.alphaPct = 40;  ConfigSave(); break;
    case IDM_ALPHA_50:  g_cfg.alphaPct = 50;  ConfigSave(); break;
    case IDM_ALPHA_60:  g_cfg.alphaPct = 60;  ConfigSave(); break;
    case IDM_ALPHA_75:  g_cfg.alphaPct = 75;  ConfigSave(); break;
    case IDM_ALPHA_90:  g_cfg.alphaPct = 90;  ConfigSave(); break;
    case IDM_ALPHA_100: g_cfg.alphaPct = 100; ConfigSave(); break;
    case IDM_SHOW_300:  g_cfg.showMs = 300;  ConfigSave(); break;
    case IDM_SHOW_500:  g_cfg.showMs = 500;  ConfigSave(); break;
    case IDM_SHOW_800:  g_cfg.showMs = 800;  ConfigSave(); break;
    case IDM_SHOW_1000: g_cfg.showMs = 1000; ConfigSave(); break;
    case IDM_SHOW_1500: g_cfg.showMs = 1500; ConfigSave(); break;
    case IDM_SHOW_2000: g_cfg.showMs = 2000; ConfigSave(); break;
    case IDM_DISP_CAPS:   g_cfg.showCaps = !g_cfg.showCaps;   ConfigSave(); break;
    case IDM_DISP_NUM:    g_cfg.showNum = !g_cfg.showNum;     ConfigSave(); break;
    case IDM_DISP_SCROLL: g_cfg.showScroll = !g_cfg.showScroll; ConfigSave(); break;
    case IDM_DISP_IME:    g_cfg.showIme = !g_cfg.showIme;     ConfigSave(); break;
    case IDM_SETTINGS:
        ConfigShowDialog();
        break;
    case IDM_RESET:
        ConfigSetDefault();
        ConfigSave();
        break;
    case IDM_EXIT:
        DestroyWindow(g_hMain);
        return;
    }
    /* 若气泡可见且位置模式变化，立即按新位置刷新 */
    if (g_hOsd && IsWindowVisible(g_hOsd))
        OsdPosition(g_hOsd);
}

/* 关闭整棵菜单（从最深开始销毁） */
/* 销毁一个菜单窗口及其所有后代（栈中 i..depth-1），并同步清理栈帧 */
static void MenuDestroyWindow(HWND hwnd)
{
    int i, j;
    for (i = 0; i < g_menuDepth; i++) {
        if (g_menuStack[i].hwnd == hwnd) {
            for (j = g_menuDepth - 1; j >= i; j--)
                DestroyWindow(g_menuStack[j].hwnd);
            g_menuDepth = i;
            return;
        }
    }
    DestroyWindow(hwnd); /* 不在栈中（防御），直接销毁 */
}

static void MenuCloseAll(void)
{
    int i;
    for (i = g_menuDepth - 1; i >= 0; i--)
        DestroyWindow(g_menuStack[i].hwnd);
    g_menuDepth = 0;
}

static MenuFrame *MenuFindFrame(HWND hwnd)
{
    int i;
    for (i = 0; i < g_menuDepth; i++)
        if (g_menuStack[i].hwnd == hwnd)
            return &g_menuStack[i];
    return NULL;
}

static void MenuOpenChild(MenuFrame *f, int idx);

/* 菜单窗口过程（同一类，栈内所有层共用） */
static LRESULT CALLBACK MenuWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    MenuFrame *f = MenuFindFrame(hwnd);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc;
        RECT rcClient;
        int i;
        HWND child;

        if (!f) /* 防御：窗口不在栈中（不应发生，销毁路径已同步清理） */
            return DefWindowProcW(hwnd, msg, wp, lp);
        dc = BeginPaint(hwnd, &ps);
        child = f->child;

        GetClientRect(hwnd, &rcClient);

        /* 白色圆角背景（窗口 region 已裁成圆角） */
        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &rcClient, bg);
        DeleteObject(bg);

        /* 细边框 */
        {
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(229, 229, 229));
            HGDIOBJ old = SelectObject(dc, pen);
            HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, 0, 0, rcClient.right - 1, rcClient.bottom - 1,
                      MENU_CORNER * 2, MENU_CORNER * 2);
            SelectObject(dc, oldB);
            SelectObject(dc, old);
            DeleteObject(pen);
        }

        SelectObject(dc, g_menuFont);
        SetBkMode(dc, TRANSPARENT);

        /* 菜单项 */
        for (i = 0; i < f->count; i++) {
            const MenuItem *it = &f->items[i];
            RECT rcItem = { 0, i * MENU_ITEM_H, rcClient.right, (i + 1) * MENU_ITEM_H };

            if (it->text == NULL) {
                /* 分隔线（Win11 更淡） */
                HPEN pen = CreatePen(PS_SOLID, 1, RGB(240, 243, 248));
                HGDIOBJ old = SelectObject(dc, pen);
                MoveToEx(dc, MENU_PAD_X, rcItem.top + MENU_ITEM_H / 2, NULL);
                LineTo(dc, rcClient.right - MENU_PAD_X, rcItem.top + MENU_ITEM_H / 2);
                SelectObject(dc, old);
                DeleteObject(pen);
                continue;
            }

            /* hover 高亮：圆角淡蓝底（Win11 Fluent 选中态） */
            if (f->hover == i) {
                HBRUSH hb = CreateSolidBrush(RGB(232, 240, 254));
                HGDIOBJ old = SelectObject(dc, hb);
                HGDIOBJ oldB = SelectObject(dc, GetStockObject(NULL_BRUSH));
                RoundRect(dc, 3, rcItem.top + 2, rcClient.right - 3, rcItem.bottom - 2,
                          6, 6);
                SelectObject(dc, oldB);
                SelectObject(dc, old);
                DeleteObject(hb);
            }

            /* 勾选/单选标记：主色蓝圆点 + 白色内芯（选中项突出） */
            if (it->checked) {
                int cx = MENU_CHECK_W / 2;
                int cy = rcItem.top + MENU_ITEM_H / 2;
                HBRUSH dot = CreateSolidBrush(RGB(45, 106, 255));
                HGDIOBJ old = SelectObject(dc, dot);
                Ellipse(dc, cx - 4, cy - 4, cx + 4, cy + 4);
                HBRUSH core = CreateSolidBrush(RGB(255, 255, 255));
                SelectObject(dc, core);
                Ellipse(dc, cx - 2, cy - 2, cx + 2, cy + 2);
                SelectObject(dc, old);
                DeleteObject(dot);
                DeleteObject(core);
            }

            /* 文字：选中项加粗+深蓝黑，普通项常规深灰 */
            {
                SIZE sz;
                HGDIOBJ oF = SelectObject(dc,
                    (it->checked && g_menuFontBold) ? g_menuFontBold : g_menuFont);
                SetTextColor(dc, it->checked ? RGB(25, 31, 64) : RGB(51, 55, 64));
                GetTextExtentPoint32W(dc, it->text, (int)wcslen(it->text), &sz);
                TextOutW(dc, MENU_PAD_X + MENU_CHECK_W,
                         rcItem.top + (MENU_ITEM_H - sz.cy) / 2,
                         it->text, (int)wcslen(it->text));
                SelectObject(dc, oF);
            }

            /* 子菜单箭头（柔和深灰三角） */
            if (it->children && it->childCount > 0) {
                int ax = rcClient.right - MENU_ARROW_W / 2;
                int ay = rcItem.top + MENU_ITEM_H / 2;
                POINT tri[3] = { { ax - 3, ay - 3 }, { ax + 2, ay }, { ax - 3, ay + 3 } };
                HBRUSH ar = CreateSolidBrush(RGB(120, 130, 145));
                HGDIOBJ old = SelectObject(dc, ar);
                Polygon(dc, tri, 3);
                SelectObject(dc, old);
                DeleteObject(ar);
            }
        }
        EndPaint(hwnd, &ps);
        (void)child;
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = (int)(short)LOWORD(lp);
        int y = (int)(short)HIWORD(lp);
        int idx;
        const MenuItem *it;
        if (!f)
            return 0;
        idx = y / MENU_ITEM_H;
        if (idx < 0 || idx >= f->count || f->items[idx].text == NULL) {
            /* 分隔线：仅关闭旧子菜单 */
            if (idx != f->hover && f->child) {
                MenuDestroyWindow(f->child);
                f->child = NULL;
            }
            f->hover = idx;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (idx != f->hover) {
            f->hover = idx;
            it = &f->items[idx];
            /* 关闭旧子菜单（若有），再为当前项打开 */
            if (f->child) {
                MenuDestroyWindow(f->child);
                f->child = NULL;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            if (it->children && it->childCount)
                MenuOpenChild(f, idx);
        }
        (void)x;
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP: {
        int y = (int)(short)HIWORD(lp);
        int idx;
        const MenuItem *it;
        if (!f)
            return 0;
        if (msg == WM_LBUTTONUP)
            return 0;
        idx = y / MENU_ITEM_H;
        if (idx < 0 || idx >= f->count)
            return 0;
        it = &f->items[idx];
        if (it->text == NULL)
            return 0;
        if (it->children && it->childCount) {
            MenuOpenChild(f, idx);
            return 0;
        }
        if (it->id != 0) {
            MenuCloseAll();
            MenuHandleCommand(it->id);
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
        MenuCloseAll();
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            MenuCloseAll();
        } else if (wp == VK_UP || wp == VK_DOWN) {
            int step = (wp == VK_UP) ? -1 : 1;
            int n = f ? f->count : 0;
            if (f && n > 0) {
                int i;
                int next;
                for (i = 1; i <= n; i++) {
                    next = (f->hover + i * step + n) % n;
                    if (f->items[next].text != NULL)
                        break;
                }
                if (i <= n) {
                    f->hover = next;
                    if (f->child) { MenuDestroyWindow(f->child); f->child = NULL; }
                    if (f->items[next].children && f->items[next].childCount)
                        MenuOpenChild(f, next);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
        } else if ((wp == VK_RETURN || wp == VK_SPACE) && f && f->hover >= 0
                   && f->hover < f->count) {
            const MenuItem *it = &f->items[f->hover];
            if (it->text != NULL) {
                if (it->children && it->childCount) {
                    MenuOpenChild(f, f->hover);
                } else if (it->id != 0) {
                    MenuCloseAll();
                    MenuHandleCommand(it->id);
                }
            }
        } else if (wp == VK_LEFT && f) {
            /* 返回父级：关闭除根以外的所有层 */
            MenuCloseAll();
        }
        return 0;

    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE)
            MenuCloseAll();
        return 0;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_NCHITTEST:
        return HTCLIENT;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* 创建一个菜单层窗口（x,y 为屏幕坐标，clamp 到工作区内） */
static HWND MenuCreate(HWND parent, const MenuItem *items, int count, int x, int y)
{
    int w = 0, h, i;
    HDC dc;
    HGDIOBJ old;
    SIZE sz;
    HWND hwnd;
    HMONITOR mon;
    MONITORINFO mi;
    RECT work;
    POINT pt = { x, y };
    HRGN rgn;

    /* 宽度：最长文本 + 勾选区 + 箭头 + 留白 */
    dc = GetDC(NULL);
    old = SelectObject(dc, g_menuFont);
    for (i = 0; i < count; i++) {
        if (!items[i].text)
            continue;
        GetTextExtentPoint32W(dc, items[i].text, (int)wcslen(items[i].text), &sz);
        if (sz.cx > w) w = sz.cx;
    }
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    w += MENU_PAD_X * 2 + MENU_CHECK_W + MENU_ARROW_W;
    h = count * MENU_ITEM_H + 2;

    /* 工作区 clamp */
    mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
        if (x + w > work.right)  x = work.right - w;
        if (y + h > work.bottom) y = work.bottom - h;
        if (x < work.left) x = work.left;
        if (y < work.top)  y = work.top;
    }

    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, g_menuClassName, L"",
                           WS_POPUP, x, y, w, h, parent, NULL, g_hInst, NULL);
    if (!hwnd)
        return NULL;

    /* 圆角裁剪 */
    rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, MENU_CORNER * 2, MENU_CORNER * 2);
    SetWindowRgn(hwnd, rgn, TRUE);

    if (g_menuDepth < MENU_MAX_DEPTH) {
        g_menuStack[g_menuDepth].hwnd = hwnd;
        g_menuStack[g_menuDepth].items = items;
        g_menuStack[g_menuDepth].count = count;
        g_menuStack[g_menuDepth].hover = -1;
        g_menuStack[g_menuDepth].width = w;
        g_menuStack[g_menuDepth].child = NULL;
        g_menuDepth++;
    }

    ShowWindow(hwnd, SW_SHOWNA);
    if (g_menuDepth == 1)
        SetForegroundWindow(hwnd); /* 顶层菜单临时激活，便于点击外部自动关闭 */
    return hwnd;
}

/* 在父菜单某项右侧打开子菜单 */
static void MenuOpenChild(MenuFrame *f, int idx)
{
    RECT prc;
    int x, y;
    if (idx < 0 || idx >= f->count || !f->items[idx].children)
        return;
    GetWindowRect(f->hwnd, &prc);
    x = prc.right - 2;
    y = prc.top + idx * MENU_ITEM_H - 2;
    f->child = MenuCreate(f->hwnd, f->items[idx].children,
                          f->items[idx].childCount, x, y);
}

/* 托盘右键：打开主菜单 */
static void ShowTrayMenu(HWND hwnd)
{
    POINT pt;
    MenuRefreshChecked();
    GetCursorPos(&pt);
    MenuCreate(hwnd, MainMenu, (int)(sizeof(MainMenu) / sizeof(MainMenu[0])),
               pt.x, pt.y);
}

static void AddTray(HWND hwnd)
{
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd   = hwnd;
    g_nid.uID    = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 1;
    g_nid.hIcon  = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(1),
                                     IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    lstrcpyW(g_nid.szTip, L"LockOnScreen");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_trayAdded = TRUE;
}

/* 托盘图标与 Caps Lock 状态联动：开=黄色 A(资源 2)，关=灰色 a(资源 3)；
 * 悬停提示文字同步显示。Num/Scroll 不联动。 */
static void UpdateTrayCaps(BOOL on)
{
    HICON hIcon;
    if (!g_trayAdded)
        return;
    hIcon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(on ? 2 : 3),
                              IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!hIcon)
        return;
    g_nid.uFlags = NIF_ICON | NIF_TIP;
    g_nid.hIcon = hIcon;
    if (on)
        lstrcpyW(g_nid.szTip, L"\u5927\u5199\u5F00\u00B7LockOnScreen");  /* 大写开·LockOnScreen */
    else
        lstrcpyW(g_nid.szTip, L"\u5927\u5199\u5173\u00B7LockOnScreen");  /* 大写关·LockOnScreen */
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void RemoveTray(void)
{
    if (g_trayAdded) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_trayAdded = FALSE;
    }
}

/* ---------------- 设置对话框 ---------------- */

/* ---------------- 显示设置对话框（现代风格） ---------------- */

/* 现代配色 */
#define CLR_BG       RGB(246, 247, 251) /* 窗口背景 */
#define CLR_CARD     RGB(255, 255, 255) /* 卡片背景 */
#define CLR_PRIMARY  RGB(45, 106, 255)  /* 主色蓝 */
#define CLR_PRIM_HOT RGB(76, 128, 255)  /* 主色悬停 */
#define CLR_PRIM_DN  RGB(36, 88, 220)   /* 主色按下 */
#define CLR_TEXT     RGB(30, 34, 42)    /* 主文字 */
#define CLR_SUB      RGB(140, 145, 158) /* 次要文字 */
#define CLR_LINE     RGB(226, 229, 237) /* 卡片描边/分隔线 */
#define CLR_BTN      RGB(255, 255, 255) /* 次级按钮底 */
#define CLR_BTN_HOT  RGB(238, 241, 248) /* 次级按钮悬停 */

/* 设置对话框控件 ID */
enum {
    CID_POS_FOLLOW = 2000, CID_POS_CENTER, CID_POS_CORNER, CID_POS_EDGE,
    CID_CORNER_SEL, CID_EDGE_SEL,
    CID_ALPHA_BASE = 2100, /* 2100..2105 */
    CID_SHOWMS_BASE = 2200, /* 2200..2205 */
    CID_DISP_CAPS = 2300, CID_DISP_NUM, CID_DISP_SCROLL, CID_DISP_IME,
    CID_BTN_DEFAULT = 2400, CID_BTN_OK, CID_BTN_CANCEL,
};

/* 布局尺寸（客户区 460x540，大字体版） */
#define CFG_W 460
#define CFG_H 540
#define CFG_CARD_X 12
#define CFG_CARD_W (CFG_W - 24)
#define CFG_TITLE_Y_OFF 34   /* 卡片内标题下控件的起始 Y 偏移 */

static LOConfig s_cfgDlg;    /* 对话框编辑副本 */
static LOConfig s_cfgBackup; /* 打开时备份（点选即存后取消不再还原，保留结构） */
static HBRUSH g_cardBrush = NULL;

/* 卡片区域（用于 WM_PAINT 自绘） */
static RECT CardRect(int index)
{
    static const int tops[] = { 12, 140, 240, 340 };
    static const int hs[]   = { 120, 92, 92, 120 };
    RECT r = { CFG_CARD_X, tops[index], CFG_CARD_X + CFG_CARD_W, tops[index] + hs[index] };
    return r;
}

/* 从配置填充控件 */
static void ConfigToControls(HWND hDlg)
{
    CheckRadioButton(hDlg, CID_POS_FOLLOW, CID_POS_EDGE,
                     CID_POS_FOLLOW + s_cfgDlg.posMode);
    SendMessageW(GetDlgItem(hDlg, CID_CORNER_SEL), CB_SETCURSEL, (WPARAM)s_cfgDlg.posCorner, 0);
    SendMessageW(GetDlgItem(hDlg, CID_EDGE_SEL),   CB_SETCURSEL, (WPARAM)s_cfgDlg.posEdge,   0);
    int alphaIdx = (s_cfgDlg.alphaPct == 40) ? 0 : (s_cfgDlg.alphaPct == 50) ? 1 :
                   (s_cfgDlg.alphaPct == 60) ? 2 : (s_cfgDlg.alphaPct == 75) ? 3 :
                   (s_cfgDlg.alphaPct == 90) ? 4 : 5;
    CheckRadioButton(hDlg, CID_ALPHA_BASE, CID_ALPHA_BASE + 5, CID_ALPHA_BASE + alphaIdx);
    int showIdx = (s_cfgDlg.showMs == 300) ? 0 : (s_cfgDlg.showMs == 500) ? 1 :
                  (s_cfgDlg.showMs == 800) ? 2 : (s_cfgDlg.showMs == 1000) ? 3 :
                  (s_cfgDlg.showMs == 1500) ? 4 : 5;
    CheckRadioButton(hDlg, CID_SHOWMS_BASE, CID_SHOWMS_BASE + 5, CID_SHOWMS_BASE + showIdx);
    CheckDlgButton(hDlg, CID_DISP_CAPS,   s_cfgDlg.showCaps   ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, CID_DISP_NUM,    s_cfgDlg.showNum    ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, CID_DISP_SCROLL, s_cfgDlg.showScroll ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, CID_DISP_IME,    s_cfgDlg.showIme    ? BST_CHECKED : BST_UNCHECKED);
}

/* 从控件读取配置（到 s_cfgDlg） */
static void ControlsToConfig(HWND hDlg)
{
    if (IsDlgButtonChecked(hDlg, CID_POS_FOLLOW) == BST_CHECKED) s_cfgDlg.posMode = POS_FOLLOW;
    else if (IsDlgButtonChecked(hDlg, CID_POS_CENTER) == BST_CHECKED) s_cfgDlg.posMode = POS_CENTER;
    else if (IsDlgButtonChecked(hDlg, CID_POS_CORNER) == BST_CHECKED) s_cfgDlg.posMode = POS_CORNER;
    else if (IsDlgButtonChecked(hDlg, CID_POS_EDGE)   == BST_CHECKED) s_cfgDlg.posMode = POS_EDGE;
    s_cfgDlg.posCorner = (int)SendMessageW(GetDlgItem(hDlg, CID_CORNER_SEL), CB_GETCURSEL, 0, 0);
    s_cfgDlg.posEdge   = (int)SendMessageW(GetDlgItem(hDlg, CID_EDGE_SEL),   CB_GETCURSEL, 0, 0);
    static const int alphas[] = { 40, 50, 60, 75, 90, 100 };
    for (int i = 0; i < 6; i++) if (IsDlgButtonChecked(hDlg, CID_ALPHA_BASE + i) == BST_CHECKED) { s_cfgDlg.alphaPct = alphas[i]; break; }
    static const int shows[] = { 300, 500, 800, 1000, 1500, 2000 };
    for (int i = 0; i < 6; i++) if (IsDlgButtonChecked(hDlg, CID_SHOWMS_BASE + i) == BST_CHECKED) { s_cfgDlg.showMs = shows[i]; break; }
    s_cfgDlg.showCaps   = (IsDlgButtonChecked(hDlg, CID_DISP_CAPS)   == BST_CHECKED);
    s_cfgDlg.showNum    = (IsDlgButtonChecked(hDlg, CID_DISP_NUM)    == BST_CHECKED);
    s_cfgDlg.showScroll = (IsDlgButtonChecked(hDlg, CID_DISP_SCROLL) == BST_CHECKED);
    s_cfgDlg.showIme    = (IsDlgButtonChecked(hDlg, CID_DISP_IME)    == BST_CHECKED);
}

/* 即时应用：控件 → g_cfg（不保存）。preview=TRUE 时显示预览气泡
 * （显示项勾选变化仅更新配置，不弹预览；预览时长用新选的 showMs） */
static void CfgApplyLive(HWND hDlg, BOOL preview)
{
    ControlsToConfig(hDlg);
    g_cfg = s_cfgDlg;
    if (!preview)
        return;
    if (g_cfg.showCaps)
        OsdShow(0, g_last[0], (UINT)s_cfgDlg.showMs);
    else if (g_cfg.showIme)
        OsdShowIme(g_imeChinese, g_imeFullShape);
    else if (g_cfg.showNum)
        OsdShow(1, g_last[1], (UINT)s_cfgDlg.showMs);
}

/* 自绘圆角按钮 */
static void DrawRoundBtn(HDC hdc, const RECT *prc, const WCHAR *text,
                         BOOL primary, BOOL hover, BOOL pressed)
{
    RECT r = *prc;
    COLORREF fill = primary ? (pressed ? CLR_PRIM_DN : (hover ? CLR_PRIM_HOT : CLR_PRIMARY))
                            : (pressed ? CLR_BTN_HOT : (hover ? CLR_BTN_HOT : CLR_BTN));
    HBRUSH br = CreateSolidBrush(fill);
    HRGN  rgn = CreateRoundRectRgn(r.left, r.top, r.right + 1, r.bottom + 1, 9, 9);
    FillRgn(hdc, rgn, br);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? fill : CLR_LINE);
    HGDIOBJ oPen = SelectObject(hdc, pen);
    HGDIOBJ oBr  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, 9, 9);
    SelectObject(hdc, oPen);
    SelectObject(hdc, oBr);
    DeleteObject(pen);
    DeleteObject(rgn);
    DeleteObject(br);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, primary ? RGB(255, 255, 255) : CLR_TEXT);
    DrawTextW(hdc, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

/* 自绘卡片背景（含标题），WM_PAINT 中调用 */
static void DrawCards(HDC hdc)
{
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < 4; i++) {
        RECT rc = CardRect(i);
        HBRUSH br = CreateSolidBrush(CLR_CARD);
        HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1, rc.bottom + 1, 10, 10);
        FillRgn(hdc, rgn, br);
        HPEN pen = CreatePen(PS_SOLID, 1, CLR_LINE);
        HGDIOBJ oPen = SelectObject(hdc, pen);
        HGDIOBJ oBr  = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
        SelectObject(hdc, oPen);
        SelectObject(hdc, oBr);
        DeleteObject(pen);
        DeleteObject(rgn);
        DeleteObject(br);
        /* 标题行：大号加粗标题 + 右侧小字提示「实时预览」（同一行，不占纵向空间） */
        static const WCHAR *titles[] = {
            L"\u663E\u793A\u4F4D\u7F6E",   /* 显示位置 */
            L"\u900F\u660E\u5EA6",         /* 透明度 */
            L"\u663E\u793A\u65F6\u957F",   /* 显示时长 */
            L"\u663E\u793A\u9879",         /* 显示项 */
        };
        static const WCHAR *hints[] = {
            L"\u5B9E\u65F6\u9884\u89C8",   /* 实时预览 */
            L"\u5B9E\u65F6\u9884\u89C8",
            L"\u5B9E\u65F6\u9884\u89C8",
            L"",                          /* 显示项勾选不触发预览，不加提示 */
        };
        SIZE tsz = { 0, 0 };
        {
            RECT tr = { rc.left + 16, rc.top + 6, rc.right - 16, rc.top + 32 };
            SetTextColor(hdc, CLR_TEXT);
            HGDIOBJ oF = g_titleFont ? SelectObject(hdc, g_titleFont) : NULL;
            DrawTextW(hdc, titles[i], -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (oF) {
                GetTextExtentPoint32W(hdc, titles[i], (int)wcslen(titles[i]), &tsz);
                SelectObject(hdc, oF);
            }
        }
        if (hints[i][0]) {
            RECT hr = { rc.left + 16 + tsz.cx + 12, rc.top + 6, rc.right - 16, rc.top + 32 };
            SetTextColor(hdc, CLR_SUB);
            HGDIOBJ oH = g_cfgFont ? SelectObject(hdc, g_cfgFont) : NULL;
            DrawTextW(hdc, hints[i], -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (oH) SelectObject(hdc, oH);
        }
    }
}

static LRESULT CALLBACK CfgDlgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        /* 背景由类 hbrBackground(CLR_BG) 自动擦除，此处补画卡片 */
        DrawCards(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
        if (msg == WM_CTLCOLORBTN) {
            /* 选中项文字高亮：单选/复选选中 → 主色蓝，未选中 → 常规深色 */
            HWND ctl = (HWND)lp;
            int cid = GetDlgCtrlID(ctl);
            BOOL isSel = FALSE;
            if (cid >= CID_POS_FOLLOW && cid <= CID_POS_EDGE)
                isSel = (SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            else if (cid >= CID_ALPHA_BASE && cid <= CID_ALPHA_BASE + 5)
                isSel = (SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            else if (cid >= CID_SHOWMS_BASE && cid <= CID_SHOWMS_BASE + 5)
                isSel = (SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            else if (cid >= CID_DISP_CAPS && cid <= CID_DISP_IME)
                isSel = (SendMessageW(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            SetTextColor((HDC)wp, isSel ? CLR_PRIMARY : CLR_TEXT);
        } else {
            SetTextColor((HDC)wp, CLR_TEXT);
        }
        SetBkColor((HDC)wp, CLR_CARD);
        if (!g_cardBrush)
            g_cardBrush = CreateSolidBrush(CLR_CARD);
        return (LRESULT)g_cardBrush;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT *)lp;
        if (di->CtlType != ODT_BUTTON)
            break;
        WCHAR txt[64];
        GetWindowTextW(di->hwndItem, txt, 64);
        BOOL hover   = (di->itemState & ODS_HOTLIGHT) != 0;
        BOOL pressed = (di->itemState & ODS_SELECTED) != 0;
        DrawRoundBtn(di->hDC, &di->rcItem, txt, di->CtlID == CID_BTN_OK, hover, pressed);
        return 1;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);
        BOOL live = FALSE;
        BOOL preview = TRUE;
        if (code == BN_CLICKED) {
            if (id == CID_BTN_OK || id == IDOK) {
                ControlsToConfig(hwnd);
                g_cfg = s_cfgDlg;
                ConfigSave();
                DestroyWindow(hwnd);
            } else if (id == CID_BTN_CANCEL || id == IDCANCEL) {
                /* 改动已实时保存（点选即存），取消仅关闭、不还原 */
                DestroyWindow(hwnd);
            } else if (id == CID_BTN_DEFAULT) {
                LOConfig def;
                ConfigFillDefault(&def);
                s_cfgDlg = def;
                ConfigToControls(hwnd);
                live = TRUE;
            } else if (id >= CID_DISP_CAPS && id <= CID_DISP_IME) {
                /* 显示项勾选：仅更新配置，不弹预览气泡 */
                live = TRUE;
                preview = FALSE;
            } else {
                live = TRUE; /* 位置/透明度/时长 → 即时生效并预览 */
            }
        } else if (code == CBN_SELCHANGE) {
            /* 点击角落/边缘下拉 → 联动选中对应位置单选 */
            if (id == CID_CORNER_SEL)
                CheckRadioButton(hwnd, CID_POS_FOLLOW, CID_POS_EDGE, CID_POS_CORNER);
            else if (id == CID_EDGE_SEL)
                CheckRadioButton(hwnd, CID_POS_FOLLOW, CID_POS_EDGE, CID_POS_EDGE);
            live = TRUE; /* 角落/边缘下拉 → 即时生效并预览 */
        }
        if (live) {
            CfgApplyLive(hwnd, preview);
            ConfigSave(); /* 点选即保存（含恢复默认） */
        }
        return 0;
    }

    case WM_CLOSE:
        /* 改动已实时保存，直接关闭（不还原） */
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_hCfgDlg = NULL;
        if (g_hMain) EnableWindow(g_hMain, TRUE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* 创建单个子控件（交互控件自动加 WS_TABSTOP；字体用设置界面大字号） */
static HWND CfgCtrl(HWND parent, LPCWSTR cls, LPCWSTR text, DWORD style, int x, int y, int w, int h, WORD id)
{
    DWORD st = style | WS_TABSTOP;
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | st, x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
    if (c) SendMessageW(c, WM_SETFONT, (WPARAM)(g_cfgFont ? g_cfgFont : GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return c;
}

static void ConfigShowDialog(void)
{
    MenuCloseAll();
    if (g_hCfgDlg) { SetForegroundWindow(g_hCfgDlg); return; }

    static BOOL clsReg = FALSE;
    if (!clsReg) {
        WNDCLASSW wc;
        memset(&wc, 0, sizeof(wc));
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = CfgDlgWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(CLR_BG);
        wc.lpszClassName = L"LockOnScreenCfg";
        RegisterClassW(&wc);
        clsReg = TRUE;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    int w = CFG_W + 16, h = CFG_H + 39; /* 客户区 + 边框/标题栏 */
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"LockOnScreenCfg", L"\u663E\u793A\u8BBE\u7F6E",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        (sw - w) / 2, (sh - h) / 2, w, h, g_hMain, NULL, g_hInst, NULL);
    if (!dlg) return;
    g_hCfgDlg = dlg;
    s_cfgDlg = g_cfg;
    s_cfgBackup = g_cfg;

    /* 卡片1：显示位置 */
    CfgCtrl(dlg, L"Button", L"\u8DDF\u8E2A\u9F20\u6807/\u5149\u6807", BS_AUTORADIOBUTTON | WS_GROUP, 28, 44, 170, 26, CID_POS_FOLLOW);
    CfgCtrl(dlg, L"Button", L"\u5C4F\u5E55\u6B63\u4E2D",              BS_AUTORADIOBUTTON,          236, 44, 110, 26, CID_POS_CENTER);
    CfgCtrl(dlg, L"Button", L"\u5C4F\u5E55\u89D2\u843D",              BS_AUTORADIOBUTTON,           28, 78,  96, 26, CID_POS_CORNER);
    CfgCtrl(dlg, L"Button", L"\u5C4F\u5E55\u8FB9\u7F18",              BS_AUTORADIOBUTTON,          236, 78,  96, 26, CID_POS_EDGE);
    HWND cbCorner = CfgCtrl(dlg, L"ComboBox", NULL, CBS_DROPDOWNLIST, 128, 76, 100, 130, CID_CORNER_SEL);
    HWND cbEdge   = CfgCtrl(dlg, L"ComboBox", NULL, CBS_DROPDOWNLIST, 328, 76, 100, 130, CID_EDGE_SEL);
    static const WCHAR *corners[] = { L"\u5DE6\u4E0A\u89D2", L"\u53F3\u4E0A\u89D2", L"\u5DE6\u4E0B\u89D2", L"\u53F3\u4E0B\u89D2" };
    static const WCHAR *edges[]   = { L"\u9876\u90E8\u5C45\u4E2D", L"\u5DE6\u4FA7\u5C45\u4E2D", L"\u53F3\u4FA7\u5C45\u4E2D", L"\u5E95\u90E8\u5C45\u4E2D" };
    for (int i = 0; i < 4; i++) {
        SendMessageW(cbCorner, CB_ADDSTRING, 0, (LPARAM)corners[i]);
        SendMessageW(cbEdge,   CB_ADDSTRING, 0, (LPARAM)edges[i]);
    }
    SendMessageW(cbCorner, WM_SETFONT, (WPARAM)g_cfgFont, TRUE);
    SendMessageW(cbEdge,   WM_SETFONT, (WPARAM)g_cfgFont, TRUE);

    /* 卡片2：透明度 */
    static const WCHAR *alphas[] = { L"40%", L"50%", L"60%", L"75%", L"90%", L"100%" };
    for (int i = 0; i < 6; i++)
        CfgCtrl(dlg, L"Button", alphas[i], BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0), 28 + i * 64, 174, 58, 26, CID_ALPHA_BASE + i);

    /* 卡片3：显示时长 */
    static const WCHAR *shows[] = { L"0.3s", L"0.5s", L"0.8s", L"1.0s", L"1.5s", L"2.0s" };
    for (int i = 0; i < 6; i++)
        CfgCtrl(dlg, L"Button", shows[i], BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0), 28 + i * 64, 274, 58, 26, CID_SHOWMS_BASE + i);

    /* 卡片4：显示项 */
    CfgCtrl(dlg, L"Button", L"\u5927\u5199\u9501\u5B9A (Caps)",   BS_AUTOCHECKBOX | WS_GROUP, 28, 374, 180, 26, CID_DISP_CAPS);
    CfgCtrl(dlg, L"Button", L"\u6570\u5B57\u9501\u5B9A (Num)",    BS_AUTOCHECKBOX,            236, 374, 180, 26, CID_DISP_NUM);
    CfgCtrl(dlg, L"Button", L"\u6EDA\u52A8\u9501\u5B9A (Scroll)", BS_AUTOCHECKBOX,             28, 406, 180, 26, CID_DISP_SCROLL);
    CfgCtrl(dlg, L"Button", L"\u8F93\u5165\u6CD5\u72B6\u6001",    BS_AUTOCHECKBOX,            236, 406, 180, 26, CID_DISP_IME);

    /* 底部按钮（自绘圆角） */
    CfgCtrl(dlg, L"Button", L"\u6062\u590D\u9ED8\u8BA4", BS_OWNERDRAW, 28,  488, 110, 38, CID_BTN_DEFAULT);
    CfgCtrl(dlg, L"Button", L"\u786E\u5B9A",             BS_OWNERDRAW, 312, 488, 120, 38, CID_BTN_OK);
    CfgCtrl(dlg, L"Button", L"\u5173\u95ED",               BS_OWNERDRAW, 168, 488, 120, 38, CID_BTN_CANCEL);

    ConfigToControls(dlg);
    if (g_hMain) EnableWindow(g_hMain, FALSE);
    SetForegroundWindow(dlg);
    MSG msg;
    while (g_hCfgDlg && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g_cardBrush) { DeleteObject(g_cardBrush); g_cardBrush = NULL; }
}
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        AddTray(hwnd);
        SetTimer(hwnd, TMR_POLL, POLL_MS, NULL);
        g_hKbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbHookProc, g_hInst, 0); /* 输入法切换热键 */
        return 0;

    case WM_TIMER:
        if (wp == TMR_POLL) {
            /* 三键轮询检测状态翻转（按显示项配置决定是否弹气泡） */
            int i;
            BOOL capsOn = IsKeyOn(g_vk[0]);
            if (capsOn != g_last[0]) {
                g_last[0] = capsOn;
                if (g_cfg.showCaps)
                    OsdShow(0, capsOn, (UINT)g_cfg.showMs);
                UpdateTrayCaps(capsOn); /* 托盘图标联动：仅 Caps */
            }
            for (i = 1; i < KEY_COUNT; i++) {
                BOOL on = IsKeyOn(g_vk[i]);
                if (on != g_last[i]) {
                    g_last[i] = on;
                    if ((i == 1 && g_cfg.showNum) || (i == 2 && g_cfg.showScroll))
                        OsdShow(i, on, (UINT)g_cfg.showMs);
                }
            }
            ImePoll(); /* 输入法状态（TSF 事件 + IMM 轮询兜底） */
            KbCheckShiftCandidate(); /* 确认单独 Shift 是否中英切换 */
        } else if (wp == TMR_ENTER) {
            OsdEnterStep();
        } else if (wp == TMR_SHOW) {
            KillTimer(hwnd, TMR_SHOW);
            OsdStartFade();
        } else if (wp == TMR_FADE) {
            g_alpha -= FADE_STEP;
            if (g_alpha <= 0) {
                g_alpha = 0;
                ShowWindow(g_hOsd, SW_HIDE);
                KillTimer(hwnd, TMR_FADE);
                g_fading = FALSE;
            } else {
                OsdCommit(g_alpha);
            }
        }
        return 0;

    case WM_APP + 1: /* 托盘回调 */
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        } else if (lp == WM_LBUTTONDBLCLK) {
            /* 双击托盘图标：打开显示设置 */
            ConfigShowDialog();
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TMR_POLL);
        KillTimer(hwnd, TMR_ENTER);
        KillTimer(hwnd, TMR_SHOW);
        KillTimer(hwnd, TMR_FADE);
        if (g_hKbHook) { UnhookWindowsHookEx(g_hKbHook); g_hKbHook = NULL; }
        MenuCloseAll();
        RemoveTray();
        if (g_osdBmp) { DeleteObject(g_osdBmp); g_osdBmp = NULL; }
        if (g_osdDC)  { DeleteDC(g_osdDC);      g_osdDC  = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---------------- 入口 ---------------- */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    WNDCLASSW wc;
    MSG msg;
    HANDLE hMutex;

    (void)hPrev; (void)lpCmd; (void)nShow;
    g_hInst = hInstance;

    GdiplusInit();

    /* 单实例 */
    hMutex = CreateMutexW(NULL, FALSE, g_mutexName);
    if (!hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        GdiplusShutdown(g_gdiplusToken);
        return 0;
    }

    /* 加载配置（ini 优先 + 注册表兜底） */
    ConfigLoad();

    /* TSF 输入法监测初始化 */
    TsfInit();

    /* 管理员运行时自动开启开机自启 */
    EnsureAutoStart();

    /* 初始化三键当前状态（避免启动首轮轮询误报翻转） */
    {
        int i;
        for (i = 0; i < KEY_COUNT; i++)
            g_last[i] = IsKeyOn(g_vk[i]);
    }

    /* 菜单字体与窗口类（自绘菜单） */
    g_menuFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (!g_menuFont)
        g_menuFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    /* 设置界面控件字体（更大一号） */
    g_cfgFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (!g_cfgFont)
        g_cfgFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    /* 菜单选中项加粗字体 */
    g_menuFontBold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (!g_menuFontBold)
        g_menuFontBold = g_menuFont;
    /* 设置界面卡片标题字体（大号加粗） */
    g_titleFont = CreateFontW(-17, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    if (!g_titleFont)
        g_titleFont = g_menuFont;
    {
        WNDCLASSW mc;
        memset(&mc, 0, sizeof(mc));
        mc.style         = CS_DROPSHADOW; /* 系统阴影（圆角由 region 裁出） */
        mc.lpfnWndProc   = MenuWndProc;
        mc.cbClsExtra    = 0;
        mc.cbWndExtra    = 0;
        mc.hInstance     = hInstance;
        mc.hIcon         = NULL;
        mc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
        mc.hbrBackground = NULL;
        mc.lpszMenuName  = NULL;
        mc.lpszClassName = g_menuClassName;
        RegisterClassW(&mc);
    }

    wc.style         = 0;
    wc.lpfnWndProc   = MainWndProc;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hInstance     = hInstance;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName  = NULL;
    wc.lpszClassName = L"CapsMainClass";
    RegisterClassW(&wc);

    g_hMain = CreateWindowExW(0, L"CapsMainClass", L"LockOnScreen",
                              0, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);

    /* 启动提示：显示一次 Caps Lock 当前状态，时长 1.2s（比切换提示更久，便于看到） */
    if (g_cfg.showCaps)
        OsdShow(0, g_last[0], STARTUP_MS);
    UpdateTrayCaps(g_last[0]); /* 托盘图标同步 Caps 状态 */

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(hMutex);
    TsfShutdown();
    if (g_menuFontBold && g_menuFontBold != g_menuFont)
        DeleteObject(g_menuFontBold);
    if (g_menuFont && g_menuFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(g_menuFont);
    if (g_titleFont && g_titleFont != g_menuFont)
        DeleteObject(g_titleFont);
    if (g_cfgFont && g_cfgFont != (HFONT)GetStockObject(DEFAULT_GUI_FONT))
        DeleteObject(g_cfgFont);
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}
