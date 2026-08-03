# LookAway

LookAway 是一个面向 Windows 的轻量级护眼计时器。它通过系统键鼠空闲时长判断你是否仍在工作，累计 45 分钟有效工作时间后显示置顶休息提醒。

## 功能

- 累计 45 分钟有效工作时间后提醒，不把离开电脑的时间算入工作时长
- 键鼠空闲超过 1 分钟时自动暂停累计，恢复操作后自动继续
- 连续空闲达到 5 分钟时清空本轮进度，回来后从新的 45 分钟周期开始
- 提供 5 分钟休息倒计时和“5 分钟后提醒”
- 有效工作计时时播放 `working.gif`，暂停、空闲和休息时播放 `waiting.gif`
- 支持手动暂停、继续和重新计时
- 关闭或最小化主窗口后常驻系统托盘
- 不记录键盘输入、鼠标位置或任何工作内容，只读取 Windows 提供的空闲时长

## 直接运行

双击 `dist\LookAway.exe`。仓库当前构建出的程序也位于 `build\LookAway.exe`。

最小化或关闭窗口不会退出程序。要完全退出，请右键单击系统托盘中的 LookAway 图标并选择“退出”。

## 从源码构建

需要 Windows 10/11、CMake 3.20 及以上版本，以及 MSVC 或 MinGW C++ 编译器。在 PowerShell 中运行：

```powershell
.\build.ps1
```

脚本会构建应用、运行测试，并将可执行文件安装到 `dist\LookAway.exe`。

也可以手动执行：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

LookAway 只提供定时提醒，不能替代医生的检查和治疗建议。
