# LockOnScreen

Windows 键盘切换键（Caps Lock / Num Lock / Scroll Lock）状态屏幕提示工具。

**一句话定位**：后台常驻托盘，按下切换键时在屏幕底部居中显示 GDI+ 抗锯齿圆角气泡（开/关状态），托盘图标随 Caps Lock 状态联动（黄 A=开/灰 a=关）。

## 怎么跑起来

### 直接使用
```bash
dist/LockOnScreen.exe
```
- 双击即可运行（无窗口，托盘可见）
- 开机自启：管理员运行→写 HKLM，普通写 HKCU；右键菜单可开关

### 自行构建
需要 MinGW-w64（x86, GCC 16+, 含 g++）与 PowerShell：
```bash
powershell -File scripts/make_ico.ps1   # 生成 app.ico
windres res/app.rc -O coff -o res/app.o
g++ -Wall -Wextra -O2 -mwindows -s -o dist/LockOnScreen.exe src/main.c res/app.o -lgdiplus
```

## 技术栈
- **语言**：C（Win32 API）+ C++（GDI+ 抗锯齿绘制）
- **编译**：g++ (-mwindows -lgdiplus)
- **依赖**：仅系统 DLL（kernel32/user32/gdi32/gdiplus/shell32/advapi32）
- **运行**：Windows 7/10/11（32/64 位均可）

## 目录与约定
```
LockOnScreen/
├── src/main.c              # 全部源码
├── res/app.ico             # 应用图标（ICONDIR 多尺寸）
├── res/app.rc              # 版本+图标资源
├── res/caps_on.ico         # 托盘开状态（黄 A）
├── res/caps_off.ico        # 托盘关状态（灰 a）
├── scripts/make_ico.ps1    # ICO 生成脚本
├── scripts/check_ico.ps1   # ICO 像素检验
├── scripts/test_render.cpp # OSD 渲染验证
├── dist/LockOnScreen.exe   # 发布成品
├── .gitignore             # dist/ res/app.o 忽略
├── LICENSE                # AGPL-3.0
└── README.md
```

## 当前状态与下一步
- **v1.0.0**（已发布）：三键 OSD、托盘 Caps 联动、开机自启、启动 1.2s/切换 0.5s、多显示器适配（气泡跟随鼠标所在屏）
- **待验证**：Win7 真机实机测试（沙箱为 Win10）
- **已决定不做**：数字签名（SmartScreen「未知发布者」提示接受现状，用户明确放弃）
- **仓库**：[github.com/mrdalin/LockOnScreen](https://github.com/mrdalin/LockOnScreen)