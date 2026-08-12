# LockOnScreen

键盘切换键状态提示工具 —— 按下 **Caps Lock / Num Lock / Scroll Lock** 时，在屏幕下方居中显示当前开关状态气泡，约 0.5 秒后自动淡出（首次启动提示显示 1.2 秒）。

## 功能

| 键 | 开 | 关 |
|---|---|---|
| Caps Lock | `A` 大写开 | `a` 大写关 |
| Num Lock | `N` 数字开 | `n` 数字关 |
| Scroll Lock | `⇅` 滚动开 | `⇅` 滚动关 |

（图标统一科技蓝圆角方块背景+白色字符；状态由右侧文字颜色区分：开=暖黄、关=亮灰）

- 后台运行，托盘图标常驻（右键菜单：开机自启开关 / 退出）
- **多显示器适配**：气泡跟随鼠标所在屏幕底部居中
- **托盘图标与 Caps Lock 状态联动**：大写开=黄色 `A`(科技蓝底)，大写关=灰色 `a`(科技蓝底)，悬停提示同步显示
- 启动时自动显示一次 Caps Lock 当前状态提示
- OSD 气泡显示约 0.5 秒后淡出（启动提示 1.2 秒）；GDI+ 抗锯齿圆角、垂直渐变背景、深色细描边、加粗文字
- **图标**：科技蓝圆角方块 + 白色加粗字符，三边等距布局
- 开机自启：**以管理员身份运行** → 写入 `HKLM`（所有用户生效）；普通权限运行 → 写入 `HKCU`（当前用户生效）。值均为本 exe 完整路径
- 单文件、绿色免安装，可放任意目录运行（建议放固定位置，自启依赖路径）

## 使用

1. 双击 `LockOnScreen.exe` 即可运行（无窗口，托盘图标显示当前大小写状态：黄 A=开/灰 a=关，科技蓝底）
2. 按 Caps / Num / Scroll 任意键，屏幕下方出现状态气泡（约 0.5 秒后淡出）
3. 右键托盘图标可关闭开机自启或退出程序

## 兼容性与体积

- 32 位单文件，Windows 7 / 10 / 11（32 位与 64 位均可）运行，仅依赖系统自带 DLL（含 GDI+，即 gdiplus.dll，Win7 自带）
- 成品约 167 KB（远小于 1 MB 限制）

## Win7 实机验证清单（本工具在 Win10 沙箱验证，以下建议在目标机器确认）

- [ ] Win7 32 位 / 64 位：双击运行、按键气泡显示正常
- [ ] Win10 / Win11：运行与气泡显示正常
- [ ] 管理员身份运行后，`HKLM\Software\Microsoft\Windows\CurrentVersion\Run` 出现 `LockOnScreen`，重启后自动启动
- [ ] 普通权限运行后，`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 出现对应值
- [ ] 托盘图标随 Caps Lock 状态变化（黄 A=开/灰 a=关，科技蓝底，悬停文字同步）
- [ ] 托盘右键菜单：开机自启开关 / 退出均正常
- [ ] 启动时提示一次 Caps Lock 当前状态（大写开/大写关）
- [ ] 多显示器时气泡跟随鼠标所在屏幕底部居中

## 关于杀毒/SmartScreen 提示

本程序由官方 GCC 工具链干净编译，不加壳、不混淆、无注入等敏感行为，主流杀软一般不报。
Microsoft SmartScreen 对**无数字签名**的新程序仍可能提示「未知发布者 / 已阻止」，本项目已决定接受此现状，不再计划购买签名证书。

## 构建（可选）

需要 MinGW-w64（x86，GCC 16+，含 g++）与 PowerShell：

```bash
powershell -File scripts/make_ico.ps1   # 生成 res/app.ico（标准格式）
windres res/app.rc -O coff -o res/app.o
g++ -Wall -Wextra -O2 -mwindows -s -o dist/LockOnScreen.exe src/main.c res/app.o -lgdiplus
```

## 目录结构

```
LockOnScreen/
├── src/main.c             # 全部源码（约 610 行）
├── res/app.ico            # 应用图标（标准 ICO，由脚本生成）
├── res/app.rc             # 图标 + 版本资源
├── res/caps_on.ico        # 托盘黄 A（Caps 开）
├── res/caps_off.ico       # 托盘灰 a（Caps 关）
├── scripts/make_ico.ps1   # ICO 生成脚本
├── scripts/check_ico.ps1  # ICO 像素检查脚本
├── scripts/test_render.cpp # OSD 渲染验证工具
├── AGENTS.md            # Agent 规则文件（五要素）
├── dist/                  # 成品 exe
└── README.md
```
