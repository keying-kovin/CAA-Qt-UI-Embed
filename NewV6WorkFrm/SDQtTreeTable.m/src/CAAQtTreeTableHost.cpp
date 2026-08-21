#include "CAAQtTreeTableHost.h"

#include <windows.h>

#include "CATDlgFrame.h"

namespace
{
	// 树表相对宿主 Frame 客户区的边距(像素),与 CAA 工程 CAAQtTableHost 一致。
	const int kMargin = 4;
}

// 构造函数:初始化宿主指针与树表句柄为空。
CAAQtTreeTableHost::CAAQtTreeTableHost()
	: _host(NULL)
	, _treeTable(NULL)
{
}

// 析构函数:自动销毁树表。
CAAQtTreeTableHost::~CAAQtTreeTableHost()
{
	Destroy();
}

// 把树表挂载到宿主 Frame:
//   先销毁旧实例,再读取 Frame 客户区尺寸创建树表;
//   Frame 的 HWND 尚未创建时返回 false,由调用方在尺寸通知中重试。
bool CAAQtTreeTableHost::Attach(CATDlgFrame *host)
{
	Destroy();
	_host = host;

	if (!_host)
		return false;

	HWND hostHwnd = (HWND)_host->GetWindowHandle();
	if (!hostHwnd || !IsWindow(hostHwnd))
		return false;

	RECT rect = {};
	if (!GetClientRect(hostHwnd, &rect))
		return false;

	const int width = rect.right - rect.left - kMargin * 2;
	const int height = rect.bottom - rect.top - kMargin * 2;
	if (width <= 0 || height <= 0)
		return false;

	_treeTable = QtTreeTableCreate(
		hostHwnd,
		kMargin,
		kMargin,
		width,
		height);

	return _treeTable != NULL;
}

// 跟随宿主 Frame 当前客户区尺寸调整树表大小。
void CAAQtTreeTableHost::Resize()
{
	if (!_host || !_treeTable)
		return;

	HWND hostHwnd = (HWND)_host->GetWindowHandle();
	if (!hostHwnd || !IsWindow(hostHwnd))
		return;

	RECT rect = {};
	if (!GetClientRect(hostHwnd, &rect))
		return;

	const int width = rect.right - rect.left - kMargin * 2;
	const int height = rect.bottom - rect.top - kMargin * 2;
	if (width <= 0 || height <= 0)
		return;

	QtTreeTableResize(_treeTable, kMargin, kMargin, width, height);
}

// 销毁树表并解除与宿主的绑定(可安全重复调用)。
void CAAQtTreeTableHost::Destroy()
{
	if (_treeTable)
	{
		QtTreeTableDestroy(_treeTable);
		_treeTable = NULL;
	}
	_host = NULL;
}

// 查询树表是否已成功挂载。
bool CAAQtTreeTableHost::IsAttached() const
{
	return _treeTable != NULL;
}

// 从 JSON 文件读取树表数据(UTF-8 编码,直接写中文字符)。
bool CAAQtTreeTableHost::LoadFromFile(const char *filePath)
{
	return _treeTable
		&& QtTreeTableLoadFromFile(_treeTable, filePath) != 0;
}

// 加载随模块发布的默认数据文件 TreeTableData.json。
bool CAAQtTreeTableHost::LoadDefaultData()
{
	return _treeTable
		&& QtTreeTableLoadDefaultData(_treeTable) != 0;
}

// 响应事件后程序化填充树数据。
bool CAAQtTreeTableHost::SetData(
	const QtTreeNode *roots,
	int rootCount)
{
	return _treeTable
		&& QtTreeTableSetData(_treeTable, roots, rootCount) != 0;
}

// 清空树表所有节点。
void CAAQtTreeTableHost::Clear()
{
	if (_treeTable)
		QtTreeTableClear(_treeTable);
}

// 注册行点击回调。
void CAAQtTreeTableHost::SetRowClickCallback(
	QtTreeRowClickCallback callback,
	void *context)
{
	if (_treeTable)
		QtTreeTableSetRowClickCallback(_treeTable, callback, context);
}

// 设置树表样式。
void CAAQtTreeTableHost::SetStyle(const QtTreeStyle *style)
{
	if (_treeTable)
		QtTreeTableSetStyle(_treeTable, style);
}

// 打开/关闭树表动画(节点展开/折叠动画)。
void CAAQtTreeTableHost::SetAnimation(int enable, int durationMs)
{
	if (_treeTable)
		QtTreeTableSetAnimation(_treeTable, enable, durationMs);
}

// 设置列与多级表头(从零开始,未调用时表格为空表)。
void CAAQtTreeTableHost::SetColumns(
	const QtTreeColumn *columns,
	int columnCount,
	const QtTreeHeaderCell *headerCells,
	int headerCellCount,
	int headerRowHeight)
{
	if (_treeTable)
	{
		QtTreeTableSetColumns(
			_treeTable,
			columns,
			columnCount,
			headerCells,
			headerCellCount,
			headerRowHeight);
	}
}

// 设置数据文件路径(LoadData 时按列定义的 field 字段读取)。
void CAAQtTreeTableHost::SetDataFile(const char *filePath)
{
	if (_treeTable)
		QtTreeTableSetDataFile(_treeTable, filePath);
}

// 按列字段从已设置的数据文件加载并填入表格。
bool CAAQtTreeTableHost::LoadData()
{
	return _treeTable
		&& QtTreeTableLoadData(_treeTable) != 0;
}

// 设置进度条列(-1 关闭,默认关闭)。
void CAAQtTreeTableHost::SetProgressColumn(int column)
{
	if (_treeTable)
		QtTreeTableSetProgressColumn(_treeTable, column);
}

// 打开/关闭列宽随窗口宽度按比例缩放。
void CAAQtTreeTableHost::SetColumnAutoScale(int enabled)
{
	if (_treeTable)
		QtTreeTableSetColumnAutoScale(_treeTable, enabled);
}
