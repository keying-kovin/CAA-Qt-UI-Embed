# CAA 对话框嵌入 Qt 树表控件完整实施方案

## 1. 文档目的

本文档说明如何把现有 Qt 树表控件移植到 CAA 项目 `V6AddinCmdTest.m` 中，并嵌入 CAA 的 `CATDlgDialog` 对话框。

目标是：

- 最外层窗口、标题栏、关闭行为由 CAA 的 `CATDlgDialog` 管理。
- 对话框客户区中的树表完全由 Qt 管理。
- 保留当前 Qt 树表的多级表头、树形节点、行号、排序、列宽调整、行高调整、进度绘制和滚动能力。
- CAA 与 Qt 只通过纯 C 接口和 Win32 `HWND` 交互。
- Qt 源码独立编译，CAA 的 `mkmk` 只负责链接，不直接编译 Qt 头文件。

本文档针对当前工作区中的 B421/Qt 5.9/MSVC2015 x64 结构编写。PDF 示例使用的是 B426/Qt 6/MSVC2022，原理相同，但库路径、编译器版本和 Qt 库名必须替换为当前实际环境。

## 2. 当前工程现状

当前相关目录：

```text
D:/MyJob/NewProductV6/NewV6WorkFrm/V6AddinCmdTest.m/
├── src/
│   ├── TestDlg.DSGen              CAA 对话框设计及用户代码模板
│   └── TestCmd.cpp                CAA 命令类
├── LocalGenerated/win_b64/
│   ├── TestDlg.h                  DSGen 生成的头文件
│   └── TestDlg.cpp                DSGen 生成的实现文件
├── LocalInterfaces/
│   ├── QtButtonEmbed.h             现有 Qt 按钮嵌入接口
│   └── treetablewidget.h           树表头文件
├── qt_src/
│   ├── QtButtonEmbed.cpp           现有 Qt 按钮嵌入实现
│   └── treetablewidget.cpp         树表实现
├── build_qt.bat                    Qt 独立编译脚本
└── Imakefile.mk                    CAA 链接配置
```

现有工程中有一个需要修正的使用方式：

```cpp
m_treeTable = new TreeTableWidget(this);
setCentralWidget(m_treeTable);
```

这段代码不能用于 CAA，因为 `TestDlg` 继承自 `CATDlgDialog`，不是 `QWidget`，也没有 `setCentralWidget()`。正确做法是：

1. CAA 获取自己的 Win32 窗口句柄。
2. Qt 创建树表并取得自己的 Win32 窗口句柄。
3. Qt 窗口设置为 `WS_CHILD`。
4. 使用 Win32 `SetParent()` 把 Qt 窗口挂到 CAA 窗口下面。
5. CAA 只保存一个不透明句柄，不直接持有 Qt C++ 对象。

## 3. 总体架构

```text
┌──────────────────────────────────────────────┐
│ CAA 主程序                                   │
│                                              │
│  TestCmd                                      │
│    └── TestDlg : CATDlgDialog                │
│          └── CAA 原生 HWND                    │
│                │                              │
│                │ SetParent                    │
│                ▼                              │
│          Qt TreeTableWidget 原生 HWND         │
│                │                              │
│                ├── MultiLevelHeader           │
│                ├── TreeTableContentView       │
│                └── TreeVerticalHeader         │
└──────────────────────────────────────────────┘

CAA 编译单元                         Qt 独立编译单元
TestDlg.cpp                          QtTreeTableEmbed.cpp
TestCmd.cpp                          treetablewidget.cpp
                                     moc_treetablewidget.cpp
        │                                      │
        └──── QtTreeTableEmbed.h 纯 C 接口 ────┘
```

### 3.1 为什么使用纯 C 接口

CAA 工程和 Qt 工程之间不应直接暴露 `QWidget`、`QTreeWidget` 或 Qt 模板类型，原因如下：

- CAA 的头文件宏、编译选项和 Qt 头文件可能冲突。
- CAA 的 `mkmk` 可能给编译器追加与 Qt 不兼容的选项。
- Qt 类包含元对象、信号槽和 ABI 信息，跨模块直接传递容易产生链接和运行时问题。
- 纯 C 接口只传递 `void*`、整数和 C 字符串，CAA 侧不需要知道 Qt 内部实现。

因此 CAA 只调用以下动作：初始化、创建、调整尺寸、销毁。所有树表功能仍留在 Qt 模块中。

## 4. 文件结构调整

建议最终结构如下：

```text
V6AddinCmdTest.m/
├── LocalInterfaces/
│   ├── QtTreeTableEmbed.h           新增，CAA 与 Qt 共用的纯 C 接口
│   ├── QtButtonEmbed.h              可保留，现有按钮功能继续使用
│   └── treetablewidget.h            Qt 树表声明，仅由 Qt 源码使用
├── qt_src/
│   ├── QtTreeTableEmbed.cpp         新增，Qt 树表创建和 HWND 嵌入
│   ├── treetablewidget.cpp         现有树表实现
│   └── moc_treetablewidget.cpp      由 moc 自动生成
├── src/
│   ├── TestDlg.DSGen                修改 UserCode 区域
│   ├── TestCmd.cpp                  保留尺寸通知和命令逻辑
│   └── QtTreeTableTypes.h           可选，CAA 侧尺寸常量
├── LocalGenerated/win_b64/
│   ├── TestDlg.h                    DSGen 生成，不直接手工维护
│   └── TestDlg.cpp                  DSGen 生成，不直接手工维护
├── build_qt.bat                     修改为编译完整 Qt 模块
└── Imakefile.mk                     修改链接库和 Qt 依赖
```

注意：`LocalGenerated/win_b64/TestDlg.h` 和 `TestDlg.cpp` 是生成文件。修改必须优先放入 `src/TestDlg.DSGen` 的 `UserCode` 区域，否则重新运行 DSGen 后会丢失。

## 5. 第一步：定义纯 C 接口

文件：`LocalInterfaces/QtTreeTableEmbed.h`

```cpp
#ifndef QT_TREE_TABLE_EMBED_H
#define QT_TREE_TABLE_EMBED_H

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 Qt。整个 CAA 进程中只执行一次。
void QtEnsureInitialized(void);

// 创建树表，并把它嵌入 parentHwnd 指向的 CAA 窗口。
// 返回值是 Qt 内部对象的不透明句柄，CAA 不得强制转换和访问其内容。
void *QtCreateTreeTable(void *parentHwnd,
                        int x,
                        int y,
                        int width,
                        int height);

// 调整已经创建的树表位置和大小。
void QtResizeTreeTable(void *handle,
                       int x,
                       int y,
                       int width,
                       int height);

// 销毁树表并释放 Qt 对象。
void QtDestroyTreeTable(void *handle);

#ifdef __cplusplus
}
#endif

#endif // QT_TREE_TABLE_EMBED_H
```

### 5.1 接口设计理由

`parentHwnd` 使用 `void*` 而不是在头文件中直接写 `HWND`，是为了让接口保持 C 兼容，CAA 侧只需要把 `GetWindowHandle()` 的返回值传进来。

`handle` 不是 `QWidget*`。它只是一个句柄，CAA 不能调用 Qt 方法，也不能直接 `delete`。所有 Qt 对象生命周期必须由 `QtDestroyTreeTable()` 管理。

## 6. 第二步：实现 Qt 独立模块

文件：`qt_src/QtTreeTableEmbed.cpp`

下面代码是核心结构，具体类名需要和当前树表实现保持一致。

```cpp
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <windows.h>

#include "QtTreeTableEmbed.h"
#include "treetablewidget.h"

namespace {

QApplication *g_qtApplication = nullptr;
UINT_PTR g_qtEventTimer = 0;
bool g_creatingTreeTable = false;

// 通过 Win32 定时器手动泵送 Qt 事件。
// CAA 宿主通常没有调用 QApplication::exec()，没有事件泵送时树表无法响应鼠标和重绘。
VOID CALLBACK QtEventPumpProc(HWND, UINT, UINT_PTR, DWORD)
{
    if (g_creatingTreeTable)
        return;

    if (QApplication::instance())
        QApplication::processEvents(QEventLoop::AllEvents, 5);
}

// 用 GetWindowLongPtr/SetWindowLongPtr，保证 x64 下窗口样式操作不会截断指针。
bool PrepareAsChildWindow(HWND childHwnd, HWND parentHwnd)
{
    if (!childHwnd || !parentHwnd || !IsWindow(childHwnd) || !IsWindow(parentHwnd))
        return false;

    LONG_PTR style = GetWindowLongPtr(childHwnd, GWL_STYLE);
    style = (style & ~(WS_POPUP | WS_OVERLAPPEDWINDOW)) | WS_CHILD;
    SetWindowLongPtr(childHwnd, GWL_STYLE, style);

    LONG_PTR exStyle = GetWindowLongPtr(childHwnd, GWL_EXSTYLE);
    exStyle &= ~(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW);
    SetWindowLongPtr(childHwnd, GWL_EXSTYLE, exStyle);

    SetLastError(ERROR_SUCCESS);
    HWND oldParent = SetParent(childHwnd, parentHwnd);
    if (!oldParent && GetLastError() != ERROR_SUCCESS)
        return false;

    return true;
}

} // namespace

void QtEnsureInitialized(void)
{
    if (g_qtApplication)
        return;

    QApplication *existing = qobject_cast<QApplication *>(QApplication::instance());
    if (existing) {
        g_qtApplication = existing;
        return;
    }

    // 作为插件运行，避免 Qt 修改 CAA 进程级别的全局状态。
    QApplication::setAttribute(Qt::AA_PluginApplication, true);

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    // CAA 自己管理 DPI，禁止 Qt 在宿主进程中重新设置 DPI 感知状态。
    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);
#endif

    static int argc = 1;
    static char applicationName[] = "CAAQtTreeTable";
    static char *argv[] = { applicationName, nullptr };

    g_qtApplication = new QApplication(argc, argv);

    // CAA 没有 Qt 的 exec()，使用 Win32 Timer 驱动 Qt 事件处理。
    g_qtEventTimer = SetTimer(nullptr, 0, 16, QtEventPumpProc);
}

void *QtCreateTreeTable(void *parentHwnd,
                        int x,
                        int y,
                        int width,
                        int height)
{
    HWND hostHwnd = static_cast<HWND>(parentHwnd);
    if (!hostHwnd || !IsWindow(hostHwnd))
        return nullptr;

    QtEnsureInitialized();
    if (!g_qtApplication)
        return nullptr;

    g_creatingTreeTable = true;

    TreeTableWidget *treeTable = new TreeTableWidget(nullptr);
    treeTable->loadDemoData();
    treeTable->setWindowFlags(Qt::FramelessWindowHint);
    treeTable->setAttribute(Qt::WA_NativeWindow, true);
    treeTable->setGeometry(x, y, width, height);

    HWND treeHwnd = reinterpret_cast<HWND>(treeTable->winId());
    if (!PrepareAsChildWindow(treeHwnd, hostHwnd)) {
        delete treeTable;
        g_creatingTreeTable = false;
        return nullptr;
    }

    // SetParent 可能改变 Qt 内部几何状态，因此再次设置几何参数。
    treeTable->setGeometry(x, y, width, height);

    // 只有已经是 WS_CHILD 后才能 show，避免 CAA Watchdog 把它识别成新的顶级窗口。
    treeTable->show();

    // Qt show() 可能覆盖位置，最后使用 Win32 强制定位。
    MoveWindow(treeHwnd, x, y, width, height, TRUE);
    ShowWindow(treeHwnd, SW_SHOW);

    g_creatingTreeTable = false;
    return treeTable;
}

void QtResizeTreeTable(void *handle,
                       int x,
                       int y,
                       int width,
                       int height)
{
    TreeTableWidget *treeTable = static_cast<TreeTableWidget *>(handle);
    if (!treeTable)
        return;

    treeTable->setGeometry(x, y, width, height);
    HWND treeHwnd = reinterpret_cast<HWND>(treeTable->winId());
    if (treeHwnd && IsWindow(treeHwnd))
        MoveWindow(treeHwnd, x, y, width, height, TRUE);
}

void QtDestroyTreeTable(void *handle)
{
    TreeTableWidget *treeTable = static_cast<TreeTableWidget *>(handle);
    if (!treeTable)
        return;

    HWND treeHwnd = reinterpret_cast<HWND>(treeTable->winId());
    if (treeHwnd && IsWindow(treeHwnd))
        SetParent(treeHwnd, nullptr);

    delete treeTable;
}
```

### 6.1 为什么不使用 `QWindow::fromWinId()`

当前 CAA/Qt 混合窗口场景中，不建议使用：

```cpp
QWindow::fromWinId(parentHwnd);
QWidget::createWindowContainer(...);
```

原因是 Qt 外部窗口容器可能重新修改父窗口样式和子窗口层级，导致 CAA Watchdog、坐标或可见性异常。直接通过 Win32 调整 `WS_CHILD` 并调用 `SetParent()`，窗口层级关系更明确。

### 6.2 为什么 `show()` 必须放在 `SetParent()` 之后

如果 Qt 控件在 `SetParent()` 之前显示，Windows 会把它当作一个独立顶级窗口。CAA 宿主可能检测到异常顶级窗口并触发保护逻辑。先取得 HWND、改为子窗口、重新挂载，最后再显示，可以避免这个问题。

## 7. 第三步：修改 CAA 对话框成员

修改源文件：`src/TestDlg.DSGen` 的 `UserCode` 区域。

### 7.1 ClassIncludes

```cpp
#include <windows.h>
#include "CATDlgContainer.h"
#include "QtTreeTableEmbed.h"
```

CAA 侧只包含纯 C 接口，不要包含 `treetablewidget.h`。尤其不要把 Qt C++ 类声明放进 `extern "C"`，那会导致 C++ 类、信号槽和 Qt 元对象声明处于错误的语言链接环境。

### 7.2 ClassMembers

```cpp
public:
    void *m_qtTreeTableHandle;

    static TestDlg *s_pendingDialog;
    static int s_retryCount;
    static UINT_PTR s_retryTimer;

    void EmbedQtTreeTable();
    bool TryCreateQtTreeTable();
    void ResizeQtTreeTable();
    void DestroyQtTreeTable();
```

### 7.3 DialogConstructor

```cpp
m_qtTreeTableHandle = nullptr;
```

构造阶段只初始化句柄，不创建 Qt 控件。原因是此时 CAA 对话框的 Win32 窗口可能还没有创建，调用 `GetWindowHandle()` 可能返回空指针。

### 7.4 DialogDestructor

```cpp
DestroyQtTreeTable();
```

销毁时必须通过纯 C 接口删除 Qt 对象，不得直接 `delete` 一个 `void*` 或强制转换为 `QWidget*`。

## 8. 第四步：实现 CAA 延迟创建和尺寸同步

下面代码放入 `UserImplementCode`。

```cpp
TestDlg *TestDlg::s_pendingDialog = nullptr;
int TestDlg::s_retryCount = 0;
UINT_PTR TestDlg::s_retryTimer = 0;

static VOID CALLBACK QtTreeTableRetryProc(HWND, UINT, UINT_PTR, DWORD)
{
    if (!TestDlg::s_pendingDialog)
        return;

    ++TestDlg::s_retryCount;
    if (TestDlg::s_retryCount > 50) {
        KillTimer(nullptr, TestDlg::s_retryTimer);
        TestDlg::s_retryTimer = 0;
        TestDlg::s_pendingDialog = nullptr;
        return;
    }

    if (TestDlg::s_pendingDialog->TryCreateQtTreeTable()) {
        KillTimer(nullptr, TestDlg::s_retryTimer);
        TestDlg::s_retryTimer = 0;
        TestDlg::s_pendingDialog = nullptr;
    }
}

bool TestDlg::TryCreateQtTreeTable()
{
    HWND dialogHwnd = reinterpret_cast<HWND>(GetWindowHandle());
    if (!dialogHwnd || !IsWindow(dialogHwnd))
        return false;

    RECT clientRect = {};
    if (!GetClientRect(dialogHwnd, &clientRect))
        return false;

    const int margin = 8;
    const int x = margin;
    const int y = margin;
    const int width = (clientRect.right - clientRect.left) - margin * 2;
    const int height = (clientRect.bottom - clientRect.top) - margin * 2;
    if (width <= 0 || height <= 0)
        return false;

    QtEnsureInitialized();
    m_qtTreeTableHandle = QtCreateTreeTable(dialogHwnd, x, y, width, height);
    return m_qtTreeTableHandle != nullptr;
}

void TestDlg::EmbedQtTreeTable()
{
    if (m_qtTreeTableHandle)
        return;

    if (TryCreateQtTreeTable())
        return;

    // BuildGraph 阶段窗口可能还没有 HWND，延迟到 Windows 窗口真正创建后再尝试。
    s_pendingDialog = this;
    s_retryCount = 0;
    if (!s_retryTimer)
        s_retryTimer = SetTimer(nullptr, 0, 200, QtTreeTableRetryProc);
}

void TestDlg::ResizeQtTreeTable()
{
    if (!m_qtTreeTableHandle)
        return;

    HWND dialogHwnd = reinterpret_cast<HWND>(GetWindowHandle());
    if (!dialogHwnd || !IsWindow(dialogHwnd))
        return;

    RECT clientRect = {};
    if (!GetClientRect(dialogHwnd, &clientRect))
        return;

    const int margin = 8;
    const int x = margin;
    const int y = margin;
    const int width = (clientRect.right - clientRect.left) - margin * 2;
    const int height = (clientRect.bottom - clientRect.top) - margin * 2;
    if (width > 0 && height > 0)
        QtResizeTreeTable(m_qtTreeTableHandle, x, y, width, height);
}

void TestDlg::DestroyQtTreeTable()
{
    if (s_pendingDialog == this) {
        s_pendingDialog = nullptr;
        if (s_retryTimer) {
            KillTimer(nullptr, s_retryTimer);
            s_retryTimer = 0;
        }
    }

    if (m_qtTreeTableHandle) {
        QtDestroyTreeTable(m_qtTreeTableHandle);
        m_qtTreeTableHandle = nullptr;
    }
}
```

### 8.1 创建时机

命令类在显示对话框后调用：

```cpp
_test->EmbedQtTreeTable();
_test->SetVisibility(CATDlgShow);
```

如果项目现有逻辑通过 `GetWindSizeNotification()` 进入 `ShowBtn()`，则在该回调中调用 `EmbedQtTreeTable()`，并在后续尺寸通知中调用 `ResizeQtTreeTable()`。

## 9. 第五步：连接 CAA 对话框尺寸通知

在 `TestCmd::BuildGraph()` 中保留现有尺寸通知注册：

```cpp
AddAnalyseNotificationCB(
    _test,
    _test->GetWindSizeNotification(),
    (CATCommandMethod)&TestCmd::OnDialogSizeChanged,
    nullptr);
```

增加方法：

```cpp
void TestCmd::OnDialogSizeChanged()
{
    if (_test)
        _test->ResizeQtTreeTable();
}
```

尺寸同步必须由 CAA 侧驱动，而不是让 Qt 自己猜父窗口大小。原因是 CAA 的布局系统和 Qt 的布局系统互不认识，Qt 只能通过父 HWND 的客户区矩形得到可靠尺寸。

## 10. 第六步：Qt 树表内部布局保持不变

Qt 树表内部继续使用当前实现：

```text
TreeTableWidget
├── MultiLevelHeader
├── TreeTableContentView
└── TreeVerticalHeader
```

不要把树表拆成多个独立的 CAA 控件，也不要让 CAA 管理每一列和每一行。以下功能全部继续由 Qt 保留：

- 多级表头绘制和合并。
- 树节点展开与折叠。
- 行号垂直表头。
- 表头排序。
- 任意单元格边缘拖动整列宽度。
- 任意单元格边缘拖动整行高度。
- 长文本显示、进度 delegate、滚动条和选择状态。

CAA 只负责树表这个整体窗口的创建、位置、大小和销毁。

## 11. 第七步：Qt 独立编译脚本

当前项目使用 Qt 5.9/MSVC2015 x64，因此脚本应使用同一套编译器和 Qt 路径。

文件：`build_qt.bat`

```bat
@echo off
setlocal

call "D:\VS2015\VC\bin\amd64\vcvars64.bat"

set QT_DIR=C:\Qt\Qt5.9.0\5.9\msvc2015_64
set MODULE_DIR=%~dp0
set OBJ_DIR=%MODULE_DIR%Objects\win_b64

if not exist "%OBJ_DIR%" mkdir "%OBJ_DIR%"

rem 先用 Qt 的 moc 处理包含 Q_OBJECT 的树表头文件。
"%QT_DIR%\bin\moc.exe" ^
    "%MODULE_DIR%LocalInterfaces\treetablewidget.h" ^
    -o "%MODULE_DIR%qt_src\moc_treetablewidget.cpp"
if errorlevel 1 exit /b 1

cl.exe /nologo /c /EHsc /O2 /MD /utf-8 ^
    /I"%QT_DIR%\include" ^
    /I"%QT_DIR%\include\QtCore" ^
    /I"%QT_DIR%\include\QtGui" ^
    /I"%QT_DIR%\include\QtWidgets" ^
    /I"%MODULE_DIR%LocalInterfaces" ^
    /Fo"%OBJ_DIR%\QtTreeTableEmbed.obj" ^
    "%MODULE_DIR%qt_src\QtTreeTableEmbed.cpp"
if errorlevel 1 exit /b 1

cl.exe /nologo /c /EHsc /O2 /MD /utf-8 ^
    /I"%QT_DIR%\include" ^
    /I"%QT_DIR%\include\QtCore" ^
    /I"%QT_DIR%\include\QtGui" ^
    /I"%QT_DIR%\include\QtWidgets" ^
    /I"%MODULE_DIR%LocalInterfaces" ^
    /Fo"%OBJ_DIR%\treetablewidget.obj" ^
    "%MODULE_DIR%qt_src\treetablewidget.cpp"
if errorlevel 1 exit /b 1

cl.exe /nologo /c /EHsc /O2 /MD /utf-8 ^
    /I"%QT_DIR%\include" ^
    /I"%QT_DIR%\include\QtCore" ^
    /I"%QT_DIR%\include\QtGui" ^
    /I"%QT_DIR%\include\QtWidgets" ^
    /I"%MODULE_DIR%LocalInterfaces" ^
    /Fo"%OBJ_DIR%\moc_treetablewidget.obj" ^
    "%MODULE_DIR%qt_src\moc_treetablewidget.cpp"
if errorlevel 1 exit /b 1

lib.exe /NOLOGO /OUT:"%OBJ_DIR%\QtTreeTable.lib" /MACHINE:X64 ^
    "%OBJ_DIR%\QtTreeTableEmbed.obj" ^
    "%OBJ_DIR%\treetablewidget.obj" ^
    "%OBJ_DIR%\moc_treetablewidget.obj"
if errorlevel 1 exit /b 1

echo Qt tree table library build completed.
endlocal
```

### 11.1 每个编译参数的理由

| 参数 | 理由 |
| --- | --- |
| `vcvars64.bat` | 进入 x64 MSVC 环境，避免生成 Win32 对象文件。 |
| `/EHsc` | 启用标准 C++ 异常语义。 |
| `/O2` | 生成适合模块使用的优化代码。 |
| `/MD` | 使用动态 CRT，和 CAA 工程保持一致。 |
| `/utf-8` | 按 UTF-8 读取中文源码和字符串。 |
| `moc.exe` | 生成 Qt 元对象代码，否则 Q_OBJECT 类会出现链接错误。 |
| `/MACHINE:X64` | 确保静态库架构与 CAA x64 模块一致。 |

如果最终项目实际升级到 Qt6/MSVC2022，应替换 `vcvars64.bat`、`QT_DIR`、Qt 库名和编译参数，但不能混用 Qt5 和 Qt6 的头文件、库文件或插件。

## 12. 第八步：修改 Imakefile.mk

在 `LOCAL_LDFLAGS` 中链接 Qt 树表静态库和同一套 Qt 动态库导入库：

```make
LOCAL_LDFLAGS = \
    "$(WSROOT)/V6AddinCmdTest.m/Objects/win_b64/QtTreeTable.lib" \
    /LIBPATH:"C:/Qt/Qt5.9.0/5.9/msvc2015_64/lib" \
    Qt5Core.lib \
    Qt5Gui.lib \
    Qt5Widgets.lib \
    user32.lib
```

实际路径必须根据模块的 `WSROOT` 和当前 CAA 工程目录调整。不要把 `QtTreeTable.lib` 加到 Qt 独立编译步骤之前，因为该库只有运行 `build_qt.bat` 后才会生成。

## 13. 第九步：Qt 插件和 DLL 部署

运行时必须保证以下文件来自同一 Qt 版本：

```text
Qt5Core.dll
Qt5Gui.dll
Qt5Widgets.dll
platforms/qwindows.dll
```

建议在 `QtEnsureInitialized()` 中明确设置插件路径：

```cpp
QCoreApplication::addLibraryPath("C:/Qt/Qt5.9.0/5.9/msvc2015_64/plugins");
qputenv("QT_QPA_PLATFORM_PLUGIN_PATH",
        "C:/Qt/Qt5.9.0/5.9/msvc2015_64/plugins/platforms");
```

如果公司 CAA 环境已经提供固定 Qt5 插件目录，则所有 Qt5 库、插件和静态库都应统一使用该目录对应的版本，不能只替换其中一部分。

## 14. 第十步：删除错误的直接嵌入代码

应删除或替换以下代码：

```cpp
m_treeTable = new TreeTableWidget(this);
setCentralWidget(m_treeTable);
m_treeTable->loadDemoData();
```

替换为：

```cpp
HWND parentHwnd = reinterpret_cast<HWND>(GetWindowHandle());
m_qtTreeTableHandle = QtCreateTreeTable(parentHwnd, 8, 8, width, height);
```

CAA 侧不能保存 `TreeTableWidget *`，只能保存：

```cpp
void *m_qtTreeTableHandle;
```

这是为了保证 CAA 模块不依赖 Qt C++ 类定义，也避免 CAA 和 Qt 的对象所有权混乱。

## 15. 生命周期设计

```text
TestDlg 构造
  └── m_qtTreeTableHandle = nullptr

TestDlg 显示
  └── EmbedQtTreeTable()
        ├── HWND 无效：启动重试 Timer
        └── HWND 有效：QtCreateTreeTable()

CAA 对话框尺寸变化
  └── ResizeQtTreeTable()

TestDlg 析构
  └── DestroyQtTreeTable()
        ├── 停止创建重试 Timer
        ├── QtDestroyTreeTable()
        └── 清空句柄
```

必须保证以下原则：

- CAA 不直接删除 Qt 对象。
- Qt 对象由创建它的 Qt/宿主线程销毁。
- 对话框销毁时先停止延迟创建 Timer。
- Qt 事件泵送 Timer 不得继续访问已经销毁的树表。
- `SetParent()` 失败时立即删除刚创建的 Qt 控件，不能留下孤立窗口。

## 16. 中文和编码方案

根据全局配置，编码分为两类：

### 16.1 文档文件

本方案 `.md` 文件使用 GBK 编码，便于当前 VS Code 设置打开。

### 16.2 Qt 和 CAA 源码

源码必须优先保证编译器和 Qt 正确解析：

- `.cpp/.h` 使用 UTF-8。
- Qt 独立编译使用 `/utf-8`。
- CAA 的 mkmk 编译配置应使用项目当前支持的 UTF-8 输入选项。
- `.pro`、`Imakefile.mk` 等构建文件不得带 UTF-8 BOM，避免旧版工具解析失败。
- CAA 的 `CATUnicodeString`、Qt 的 `QStringLiteral` 不要混用错误的本地代码页转换。

如果运行窗口出现中文乱码，应先检查 Qt 模块实际编译参数和源码保存编码，而不是把 CAA 对话框或 Qt 控件强行改成 GBK。

## 17. 编译顺序

必须按以下顺序执行：

```text
1. 打开 x64 MSVC 命令环境
2. 进入 V6AddinCmdTest.m
3. 执行 build_qt.bat
4. 确认 Objects/win_b64/QtTreeTable.lib 生成
5. 进入 CAA 开发环境
6. 执行 mkmk -a
7. 确认 CAA 模块链接到 QtTreeTable.lib
8. 启动 3DEXPERIENCE
9. 打开 TestDlg
10. 验证树表显示和交互
```

先编译 Qt 静态库的理由是：CAA 的 mkmk 编译选项可能和 Qt 头文件要求不一致。Qt 独立编译可以完全控制 MSVC、Qt 头文件、moc 和 CRT 配置；CAA 只链接最终产物。

## 18. 功能验证清单

### 18.1 窗口嵌入

- CAA 对话框标题栏仍由 CAA 显示。
- Qt 树表没有额外标题栏和边框。
- 树表完整填充 CAA 对话框客户区。
- 对话框移动时树表跟随移动。
- 对话框缩放时树表尺寸同步变化。
- 关闭对话框后没有残留 Qt 窗口。

### 18.2 树表功能

- 多级表头完整显示。
- 表头合并区域没有错位。
- 树节点可以展开和折叠。
- 行号和内容行保持对齐。
- 表头排序仍然有效。
- 列宽拖动仍然有效。
- 单元格右边缘拖动整列宽度仍然有效。
- 单元格下边缘拖动整行高度仍然有效。
- 长文本在列宽增加后能够显示。
- Qt 进度 delegate 正常绘制。
- 鼠标 hover、选中和键盘操作正常。

### 18.3 异常场景

- `GetWindowHandle()` 初次返回空时能够重试。
- Qt 插件路径错误时能输出明确日志。
- `SetParent()` 失败时不会泄漏 Qt 控件。
- Qt DLL 缺失时能够定位到部署问题。
- 32 位和 64 位库混用时能在构建阶段发现，而不是运行时崩溃。
- 重复调用 `EmbedQtTreeTable()` 不会创建多个树表。
- 对话框析构时 Timer 已经停止。

## 19. 日志和故障定位

建议在 CAA/Qt 桥接层保留阶段日志：

```text
QtEnsureInitialized entered
Qt application created
CAA parent HWND = ...
Qt tree table HWND = ...
SetParent succeeded
Tree table shown
Tree table resized
Tree table destroyed
```

排查顺序：

1. 检查 CAA 父窗口句柄是否为空。
2. 检查 Qt 是否成功创建 `QApplication`。
3. 检查 Qt 树表 `winId()` 是否有效。
4. 检查 `SetParent()` 返回值和 `GetLastError()`。
5. 检查 Qt DLL 和 `qwindows.dll` 是否来自同一版本。
6. 检查 `QtTreeTable.lib` 是否为 x64、MSVC2015 版本。
7. 检查 CAA 侧是否误用了 `setCentralWidget()` 或直接 delete Qt 对象。

## 20. 实施阶段建议

### 阶段一：先嵌入空白 Qt 容器

暂时不加载树表数据，只创建一个空的 `TreeTableWidget`，验证 HWND、`SetParent`、显示和尺寸同步。

理由：先隔离窗口嵌入问题，避免树表绘制问题和窗口层级问题同时出现。

### 阶段二：嵌入完整树表

接入现有 `treetablewidget.cpp`，加载当前演示数据，验证多级表头、树数据和滚动。

理由：确认现有 Qt 控件在 CAA 宿主中可以正常绘制和响应事件。

### 阶段三：接入真实业务数据

把 `loadDemoData()` 替换成业务数据适配层，但不改变控件的窗口嵌入接口。

理由：数据来源变化不应影响 Qt/CAA 的窗口边界和生命周期。

### 阶段四：完善 CAA 业务回调

根据树节点选中、排序、双击或复选状态，增加 Qt 到 CAA 的业务通知接口。

建议后续新增纯 C 回调：

```cpp
typedef void (*QtTreeTableCallback)(int eventType,
                                    const char *nodeId,
                                    void *userData);

void QtSetTreeTableCallback(void *handle,
                            QtTreeTableCallback callback,
                            void *userData);
```

理由：业务回调也保持 C ABI，CAA 不直接连接 Qt signal，避免跨模块暴露 Qt 类型。

## 21. 最终交付文件清单

完成移植后应包含：

```text
LocalInterfaces/QtTreeTableEmbed.h
qt_src/QtTreeTableEmbed.cpp
qt_src/treetablewidget.cpp
qt_src/moc_treetablewidget.cpp       自动生成，不建议手工编辑
src/TestDlg.DSGen
src/TestCmd.cpp
build_qt.bat
Imakefile.mk
Objects/win_b64/QtTreeTable.lib      构建产物
```

最终检查标准是：CAA 只看到一个普通的子窗口句柄，Qt 只负责树表内部逻辑；两侧边界清晰、生命周期成对、尺寸同步可靠，且 Qt 树表原有样式和交互不被 CAA 控件替换或破坏。
