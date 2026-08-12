/*
 * LockOnScreen - 键盘切换键状态提示 (Caps / Num / Scroll)
 * 纯 Win32 API + GDI+ 抗锯齿绘制，32 位单文件，兼容 Windows 7/10/11
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string.h>
#include <objidl.h>
#include <gdiplus.h>
using namespace Gdiplus;

#define KEY_COUNT   3
#define POLL_MS     30      /* 轮询间隔 */
#define SHOW_MS     500     /* OSD 显示时长(ms)后开始淡出 */
#define FADE_MS     30      /* 淡出步进间隔 */
#define FADE_STEP   60      /* 每步 alpha 减少量（快速淡出） */
#define OSD_ALPHA   200     /* 初始不透明度 */
#define BOTTOM_GAP  48      /* OSD 距屏幕底部距离 */
#define OSD_CORNER  6       /* 圆角半径（小一倍） */
#define OSD_H       48      /* OSD 高度 */
#define OSD_TEXT_SIZE 18    /* 文字字号(px) */
#define OSD_ICON_SIZE 26    /* 图标字号(px) */
#define OSD_GAP     8       /* 统一间距：图标-左/图标-文字/文字-右 三边相等 */
#define OSD_ICON_W  30      /* 蓝色图标方块尺寸(正方形) */
#define OSD_ICON_BLUE_R 0   /* 科技蓝背景 RGB */
#define OSD_ICON_BLUE_G 120
#define OSD_ICON_BLUE_B 215

/* 三个切换键 */
static const UINT g_vk[KEY_COUNT]  = { VK_CAPITAL, VK_NUMLOCK, VK_SCROLL };
static const WCHAR *g_iconOn[KEY_COUNT]  = { L"A", L"N", L"\u21C5" };  /* ⇅ */
static const WCHAR *g_iconOff[KEY_COUNT] = { L"a", L"n", L"\u21C5" };  /* ⇅ 开/关同图标，用颜色区分 */
static const WCHAR *g_textOn[KEY_COUNT]  = { L"\u5927\u5199\u5F00",  /* 大写开 */
                                             L"\u6570\u5B57\u5F00",  /* 数字开 */
                                             L"\u6EDA\u52A8\u5F00" }; /* 滚动开 */
static const WCHAR *g_textOff[KEY_COUNT] = { L"\u5927\u5199\u5173",  /* 大写关 */
                                             L"\u6570\u5B57\u5173",  /* 数字关 */
                                             L"\u6EDA\u52A8\u5173" }; /* 滚动关 */

static BOOL  g_last[KEY_COUNT];      /* 上一轮状态 */
static int   g_showKey = -1;         /* 当前显示的键 */
static BOOL  g_showOn  = FALSE;      /* 当前显示的状态 */
static HWND  g_hOsd    = NULL;       /* OSD 窗口 */
static HWND  g_hMain   = NULL;       /* 隐藏主窗口(消息循环/托盘) */
static HINSTANCE g_hInst = NULL;
static BOOL  g_fading  = FALSE;      /* 是否在淡出 */
static int   g_alpha   = 0;
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

enum { IDM_AUTOSTART = 1, IDM_EXIT, IDM_SEP };
enum { TMR_POLL = 1, TMR_FADE = 2, TMR_SHOW = 3 };

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

/* GDI+ 绘制 OSD 内容到 DIB：抗锯齿圆角气泡（渐变+投影+高光描边）+ 图标 + 文字 */
static void OsdRender(void)
{
    if (!g_osdBits || g_showKey < 0 || g_showKey >= KEY_COUNT)
        return;
    Bitmap bmp(g_osdW, g_osdH, g_osdW * 4, PixelFormat32bppARGB, (BYTE *)g_osdBits);
    Graphics g(&bmp);
    WCHAR iconBuf[8], textBuf[32];
    /* 蓝色图标方块：垂直居中，水平位于左侧 GAP 处 */
    REAL boxX = (REAL)OSD_GAP;
    REAL boxY = (REAL)((g_osdH - OSD_ICON_W) / 2);
    REAL textX  = boxX + (REAL)OSD_ICON_W + (REAL)OSD_GAP;
    RectF layout, bounds;

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.Clear(Color(0, 0, 0, 0)); /* 全透明底 */

    /* 圆角矩形路径 */
    GraphicsPath path;
    {
        INT d = OSD_CORNER * 2;
        path.AddArc(0, 0, d, d, 180, 90);
        path.AddArc((INT)g_osdW - d, 0, d, d, 270, 90);
        path.AddArc((INT)g_osdW - d, (INT)g_osdH - d, d, d, 0, 90);
        path.AddArc(0, (INT)g_osdH - d, d, d, 90, 90);
        path.CloseFigure();
    }

    /* 对称投影：主体下移 2px 的半透明黑圆角矩形（四角完全一致） */
    {
        GraphicsPath sh;
        sh.AddPath(&path, TRUE);
        {
            Matrix m(1.0f, 0, 0, 1.0f, 0, 2.0f); /* 平移 (0,2) */
            sh.Transform(&m);
        }
        SolidBrush shBrush(Color(60, 0, 0, 0));
        g.FillPath(&shBrush, &sh);
    }

    /* 背景：垂直渐变（顶部略亮 → 底部略暗，立体感） */
    {
        LinearGradientBrush bgBrush(Point(0, 0), Point(0, g_osdH),
                                    Color(255, 82, 86, 96), Color(255, 54, 56, 64));
        g.FillPath(&bgBrush, &path);
    }

    /* 顶部高光描边（半透明白 1px，磨砂玻璃质感） */
    {
        Pen edgePen(Color(72, 255, 255, 255), 1.0f);
        g.DrawPath(&edgePen, &path);
    }

    /* 图标：蓝色圆角方块（科技蓝）+ 白色加粗字符，方块垂直居中 */
    lstrcpyW(iconBuf, g_showOn ? g_iconOn[g_showKey] : g_iconOff[g_showKey]);
    {
        /* 蓝色方块背景（垂直微渐变 + 顶部高光，精致光泽） */
        GraphicsPath ib;
        {
            INT bs = OSD_ICON_W, d = 4; /* 蓝色方块圆角，与气泡圆角协调 */
            ib.AddArc((INT)boxX, (INT)boxY, d, d, 180, 90);
            ib.AddArc((INT)boxX + bs - d, (INT)boxY, d, d, 270, 90);
            ib.AddArc((INT)boxX + bs - d, (INT)boxY + bs - d, d, d, 0, 90);
            ib.AddArc((INT)boxX, (INT)boxY + bs - d, d, d, 90, 90);
            ib.CloseFigure();
        }
        LinearGradientBrush ibBrush(Point((INT)boxX, (INT)boxY),
                                    Point((INT)boxX, (INT)(boxY + OSD_ICON_W)),
                                    Color(255, OSD_ICON_BLUE_R + 30, OSD_ICON_BLUE_G + 25, OSD_ICON_BLUE_B),
                                    Color(255, OSD_ICON_BLUE_R, OSD_ICON_BLUE_G, OSD_ICON_BLUE_B));
        g.FillPath(&ibBrush, &ib);
        {
            Pen ibEdge(Color(90, 255, 255, 255), 1.0f);
            g.DrawPath(&ibEdge, &ib);
        }

        /* 白色加粗字符，方块内水平垂直居中（StringFormat 矩形内居中，消除字体 leading 偏差） */
        FontFamily ffIcon(L"Segoe UI Symbol");
        Font fIcon(&ffIcon, (REAL)OSD_ICON_SIZE, FontStyleBold, UnitPixel);
        StringFormat sf;
        sf.SetAlignment(StringAlignmentCenter);
        sf.SetLineAlignment(StringAlignmentCenter);
        SolidBrush fgBrush(Color(255, 255, 255, 255));
        g.DrawString(iconBuf, -1, &fIcon,
                     RectF(boxX, boxY, (REAL)OSD_ICON_W, (REAL)OSD_ICON_W),
                     &sf, &fgBrush);
    }

    /* 文字（右侧，垂直居中；加粗 18px 保证清晰）；开=黄色，关=亮灰 */
    lstrcpyW(textBuf, g_showOn ? g_textOn[g_showKey] : g_textOff[g_showKey]);
    {
        FontFamily ffText(L"Microsoft YaHei");
        Font fText(&ffText, (REAL)OSD_TEXT_SIZE, FontStyleBold, UnitPixel);
        SolidBrush fgBrush(g_showOn ? Color(255, 255, 210, 70)
                                    : Color(255, 240, 240, 240));
        layout.X = textX; layout.Y = 0;
        layout.Width = (REAL)(g_osdW - textX - OSD_GAP); layout.Height = (REAL)g_osdH;
        g.MeasureString(textBuf, -1, &fText, layout, &bounds);
        g.DrawString(textBuf, -1, &fText,
                     PointF(textX, (REAL)g_osdH / 2 - bounds.Height / 2 - bounds.Y),
                     &fgBrush);
    }

    OsdPremultiply();
}

/* 定位：屏幕下方居中 */
static void OsdPosition(HWND hwnd)
{
    RECT rc;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, HWND_TOPMOST,
                 sw / 2 - (rc.right - rc.left) / 2,
                 sh - (rc.bottom - rc.top) - BOTTOM_GAP,
                 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
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
    int w, h, textW;

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

    /* 按内容测量窗口尺寸：用与绘制相同的 GDI+ 字体测量（消除左右留白不一致） */
    {
        Bitmap tmp(1, 1, PixelFormat32bppARGB);
        Graphics g(&tmp);
        FontFamily ffText(L"Microsoft YaHei");
        Font fText(&ffText, (REAL)OSD_TEXT_SIZE, FontStyleBold, UnitPixel);
        RectF mlayout(0, 0, 1000, 100), mbounds;
        REAL maxW = 0;
        int i;
        for (i = 0; i < KEY_COUNT; i++) {
            g.MeasureString(g_textOn[i], -1, &fText, mlayout, &mbounds);
            if (mbounds.Width > maxW) maxW = mbounds.Width;
            g.MeasureString(g_textOff[i], -1, &fText, mlayout, &mbounds);
            if (mbounds.Width > maxW) maxW = mbounds.Width;
        }
        textW = (int)(maxW + 2); /* +2 防测量/绘制舍入差异 */
    }
    h = OSD_H;
    w = OSD_GAP + OSD_ICON_W + OSD_GAP + textW + OSD_GAP;

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

static void OsdShow(int key, BOOL on)
{
    g_showKey = key;
    g_showOn  = on;
    if (!g_hOsd)
        OsdCreate();
    if (!g_hOsd)
        return;

    /* 取消正在进行的淡出 */
    if (g_fading) {
        KillTimer(g_hMain, TMR_FADE);
        g_fading = FALSE;
    }
    g_alpha = OSD_ALPHA;
    OsdRender();      /* 重绘内容（抗锯齿圆角/文字） */
    OsdPosition(g_hOsd);
    ShowWindow(g_hOsd, SW_SHOWNA); /* 确保可见（UpdateLayeredWindow 不改变可见性） */
    OsdCommit(g_alpha); /* 提交并显示 */
    SetTimer(g_hMain, TMR_SHOW, SHOW_MS, NULL); /* 显示计时，到时开始淡出 */
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

static void ShowTrayMenu(HWND hwnd)
{
    HMENU hm = CreatePopupMenu();
    POINT pt;
    BOOL as = AutoStartEnabled();

    AppendMenuW(hm, MF_STRING | (as ? MF_CHECKED : 0), IDM_AUTOSTART,
                L"\u5F00\u673A\u81EA\u542F");               /* 开机自启 */
    AppendMenuW(hm, MF_SEPARATOR, IDM_SEP, NULL);
    AppendMenuW(hm, MF_STRING, IDM_EXIT,
                L"\u9000\u51FA");                           /* 退出 */

    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hm, TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hm);
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

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        AddTray(hwnd);
        SetTimer(hwnd, TMR_POLL, POLL_MS, NULL);
        return 0;

    case WM_TIMER:
        if (wp == TMR_POLL) {
            /* 三键轮询检测状态翻转 */
            int i;
            BOOL capsOn = IsKeyOn(g_vk[0]);
            if (capsOn != g_last[0]) {
                g_last[0] = capsOn;
                OsdShow(0, capsOn);
                UpdateTrayCaps(capsOn); /* 托盘图标联动：仅 Caps */
            }
            for (i = 1; i < KEY_COUNT; i++) {
                BOOL on = IsKeyOn(g_vk[i]);
                if (on != g_last[i]) {
                    g_last[i] = on;
                    OsdShow(i, on);
                }
            }
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
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_AUTOSTART:
            if (AutoStartEnabled())
                DisableAutoStart();
            else
                EnsureAutoStart();
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TMR_POLL);
        KillTimer(hwnd, TMR_SHOW);
        KillTimer(hwnd, TMR_FADE);
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

    /* 管理员运行时自动开启开机自启 */
    EnsureAutoStart();

    /* 初始化三键当前状态（避免启动首轮轮询误报翻转），
     * 并主动显示一次 Caps Lock 状态作为启动提示（更常用） */
    {
        int i;
        for (i = 0; i < KEY_COUNT; i++)
            g_last[i] = IsKeyOn(g_vk[i]);
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

    /* 启动提示：显示一次 Caps Lock 当前状态 */
    OsdShow(0, g_last[0]);
    UpdateTrayCaps(g_last[0]); /* 托盘图标同步 Caps 状态 */

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(hMutex);
    GdiplusShutdown(g_gdiplusToken);
    return (int)msg.wParam;
}