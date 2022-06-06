#include "TableModels/AsyncSqlTableModel.h"
#include "Tracer.h"
#include "uiobjects/DataUnavailable.h"

#include <QtSql/QSqlError>
#include <QtSql/QSqlDriver>
#include <QtSql/QSqlField>
#include <QDateTime>
#include <QTimer>
#include <QApplication>
#include <QtConcurrent/QtConcurrent>
#include <QColor>

AsyncColumnSqlTableModel::AsyncColumnSqlTableModel(
    IDataController* aDataController,
    const QString& aTableName,
    const SqlFieldDescription* aFieldList,
    size_t aFieldListSize,
    const SqlQueryUtils::TSortOrder& aDefaultSortOrder,
    Qt::SortOrder aDefaultSortDirection,
    const QString& aPrimaryKey,
    int aSqlPrimaryKeyIndex,
    int aDataIdIndex,
    const TCommonIndexesRanges& aCommonIndexRanges,
    bool aUseFileStorage,
    QObject* aParent,
    const QPointer<TableOperationHandlerBase>& aHandler)
    : AsyncSqlTableModelBase(
        aDataController->GetDatabaseConnections(),
        aTableName,
        aFieldList,
        aFieldListSize,
        SqlQueryUtils::NormalizeSortOrder(aDefaultSortOrder),
        aDefaultSortDirection,
        aPrimaryKey,
        aCommonIndexRanges,
        aSqlPrimaryKeyIndex,
        aUseFileStorage,
        aParent,
        aHandler)
    , mFont(QApplication::font())
    , mCheckFont(QFont("nt-symbol"))
    , mFieldListSize(static_cast<int>(aFieldListSize))
    , mDataIdIndex(aDataIdIndex)
    , mDataControllerInt(aDataController)
    , mCommonIndexes(aCommonIndexRanges)
{
    emit InitDbTableAsync();
}

AsyncColumnSqlTableModel::~AsyncColumnSqlTableModel()
{
}

void AsyncColumnSqlTableModel::PrepareToSubscribe()
{
    Clear();
    SetLoadingFinished(false);
    SetDirty();
    InitFilter();
}

int AsyncColumnSqlTableModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent)
    return ColumnCount();
}

QModelIndex AsyncColumnSqlTableModel::indexExtended(int row, int column, const QModelIndex &parent) const
{
    return row >= 0 && row < rowCount(parent) && column >= 0 && column < mFieldListSize
        ? createIndex(row, column)
        : QModelIndex();
}

int AsyncColumnSqlTableModel::AlterColumn(int aColumn) const
{
    return aColumn;
}

QModelIndex AsyncColumnSqlTableModel::GetIndexById(const QString& aId) const
{
    auto index = Match(mDataIdIndex, aId);
    if (!index)
    {
        throw uiobj::DataUnavailable("OrdersSqlTableModel::GetIndexById: " + aId);
    }
    return *index;
}

bool AsyncColumnSqlTableModel::GetIndexById(const QString& aId, QModelIndex& outIndex) const
{
    auto index = Match(mDataIdIndex, aId);
    if (!index)
    {
        return false;
    }

    outIndex = *index;
    return true;
}

std::optional<QVariant> AsyncColumnSqlTableModel::GetInvalidData(const QModelIndex& aIndex, int aRole) const
{
    if (aIndex.isValid())
    {
        if (IsDataLoaded(aIndex))
        {
            return std::nullopt;
        }
        else
        {
            if (aRole == Qt::DisplayRole)
            {
                return QString("Loading...");
            }
            else if (aRole == Qt::ForegroundRole)
            {
                return QColor(Qt::lightGray);
            }
        }
    }

    return QVariant();
}

void AsyncColumnSqlTableModel::sort(int aColumn, Qt::SortOrder aOrder)
{
    if (aColumn == columnCount() - 1)
    {
        return;
    }

    mSortColumn = aColumn;
    mSortOrder = aOrder;

    int realColumn = GetColumnEnumByOffset(aColumn);
    realColumn = AlterColumn(realColumn);
    if (realColumn < StartColumnEnumValue)
    {
        mAsyncTableTracer.Error(
            QString("%1: invalid column value: %2, %3").arg(Q_FUNC_INFO).arg(aColumn).arg(realColumn));
        return;
    }

    PrepareSortOperation(realColumn - StartColumnEnumValue, static_cast<int>(aOrder));
}

QVariant AsyncColumnSqlTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    return HeaderData(section, orientation, role);
}

QFont AsyncColumnSqlTableModel::GetFont(int aColumn, const QSet<int>& aBooleanColumns) const
{
    if (!aBooleanColumns.isEmpty() && aBooleanColumns.contains(aColumn))
    {
        return mCheckFont;
    }
    return mFont;
}

QVariant AsyncColumnSqlTableModel::GetValue(const QVariantList& aRow, int aColumn) const
{
    aColumn = AlterColumn(aColumn);
    if (aColumn < StartColumnEnumValue)
    {
        mAsyncTableTracer.Error(
            QString("%1: invalid column value: %2").arg(Q_FUNC_INFO).arg(aColumn));
        return QVariant {};
    }
    return aRow.value(aColumn - StartColumnEnumValue);
}

QVariant AsyncColumnSqlTableModel::GetDataByRowAndColumn(int aRow, int aColumn) const
{
    aColumn = AlterColumn(aColumn);
    if (aColumn < StartColumnEnumValue)
    {
        mAsyncTableTracer.Error(
            QString("%1: invalid column value: %2").arg(Q_FUNC_INFO).arg(aColumn));
        return QVariant {};
    }

    return AsyncSqlTableModelBase::data(indexExtended(aRow, aColumn - StartColumnEnumValue));
}

std::optional<QModelIndex> AsyncColumnSqlTableModel::Match(int aColumn, const QVariant &aValue) const
{
    for (int r = mViewData.Rows.Top; r <= mViewData.Rows.Bottom; ++r)
    {
        const auto* row = mViewData.GetRow(r);
        if (row
            && (aColumn < row->size())
            && row->at(aColumn) == aValue)
        {
            return createIndex(r, aColumn);
        }
    }

    return std::nullopt;
}

QStringList AsyncColumnSqlTableModel::GetFilters() const
{
    return QStringList();
}

void AsyncColumnSqlTableModel::InitFilter()
{
    PrepareFilterOperation(GetFilters().join(" AND "));
}

QSet<QString> AsyncColumnSqlTableModel::GetBooleanFields() const
{
    QSet<QString> ret;

    for (const auto& column : mBooleanColumns)
    {
        ret.insert(headerData(GetColumnOffsetByEnum(column), Qt::Horizontal,Qt::DisplayRole).toString());
    }

    return ret;
}
