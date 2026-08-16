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

## 技术栈
- **语言**：C（Win32 API）+ C++（GDI+ 抗锯齿绘制）
- **编译**：g++ (-mwindows -lgdiplus -lmsctf -limm32 -lole32)
- **依赖**：仅系统 DLL（kernel32/user32/gdi32/gdiplus/shell32/advapi32/msctf/ole32/imm32）
- **输入法监测**：TSF 事件通知（Win8+）+ IMM 轮询兜底（Win7 及全/半角检测）
- **运行**：Windows 7/10/11（32/64 位均可）

## 目录与约定
```
LockOnScreen/
├── src/main.c              # 全部源码（约 1640 行）
├── res/app.ico             # 应用图标（ICONDIR 多尺寸）
├── res/app.rc              # 版本+图标资源
├── res/caps_on.ico         # 托盘开状态（黄 A）
├── res/caps_off.ico        # 托盘关状态（灰 a）
├── scripts/make_ico.ps1    # ICO 生成脚本
├── scripts/check_ico.ps1   # ICO 像素检验
├── scripts/test_render.cpp # OSD 渲染验证
├── scripts/test_tsf.cpp    # TSF+IMM 输入法检测验证工具
├── dist/LockOnScreen.exe   # 发布成品
├── .gitignore             # dist/ res/app.o 忽略
├── LICENSE                # AGPL-3.0
└── README.md
```

## 当前状态与下一步
- **v1.0.0**（已发布）：三键 OSD、托盘 Caps 联动、开机自启、启动 1.2s/切换 0.5s、多显示器适配（气泡跟随鼠标所在屏）
- **v2.0.0**（当前）：输入法状态监测（**实时读取**：ImTip/aardio 方式 `WM_IME_CONTROL` 取 convMode 按位判定中英/全角——`(conv&3)&&!(conv&0x100)` 中文、`conv&8` 全角，**opened 通道实测抖动不可靠故不用**；前台窗口/输入法切换时强制重读；**兜底**：键盘钩子热键翻转防漂移——Shift ≥160ms、Win+Space 不走翻转；TSF 语言事件） + 输入法气泡 + 自绘右键菜单（圆角阴影，仅保留 开机自启/显示时长/透明度/显示设置/退出）+ 可配置气泡位置（跟随光标/屏幕正中/角落/边缘）/透明度/时长 + 动画入场（默认位置：屏幕正中）+ **显示设置对话框**（现代风格：浅色背景/圆角卡片/自绘蓝色主按钮；**卡片标题大号加粗** + 右侧「实时预览」小字提示；**选中单选/复选文字变主色蓝**高亮；集中配置显示位置/透明度/显示时长/显示项；调整即时生效并气泡实时预览，确定保存、取消还原）+ **Win11 Fluent 风格**（**OSD 气泡**：白色渐变+大圆角 14+多层柔和阴影+主色蓝图标方块+开=深蓝/关=灰文字；**右键菜单**：淡蓝 hover 圆角底+主色蓝勾选圆点+选中项加粗+更淡分隔线）+ 三键专属色（Caps=黄/Num=蓝/Scroll=红，关=灰+×标记）+ 气泡宽度按内容自适应 + **文本输入场景过滤**（被动输入法提示仅在有 caret 的窗口弹出，非输入窗口如解压/看图器不打扰；主动热键始终反馈）
- **待验证**：Win7 实机测试（IMM 兜底通道）、搜狗/QQ/百度等第三方输入法兼容性
- **已决定不做**：数字签名（SmartScreen「未知发布者」提示接受现状，用户明确放弃）
- **仓库**：[github.com/mrdalin/LockOnScreen](https://github.com/mrdalin/LockOnScreen)