# LockOnScreen

Windows 键盘切换键（Caps Lock / Num Lock / Scroll Lock + 输入法）状态屏幕提示工具。

**一句话定位**：后台常驻托盘，按下切换键时弹出 GDI+ 抗锯齿圆角气泡（开/关状态）；切换中/英文输入法时同样弹出提示；托盘图标随 Caps Lock 状态联动（黄 A=开/灰 a=关）。

## 怎么跑起来

### 直接使用
```bash
dist/LockOnScreen.exe
```
- 双击即可运行（无窗口，托盘可见）
- 右键托盘菜单：开机自启 / 显示时长 / 透明度 / 显示设置（图形化配置窗口）/ 退出

### 自行构建
需要 MinGW-w64（x86, GCC 16+, 含 g++）与 PowerShell：
```bash
powershell -File scripts/make_ico.ps1   # 生成 app.ico
windres res/app.rc -O coff -o res/app.o
g++ -Wall -Wextra -O2 -mwindows -s -o dist/LockOnScreen.exe src/main.c res/app.o -lgdiplus -lmsctf -limm32 -lole32
```
> 注意：main.c 是 C 源码但用 **g++** 编译（走 C++ 编译路径）。C++ 下不存在 tentative definition，`static` 全局变量只能声明一次（重复前向声明 + 定义会报 redefinition）。

## 技术栈
- **语言**：C（Win32 API）+ C++（GDI+ 抗锯齿绘制）
- **编译**：g++ (-mwindows -lgdiplus -lmsctf -limm32 -lole32)
- **依赖**：仅系统 DLL（kernel32/user32/gdi32/gdiplus/shell32/advapi32/msctf/ole32/imm32）
- **输入法监测**：TSF 事件通知（Win8+）+ IMM 轮询兜底（Win7 及全/半角检测）
- **运行**：Windows 7/10/11（32/64 位均可）

## 目录与约定
```
LockOnScreen/
├── src/main.c              # 全部源码（约 2460 行，单文件）
├── res/app.ico             # 应用图标（ICONDIR 多尺寸）
├── res/app.rc              # 版本+图标资源
├── res/caps_on.ico         # 托盘开状态（黄 A）
├── res/caps_off.ico        # 托盘关状态（灰 a）
├── res/img/                # README 截图（8 张状态图）+ 演示视频
├── scripts/make_ico.ps1    # ICO 生成脚本
├── scripts/check_ico.ps1   # ICO 像素检验
├── scripts/test_render.cpp # OSD 渲染验证（GDI+ 逻辑独立绘制到 PNG）
├── scripts/test_tsf.cpp    # TSF+IMM 输入法检测验证工具
├── scripts/test_ime_live.cpp # 实时输入法状态诊断（ImTip 方式，验证通道有效性）
├── scripts/test_ime_diag.cpp / test_tray_diag.cpp / test_indicator_*.cpp  # 通道探索期诊断（历史保留）
├── CHANGELOG.md            # 版本更新记录（v1.0.0 → v2.0.0）
├── README.md               # 用户文档（含演示视频/截图/构建说明）
├── .gitignore             # dist/ 参考目录/测试exe 忽略
├── LICENSE                # AGPL-3.0
└── dist/LockOnScreen.exe  # 发布成品（git 忽略）
```

## 版本与状态
- **v1.0.0**（已发布）：三键 OSD、托盘 Caps 联动、开机自启、启动 1.2s/切换 0.5s、多显示器适配（气泡跟随鼠标所在屏）
- **v2.0.0**（已发布，GitHub Releases 含 exe 附件）：输入法状态监测 + Win11 Fluent 重构 + 三键专属色 + 文本输入场景过滤，详见 CHANGELOG.md

### v2.0.0 关键技术实现（维护时勿破坏）
- **输入法判定**（ImTip/aardio 方式，实测校准）：`WM_IME_CONTROL` 取 convMode 按位判定——`chinese = !(conv&0x100) && (conv&3)!=0`、`full = conv&8`。**opened 通道实测在切换/按键瞬间抖动归零，不可作为中英必要条件**（否则误判英文，即「识别反了」根因）；conv 位稳定可靠，仅按 conv 判定
- **强制重读**：前台窗口变化 / TSF 事件 → 强制重读真实状态（解决「同一窗口切换输入法后提示相反」）
- **文本输入场景过滤**：被动触发的 IME 提示仅在前台窗口有输入光标（`GetGUIThreadInfo.hwndCaret`）时弹出；主动热键（Shift/Ctrl+Space/Shift+Space）任何场景都反馈
- **热键翻转兜底**：Shift ≥160ms 且无同按其他键才判定中英切换防漂移；Win+Space 切换系统语言布局，不走翻转（由 TSF 语言事件处理）
- **三层状态融合**：任务栏指示器 > IME(conv) > TSF 语言 > 热键翻转

## 维护与交接要点
- **发布流程**：本地提交 → tag → GitHub 推送 → Releases（可参考仓库历史与 CHANGELOG 格式）。国内直连 GitHub 常 SSL 失败，需经代理（`git -c http.proxy=...`）；HTTPS push 需 PAT（GitHub 已取消密码登录）
- **验证手段（无 GUI 依赖）**：`PrintWindow(PW_RENDERFULLCONTENT)` 抓分层窗口（OSD）；`GetWindowDC + BitBlt` 抓自绘窗口（设置界面）；`CopyFromScreen` 受 DPI/遮挡影响不可靠。诊断工具日志写 `test_ime_live.log`
- **已知取舍（接受现状）**：无数字签名（SmartScreen「未知发布者」，用户明确放弃）；AGPL-3.0；少量可接受小泄漏（`wc.hbrBackground` 画刷不删等）；菜单/设置对话框打开期间 ImePoll 跳过（避免误触发）
- **待验证**：Win7 实机（IMM 兜底通道）、搜狗/QQ/百度第三方输入法兼容性精测
- **仓库**：[github.com/mrdalin/LockOnScreen](https://github.com/mrdalin/LockOnScreen)（main 分支，Release v2.0.0）