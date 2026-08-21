#include <QApplication>
#include <QCoreApplication>

#include <windows.h>

#include <cstdlib>
#include <string>

#include "QtTreeTable.h"
#include "TreeTableWidget.h"

namespace
{
	QApplication *g_qtApplication = nullptr;
	UINT_PTR g_qtEventTimer = 0;

	// 创建树表期间禁止重入事件泵,避免 Watchdog 检测到未完成窗口创建。
	bool g_creatingTreeTable = false;
	// 事件泵正在处理 Qt 事件时禁止再次进入。
	bool g_processingQtEvents = false;

	// 事件泵送保护类。
	class QtEventProcessGuard
	{
	public:
		QtEventProcessGuard()
		{
			g_processingQtEvents = true;
		}

		~QtEventProcessGuard()
		{
			g_processingQtEvents = false;
		}
	};

	// 通过 Win32 定时器手动泵送 Qt 事件。
	// CAA 宿主通常没有调用 QApplication::exec(),没有事件泵送时树表无法响应鼠标和重绘。
	VOID CALLBACK QtEventPumpProc(HWND, UINT, UINT_PTR, DWORD)
	{
		if (g_creatingTreeTable || g_processingQtEvents)
			return;
		if (!QApplication::instance())
			return;

		QtEventProcessGuard guard;
		QApplication::processEvents();
	}

	// 用 GetWindowLongPtr/SetWindowLongPtr,保证 x64 下窗口样式操作不会截断指针。
	bool PrepareAsChildWindow(HWND childHwnd, HWND parentHwnd)
	{
		if (!childHwnd
			|| !parentHwnd
			|| !IsWindow(childHwnd)
			|| !IsWindow(parentHwnd))
			return false;

		LONG_PTR style = GetWindowLongPtr(childHwnd, GWL_STYLE);
		style = (style & ~(WS_POPUP | WS_OVERLAPPEDWINDOW)) | WS_CHILD;
		SetWindowLongPtr(childHwnd, GWL_STYLE, style);

		LONG_PTR exStyle = GetWindowLongPtr(childHwnd, GWL_EXSTYLE);
		exStyle &= ~(WS_EX_TOOLWINDOW | WS_EX_APPWINDOW);
		SetWindowLongPtr(childHwnd, GWL_EXSTYLE, exStyle);

		SetLastError(ERROR_SUCCESS);
		const HWND oldParent = SetParent(childHwnd, parentHwnd);
		return oldParent || GetLastError() == ERROR_SUCCESS;
	}

	TreeTableWidget *ToWidget(void *handle)
	{
		return static_cast<TreeTableWidget *>(handle);
	}

	// 获取 SDQtTreeTable.dll 所在目录(失败时退回宿主 EXE 目录)。
	bool GetOwnModuleDir(std::string &outDir)
	{
		HMODULE module = GetModuleHandleA("SDQtTreeTable.dll");
		if (!module)
			module = GetModuleHandleA("SDQtTreeTable");
		if (!module)
			module = GetModuleHandleA(nullptr);
		if (!module)
			return false;

		char buffer[MAX_PATH] = {};
		const DWORD length = GetModuleFileNameA(module, buffer, MAX_PATH);
		if (length == 0 || length >= MAX_PATH)
			return false;

		outDir = buffer;
		const size_t pos = outDir.find_last_of('\\');
		if (pos == std::string::npos)
			return false;
		outDir = outDir.substr(0, pos);
		return true;
	}

	bool FileExists(const std::string &path)
	{
		const DWORD attributes = GetFileAttributesA(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES;
	}

	// 定位默认数据文件 TreeTableData.json,顺序:
	//   1. 环境变量 SDQT_TREETABLE_DATA;
	//   2. SDQtTreeTable.dll 同目录(部署时把 JSON 放到 bin 目录即可);
	//   3. 工作区源码目录 win_b64\code\bin 上溯三级 + NewV6WorkFrm\SDQtTreeTable.m。
	bool ResolveDefaultDataFile(std::string &outPath)
	{
		const char *envPath = getenv("SDQT_TREETABLE_DATA");
		if (envPath && envPath[0] && FileExists(envPath))
		{
			outPath = envPath;
			return true;
		}

		std::string moduleDir;
		if (!GetOwnModuleDir(moduleDir))
			return false;

		
		std::string candidate = moduleDir + "\\TreeTableDataTest.json";
		if (FileExists(candidate))
		{
			outPath = candidate;
			return true;
		}

		std::string workspaceRoot = moduleDir;
		for (int level = 0; level < 3; ++level)
		{
			const size_t pos = workspaceRoot.find_last_of('\\');
			if (pos == std::string::npos)
				break;
			workspaceRoot = workspaceRoot.substr(0, pos);
		}
		// its a fake path, you need fill in your real json file path
		candidate =
			workspaceRoot + "TreeTableDataTest.json";
		if (FileExists(candidate))
		{
			outPath = candidate;
			return true;
		}
		return false;
	}
} // namespace

// 初始化 Qt 运行环境(进程全局只执行一次)。
void QtTreeTableEnsureInitialized(void)
{
	if (g_qtApplication)
		return;

	QApplication *existing =
		qobject_cast<QApplication *>(QApplication::instance());
	if (existing)
	{
		g_qtApplication = existing;
		return;
	}

	// 作为插件运行,避免 Qt 修改 CAA 进程级别的全局状态。
	QApplication::setAttribute(Qt::AA_PluginApplication, true);
	// CAA 自己管理 DPI,禁止 Qt 在宿主进程中重新设置 DPI 感知状态。
	QApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true);

	// 运行时插件路径指向 3DEXPERIENCE 自带的 Qt5Plugins(目标机无需安装 Qt)。
	// 3DE 的 Qt5Core/Qt5Gui/Qt5Widgets 运行库在其 win_b64\code\bin 中,
	// 进程启动时随 PATH 自动加载;这里只需告诉 Qt 去哪找 qwindows 平台插件。
	QCoreApplication::addLibraryPath(
		"B421/win_b64/code/bin/Qt5Plugins");
	SetEnvironmentVariableA(
		"QT_QPA_PLATFORM_PLUGIN_PATH",
		"B421\\win_b64\\code\\bin\\Qt5Plugins\\platforms");

	static int argc = 1;
	static char applicationName[] = "CAAQtTreeTable";
	static char *argv[] = { applicationName, nullptr };

	g_qtApplication = new QApplication(argc, argv);
	// CAA 没有 Qt 的 exec(),使用 Win32 Timer 驱动 Qt 事件处理。
	g_qtEventTimer = SetTimer(nullptr, 0, 16, QtEventPumpProc);
}

// 创建树表子窗口并嵌入 CAA 父窗口。
void *QtTreeTableCreate(
	void *parentHwnd,
	int x,
	int y,
	int width,
	int height)
{
	HWND hostHwnd = static_cast<HWND>(parentHwnd);
	if (!hostHwnd || !IsWindow(hostHwnd) || width <= 0 || height <= 0)
		return nullptr;

	QtTreeTableEnsureInitialized();
	if (!g_qtApplication)
		return nullptr;

	g_creatingTreeTable = true;

	TreeTableWidget *treeTable = new TreeTableWidget(nullptr);
	// 【关键】去除窗口装饰,避免 Qt 添加标题栏导致尺寸偏差。
	treeTable->setWindowFlags(Qt::FramelessWindowHint);
	// 强制创建原生 HWND(但不调用 show)。
	treeTable->setAttribute(Qt::WA_NativeWindow, true);

	HWND treeHwnd = (HWND)treeTable->winId();
	if (!PrepareAsChildWindow(treeHwnd, hostHwnd))
	{
		delete treeTable;
		g_creatingTreeTable = false;
		return nullptr;
	}

	// 只有已经是 WS_CHILD 后才能 show,避免 CAA Watchdog 把它识别成新的顶级窗口。
	treeTable->show();

	// Qt show() 可能覆盖位置,最后使用 Win32 强制定位。
	MoveWindow(treeHwnd, x, y, width, height, TRUE);
	ShowWindow(treeHwnd, SW_SHOW);

	g_creatingTreeTable = false;
	return treeTable;
}

// 销毁树表并释放 Qt 对象。
void QtTreeTableDestroy(void *handle)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (!treeTable)
		return;

	HWND treeHwnd = (HWND)treeTable->winId();
	if (treeHwnd && IsWindow(treeHwnd))
		SetParent(treeHwnd, nullptr);

	delete treeTable;
}

// 调整树表位置和大小。
void QtTreeTableResize(
	void *handle,
	int x,
	int y,
	int width,
	int height)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (!treeTable || width <= 0 || height <= 0)
		return;

	HWND treeHwnd = (HWND)treeTable->winId();
	if (treeHwnd && IsWindow(treeHwnd))
	{
		// MoveWindow(..., TRUE) 已会发送尺寸和重绘消息,无需再强制 repaint()。
		MoveWindow(treeHwnd, x, y, width, height, TRUE);
	}
}

// 从 JSON 文件读取树表数据。
int QtTreeTableLoadFromFile(void *handle, const char *filePath)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (!treeTable || !filePath || filePath[0] == '\0')
		return 0;
	return treeTable->loadFromFile(QString::fromLocal8Bit(filePath)) ? 1 : 0;
}

// 加载随模块发布的默认数据文件 TreeTableData.json。
int QtTreeTableLoadDefaultData(void *handle)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (!treeTable)
		return 0;

	std::string path;
	if (!ResolveDefaultDataFile(path))
		return 0;
	return treeTable->loadFromFile(QString::fromLocal8Bit(path.c_str())) ? 1 : 0;
}

// 用 C 结构体数组程序化填充树数据。
int QtTreeTableSetData(
	void *handle,
	const QtTreeNode *roots,
	int rootCount)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (!treeTable)
		return 0;
	return treeTable->setData(roots, rootCount) ? 1 : 0;
}

// 清空树表所有节点。
void QtTreeTableClear(void *handle)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable)
		treeTable->clearData();
}

// 注册行点击回调。
void QtTreeTableSetRowClickCallback(
	void *handle,
	QtTreeRowClickCallback callback,
	void *context)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable)
		treeTable->setRowClickCallback(callback, context);
}

// 设置树表样式。
void QtTreeTableSetStyle(void *handle, const QtTreeStyle *style)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable)
		treeTable->setStyle(style);
}

// 设置树表动画开关(节点展开/折叠动画)。
void QtTreeTableSetAnimation(
	void *handle,
	int enable,
	int durationMs)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable)
		treeTable->setAnimation(enable != 0, durationMs);
}

// 设置列与多级表头(从零开始,未调用时表格为空表)。
void QtTreeTableSetColumns(
	void *handle,
	const QtTreeColumn *columns,
	int columnCount,
	const QtTreeHeaderCell *headerCells,
	int headerCellCount,
	int headerRowHeight)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable)
		treeTable->setColumns(
			columns,
			columnCount,
			headerCells,
			headerCellCount,
			headerRowHeight);
}

// 设置数据文件路径(LoadData 时按列定义的 field 字段读取)。
void QtTreeTableSetDataFile(void *handle, const char *filePath)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable && filePath)
		treeTable->setDataFile(QString::fromLocal8Bit(filePath));
}

// 按列字段从已设置的数据文件加载并填入表格。
int QtTreeTableLoadData(void *handle)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (!treeTable)
		return 0;
	return treeTable->loadData() ? 1 : 0;
}

// 设置进度条列(-1 关闭,默认关闭)。
void QtTreeTableSetProgressColumn(void *handle, int column)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable)
		treeTable->setProgressColumn(column);
}

// 设置列宽是否随窗口宽度按比例缩放(默认开启)。
void QtTreeTableSetColumnAutoScale(void *handle, int enabled)
{
	TreeTableWidget *treeTable = ToWidget(handle);
	if (treeTable)
		treeTable->setColumnAutoScale(enabled != 0);
}
