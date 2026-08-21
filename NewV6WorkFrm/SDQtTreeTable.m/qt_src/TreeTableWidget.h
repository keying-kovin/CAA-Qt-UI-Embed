#ifndef TREE_TABLE_WIDGET_H
#define TREE_TABLE_WIDGET_H

#include <QWidget>
#include <QList>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QTreeWidget>
#include <QVector>

#include <functional>

#include "QtTreeTable.h"

class QTreeWidgetItem;
class QHeaderView;
class QScrollBar;

/**
 * 树表内容区。
 * 重写鼠标事件,使用户可以从任意单元格右边缘调整整列宽度,
 * 或从任意单元格下边缘调整整行高度。
 * 说明:行高变化通过 onRowHeightChanged 回调通知(不使用 Qt 信号,
 *       从而整个控件不需要 moc,构建流程与 CAA 工程 SDQtTable 一致)。
 */
class TreeTableContentView : public QTreeWidget
{
public:
	explicit TreeTableContentView(QWidget *parent = nullptr);
	void clearManualRowHeights();

	/// 行高变化后通知垂直表头与组合控件重绘。
	std::function<void()> onRowHeightChanged;

protected:
	/// 重写按下事件,命中单元格边缘时进入列宽或行高调整状态。
	void mousePressEvent(QMouseEvent *event) override;
	/// 重写移动事件,拖动时修改统一列宽/行高,悬停时显示正确的调整光标。
	void mouseMoveEvent(QMouseEvent *event) override;
	/// 重写释放事件,结束调整状态;非调整操作交回 QTreeWidget 处理。
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	enum ResizeMode { NoResize, ResizeColumn, ResizeRow };

	int columnBoundaryAt(const QPoint &position) const;
	QTreeWidgetItem *rowBoundaryItemAt(const QPoint &position) const;
	void setManualRowHeight(QTreeWidgetItem *item, int height);

	ResizeMode m_resizeMode;
	int m_resizeColumn;
	QTreeWidgetItem *m_resizeItem;
	int m_pressPosition;
	int m_originalSize;
	QHash<QTreeWidgetItem *, int> m_manualRowHeights;
};

/**
 * 进度条列绘制代理(仅对配置的进度列生效,其余列走默认绘制)。
 * 不先绘制单元格文本,避免进度条边缘露出多余文字。
 */
class ProgressDelegate : public QStyledItemDelegate
{
public:
	explicit ProgressDelegate(QObject *parent = nullptr);

	/// 设置进度条列索引(-1 关闭)。
	void setProgressColumn(int column);
	int progressColumn() const;

	void paint(
		QPainter *painter,
		const QStyleOptionViewItem &option,
		const QModelIndex &index) const override;

private:
	int m_progressColumn;
};

/**
 * 多级水平表头。
 * QHeaderView 原生只支持一行表头,这个类根据调用方传入的单元格描述
 * 自行计算合并单元格。内容区仍使用 QTreeWidget 的 header 管理列宽。
 * 默认不包含任何单元格,由 TreeTableWidget::setColumns 传入配置。
 */
class MultiLevelHeader : public QWidget
{
public:
	/// 表头单元格(支持横向/纵向合并)。
	struct HeaderCell
	{
		QString text;
		int row;
		int column;
		int rowSpan;
		int columnSpan;
		bool sortable;
	};

	explicit MultiLevelHeader(QTreeWidget *tree, QWidget *parent = nullptr);

	QSize sizeHint() const override;

	/// 用调用方提供的单元格重建表头。
	/// cells    : 单元格数组;传空数组表示清空表头
	/// rowHeight: 每行高度(像素),<=0 使用默认值 28
	void setHeader(const QVector<HeaderCell> &cells, int rowHeight);

	/// 点击可排序叶子表头后通知组合控件执行排序。
	std::function<void(int)> onSortRequested;

protected:
	/// 重写绘制事件,按当前列宽和滚动偏移绘制可见的合并表头单元格。
	void paintEvent(QPaintEvent *event) override;
	/// 重写鼠标按下事件,识别表头点击和最底层列分隔线拖动。
	void mousePressEvent(QMouseEvent *event) override;
	/// 重写鼠标移动事件,调整列宽或在分隔线处显示调整光标。
	void mouseMoveEvent(QMouseEvent *event) override;
	/// 重写鼠标释放事件,结束列宽拖动或触发叶子列排序。
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	QRect cellRect(const HeaderCell &cell) const;
	int leafColumnAt(const QPoint &position) const;
	int resizeBoundaryAt(const QPoint &position) const;

	QTreeWidget *m_tree;
	QVector<HeaderCell> m_cells;
	int m_rowHeight;
	int m_rowCount;
	int m_resizeColumn;
	int m_pressX;
	int m_originalWidth;
	int m_pressedColumn;
};

/**
 * 树控件没有原生垂直表头,此类根据每个可见 QModelIndex 的 visualRect 绘制行号。
 * 行高完全跟随内容区,不自己维护一套行高数据。
 */
class TreeVerticalHeader : public QWidget
{
public:
	explicit TreeVerticalHeader(QTreeWidget *tree, QWidget *parent = nullptr);

	QSize sizeHint() const override;

protected:
	/// 重写绘制事件,递归遍历已展开节点并仅绘制视口内的行号。
	void paintEvent(QPaintEvent *event) override;
	/// 重写鼠标按下事件,点击行号时选择对应的树节点。
	void mousePressEvent(QMouseEvent *event) override;

private:
	void collectVisibleItems(
		QTreeWidgetItem *item,
		QList<QTreeWidgetItem *> *items) const;
	QTreeWidget *m_tree;
};

/**
 * 完整树表组合控件:自绘多级表头 + QTreeWidget 内容区 + 垂直行头。
 * 初始为空表(无列、无表头、无数据),所有配置均由外部接口设置:
 *   - setColumns:列定义与多级表头;
 *   - setDataFile / loadData:数据文件与字段填充;
 *   - setProgressColumn:进度条列;
 *   - setStyle:行高、交替行色、样式表;
 *   - setAnimation:展开/折叠动画开关。
 */
class TreeTableWidget : public QWidget
{
public:
	explicit TreeTableWidget(QWidget *parent = nullptr);

	QTreeWidget *treeWidget() const;

	// 设置列与多级表头(从零开始,调用前表格为空表)。
	// columns         : 列定义(id/表头/JSON字段/宽度/数值排序),数量即列数
	// headerCells     : 多级表头单元格;传 NULL 且 headerCellCount 为 0 时,
	//                    按列标题生成单行表头
	// headerRowHeight : 表头每行高度(像素),<=0 使用默认值 28
	void setColumns(
		const QtTreeColumn *columns,
		int columnCount,
		const QtTreeHeaderCell *headerCells,
		int headerCellCount,
		int headerRowHeight);

	// 设置数据文件路径(LoadData 时按列定义的 field 字段读取)。
	void setDataFile(const QString &filePath);

	// 按列字段从已设置的数据文件加载并填入表格。
	// 返回:true 成功,false 失败。
	bool loadData();

	// 从 JSON 文件加载树数据(UTF-8 编码,直接写中文字符)。
	// 返回:true 成功,false 失败。
	bool loadFromFile(const QString &filePath);

	// 从 JSON 文本加载树数据(内部实现,LoadFromFile 最终走这里)。
	bool loadFromJson(const QByteArray &jsonData);

	// 从 C 结构体数组填充树数据(响应事件后由业务代码调用)。
	bool setData(const QtTreeNode *roots, int rootCount);

	// 清空所有节点(保留列定义与样式)。
	void clearData();

	// 注册行点击回调。
	void setRowClickCallback(
		QtTreeRowClickCallback callback,
		void *context);

	// 设置样式(行高、交替行色、样式表)。
	void setStyle(const QtTreeStyle *style);

	// 设置进度条列(-1 关闭,默认关闭;建议在加载数据前设置)。
	void setProgressColumn(int column);

	// 设置树表动画开关(节点展开/折叠动画)。
	// enabled   : true 启用动画,false 关闭(默认关闭);
	// durationMs: 动画时长(毫秒),供后续行高/展开动画扩展使用,
	//             QTreeWidget 原生展开动画时长由系统样式决定,此参数暂为预留。
	void setAnimation(bool enabled, int durationMs);

	// 设置列宽是否随窗口宽度按比例缩放(拖动 CAA 对话框扩宽/收窄时,
	// 字段宽度跟着成比例变化)。
	// enabled: true 开启(默认开启),false 关闭。
	void setColumnAutoScale(bool enabled);

protected:
	/// 窗口宽度变化时按比例缩放各列宽度(拖动 CAA 对话框扩宽时字段自动变宽)。
	void resizeEvent(QResizeEvent *event) override;

private:
	struct ColumnInfo
	{
		QString id;
		QString title;
		QString field;
		int width;
		bool numeric;
	};

	QTreeWidgetItem *createItem(
		const QStringList &values,
		int progress,
		bool checkable);
	QTreeWidgetItem *createItemFromJson(const QJsonObject &node);
	QTreeWidgetItem *createItemFromNode(const QtTreeNode &node);
	int stateColumn() const;
	void applyExpandedFlags(QTreeWidgetItem *parent);
	void collectVisibleItems(
		QTreeWidgetItem *item,
		QList<QTreeWidgetItem *> *items) const;
	int visibleRowOf(QTreeWidgetItem *item) const;
	void onItemClicked(QTreeWidgetItem *item, int column);
	void onSectionResized(int logicalIndex, int oldSize, int newSize);
	void applyColumnScale();
	void refreshScaleBase();

	void refreshHeaders();
	void sortByColumn(int column);

	TreeTableContentView *m_tree;
	MultiLevelHeader *m_horizontalHeader;
	TreeVerticalHeader *m_verticalHeader;
	QWidget *m_corner;
	ProgressDelegate *m_progressDelegate;
	QVector<ColumnInfo> m_columns;
	QtTreeRowClickCallback m_rowClickCallback;
	void *m_rowClickContext;
	QString m_dataFilePath;
	int m_progressColumn;
	int m_animationDuration;
	bool m_columnAutoScale;
	int m_baseWidth;
	QVector<int> m_baseColumnWidths;
	bool m_scalingColumns;
	int m_sortColumn;
	Qt::SortOrder m_sortOrder;
};

#endif // TREE_TABLE_WIDGET_H
