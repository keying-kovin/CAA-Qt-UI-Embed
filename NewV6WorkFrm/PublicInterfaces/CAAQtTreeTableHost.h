#ifndef CAA_QT_TREE_TABLE_HOST_H
#define CAA_QT_TREE_TABLE_HOST_H

#include "QtTreeTable.h"
#include "SDQtTreeTable.h"

class CATDlgFrame;

// =====================================================================
// Qt 树表宿主类(随调随用,推荐入口)
// =====================================================================
// 用法(与 CAA 工程 CAAQtTableHost 完全一致):
//   1. 在对话框中放一个 CATDlgFrame 作为树表宿主区域;
//   2. 在命令类中持有一个 CAAQtTreeTableHost 成员;
//   3. 对话框尺寸变化通知里调用 EnsureTableAttached() 模式:
//        if (!_host.IsAttached()) { _host.Attach(dlg->GetHostFrame()); 配置...; }
//        _host.Resize();
//   4. 数据来源二选一(可混合):
//        a) LoadFromFile / LoadDefaultData —— 读取 UTF-8 JSON 文件;
//        b) SetData —— 响应事件后由业务代码构造 QtTreeNode 填入。
// =====================================================================
class ExportedBySDQtTreeTable  CAAQtTreeTableHost
{
public:
	CAAQtTreeTableHost();
	~CAAQtTreeTableHost();

	// 把树表挂载到宿主 Frame(会先销毁旧实例再重建)。
	// 参数:
	//   host : 对话框中用于放置树表的 CATDlgFrame 指针
	// 返回:true 挂载成功;false 表示 Frame 的 HWND 尚未创建,
	//       调用方应在下一次尺寸通知中重试(与 CAA 工程相同)。
	bool Attach(CATDlgFrame *host);

	// 跟随宿主 Frame 当前客户区尺寸调整树表大小。
	// 说明:应在 Attach 成功后以及每次对话框尺寸变化时调用。
	void Resize();

	// 销毁树表并解除与宿主的绑定(析构函数会自动调用,可安全重复调用)。
	void Destroy();

	// 查询树表是否已成功挂载。
	// 返回:true 已挂载,false 未挂载。
	bool IsAttached() const;

	// 从 JSON 文件读取树表数据(UTF-8 编码,直接写中文字符)。
	// 参数:
	//   filePath : JSON 文件完整路径
	// 返回:true 成功,false 失败。
	bool LoadFromFile(const char *filePath);

	// 加载随 SDQtTreeTable 模块发布的默认数据 TreeTableData.json。
	// 返回:true 成功,false 失败(未找到数据文件)。
	bool LoadDefaultData();

	// 响应事件后程序化填充树数据。
	// 参数:
	//   roots     : 顶层节点数组(QtTreeNode,可含递归子节点)
	//   rootCount : 顶层节点数量
	// 返回:true 成功,false 失败。
	bool SetData(const QtTreeNode *roots, int rootCount);

	// 清空树表所有节点。
	void Clear();

	// 注册行点击回调(点击任意单元格触发)。
	// 参数:
	//   callback : 回调函数(静态成员函数或全局函数)
	//   context  : 随回调返回的上下文指针(传 this)
	void SetRowClickCallback(
		QtTreeRowClickCallback callback,
		void *context);

	// 设置树表样式。
	// 参数:
	//   style : QtTreeStyle 结构体(行高、交替行色、样式表)
	void SetStyle(const QtTreeStyle *style);

	// 设置树表动画开关(节点展开/折叠动画)。
	// 参数:
	//   enable     : 1 启用动画,0 关闭(默认关闭)
	//   durationMs : 动画时长(毫秒),供后续扩展使用,QTreeWidget 原生动画
	//                时长由系统样式决定,此参数暂为预留
	void SetAnimation(int enable, int durationMs);

	// 设置列与多级表头(从零开始,未调用时表格为空表)。
	// 参数:
	//   columns         : 列定义数组(id/表头/JSON字段/宽度/数值排序),数量即列数
	//   columnCount     : 列数量
	//   headerCells     : 多级表头单元格数组;传 NULL 且 headerCellCount 为 0 时,
	//                      按列标题生成单行表头
	//   headerCellCount : 表头单元格数量
	//   headerRowHeight : 表头每行高度(像素),<=0 使用默认值 28
	void SetColumns(
		const QtTreeColumn *columns,
		int columnCount,
		const QtTreeHeaderCell *headerCells,
		int headerCellCount,
		int headerRowHeight);

	// 设置数据文件路径(LoadData 时按列定义的 field 字段读取)。
	// 参数:
	//   filePath : JSON 文件完整路径(UTF-8 编码文件,直接写中文字符)
	void SetDataFile(const char *filePath);

	// 按列字段从已设置的数据文件加载并填入表格。
	// 返回:true 成功,false 失败。
	bool LoadData();

	// 设置进度条列(绘制进度条并显示百分比文本)。
	// 参数:
	//   column : 列索引(0 起);-1 关闭进度条(默认关闭,不是每种数据都需要)
	// 说明:建议在加载数据前调用。
	void SetProgressColumn(int column);

	// 设置列宽是否随窗口宽度按比例缩放(拖动 CAA 对话框扩宽/收窄时,
	// 字段宽度跟着成比例变化)。
	// 参数:
	//   enabled : 1 开启(默认开启),0 关闭
	void SetColumnAutoScale(int enabled);

private:
	CATDlgFrame *_host;
	void *_treeTable;
};

#endif
