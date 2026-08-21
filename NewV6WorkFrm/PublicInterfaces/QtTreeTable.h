#ifndef QT_TREE_TABLE_H
#define QT_TREE_TABLE_H

// =====================================================================
// Qt 树表公共接口(SDQtTreeTable 模块)
// =====================================================================
// 本头文件提供两层接口,供任意 CAA/V6 模块使用:
//   1. 数据结构(QtTreeNode / QtTreeStyle / QtTreeRowClickCallback):
//      - 通过 C 结构体描述树节点、单元格文本与样式;
//      - 单元格文本约定为"系统本地编码"(与 CATUnicodeString::ConvertToChar()
//        一致,Qt 侧自动用 fromLocal8Bit 解码),直接写中文字符即可。
//   2. C 桥接函数(QtTreeTable*):
//      - 创建/销毁/调整树表窗口;
//      - 从 JSON 文件读取数据(文件须为 UTF-8 编码,直接写中文字符);
//      - 通过 QtTreeNode 数组程序化填充数据(适用于"响应事件后填表");
//      - 注册行点击回调、设置样式。
// =====================================================================

// 树表样式:
//   rowHeight            : 行高(像素),<=0 时使用控件默认值 27
//   alternatingRowColors : 1 开启交替行背景色,0 关闭
//   styleSheet           : 可选 Qt 样式表字符串(本地编码),NULL 表示使用默认样式
typedef struct QtTreeStyle
{
	int rowHeight;
	int alternatingRowColors;
	const char *styleSheet;
} QtTreeStyle;

// 列定义(设置表头字段时使用):
//   id     : 程序字段键(行点击回调 columnId 返回此值)
//   title  : 表头文本(未提供多级表头单元格时按此生成单行表头)
//   field  : 数据文件(JSON)中读取该列所用的字段名,NULL 表示该列不读数据
//   width  : 列宽(像素),<=0 使用默认宽度 100
//   numeric: 1 该列按数值排序,0 按文本排序
typedef struct QtTreeColumn
{
	const char *id;
	const char *title;
	const char *field;
	int width;
	int numeric;
} QtTreeColumn;

// 多级表头单元格(支持横向/纵向合并):
//   title      : 单元格文本
//   row        : 起始行(0 起)
//   column     : 起始列(0 起)
//   rowSpan    : 跨行数(>=1)
//   columnSpan : 跨列数(>=1)
//   sortable   : 1 点击该叶子表头可排序,0 不可
typedef struct QtTreeHeaderCell
{
	const char *title;
	int row;
	int column;
	int rowSpan;
	int columnSpan;
	int sortable;
} QtTreeHeaderCell;

// 树节点(程序化填表时使用,可通过 children 递归描述任意层级树):
//   cells     : 单元格文本数组,与 SetColumns 设置的列一一对应(顺序同列定义)
//   cellCount : cells 数组长度,不足列数时其余列显示为空
//   progress  : 完成进度数值 0~100,用于进度条列与数值排序
//   checkable : 1 显示勾选框(初始未勾选),0 不显示
//   expanded  : 1 加载后默认展开该节点,0 默认折叠
//   childCount: 子节点数量
//   children  : 子节点数组指针,无子节点时传 NULL
typedef struct QtTreeNode
{
	const char * const *cells;
	int cellCount;
	int progress;
	int checkable;
	int expanded;
	int childCount;
	const struct QtTreeNode *children;
} QtTreeNode;

// 行点击回调函数类型:
//   context  : 注册回调时传入的上下文指针(通常传业务命令对象 this)
//   row      : 被点击节点在"当前可见行"中的序号(从 0 开始,展开/折叠后变化)
//   column   : 被点击的列索引(从 0 开始)
//   columnId : 该列的程序字段键(如 "taskName"、"progress")
//   cellText : 该单元格的文本内容(本地编码)
typedef void(*QtTreeRowClickCallback)(
	void *context,
	int row,
	int column,
	const char *columnId,
	const char *cellText);

#ifdef __cplusplus
extern "C" {
#endif

	// 初始化 Qt 运行环境(QApplication 与 16ms 事件泵)。
	// 无参数;创建树表时自动调用,一般无需手动调用。
	void QtTreeTableEnsureInitialized(void);

	// 创建树表子窗口并嵌入父窗口。
	// 参数:
	//   parentHwnd : 父窗口句柄(CAA 对话框 Frame 的 HWND)
	//   x, y       : 树表左上角相对父窗口客户区的位置(像素)
	//   width      : 树表宽度(像素)
	//   height     : 树表高度(像素)
	// 返回:树表句柄;失败返回 NULL。
	void *QtTreeTableCreate(
		void *parentHwnd,
		int x,
		int y,
		int width,
		int height);

	// 销毁树表并释放 Qt 对象。
	// 参数:
	//   handle : 树表句柄
	void QtTreeTableDestroy(void *handle);

	// 调整树表的位置和大小。
	// 参数:
	//   handle     : 树表句柄
	//   x, y       : 树表左上角相对父窗口客户区的位置(像素)
	//   width      : 树表宽度(像素)
	//   height     : 树表高度(像素)
	void QtTreeTableResize(
		void *handle,
		int x,
		int y,
		int width,
		int height);

	// 从 JSON 文件读取树表数据并刷新。
	// 参数:
	//   handle   : 树表句柄
	//   filePath : JSON 文件路径(UTF-8 编码文件,直接写中文字符)
	// 返回:1 成功,0 失败(文件不存在或格式错误)。
	// 说明:JSON 根对象含可选 "columns" 数组(列 id/标题/宽度)
	//       与必选 "rows" 数组(节点字段见 QtTreeNode 注释)。
	int QtTreeTableLoadFromFile(void *handle, const char *filePath);

	// 加载随模块发布的默认数据文件 TreeTableData.json。
	// 参数:
	//   handle : 树表句柄
	// 返回:1 成功,0 失败。
	// 说明:查找顺序为环境变量 SDQT_TREETABLE_DATA ->
	//       SDQtTreeTable.dll 同目录 -> 工作区源码目录 NewV6WorkFrm\SDQtTreeTable.m。
	int QtTreeTableLoadDefaultData(void *handle);

	// 用 C 结构体数组程序化填充树数据(适用于响应事件后填表)。
	// 参数:
	//   handle    : 树表句柄
	//   roots     : 顶层节点数组;传 NULL 且 rootCount 为 0 时等价于清空
	//   rootCount : 顶层节点数量
	// 返回:1 成功,0 失败。
	int QtTreeTableSetData(
		void *handle,
		const QtTreeNode *roots,
		int rootCount);

	// 清空树表所有节点(保留列定义与样式)。
	// 参数:
	//   handle : 树表句柄
	void QtTreeTableClear(void *handle);

	// 注册行点击回调。
	// 参数:
	//   handle   : 树表句柄
	//   callback : 点击回调函数
	//   context  : 随回调返回的上下文指针(通常传业务命令对象 this)
	void QtTreeTableSetRowClickCallback(
		void *handle,
		QtTreeRowClickCallback callback,
		void *context);

	// 设置树表样式。
	// 参数:
	//   handle : 树表句柄
	//   style  : 样式结构体(行高、交替行色、样式表)
	void QtTreeTableSetStyle(void *handle, const QtTreeStyle *style);

	// 设置树表动画开关(节点展开/折叠动画)。
	// 参数:
	//   handle     : 树表句柄
	//   enable     : 1 启用动画,0 关闭(默认关闭)
	//   durationMs : 动画时长(毫秒),供后续行高/展开动画扩展使用,
	//                QTreeWidget 原生展开动画时长由系统样式决定,此参数暂为预留
	void QtTreeTableSetAnimation(
		void *handle,
		int enable,
		int durationMs);

	// 设置列与多级表头(从零开始,未调用时表格为空表)。
	// 参数:
	//   handle          : 树表句柄
	//   columns         : 列定义数组(id/表头/JSON字段/宽度/数值排序),数量即列数
	//   columnCount     : 列数量
	//   headerCells     : 多级表头单元格数组;传 NULL 且 headerCellCount 为 0 时,
	//                      按列标题生成单行表头
	//   headerCellCount : 表头单元格数量
	//   headerRowHeight : 表头每行高度(像素),<=0 使用默认值 28
	void QtTreeTableSetColumns(
		void *handle,
		const QtTreeColumn *columns,
		int columnCount,
		const QtTreeHeaderCell *headerCells,
		int headerCellCount,
		int headerRowHeight);

	// 设置数据文件路径(LoadData 时按列定义的 field 字段读取)。
	// 参数:
	//   handle   : 树表句柄
	//   filePath : JSON 文件完整路径(UTF-8 编码文件,直接写中文字符)
	void QtTreeTableSetDataFile(void *handle, const char *filePath);

	// 按列字段从已设置的数据文件加载并填入表格。
	// 参数:
	//   handle : 树表句柄
	// 返回:1 成功,0 失败(未设置文件或文件不存在/格式错误)。
	int QtTreeTableLoadData(void *handle);

	// 设置进度条列(绘制进度条并显示百分比文本)。
	// 参数:
	//   handle : 树表句柄
	//   column : 列索引(0 起);-1 关闭进度条(默认关闭,不是每种数据都需要)
	// 说明:建议在加载数据前调用。
	void QtTreeTableSetProgressColumn(void *handle, int column);

	// 设置列宽是否随窗口宽度按比例缩放(拖动 CAA 对话框扩宽/收窄时,
	// 字段宽度跟着成比例变化,基准为设置列时的窗口宽度与配置列宽)。
	// 参数:
	//   handle  : 树表句柄
	//   enabled : 1 开启(默认开启),0 关闭
	void QtTreeTableSetColumnAutoScale(void *handle, int enabled);

#ifdef __cplusplus
}
#endif

#endif
