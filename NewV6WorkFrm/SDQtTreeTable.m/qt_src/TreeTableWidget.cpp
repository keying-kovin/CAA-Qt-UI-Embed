#include "TreeTableWidget.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QFile>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionHeader>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
#include <QTreeWidget>

namespace
{

/**
 * 树节点比较规则。
 * QTreeWidget 默认按显示文本比较,百分比和金额会产生字典序错误,
 * 因此对数值列(5 完成进度 / 6 预算 / 7 实际)读取 UserRole 中保存的数值比较。
 */
class DemoTreeItem : public QTreeWidgetItem
{
public:
	explicit DemoTreeItem(const QStringList &values)
		: QTreeWidgetItem(values)
	{
	}

	bool operator<(const QTreeWidgetItem &other) const override
	{
		const int column = treeWidget() ? treeWidget()->sortColumn() : 0;
		// 数值列(列定义 numeric=1)按 UserRole+5 中保存的数值比较,避免字典序错误。
		const QVariant numeric = data(column, Qt::UserRole + 5);
		if (numeric.isValid())
			return numeric.toDouble() < other.data(column, Qt::UserRole + 5).toDouble();
		return QTreeWidgetItem::operator<(other);
	}
};

} // namespace

// 进度列采用绘制代理而非嵌入 QProgressBar,这样进度单元格也能接收到
// 内容视图的边缘拖拽操作。仅对配置的进度列生效,其余列走默认绘制。
ProgressDelegate::ProgressDelegate(QObject *parent)
	: QStyledItemDelegate(parent),
	m_progressColumn(-1)
{
}

void ProgressDelegate::setProgressColumn(int column)
{
	m_progressColumn = column;
}

int ProgressDelegate::progressColumn() const
{
	return m_progressColumn;
}

void ProgressDelegate::paint(
	QPainter *painter,
	const QStyleOptionViewItem &option,
	const QModelIndex &index) const
{
	if (index.column() != m_progressColumn)
	{
		QStyledItemDelegate::paint(painter, option, index);
		return;
	}

	// 先画背景/选中态但不画单元格文本,避免进度条边缘露出多余文字。
	QStyleOptionViewItem baseOption = option;
	initStyleOption(&baseOption, index);
	baseOption.text.clear();
	QStyledItemDelegate::paint(painter, baseOption, index);

	// 进度条覆盖整个单元格(仅留 1px 边距),百分比文本由进度条自绘。
	QStyleOptionProgressBar progressOption;
	progressOption.initFrom(option.widget);
	progressOption.rect = option.rect.adjusted(1, 1, -1, -1);
	progressOption.minimum = 0;
	progressOption.maximum = 100;
	progressOption.progress = index.data(Qt::UserRole).toInt();
	progressOption.text = QString::number(progressOption.progress) + "%";
	progressOption.textVisible = true;
	progressOption.textAlignment = Qt::AlignCenter;
	QApplication::style()->drawControl(
		QStyle::CE_ProgressBar,
		&progressOption,
		painter,
		option.widget);
}

TreeTableContentView::TreeTableContentView(QWidget *parent)
	: QTreeWidget(parent),
	m_resizeMode(NoResize),
	m_resizeColumn(-1),
	m_resizeItem(nullptr),
	m_pressPosition(0),
	m_originalSize(0)
{
	setMouseTracking(true);
	// 禁用省略号。列宽增加时,原先被裁剪的文本会自然显示出来。
	setTextElideMode(Qt::ElideNone);
}

void TreeTableContentView::clearManualRowHeights()
{
	// clear() 会销毁所有 QTreeWidgetItem,重新加载前必须丢弃旧指针。
	m_manualRowHeights.clear();
}

int TreeTableContentView::columnBoundaryAt(const QPoint &position) const
{
	const QModelIndex index = indexAt(position);
	if (!index.isValid())
		return -1;

	const QRect cellRect = visualRect(index);
	const int margin = 4;
	if (qAbs(position.x() - cellRect.right()) <= margin)
		return index.column();
	if (index.column() > 0 && qAbs(position.x() - cellRect.left()) <= margin)
		return index.column() - 1;
	return -1;
}

QTreeWidgetItem *TreeTableContentView::rowBoundaryItemAt(
	const QPoint &position) const
{
	const QModelIndex index = indexAt(position);
	if (!index.isValid())
		return nullptr;

	// 仅识别当前行底边,避免在相邻行之间出现两个行高调整目标。
	const QRect cellRect = visualRect(index);
	return qAbs(position.y() - cellRect.bottom()) <= 4
		? itemFromIndex(index)
		: nullptr;
}

void TreeTableContentView::setManualRowHeight(
	QTreeWidgetItem *item,
	int height)
{
	const int finalHeight = qMax(27, height);
	m_manualRowHeights.insert(item, finalHeight);

	// 每列设定相同高度,保证树列、进度列和普通内容列使用一个行高。
	for (int column = 0; column < columnCount(); ++column)
		item->setSizeHint(column, QSize(0, finalHeight));

	doItemsLayout();
	viewport()->update();
	if (onRowHeightChanged)
		onRowHeightChanged();
}

void TreeTableContentView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
	{
		QTreeWidget::mousePressEvent(event);
		return;
	}

	const int column = columnBoundaryAt(event->pos());
	QTreeWidgetItem *item = rowBoundaryItemAt(event->pos());
	if (column >= 0)
	{
		m_resizeMode = ResizeColumn;
		m_resizeColumn = column;
		m_pressPosition = event->pos().x();
		m_originalSize = header()->sectionSize(column);
		viewport()->setCursor(Qt::SplitHCursor);
		event->accept();
		return;
	}
	if (item)
	{
		m_resizeMode = ResizeRow;
		m_resizeItem = item;
		m_pressPosition = event->pos().y();
		m_originalSize = visualItemRect(item).height();
		viewport()->setCursor(Qt::SplitVCursor);
		event->accept();
		return;
	}

	QTreeWidget::mousePressEvent(event);
}

void TreeTableContentView::mouseMoveEvent(QMouseEvent *event)
{
	if (m_resizeMode == ResizeColumn && (event->buttons() & Qt::LeftButton))
	{
		const int width = qMax(
			75,
			m_originalSize + event->pos().x() - m_pressPosition);
		header()->resizeSection(m_resizeColumn, width);
		return;
	}
	if (m_resizeMode == ResizeRow && (event->buttons() & Qt::LeftButton))
	{
		setManualRowHeight(
			m_resizeItem,
			m_originalSize + event->pos().y() - m_pressPosition);
		return;
	}

	const int column = columnBoundaryAt(event->pos());
	QTreeWidgetItem *item = rowBoundaryItemAt(event->pos());
	viewport()->setCursor(
		column >= 0 ? Qt::SplitHCursor
		: item ? Qt::SplitVCursor
		: Qt::ArrowCursor);
	QTreeWidget::mouseMoveEvent(event);
}

void TreeTableContentView::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_resizeMode != NoResize)
	{
		m_resizeMode = NoResize;
		m_resizeColumn = -1;
		m_resizeItem = nullptr;
		viewport()->unsetCursor();
		event->accept();
		return;
	}
	QTreeWidget::mouseReleaseEvent(event);
}

MultiLevelHeader::MultiLevelHeader(QTreeWidget *tree, QWidget *parent)
	: QWidget(parent),
	m_tree(tree),
	m_rowHeight(28),
	m_rowCount(1),
	m_resizeColumn(-1),
	m_pressX(0),
	m_originalWidth(0),
	m_pressedColumn(-1)
{
	setMouseTracking(true);
	// 初始为空表头,由 TreeTableWidget::setColumns 传入配置。
	setMinimumHeight(m_rowHeight * m_rowCount);
}

QSize MultiLevelHeader::sizeHint() const
{
	return QSize(400, m_rowHeight * m_rowCount);
}

void MultiLevelHeader::setHeader(
	const QVector<HeaderCell> &cells,
	int rowHeight)
{
	m_cells = cells;
	if (rowHeight > 0)
		m_rowHeight = rowHeight;

	// 表头总行数 = 单元格最大 row+rowSpan,空表头至少保留一行。
	int rowCount = 1;
	for (const HeaderCell &cell : m_cells)
		rowCount = qMax(rowCount, cell.row + cell.rowSpan);
	m_rowCount = rowCount;

	setMinimumHeight(m_rowHeight * m_rowCount);
	updateGeometry();
	update();
}

QRect MultiLevelHeader::cellRect(const HeaderCell &cell) const
{
	QHeaderView *header = m_tree->header();
	const int left = header->sectionViewportPosition(cell.column);
	int width = 0;
	for (int column = cell.column; column < cell.column + cell.columnSpan; ++column)
		width += header->sectionSize(column);
	return QRect(left, cell.row * m_rowHeight, width, cell.rowSpan * m_rowHeight);
}

int MultiLevelHeader::leafColumnAt(const QPoint &position) const
{
	// 只有最底层表头行能直接对应一个内容列,分组表头默认不参与排序。
	if (position.y() < m_rowHeight * (m_rowCount - 1))
		return -1;
	for (const HeaderCell &cell : m_cells)
	{
		if (cell.row + cell.rowSpan == m_rowCount
			&& cell.sortable
			&& cellRect(cell).contains(position))
			return cell.column;
	}
	return -1;
}

int MultiLevelHeader::resizeBoundaryAt(const QPoint &position) const
{
	// 列宽只允许在最底层行拖动,防止用户误拖动分组合并表头内部。
	if (position.y() < m_rowHeight * (m_rowCount - 1))
		return -1;

	QHeaderView *header = m_tree->header();
	for (int column = 0; column < header->count() - 1; ++column)
	{
		const int right = header->sectionViewportPosition(column)
			+ header->sectionSize(column);
		if (qAbs(position.x() - right) <= 4)
			return column;
	}
	return -1;
}

void MultiLevelHeader::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event)
	QPainter painter(this);
	painter.fillRect(rect(), palette().color(QPalette::Button));

	for (const HeaderCell &cell : m_cells)
	{
		const QRect target = cellRect(cell);
		if (!target.intersects(rect()))
			continue;

		QStyleOptionHeader option;
		option.initFrom(this);
		option.rect = target.adjusted(0, 0, -1, -1);
		option.text = cell.text;
		option.textAlignment = Qt::AlignCenter;
		option.position = QStyleOptionHeader::Middle;
		option.state |= QStyle::State_Raised;
		style()->drawControl(QStyle::CE_Header, &option, &painter, this);

		// 排序状态只展示在叶子列,避免分组表头给出误导性的排序提示。
		if (cell.sortable
			&& cell.row + cell.rowSpan == m_rowCount
			&& m_tree->isSortingEnabled()
			&& m_tree->sortColumn() == cell.column)
		{
			QStyleOptionHeader arrowOption = option;
			arrowOption.sortIndicator =
				m_tree->header()->sortIndicatorOrder() == Qt::AscendingOrder
				? QStyleOptionHeader::SortDown
				: QStyleOptionHeader::SortUp;
			style()->drawPrimitive(
				QStyle::PE_IndicatorHeaderArrow,
				&arrowOption,
				&painter,
				this);
		}
	}
}

void MultiLevelHeader::mousePressEvent(QMouseEvent *event)
{
	m_resizeColumn = resizeBoundaryAt(event->pos());
	m_pressedColumn = leafColumnAt(event->pos());
	m_pressX = event->pos().x();
	if (m_resizeColumn >= 0)
	{
		m_originalWidth = m_tree->header()->sectionSize(m_resizeColumn);
		setCursor(Qt::SplitHCursor);
		event->accept();
		return;
	}
	QWidget::mousePressEvent(event);
}

void MultiLevelHeader::mouseMoveEvent(QMouseEvent *event)
{
	if (m_resizeColumn >= 0 && (event->buttons() & Qt::LeftButton))
	{
		const int width = qMax(
			75,
			m_originalWidth + event->pos().x() - m_pressX);
		m_tree->header()->resizeSection(m_resizeColumn, width);
		update();
		return;
	}
	setCursor(resizeBoundaryAt(event->pos()) >= 0
		? Qt::SplitHCursor
		: Qt::ArrowCursor);
	QWidget::mouseMoveEvent(event);
}

void MultiLevelHeader::mouseReleaseEvent(QMouseEvent *event)
{
	if (m_resizeColumn >= 0)
	{
		m_resizeColumn = -1;
		setCursor(Qt::ArrowCursor);
		return;
	}
	const int releasedColumn = leafColumnAt(event->pos());
	if (m_pressedColumn >= 0 && m_pressedColumn == releasedColumn)
	{
		if (onSortRequested)
			onSortRequested(releasedColumn);
	}
	m_pressedColumn = -1;
	QWidget::mouseReleaseEvent(event);
}

TreeVerticalHeader::TreeVerticalHeader(QTreeWidget *tree, QWidget *parent)
	: QWidget(parent),
	m_tree(tree)
{
	setMinimumWidth(48);
	connect(
		m_tree->verticalScrollBar(),
		&QScrollBar::valueChanged,
		this,
		[this](int) { update(); });
	connect(
		m_tree,
		&QTreeWidget::itemExpanded,
		this,
		[this](QTreeWidgetItem *) { update(); });
	connect(
		m_tree,
		&QTreeWidget::itemCollapsed,
		this,
		[this](QTreeWidgetItem *) { update(); });
}

QSize TreeVerticalHeader::sizeHint() const
{
	return QSize(48, 200);
}

void TreeVerticalHeader::collectVisibleItems(
	QTreeWidgetItem *item,
	QList<QTreeWidgetItem *> *items) const
{
	items->append(item);
	if (!item->isExpanded())
		return;
	for (int row = 0; row < item->childCount(); ++row)
		collectVisibleItems(item->child(row), items);
}

void TreeVerticalHeader::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event)
	QPainter painter(this);
	painter.fillRect(rect(), palette().color(QPalette::Button));
	painter.setPen(palette().color(QPalette::Mid));

	QList<QTreeWidgetItem *> items;
	for (int row = 0; row < m_tree->topLevelItemCount(); ++row)
		collectVisibleItems(m_tree->topLevelItem(row), &items);

	int rowNumber = 1;
	for (QTreeWidgetItem *item : items)
	{
		const QRect itemRect = m_tree->visualItemRect(item);
		if (!itemRect.isValid()
			|| itemRect.bottom() < 0
			|| itemRect.top() >= height())
		{
			++rowNumber;
			continue;
		}
		const QRect numberRect(
			0,
			itemRect.top(),
			width() - 1,
			itemRect.height());
		painter.drawText(numberRect, Qt::AlignCenter, QString::number(rowNumber));
		painter.drawLine(numberRect.bottomLeft(), numberRect.bottomRight());
		++rowNumber;
	}
	painter.drawLine(width() - 1, 0, width() - 1, height());
}

void TreeVerticalHeader::mousePressEvent(QMouseEvent *event)
{
	// 用内容区 y 坐标寻找节点,行号区和内容区始终共享同一垂直滚动位置。
	QTreeWidgetItem *item = m_tree->itemAt(4, event->pos().y());
	if (item)
		m_tree->setCurrentItem(item);
	QWidget::mousePressEvent(event);
}

TreeTableWidget::TreeTableWidget(QWidget *parent)
	: QWidget(parent),
	m_rowClickCallback(nullptr),
	m_rowClickContext(nullptr),
	m_dataFilePath(),
	m_progressColumn(-1),
	m_animationDuration(0),
	m_columnAutoScale(true),
	m_scalingColumns(false),
	m_baseWidth(0),
	m_sortColumn(-1),
	m_sortOrder(Qt::AscendingOrder)
{
	m_tree = new TreeTableContentView(this);
	m_tree->setRootIsDecorated(true);
	m_tree->setAlternatingRowColors(true);
	m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_tree->setSortingEnabled(true);
	m_tree->setUniformRowHeights(false);
	m_tree->setAnimated(false);
	m_tree->setIndentation(22);

	// 初始为空表:不设置任何列/表头,进度条列默认关闭,
	// 所有配置由外部接口(setColumns/setProgressColumn/setStyle)提供。
	m_progressDelegate = new ProgressDelegate(m_tree);
	m_tree->setItemDelegate(m_progressDelegate);

	m_horizontalHeader = new MultiLevelHeader(m_tree, this);
	m_verticalHeader = new TreeVerticalHeader(m_tree, this);

	m_corner = new QWidget(this);
	m_corner->setFixedSize(
		m_verticalHeader->sizeHint().width(),
		m_horizontalHeader->sizeHint().height());
	m_corner->setStyleSheet(
		"background: palette(button); border-right: 1px solid palette(mid);");

	QGridLayout *layout = new QGridLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(m_corner, 0, 0);
	layout->addWidget(m_horizontalHeader, 0, 1);
	layout->addWidget(m_verticalHeader, 1, 0);
	layout->addWidget(m_tree, 1, 1);
	layout->setRowStretch(1, 1);
	layout->setColumnStretch(1, 1);

	QHeaderView *contentHeader = m_tree->header();
	contentHeader->setSectionsMovable(false);
	contentHeader->setStretchLastSection(false);
	connect(
		contentHeader,
		&QHeaderView::sectionResized,
		this,
		&TreeTableWidget::refreshHeaders);
	connect(
		contentHeader,
		&QHeaderView::sectionResized,
		this,
		&TreeTableWidget::onSectionResized);
	connect(
		m_tree->horizontalScrollBar(),
		&QScrollBar::valueChanged,
		this,
		&TreeTableWidget::refreshHeaders);
	m_tree->onRowHeightChanged = [this]() { refreshHeaders(); };
	m_horizontalHeader->onSortRequested =
		[this](int column) { sortByColumn(column); };
	connect(
		m_tree,
		&QTreeWidget::itemClicked,
		this,
		&TreeTableWidget::onItemClicked);
}

QTreeWidget *TreeTableWidget::treeWidget() const
{
	return m_tree;
}

void TreeTableWidget::setColumns(
	const QtTreeColumn *columns,
	int columnCount,
	const QtTreeHeaderCell *headerCells,
	int headerCellCount,
	int headerRowHeight)
{
	m_columns.clear();
	if (columns && columnCount > 0)
	{
		for (int index = 0; index < columnCount; ++index)
		{
			ColumnInfo info;
			info.id = QString::fromLocal8Bit(columns[index].id ? columns[index].id : "");
			info.title = QString::fromLocal8Bit(columns[index].title ? columns[index].title : "");
			info.field = QString::fromLocal8Bit(columns[index].field ? columns[index].field : "");
			info.width = columns[index].width;
			info.numeric = columns[index].numeric != 0;
			m_columns.append(info);
		}
	}

	m_tree->setSortingEnabled(false);
	m_tree->clearManualRowHeights();
	m_tree->clear();
	m_tree->setColumnCount(m_columns.size());
	m_tree->setHeaderHidden(true);

	QHeaderView *contentHeader = m_tree->header();
	for (int column = 0; column < m_columns.size(); ++column)
	{
		contentHeader->resizeSection(
			column,
			m_columns[column].width > 0 ? m_columns[column].width : 100);
	}

	// 多级表头:未提供单元格时按列标题生成单行表头。
	QVector<MultiLevelHeader::HeaderCell> cells;
	if (headerCells && headerCellCount > 0)
	{
		for (int index = 0; index < headerCellCount; ++index)
		{
			MultiLevelHeader::HeaderCell cell;
			cell.text = QString::fromLocal8Bit(
				headerCells[index].title ? headerCells[index].title : "");
			cell.row = headerCells[index].row;
			cell.column = headerCells[index].column;
			cell.rowSpan = headerCells[index].rowSpan > 0
				? headerCells[index].rowSpan
				: 1;
			cell.columnSpan = headerCells[index].columnSpan > 0
				? headerCells[index].columnSpan
				: 1;
			cell.sortable = headerCells[index].sortable != 0;
			cells.append(cell);
		}
	}
	else
	{
		for (int column = 0; column < m_columns.size(); ++column)
		{
			MultiLevelHeader::HeaderCell cell;
			cell.text = m_columns[column].title;
			cell.row = 0;
			cell.column = column;
			cell.rowSpan = 1;
			cell.columnSpan = 1;
			cell.sortable = true;
			cells.append(cell);
		}
	}
	m_horizontalHeader->setHeader(cells, headerRowHeight);

	// 表头尺寸变化后同步左上角方块。
	if (m_corner)
	{
		m_corner->setFixedSize(
			m_verticalHeader->sizeHint().width(),
			m_horizontalHeader->sizeHint().height());
	}

	m_tree->setSortingEnabled(true);
	refreshHeaders();

	// 列宽缩放基准:记录"当前窗口宽度 + 各列实际宽度"。
	// 开启自动缩放时,若配置总宽超过窗口,先按比例收进窗口,
	// 避免初始布局就超出对话框宽度;手动拖列宽也会同步更新基准。
	m_baseWidth = 0;
	m_baseColumnWidths.clear();
	{
		QHeaderView *header = m_tree->header();
		const int windowWidth = width();
		int total = 0;
		for (int column = 0; column < m_columns.size(); ++column)
			total += m_columns[column].width > 0 ? m_columns[column].width : 100;

		const qreal fitRatio =
			m_columnAutoScale && windowWidth > 0 && total > windowWidth
			? static_cast<qreal>(windowWidth) / total
			: 1.0;

		m_scalingColumns = true; // 初始收进窗口的列宽由程序设置,不触发手动基准更新
		for (int column = 0; column < m_columns.size(); ++column)
		{
			const int baseColumnWidth = m_columns[column].width > 0
				? m_columns[column].width
				: 100;
			const int fittedWidth = qMax(30, qRound(baseColumnWidth * fitRatio));
			m_baseColumnWidths.append(fittedWidth);
			if (fitRatio < 1.0)
				header->resizeSection(column, fittedWidth);
		}
		m_scalingColumns = false;
		if (windowWidth > 0)
			m_baseWidth = windowWidth;
	}
}

// 窗口宽度变化时按比例缩放各列宽度(开关见 setColumnAutoScale)。
void TreeTableWidget::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);

	if (m_columns.isEmpty() || !m_columnAutoScale)
		return;

	applyColumnScale();
}

// 按"固定基准"重算各列宽度:newWidth = 基准列宽 × 当前窗口宽度 / 基准窗口宽度。
// 基准 = 当前实际列宽 + 当前窗口宽度(手动拖列宽时会同步更新基准),
// 因此窗口缩放始终基于"当前比例",不会跳回配置默认值,也不会只缩不扩。
void TreeTableWidget::applyColumnScale()
{
	const int currentWidth = width();
	if (currentWidth <= 0)
		return;

	// 首次布局兜底:以当前窗口宽度与各列实际宽度为基准。
	if (m_baseWidth <= 0 || m_baseColumnWidths.size() != m_columns.size())
	{
		refreshScaleBase();
		return;
	}

	const qreal ratio = static_cast<qreal>(currentWidth) / m_baseWidth;
	m_scalingColumns = true; // 本节流中由程序设置的列宽,不更新手动基准
	QHeaderView *header = m_tree->header();
	for (int column = 0; column < m_columns.size(); ++column)
	{
		const int newWidth = qMax(
			30,
			qRound(m_baseColumnWidths[column] * ratio));
		header->resizeSection(column, newWidth);
	}
	m_scalingColumns = false;
}

// 以"当前窗口宽度 + 当前各列实际宽度"重建缩放基准。
void TreeTableWidget::refreshScaleBase()
{
	const int currentWidth = width();
	if (currentWidth <= 0)
		return;
	m_baseWidth = currentWidth;
	m_baseColumnWidths.clear();
	QHeaderView *header = m_tree->header();
	for (int column = 0; column < m_columns.size(); ++column)
		m_baseColumnWidths.append(header->sectionSize(column));
}

// 打开/关闭列宽随窗口宽度自动缩放;打开时立即按当前宽度重算一次。
void TreeTableWidget::setColumnAutoScale(bool enabled)
{
	m_columnAutoScale = enabled;
	if (enabled)
	{
		// 开启时以当前布局为基准,再按当前宽度重算一次,
		// 保证后续缩放基于"已经改好的当前比例"。
		refreshScaleBase();
		applyColumnScale();
	}
}

// 用户手动拖动列宽时,把新宽度换算回基准窗口宽度下的尺度再更新基准,
// 保证后续窗口缩放仍按同一比例进行,不会出现比例失调。
void TreeTableWidget::onSectionResized(
	int logicalIndex,
	int oldSize,
	int newSize)
{
	Q_UNUSED(oldSize);
	if (m_scalingColumns || !m_columnAutoScale)
		return;
	if (logicalIndex < 0 || logicalIndex >= m_baseColumnWidths.size())
		return;

	const int currentWidth = width();
	if (m_baseWidth > 0 && currentWidth > 0)
	{
		m_baseColumnWidths[logicalIndex] = qRound(
			static_cast<qreal>(newSize) * m_baseWidth / currentWidth);
	}
	else
	{
		m_baseColumnWidths[logicalIndex] = newSize;
	}
}

// 设置数据文件路径(LoadData 时按列定义的 field 字段读取)。
void TreeTableWidget::setDataFile(const QString &filePath)
{
	m_dataFilePath = filePath;
}

// 按列字段从已设置的数据文件加载并填入表格。
bool TreeTableWidget::loadData()
{
	if (m_dataFilePath.isEmpty())
		return false;
	return loadFromFile(m_dataFilePath);
}

// 设置进度条列(-1 关闭,默认关闭;建议在加载数据前调用)。
void TreeTableWidget::setProgressColumn(int column)
{
	m_progressColumn = column;
	if (m_progressDelegate)
		m_progressDelegate->setProgressColumn(column);
	m_tree->viewport()->update();
}

bool TreeTableWidget::loadFromFile(const QString &filePath)
{
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly))
		return false;

	const QByteArray data = file.readAll();
	file.close();
	return loadFromJson(data);
}

bool TreeTableWidget::loadFromJson(const QByteArray &jsonData)
{
	QJsonParseError parseError;
	const QJsonDocument document =
		QJsonDocument::fromJson(jsonData, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject())
		return false;

	const QJsonObject root = document.object();

	// 清空旧数据后按 JSON 递归构建树。
	m_tree->setSortingEnabled(false);
	m_tree->clearManualRowHeights();
	m_tree->clear();

	const QJsonArray rows = root.value("rows").toArray();
	for (int index = 0; index < rows.size(); ++index)
	{
		QTreeWidgetItem *item = createItemFromJson(rows.at(index).toObject());
		if (item)
			m_tree->addTopLevelItem(item);
	}

	applyExpandedFlags(m_tree->invisibleRootItem());
	if (m_tree->topLevelItemCount() > 0)
		m_tree->setCurrentItem(m_tree->topLevelItem(0));

	m_tree->setSortingEnabled(true);
	refreshHeaders();
	return true;
}

bool TreeTableWidget::setData(const QtTreeNode *roots, int rootCount)
{
	m_tree->setSortingEnabled(false);
	m_tree->clearManualRowHeights();
	m_tree->clear();

	if (roots && rootCount > 0)
	{
		for (int index = 0; index < rootCount; ++index)
		{
			QTreeWidgetItem *item = createItemFromNode(roots[index]);
			if (item)
				m_tree->addTopLevelItem(item);
		}
		applyExpandedFlags(m_tree->invisibleRootItem());
		if (m_tree->topLevelItemCount() > 0)
			m_tree->setCurrentItem(m_tree->topLevelItem(0));
	}

	m_tree->setSortingEnabled(true);
	refreshHeaders();
	return true;
}

void TreeTableWidget::clearData()
{
	m_tree->setSortingEnabled(false);
	m_tree->clearManualRowHeights();
	m_tree->clear();
	m_tree->setSortingEnabled(true);
	refreshHeaders();
}

QTreeWidgetItem *TreeTableWidget::createItem(
	const QStringList &values,
	int progress,
	bool checkable)
{
	QTreeWidgetItem *item = new DemoTreeItem(values);

	// 进度条列:百分比文本由数值生成,数值存入 UserRole 供绘制代理使用。
	if (m_progressColumn >= 0 && m_progressColumn < values.size())
	{
		item->setData(m_progressColumn, Qt::UserRole, progress);
		item->setText(m_progressColumn, QString::number(progress) + "%");
	}

	// 数值列(列定义 numeric=1):存入 UserRole+5 供排序比较。
	for (int column = 0; column < m_columns.size() && column < values.size(); ++column)
	{
		if (m_columns[column].numeric)
			item->setData(column, Qt::UserRole + 5, values[column].toDouble());
	}

	// 状态图标:取自 id 为 "state" 的列;未配置该列则不区分状态图标。
	const int stateColumnIndex = stateColumn();
	const QString state = stateColumnIndex >= 0 && stateColumnIndex < values.size()
		? values[stateColumnIndex]
		: QString();
	item->setIcon(
		0,
		style()->standardIcon(
			state == QStringLiteral("已完成")
			? QStyle::SP_DialogApplyButton
			: QStyle::SP_BrowserReload));
	if (checkable)
	{
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(0, Qt::Unchecked);
	}
	return item;
}

// 返回 id 为 "state" 的列索引(用于状态图标),未配置返回 -1。
int TreeTableWidget::stateColumn() const
{
	for (int column = 0; column < m_columns.size(); ++column)
	{
		if (m_columns[column].id == QStringLiteral("state"))
			return column;
	}
	return -1;
}

QTreeWidgetItem *TreeTableWidget::createItemFromJson(const QJsonObject &node)
{
	// 按列定义的 field 字段从 JSON 节点取值,未定义 field 的列显示为空。
	QStringList values;
	for (int column = 0; column < m_columns.size(); ++column)
	{
		const QString field = m_columns[column].field;
		values << (field.isEmpty() ? QString() : node.value(field).toString());
	}

	// 进度值取自进度列对应的 JSON 字段。
	int progress = 0;
	if (m_progressColumn >= 0 && m_progressColumn < m_columns.size())
	{
		const QString field = m_columns[m_progressColumn].field;
		if (!field.isEmpty())
			progress = node.value(field).toInt(0);
	}

	const bool checkable = node.value("checkable").toBool(false);
	QTreeWidgetItem *item = createItem(values, progress, checkable);
	// 节点级保留字段:expanded 默认展开,remark 备注文本(暂不显示,保留在数据中)。
	item->setData(0, Qt::UserRole + 1, node.value("expanded").toBool(false));
	item->setData(0, Qt::UserRole + 2, node.value("remark").toString());

	const QJsonArray children = node.value("children").toArray();
	for (int index = 0; index < children.size(); ++index)
	{
		QTreeWidgetItem *child =
			createItemFromJson(children.at(index).toObject());
		if (child)
			item->addChild(child);
	}
	return item;
}

QTreeWidgetItem *TreeTableWidget::createItemFromNode(const QtTreeNode &node)
{
	// 单元格文本按"系统本地编码"解码(与 CAA 工程 CAAQtTable 的约定一致)。
	const int cellCount = qMin(node.cellCount, m_columns.size());
	QStringList values;
	for (int index = 0; index < cellCount; ++index)
		values << QString::fromLocal8Bit(node.cells ? node.cells[index] : "");
	while (values.size() < m_columns.size())
		values << QString();

	const int progress = node.progress;
	QTreeWidgetItem *item =
		createItem(values, progress, node.checkable != 0);
	item->setData(0, Qt::UserRole + 1, node.expanded != 0);

	if (node.childCount > 0 && node.children)
	{
		for (int index = 0; index < node.childCount; ++index)
		{
			QTreeWidgetItem *child = createItemFromNode(node.children[index]);
			if (child)
				item->addChild(child);
		}
	}
	return item;
}

void TreeTableWidget::applyExpandedFlags(QTreeWidgetItem *parent)
{
	const int count = parent->childCount();
	for (int index = 0; index < count; ++index)
	{
		QTreeWidgetItem *child = parent->child(index);
		child->setExpanded(child->data(0, Qt::UserRole + 1).toBool());
		applyExpandedFlags(child);
	}
}

void TreeTableWidget::collectVisibleItems(
	QTreeWidgetItem *item,
	QList<QTreeWidgetItem *> *items) const
{
	items->append(item);
	if (!item->isExpanded())
		return;
	for (int row = 0; row < item->childCount(); ++row)
		collectVisibleItems(item->child(row), items);
}

int TreeTableWidget::visibleRowOf(QTreeWidgetItem *item) const
{
	QList<QTreeWidgetItem *> items;
	for (int row = 0; row < m_tree->topLevelItemCount(); ++row)
		collectVisibleItems(m_tree->topLevelItem(row), &items);
	return items.indexOf(item);
}

void TreeTableWidget::onItemClicked(QTreeWidgetItem *item, int column)
{
	if (!item || column < 0 || column >= m_columns.size())
		return;
	if (!m_rowClickCallback)
		return;

	const int row = visibleRowOf(item);
	if (row < 0)
		return;

	const QByteArray columnId = m_columns[column].id.toLocal8Bit();
	const QByteArray cellText = item->text(column).toLocal8Bit();
	m_rowClickCallback(
		m_rowClickContext,
		row,
		column,
		columnId.constData(),
		cellText.constData());
}

void TreeTableWidget::setRowClickCallback(
	QtTreeRowClickCallback callback,
	void *context)
{
	m_rowClickCallback = callback;
	m_rowClickContext = context;
}

void TreeTableWidget::setStyle(const QtTreeStyle *style)
{
	if (!style)
		return;

	const int rowHeight = style->rowHeight > 0 ? style->rowHeight : 27;
	if (style->styleSheet && style->styleSheet[0] != '\0')
	{
		m_tree->setStyleSheet(QString::fromLocal8Bit(style->styleSheet));
	}
	else
	{
		m_tree->setStyleSheet(
			QString("QTreeWidget::item { min-height: %1px; }"
					"QTreeWidget::item:selected { background: #cfe8ff; color: #202020; }")
				.arg(rowHeight));
	}
	m_tree->setAlternatingRowColors(style->alternatingRowColors != 0);
}

// 打开/关闭树表展开与折叠动画。
void TreeTableWidget::setAnimation(bool enabled, int durationMs)
{
	m_tree->setAnimated(enabled);
	m_animationDuration = durationMs > 0 ? durationMs : 0;
}

void TreeTableWidget::refreshHeaders()
{
	m_horizontalHeader->update();
	m_verticalHeader->update();
}

void TreeTableWidget::sortByColumn(int column)
{
	if (m_sortColumn == column)
	{
		m_sortOrder = m_sortOrder == Qt::AscendingOrder
			? Qt::DescendingOrder
			: Qt::AscendingOrder;
	}
	else
	{
		m_sortColumn = column;
		m_sortOrder = Qt::AscendingOrder;
	}
	m_tree->sortItems(column, m_sortOrder);
	m_tree->header()->setSortIndicator(column, m_sortOrder);
	m_horizontalHeader->update();
}
