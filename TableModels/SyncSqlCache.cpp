#include "SyncSqlCache.h"
#include "Tracer.h"

#include <QtSql/QSqlError>
#include <QtSql/QSqlDriver>
#include <QtSql/QSqlField>
#include <QDateTime>
#include <QTimer>
#include <QApplication>
#include <QtConcurrent/QtConcurrent>

const QVariantList *ViewWindowValues::GetRow(int aRow) const
{
    if (aRow >= RecordsCount)
    {
        return nullptr;
    }
    if (!Rows.Contains(aRow))
    {
        return nullptr;
    }
    auto index = aRow - Rows.Top;
    if (index >= Data.size())
    {
        return nullptr;
    }
    return &Data[index];
}

QVariantList *ViewWindowValues::GetRow(int aRow)
{
    return const_cast<QVariantList*>(static_cast<const ViewWindowValues*>(this)->GetRow(aRow));
}

RowRange ViewWindowValues::PrepareRemoveRows(int aRecordsCount) const
{
    aRecordsCount = qMax(0, aRecordsCount);
    RowRange result;
    if (aRecordsCount < RecordsCount)
    {
        result.Top = aRecordsCount;
        result.Bottom = RecordsCount - 1;
    }
    return result;
}

void ViewWindowValues::RemoveRows(int aRecordsCount)
{
    aRecordsCount = qMax(0, aRecordsCount);
    while (Rows.Bottom >= aRecordsCount)
    {
        if (GetRow(Rows.Bottom))
        {
            Data.removeLast();
        }
        --Rows.Bottom;
        RowsVisible.Top = qMin(RowsVisible.Top, Rows.Bottom);
    }
    if (Data.empty())
    {
        Rows = RowRange {};
        RowsVisible = RowRange {};
    }
    RecordsCount = qMin(RecordsCount, aRecordsCount);
}

QVector<RowRange> ViewWindowValues::PrepareChangeRows(const ViewWindowValues& aNewValues) const
{
    QVector<RowRange> result;
    auto minRecordsCount = qMin(aNewValues.RecordsCount, RecordsCount);
    auto unitedRows = Rows.Union(aNewValues.Rows);
    for (auto it = unitedRows.cbegin(); it != unitedRows.cend(); ++it)
    {
        if (it->Bottom < minRecordsCount)
        {
            result.push_back(*it);
        }
        else
        {
            RowRange changedRange = *it;
            changedRange.Bottom = minRecordsCount - 1;
            if (changedRange.IsValid())
            {
                result.push_back(changedRange);
            }
        }
    }
    return result;
}

void ViewWindowValues::ChangeRows(const ViewWindowValues& aNewValues)
{
    Rows = aNewValues.Rows;
    RowsVisible = aNewValues.RowsVisible;
    Data = aNewValues.Data;
}

RowRange ViewWindowValues::PrepareAddRows(int aRecordsCount) const
{
    aRecordsCount = qMax(0, aRecordsCount);
    RowRange result;
    if (aRecordsCount > RecordsCount)
    {
        result.Top = RecordsCount;
        result.Bottom = aRecordsCount - 1;
    }
    return result;
}

void ViewWindowValues::AddRows(int aRecordsCount)
{
    RecordsCount = qMax(RecordsCount, aRecordsCount);
}

void ViewWindowValues::SetData(
    const QList<QVariantList>& aData,
    const RowRange& aRows,
    const RowRange& aRowsVisible,
    const int aRecordsCount)
{
    Data = aData;
    Rows = aRows;
    RowsVisible = aRowsVisible;
    RecordsCount = aRecordsCount;
}

SyncSqlCache::SyncSqlCache(
    const std::weak_ptr<DataBaseConnections>& aConnections,
    const QString& aTableName,
    const SqlFieldDescription* aFieldList,
    size_t aFieldListSize,
    const QString& aPrimaryKey,
    const TCommonIndexesRanges& aCommonFieldsIndexes,
    int aIdColumn,
    const SqlQueryUtils::TSortOrder& aDefaultSortOrder,
    Qt::SortOrder aDefaultSortDirection,
    QObject* aParent,
    bool aIsFile,
    const QPointer<TableOperationHandlerBase>& aHandler)
    : QObject(aParent)
    , mCommonFieldsIndexes(aCommonFieldsIndexes)
    , mSortOrder(aDefaultSortDirection)
    , mDefaultSortOrder(MakeDefaultSortOrder(aDefaultSortOrder, static_cast<int>(aFieldListSize), aIdColumn))
    , mDefaultSortDirection(aDefaultSortDirection)
    , mDbConnection(aConnections, aIsFile)
    , mTable(
        mDbConnection.GetDatabase(),
        SqlQueryUtils::MakeUniqueName(aTableName),
        aFieldList,
        aFieldListSize,
        aPrimaryKey)
    , mSuspendedItemsTable(
        mDbConnection.GetDatabase(),
        mTable.GetName() + "_ssp", // ssp - suspended
        aFieldList,
        aFieldListSize,
        aPrimaryKey)
    , mSqlCacheTracer(
        GetTracer(
            QString("model.%1.sync").arg(mTable.GetName()).toStdString().c_str()))
{
    if (!QMetaType(qMetaTypeId<ViewWindowValues>()).isRegistered())
    {
        qRegisterMetaType<ViewWindowValues>();
    }
    if (!QMetaType(qMetaTypeId<QItemSelection>()).isRegistered())
    {
        qRegisterMetaType<QItemSelection>();
    }
    if (!QMetaType(qMetaTypeId<QVector<RowRange>>()).isRegistered())
    {
        qRegisterMetaType<QVector<RowRange>>();
    }
    if (!QMetaType(qMetaTypeId<QVector<ColumnsExportInfo>>()).isRegistered())
    {
        qRegisterMetaType<QVector<ColumnsExportInfo>>();
    }

    if (!QMetaType(qMetaTypeId<TNewItemsBufferPtr>()).isRegistered())
    {
        qRegisterMetaType<TNewItemsBufferPtr>();
    }
    if (!QMetaType(qMetaTypeId<LoadingStatus>()).isRegistered())
    {
        qRegisterMetaType<LoadingStatus>();
    }
    if (!QMetaType(qMetaTypeId<TSortParametersArg>()).isRegistered())
    {
        qRegisterMetaType<TSortParametersArg>();
    }
    if (!QMetaType(qMetaTypeId<TFilterParametersArg>()).isRegistered())
    {
        qRegisterMetaType<TFilterParametersArg>();
    }
    if (!QMetaType(qMetaTypeId<RowRequest>()).isRegistered())
    {
        qRegisterMetaType<RowRequest>();
    }
    if (!QMetaType(qMetaTypeId<SelectionRequest>()).isRegistered())
    {
        qRegisterMetaType<SelectionRequest>();
    }
    if (!QMetaType(qMetaTypeId<HintsRequest>()).isRegistered())
    {
        qRegisterMetaType<HintsRequest>();
    }
    if (!QMetaType(qMetaTypeId<ScrollHintType>()).isRegistered())
    {
        qRegisterMetaType<ScrollHintType>();
    }
    if (!QMetaType(qMetaTypeId<EdgeRowHintType>()).isRegistered())
    {
        qRegisterMetaType<EdgeRowHintType>();
    }
    if (!QMetaType(qMetaTypeId<TSelectedIds>()).isRegistered())
    {
        qRegisterMetaType<TSelectedIds>();
    }

    mSqlCacheTracer.Info("QSqlDriver::QuerySize: " + QString::number(mDbConnection.GetDatabase().driver()->hasFeature(QSqlDriver::QuerySize)));
    mSqlCacheTracer.Info("QSqlDriver::LastInsertId: " + QString::number(mDbConnection.GetDatabase().driver()->hasFeature(QSqlDriver::LastInsertId)));
    mSqlCacheTracer.Info("QSqlDriver::SimpleLocking: " + QString::number(mDbConnection.GetDatabase().driver()->hasFeature(QSqlDriver::SimpleLocking)));
    mSqlCacheTracer.Info("QSqlDriver::EventNotifications: " + QString::number(mDbConnection.GetDatabase().driver()->hasFeature(QSqlDriver::EventNotifications)));

    SetOperationHandler(aHandler);
}

SyncSqlCache::~SyncSqlCache()
{
}

void SyncSqlCache::ReportError(const QString& aContext)
{
    mSqlCacheTracer.Error(aContext + ": " + mTable.GetLastError());
    emit ErrorOccured(mTable.GetLastError());
}

void SyncSqlCache::InitDbTable()
{
    try
    {
        mTable.PerformAction(SqlCacheTable::Action::Create);
        mSuspendedItemsTable.PerformAction(SqlCacheTable::Action::Create);
    }
    catch(std::runtime_error&) { ReportError(Q_FUNC_INFO); }

    emit InitializationCompleted();
}

int SyncSqlCache::GetRecordsCount() const
{
    auto it = mVersionedIds.find(mViewWindowValues.Version);
    if (it == mVersionedIds.cend())
    {
        return 0;
    }

    return static_cast<int>(it->second.Ids.size());
}

QSqlQuery SyncSqlCache::PerformSqlUnsafe(
    const QString& aSql,
    const QVariantList& aParams) noexcept(false)
{
    mTable.PerformSql(aSql, aParams, mFilter);
    return mTable.GetLastQuery();
}
QSqlQuery SyncSqlCache::PerformSqlSafe(
    const QString& aSql,
    const QVariantList& aParams) noexcept
{
    try { return PerformSqlUnsafe(aSql, aParams); }
    catch(std::runtime_error&)
    {
        ReportError(Q_FUNC_INFO);
        return mTable.GetLastQuery();
    }
}

QSqlRecord SyncSqlCache::GetRecord(int aRow)
{
    auto it = mVersionedIds.find(mViewWindowValues.Version);
    if (it == mVersionedIds.cend())
    {
        return QSqlRecord {};
    }

    const auto& ids = it->second;
    if (ids.IsOutOfRange(aRow))
    {
        return QSqlRecord {};
    }

    const auto& rowId = ids.GetId(aRow);
    if (!rowId.isValid())
    {
        return QSqlRecord {};
    }

    return GetItem(rowId);
}

QSqlRecord SyncSqlCache::GetItem(const QVariant& aId)
{
    try { mTable.PerformAction(SqlCacheTable::Action::Select, aId); }
    catch (std::runtime_error&) { ReportError(Q_FUNC_INFO); }

    auto& query = mTable.GetLastQuery();
    if (!query.next())
    {
        return QSqlRecord {};
    }

    return query.record();
}

QVariantList SyncSqlCache::GetItemValues(const QVariant& aId)
{
    return SqlQueryUtils::Record2Fields(GetItem(aId));
}

const QString& SyncSqlCache::GetTableName() const
{
    return mTable.GetName();
}

const SyncSqlCache::IdsInfo* SyncSqlCache::GetIdMapping() const
{
    if (mVersionedIds.empty())
    {
        return nullptr;
    }

    return &mVersionedIds.rbegin()->second;
}

const std::tuple<std::optional<std::set<qlonglong>>, std::optional<qlonglong>> SyncSqlCache::GetSelectedIds() const
{
    std::optional<std::set<qlonglong>> selectedIds;
    std::optional<qlonglong> topSelectedId;
    const auto* ids = GetIdMapping();

    if (ids)
    {
        selectedIds = std::set<qlonglong> {};
        foreach (const auto& range, mViewWindowValues.Selection)
        {
            if (ids->IsOutOfRange(range.Top) || ids->IsOutOfRange(range.Bottom))
            {
                continue;
            }

            for (auto row = range.Top; row <= range.Bottom; ++row)
            {
                const auto id = ids->Ids[static_cast<size_t>(row)];
                if (!topSelectedId)
                {
                    topSelectedId = id;
                }
                selectedIds->insert(id);
            }
        }
    }

    return std::make_tuple(selectedIds, topSelectedId);
}


SqlQueryUtils::TSortOrder SyncSqlCache::MakeDefaultSortOrder(
    const SqlQueryUtils::TSortOrder& aSortOrder,
    int aMaxColumn,
    int aIdColumn)
{
    SqlQueryUtils::TSortOrder idSortOrder = SqlQueryUtils::TSortOrder { SqlQueryUtils::TSortSequence {aIdColumn} };
    const auto& order = aSortOrder.empty() ? idSortOrder : aSortOrder;

    std::set<int> fieldIndexes;
    for (const auto& sortSequence : order)
    {
        for (auto column : sortSequence)
        {
            if (!fieldIndexes.emplace(column).second)
            {
                throw std::runtime_error("Invalid sort order: columns duplicated");
            }

            if (column >= aMaxColumn || column < 0)
            {
                throw std::runtime_error("Invalid sort order: column index out of range");
            }
        }
    }

    return order;
}

void SyncSqlCache::UpdateViewWindowValues(bool aRefreshAll)
{
    UpdateViewWindowValuesInternal(aRefreshAll);
    UpdateExtraData();
}

void SyncSqlCache::UpdateViewWindowValuesInternal(bool aRefreshAll)
{
    ViewWindowValues newValues;
    if (mRequestedRowRange.IsValid())
    {
        const auto rCnt = mRequestedRowRange.Bottom + 1;
        for (int i = mRequestedRowRange.Top; (i < rCnt); ++i)
        {
            const QVariantList* oldRowPtr = nullptr;
            if (!aRefreshAll)
            {
                oldRowPtr = mViewWindowValues.GetRow(i);
            }

            if (oldRowPtr)
            {
                newValues.Data.push_back(*oldRowPtr);
            }
            else
            {
                auto rowData = GetRecord(i);
                if (rowData.isEmpty())
                {
                    break;
                }
                Q_ASSERT(rowData.count() == mTable.GetColumnCount());
                newValues.Data.push_back(SqlQueryUtils::Record2Fields(rowData));
            }
        }
    }

    if (!newValues.Data.empty())
    {
        // реально возможный диапазон - на который хватило данных в таблице
        newValues.Rows.Top = mRequestedRowRange.Top;
        newValues.Rows.Bottom = mRequestedRowRange.Top + newValues.Data.size() - 1;

        // видимый диапазон теперь должен подстроиться под реальный
        newValues.RowsVisible.Bottom = qMin(mRequestedRowRangeVisible.Bottom, newValues.Rows.Bottom);
        newValues.RowsVisible.Top = qMax(0, newValues.RowsVisible.Bottom - mRequestedRowRangeVisible.Distance());
    }

    mSqlCacheTracer.Trace(QString("%1: range: %2, range vis: %3")
        .arg(Q_FUNC_INFO)
        .arg(ToString(newValues.Rows))
        .arg(ToString(newValues.RowsVisible)));

    mViewWindowValues.SetData(newValues.Data, newValues.Rows, newValues.RowsVisible, GetRecordsCount());
}

void SyncSqlCache::UpdateExtraData()
{
    if (!mOperationHandler)
    {
        return;
    }
    mOperationHandler->MakeExtraData(mViewWindowValues);
}

void SyncSqlCache::UpdateIdMapping(QSqlQuery& aQuery)
{
    auto [it, emplaced] = mVersionedIds.try_emplace(mViewWindowValues.Version, IdsInfo {});
    if (!emplaced)
    {
        mSqlCacheTracer.Warning("UpdateIdMapping: version already exists");
        return;
    }

    auto& ids = it->second;

    if (it != mVersionedIds.begin())
    {
        auto previousIt = it;
        --previousIt;

        ids.Ids.reserve(previousIt->second.Ids.size());
    }
    while (aQuery.next())
    {
        ids.AddId(aQuery.value(0));
    }
}

void SyncSqlCache::ProcessDataPopulation(QSqlQuery& aQuery)
{
    ++mViewWindowValues.Version;

    UpdateIdMapping(aQuery);

    TransformSelection(mViewWindowValues.Version - 1, mViewWindowValues.Selection, mViewWindowValues.CurrentRow);
    UpdateRowWindow();
    UpdateViewWindowValues(true);
    if (mOperationHandler)
    {
        mOperationHandler->ProcessDataSelected();
    }
}

std::optional<SyncSqlCache::RowTransformator> SyncSqlCache::GetRowTransformation(qint64 aVersion) const
{
    if (mViewWindowValues.Version == aVersion)
    {
        return std::nullopt;
    }
    
    auto currentIt = mVersionedIds.find(mViewWindowValues.Version);
    if (currentIt == mVersionedIds.cend())
    {
        return std::nullopt;
    }

    auto previousIt = currentIt;
    if (aVersion < mViewWindowValues.Version)
    {
        previousIt = mVersionedIds.find(aVersion);
        if (previousIt == mVersionedIds.cend())
        {
            return std::nullopt;
        }
    }

    return RowTransformator { previousIt->second, currentIt->second };
}

qlonglong SyncSqlCache::GetDbRowCount()
{
    try { return mTable.GetRowCount(); }
    catch (std::runtime_error&)
    {
        ReportError(Q_FUNC_INFO);
        return 0;
    }
}

qlonglong SyncSqlCache::GetSuspendDbRowCount()
{
    try { return mSuspendedItemsTable.GetRowCount(); }
    catch (std::runtime_error&)
    {
        ReportError(Q_FUNC_INFO);
        return 0;
    }
}

void SyncSqlCache::TransformSelection(
    qint64 aVersion,
    QVector<RowRange>& outSelection,
    int& outCurrentRow)
{
    auto tranformator = GetRowTransformation(aVersion);
    if (!tranformator)
    {
        return;
    }

    outCurrentRow = tranformator->Transform(outCurrentRow);
    QVector<RowRange> selection;

    std::set<int> rows;
    foreach (const auto& s, outSelection)
    {
        for (int i = s.Top; i <= s.Bottom; ++i)
        {
            auto rowNumber = tranformator->Transform(i);
            if (rowNumber >= 0)
            {
                rows.emplace(rowNumber);
            }
        }
    }
    for (auto r : rows)
    {
        if (!selection.isEmpty() && (selection.back().Bottom == (r - 1)))
        {
            selection.back().Bottom = r;
        }
        else
        {
            selection.push_back(RowRange {r, r});
        }
    }

    outSelection = selection;
}

SelectionRequest SyncSqlCache::TransformSelection(const SelectionRequest& aSelectionRequest)
{
    auto selection = aSelectionRequest.Selection;
    auto currectRow = aSelectionRequest.CurrentRow;

    TransformSelection(aSelectionRequest.Version, selection, currectRow);

    auto selectionRequest = aSelectionRequest;
    {
        selectionRequest.Selection = selection;
        selectionRequest.CurrentRow = currectRow;
    }

    return selectionRequest;
}

void SyncSqlCache::UpdateRowWindow()
{
    mSqlCacheTracer.Trace(QString("%1: before : range: %2, range vis: %3")
        .arg(Q_FUNC_INFO)
        .arg(ToString(mRequestedRowRange))
        .arg(ToString(mRequestedRowRangeVisible)));

    auto tranformator = GetRowTransformation(mViewWindowValues.Version - 1);
    if (tranformator && mRequestedRowRange.IsValid() && !mIsAutoScroll)
    {
        TransformRowRange(*tranformator, mRequestedRowRange, mRequestedRowRangeVisible);

        mSqlCacheTracer.Trace(QString("%1: transf : range: %2, range vis: %3")
            .arg(Q_FUNC_INFO)
            .arg(ToString(mRequestedRowRange))
            .arg(ToString(mRequestedRowRangeVisible)));
    }

    if (mRequestedRowRangeVisible.Top >= GetRecordsCount())
    {
        mRequestedRowRangeVisible = RowRange {};
        mRequestedRowRange = RowRange {};
    }

    if (!mRequestedRowRangeVisible.IsValid())
    {
        mRequestedRowRangeVisible = mRequestedRowRangeVisible.Expand(SqlQueryUtils::RowWindowOffset);
    }
    if (!mRequestedRowRange.Contains(mRequestedRowRangeVisible, SqlQueryUtils::RowWindowOffset))
    {
        mRequestedRowRange = mRequestedRowRangeVisible.Expand(SqlQueryUtils::RowWindowOffset);
    }

    mSqlCacheTracer.Trace(QString("%1: after  : range: %2, range vis: %3")
        .arg(Q_FUNC_INFO)
        .arg(ToString(mRequestedRowRange))
        .arg(ToString(mRequestedRowRangeVisible)));
}

bool SyncSqlCache::TransformRowRange(
    const SyncSqlCache::RowTransformator& aTransformator,
    RowRange& outRange,
    RowRange& outRangeVisible)
{
    const auto newVisibleTop = aTransformator.Transform(outRangeVisible.Top);
    if (newVisibleTop < 0)
    {
        return false;
    }

    outRangeVisible = RowRange { newVisibleTop, newVisibleTop + outRangeVisible.Distance() };
    outRange = outRangeVisible.Expand(SqlQueryUtils::RowWindowOffset);

    return true;
}

std::optional<RowRequest> SyncSqlCache::TransformRowRange(const RowRequest& aRowRequest)
{
    auto transformator = GetRowTransformation(aRowRequest.Version);

    if (transformator)
    {
        RowRequest request = aRowRequest;
        TransformRowRange(*transformator, request.RowWindow, request.RowWindowVisible);
        return request;
    }

    return std::nullopt;
}

QVariant SyncSqlCache::IdsInfo::GetId(int aI) const
{
    if (IsOutOfRange(aI))
    {
        return {};
    }
    return Ids.at(static_cast<size_t>(aI));
}

bool SyncSqlCache::IdsInfo::IsOutOfRange(int aI) const
{
    return (aI < 0 || static_cast<size_t>(aI) >= Ids.size());
}

std::optional<size_t> SyncSqlCache::IdsInfo::GetRow(const QVariant& aId) const
{
    auto id = aId.toLongLong();
    auto it = IdPositions.find(id);
    if (it != IdPositions.cend())
    {
        return it->second;
    }

    if (IdPositions.size() < Ids.size())
    {
        if (IdPositions.empty())
        {
            IdPositions.reserve(Ids.size());
        }
        for (size_t i = IdPositions.size(); i < Ids.size(); ++i)
        {
            const auto& value = Ids[i];
            IdPositions.emplace(value, i);
            if (value == id)
            {
                return i;
            }
        }
    }

    return std::nullopt;
}

void SyncSqlCache::IdsInfo::AddId(const QVariant& aId)
{
    Ids.push_back(aId.toLongLong());
}

void SyncSqlCache::ClearTable(bool aIsFinal)
{
    Q_UNUSED(aIsFinal)

    mIsSelectionAllowed = false;
    mVersionedIds.clear();

    if (mOperationHandler)
    {
        mOperationHandler->ProcessClear();
    }

    try {
        mTable.PerformAction(SqlCacheTable::Action::Clear);
        mSuspendedItemsTable.PerformAction(SqlCacheTable::Action::Clear);
    }
    catch (std::runtime_error&) { ReportError(Q_FUNC_INFO); }

    mRequestedRowRange = RowRange {};
    mRequestedRowRangeVisible = RowRange {};
    mViewWindowValues = ViewWindowValues {};
    mSuspendedDeletedIds.clear();
    mTableOperationsCounter = 0;
    mSuspendedRecordsCounter = 0;

    emit ClearCompleted();
}

bool SyncSqlCache::AddPendingValue(QVariantList& aValues)
{
    if (mOperationHandler)
    {
        if (!mOperationHandler->AddPendingValue(aValues))
        {
            return false;
        }
    }

    for (const auto& [commonIndex, indexes] : mCommonFieldsIndexes)
    {
        aValues[commonIndex] = SqlQueryUtils::GetFullTextSearchValue(aValues, indexes);
    }

    return true;
}

void SyncSqlCache::DeleteRecord(qlonglong aId, bool aSuspend)
{
    GetTable(aSuspend).PerformAction(SqlCacheTable::Action::Delete, aId);

    if (aSuspend)
    {
        mSuspendedDeletedIds.insert(aId);
    }
    else if (mOperationHandler)
    {
        mOperationHandler->DeletePendingValue(aId);
    }
}

void SyncSqlCache::InsertOrReplace(const QVariantList& aFields, bool aSuspend)
{
    auto fields = aFields;
    /// Не вызываем AddPendingValue в случае Suspend
    if (aSuspend || AddPendingValue(fields))
    {
        GetTable(aSuspend).PerformAction(SqlCacheTable::Action::Insert, fields);
    }
}

void SyncSqlCache::StoreItemsToDb(
    const TNewItemsBufferPtr& aValues,
    bool aSuspend) noexcept(false)
{
    for (const auto& item : *aValues)
    {
        if (item.size() > 1)
        {
            InsertOrReplace(item, aSuspend);
        }
        else
        {
            DeleteRecord(item.at(0).toLongLong(), aSuspend);
        }
    }
                
    auto& counter = aSuspend ? mSuspendedRecordsCounter : mTableOperationsCounter;
    counter += aValues->size();
}

void SyncSqlCache::ResumeSuspendedItems() noexcept(false)
{
    size_t i = 0;
    int progress = 0;
    auto updateProgress = [&]()
    {
        if (i >= mSuspendedRecordsCounter)
        {
            return;
        }
        auto v = static_cast<int>((++i) * 100 / mSuspendedRecordsCounter);
        if (v != progress)
        {
            progress = v;
            emit PendingUpdatesProgressChanged(progress);
        }
    };

    mSuspendedRecordsCounter = GetSuspendDbRowCount() + mSuspendedDeletedIds.size();

    /// Удаляем
    for (const auto& id : mSuspendedDeletedIds)
    {
        DeleteRecord(id, false);
        updateProgress();
    }
    mSuspendedDeletedIds.clear();

    /// Перекачиваем данные из одной таблицы в другую
    auto sql = QString("SELECT * FROM %1 ORDER BY id")
        .arg(SqlQueryUtils::TablePlaceholder);
    mSuspendedItemsTable.PerformSql(sql, {}, {}, true);

    auto& query = mSuspendedItemsTable.GetLastQuery();
    while (query.next())
    {
        InsertOrReplace(Record2List(query.record()), false);
        updateProgress();
    }
    /// Очищаем временную таблицу
    mSuspendedItemsTable.PerformAction(SqlCacheTable::Action::Clear);
    mSuspendedRecordsCounter = 0;
    emit PendingUpdatesProgressChanged(100);
}

std::optional<std::pair<qint64, qint64>> SyncSqlCache::TryStoreItemsToDb(
    const TNewItemsBufferPtr& aValues,
    bool aSuspend) noexcept
{
    if (aValues->empty() && (aSuspend || !mSuspendedRecordsCounter))
    {
        /// Ничего не пришло и нечего применять из приостановленных записей
        return std::nullopt;
    }
    
    const auto d1 = QDateTime::currentDateTime().toMSecsSinceEpoch();
    auto d2 = d1;
    try
    {
        mDbConnection.GetDatabase().transaction();

        if (!aSuspend)
        {
            ResumeSuspendedItems();
        }
        
        StoreItemsToDb(aValues, aSuspend);
        
        d2 = QDateTime::currentDateTime().toMSecsSinceEpoch();
        if (!aSuspend
            && mOperationHandler
            && mOperationHandler->IsInsertionNeeded())
        {
            mOperationHandler->ProcessDataInserted();
        }

        mDbConnection.GetDatabase().commit();
    }
    catch (std::runtime_error&)
    {
        ReportError(Q_FUNC_INFO);
        mDbConnection.GetDatabase().rollback();
    }
    const auto d3 = QDateTime::currentDateTime().toMSecsSinceEpoch();

    return std::make_pair(d2 - d1, d3 - d2);
}

std::pair<std::optional<int>, std::optional<int>> SyncSqlCache::EstimateDbRowCount(
    bool aMainTableUpdated,
    bool aIsSuspend)
{
    std::optional<int> dbRecordCount;
    std::optional<int> rowCountingDuration;
    if (aMainTableUpdated)
    {
        /// Обновляем количество строк только если пришли новые данные.
        if (mIsSelectionAllowed)
        {
            auto d4 = QDateTime::currentDateTime().toMSecsSinceEpoch();
            dbRecordCount = GetDbRowCount();
            rowCountingDuration = static_cast<int>(QDateTime::currentDateTime().toMSecsSinceEpoch() - d4);
        }
        else
        {
            /// Пока крутятся шарики приблизительно считаем количество добавленных записей,
            /// т.к. проверка размера таблицы - тяжелая операция.
            dbRecordCount = mTableOperationsCounter;
        }
    }
    return std::make_pair(dbRecordCount, rowCountingDuration);
}

std::optional<int> SyncSqlCache::TryPerformSelection(
    bool aMainTableUpdated,
    const TSortParametersArg aSorting,
    const TFilterParametersArg aFilter)
{
    if (!mIsSelectionAllowed)
    {
        return std::nullopt;
    }
    
    if (aMainTableUpdated
        || aSorting
        || aFilter)
    {
        const auto d = QDateTime::currentDateTime().toMSecsSinceEpoch();
        PerformSelection();
        return static_cast<int>(QDateTime::currentDateTime().toMSecsSinceEpoch() - d);
    }
    else
    {
        return std::nullopt;
    }
}

void SyncSqlCache::LogHeavyAction(
    std::optional<std::pair<qint64, qint64>> insertionDuration,
    std::optional<int> selectionDuration,
    std::optional<int> dbRecordCount,
    std::optional<int> rowCountingDuration,
    size_t aValuesSize)
{
    auto insertionLog = insertionDuration
        ? QString(", insertion: %1 ms, updating: %2 ms")
            .arg(insertionDuration->first).arg(insertionDuration->second)
        : QString {};
    
    auto selectionLog = selectionDuration
        ? QString(", selection: %1 ms").arg(*selectionDuration)
        : QString {};

    auto rowCountingLog = rowCountingDuration
        ? (QString(", db size: %1, %2 ms, table name: %3")
           .arg(*dbRecordCount)
           .arg(*rowCountingDuration)
           .arg(mTable.GetName()))
        : QString {};

    mSqlCacheTracer.Trace(QString("%1: size: %2%3%4%5")
        .arg(Q_FUNC_INFO)
        .arg(aValuesSize)
        .arg(insertionLog)
        .arg(selectionLog)
        .arg(rowCountingLog));
}

void SyncSqlCache::ProcessHeavyAction(
    const qint64 aRequestId,
    const TNewItemsBufferPtr& aValues,
    const LoadingStatus aLoadingStatus,
    const TSortParametersArg aSorting,
    const TFilterParametersArg aFilter,
    bool aReportSelected,
    bool aSuspendUpdates)
{
    auto selectedRows = aReportSelected ? std::get<0>(GetSelectedIds()) : TSelectedIds {};
    SetSorting(aSorting);
    SetFilter(aFilter);
    mViewWindowValues.RequestId = aRequestId;
    /// Накапливать записи нельзя в момент первоначальной загрузки таблицы.
    const bool isSuspend = aSuspendUpdates && mIsSelectionAllowed;
    /// Сначала считаем isSuspend, затем обновляем mIsSelectionAllowed.
    SetUpdatesFromDbAllowed(aLoadingStatus);

    const auto insertionDuration = TryStoreItemsToDb(aValues, isSuspend);
    const bool mainTableUpdated = insertionDuration && !isSuspend;
    /// TradeStoreComponent отправляет инкременты с IsNextDataPending.
    /// Поэтому на каждый инкремент нужно пересчитывать запрос.
    const bool mainTableUpdatedOrLoadFinished = mainTableUpdated
        || aLoadingStatus == LoadingStatus::Finished;
    
    const auto selectionDuration = TryPerformSelection(
        mainTableUpdatedOrLoadFinished,
        aSorting,
        aFilter);
    
    auto [dbRecordCount, rowCountingDuration] = EstimateDbRowCount(
        mainTableUpdatedOrLoadFinished,
        isSuspend);

    LogHeavyAction(
        insertionDuration,
        selectionDuration,
        dbRecordCount,
        rowCountingDuration,
        aValues->size());

    emit OperationCompleted(
        selectionDuration ? QVariant {*selectionDuration} : QVariant {},
        dbRecordCount ? QVariant {*dbRecordCount} : QVariant {},
        static_cast<qulonglong>(mSuspendedRecordsCounter),
        mViewWindowValues,
        selectionDuration.has_value(),
        selectedRows);
}

void SyncSqlCache::ProcessEasyAction(
    const qint64 aRequestId,
    const RowRequest& aRowRequest,
    const SelectionRequest& aSelectionRequest,
    const HintsRequest& aHintsRequest)
{
    // В режиме EnsureVisible приоритет отдается выделению - оно применяется,
    // а запрос на изменение окна подменяется на запрос с окном, которое отследит новое положение выделения.

    mViewWindowValues.RequestId = aRequestId;

    bool isUpdated = false;
    if (mIsSelectionAllowed)
    {
        isUpdated |= SetSelection(aSelectionRequest);// выделение применяется первым

        RowRequest rowRequest = aRowRequest;

        const auto recordsCountMinesOne = GetRecordsCount() - 1;

        auto correctVisibleRange = [&](const int aCurrentRow, const bool aBottomIsEnd)
        {
            const auto newVisibleRange =
                aRowRequest.RowWindowVisible
                .ScrollToWithCorrection(
                    aCurrentRow,
                    aHintsRequest.TopRowHint == EdgeRowHintType::Full,
                    aHintsRequest.BottomRowHint == EdgeRowHintType::Full,
                    aBottomIsEnd);

            rowRequest.RowWindowVisible = newVisibleRange;
            rowRequest.RowWindow = newVisibleRange.Expand(SqlQueryUtils::RowWindowOffset);
        };

        if (aHintsRequest.ScrollHint == ScrollHintType::EnsureVisible)
        {
            correctVisibleRange(
                mViewWindowValues.CurrentRow,
                mViewWindowValues.CurrentRow == recordsCountMinesOne);
        }
        else if (rowRequest.RowWindowVisible.Bottom == recordsCountMinesOne)
            // если видимый низ - это последняя строка данных,
            // то корректируем диапазон, как если бы эта строка была выделением
        {
            correctVisibleRange(recordsCountMinesOne, true);
        }

        isUpdated |= SetRowWindow(rowRequest);// окно применяется после выделения

        if (isUpdated)
        {
            UpdateViewWindowValues(false);
        }
    }

    emit OperationCompleted(QVariant(), QVariant(), QVariant(), mViewWindowValues, isUpdated, std::nullopt);
}

bool SyncSqlCache::SetRowWindow(const RowRequest& aRowRequest)
{
    const RowRequest rowRequestUpdated = 
        (!mIsAutoScroll)
        ? TransformRowRange(aRowRequest).value_or(aRowRequest)
        : aRowRequest;

    if (rowRequestUpdated != aRowRequest)
    {
        mSqlCacheTracer.Trace(QString("%1: incoming   : range: %2, range vis: %3")
            .arg(Q_FUNC_INFO)
            .arg(ToString(aRowRequest.RowWindow))
            .arg(ToString(rowRequestUpdated.RowWindowVisible)));
        mSqlCacheTracer.Trace(QString("%1: transformed: range: %2, range vis: %3")
            .arg(Q_FUNC_INFO)
            .arg(ToString(rowRequestUpdated.RowWindow))
            .arg(ToString(rowRequestUpdated.RowWindowVisible)));
    }

    if ((mRequestedRowRange != rowRequestUpdated.RowWindow)
        || (mRequestedRowRangeVisible != rowRequestUpdated.RowWindowVisible))
    {
        mRequestedRowRange = rowRequestUpdated.RowWindow;
        mRequestedRowRangeVisible = rowRequestUpdated.RowWindowVisible;

        mSqlCacheTracer.Trace(QString("%1: range: %2, range vis: %3")
            .arg(Q_FUNC_INFO)
            .arg(ToString(mRequestedRowRange))
            .arg(ToString(mRequestedRowRangeVisible)));

        return true;
    }

    return false;
}

bool SyncSqlCache::SetSelection(const SelectionRequest& aSelectionRequest)
{
    const auto transformedSelection = TransformSelection(aSelectionRequest);

    if (mViewWindowValues.CurrentRow != transformedSelection.CurrentRow
        || mViewWindowValues.Selection != transformedSelection.Selection)
    {
        mViewWindowValues.CurrentRow = transformedSelection.CurrentRow;
        mViewWindowValues.Selection = transformedSelection.Selection;
        return true;
    }

    return false;
}

void SyncSqlCache::ConfirmVersion(qint64 aVersion)
{
    auto itEnd = mVersionedIds.lower_bound(aVersion);
    for (auto it = mVersionedIds.begin(); it != itEnd;)
    {
        it = mVersionedIds.erase(it);
    }
}

void SyncSqlCache::PerformSelection()
{
    auto d1 = QDateTime::currentDateTime().toMSecsSinceEpoch();

    auto sql = QString("SELECT id FROM %1 WHERE %2 %3")
        .arg(SqlQueryUtils::TablePlaceholder)
        .arg(SqlQueryUtils::FilterPlaceholder)
        .arg(OrderByClause());
    try { mTable.PerformSql(sql, {}, mFilter, true); }
    catch(std::runtime_error&) { ReportError(Q_FUNC_INFO); }

    auto d2 = QDateTime::currentDateTime().toMSecsSinceEpoch();

    ProcessDataPopulation(mTable.GetLastQuery());

    mSqlCacheTracer.Trace("PerformSelection: " + sql);
    mSqlCacheTracer.Trace(QString("%1: selection: %2 ms, processing: %3 ms")
        .arg(Q_FUNC_INFO)
        .arg(d2 - d1)
        .arg(QDateTime::currentDateTime().toMSecsSinceEpoch() - d2));
}

void SyncSqlCache::SetSorting(const TSortParametersArg& aSorting)
{
    if (!aSorting)
    {
        return;
    }

    mSortColumn = aSorting->Column;
    mSortOrder = static_cast<Qt::SortOrder>(aSorting->Order);

    mSqlCacheTracer.Info(QString("%1: mSortColumn: %2, mSortOrder: %3")
        .arg(Q_FUNC_INFO)
        .arg(mSortColumn)
        .arg((mSortOrder == Qt::AscendingOrder) ? QString("asc") : QString("desc")));
}

void SyncSqlCache::SetFilter(const TFilterParametersArg& aFilter)
{
    if (!aFilter)
    {
        return;
    }

    mFilter = *aFilter;
    mSqlCacheTracer.Info(QString("%1: %2").arg(Q_FUNC_INFO).arg(mFilter));
}

void SyncSqlCache::On_PerformSelect(QString aSql, QVariantList aParams)
{
    auto query = PerformSqlSafe(aSql, aParams);

    if (!query.isSelect())
    {
        static constexpr char error[] = "On_PerformSelect: only select statements allowed here";
        mSqlCacheTracer.Error(error);
        emit ErrorOccured(error);
        return;
    }

    QVariantList result;
    while (query.next())
    {
        result.push_back(Record2List(query.record()));
    }

    emit UserQueryPerformed(result);
}

QVariantList SyncSqlCache::Record2List(const QSqlRecord& aRecord)
{
    QVariantList row;
    for (int i = 0; i < aRecord.count(); ++i)
    {
        row.push_back(aRecord.value(i));
    }
    return row;
}

void SyncSqlCache::SetUpdatesFromDbAllowed(LoadingStatus aLoadingStatus)
{
    if (aLoadingStatus == LoadingStatus::NotChanged)
    {
        return;
    }
    
    mIsSelectionAllowed = (aLoadingStatus == LoadingStatus::Finished);
}

void SyncSqlCache::OnExport(const QString &aExportFileName, const ColumnsExportInfo &aColumns)
{
    CsvExporter exp(aExportFileName);
    if(!exp.IsReadyForWrite())
    {
        emit ExportFinished("Export file is not valid");
        return;
    }

    int i = -1;
    QSqlRecord record;

    auto cellGetter = [&](int aRow, int aColumn)
    {
        if (aRow != i)
        {
            i = aRow;
            record = GetRecord(aRow);
        }
        return record.value(aColumn);
    };

    IterateTable(
        mViewWindowValues.RecordsCount,
        cellGetter,
        &exp,
        aColumns,
        0,
        [this](int aProgress){ emit ExportProgressChanged(aProgress); },
        [this](){ return mStopExport.load(); });

    exp.CloseFile();
    if (mStopExport.load())
    {
        QFile file( aExportFileName );
        file.remove();
    }
    mStopExport.store(false);
    emit ExportFinished(QString());
}

QString SyncSqlCache::OrderByClause() const
{
    auto columnName = [this](int i) { return mTable.GetColumnName(i) + " %1"; };

    auto transformer = [&](const auto& aSequence, QStringList& outColumns)
    {
        for (auto column : aSequence)
        {
            outColumns << columnName(column);
        }
    };

    if ((mSortColumn < 0 || mSortColumn >= mTable.GetColumnCount())
        && mDefaultSortOrder.empty())
    {
        return QString();
    }

    QStringList defaultColumnList;
    QStringList columnList;
    for (const auto& sortSequence : mDefaultSortOrder)
    {
        if (columnList.empty())
        {
            auto it = std::find(sortSequence.cbegin(), sortSequence.cend(), mSortColumn);
            if (it != sortSequence.cend())
            {
                transformer(sortSequence, columnList);
                continue;
            }
        }
        transformer(sortSequence, defaultColumnList);
    }

    QString sortOrder = (mSortOrder == Qt::AscendingOrder) ? "ASC" : "DESC";
    QString defaultSortOrder = sortOrder;

    if (columnList.empty()
        && (mSortColumn >= 0)
        && (mSortColumn < mTable.GetColumnCount()))
    {
        columnList << columnName(mSortColumn);
        defaultSortOrder = (mDefaultSortDirection == Qt::AscendingOrder) ? "ASC" : "DESC";
    }

    QString sortColumns;
    if (!columnList.empty())
    {
        sortColumns = columnList.join(", ").arg(sortOrder);
    }
    if (!defaultColumnList.empty())
    {
        if (!sortColumns.isEmpty())
        {
            sortColumns.append(", ");
        }
        sortColumns.append(defaultColumnList.join(", ").arg(defaultSortOrder));
    }

    return "ORDER BY " + sortColumns;
}

void SyncSqlCache::On_SetAutoScroll(bool aIsAutoScroll)
{
    mIsAutoScroll = aIsAutoScroll;
}

void SyncSqlCache::SetOperationHandler(const QPointer<TableOperationHandlerBase>& aHandler)
{
    mOperationHandler = aHandler;
    if (mOperationHandler)
    {
        mOperationHandler->SetTableModel(this);
        mOperationHandler->setParent(this);
    }
}

void SyncSqlCache::StopExport()
{
    mStopExport.store(true);
}

int SyncSqlCache::RowTransformator::Transform(int aRow) const
{
    auto row = New.GetRow(Old.GetId(aRow));
    if (!row)
    {
        return -1;
    }
    return static_cast<int>(*row);
}

RowRange SyncSqlCache::RowTransformator::Transform(const RowRange& aRowRange) const
{
    RowRange range;
    {
        range.Top = Transform(aRowRange.Top);
        range.Bottom = Transform(aRowRange.Bottom);
    }
    return range;
}

bool operator==(const RowRequest& lhd, const RowRequest& rhd)
{
    return lhd.GetTuple() == rhd.GetTuple();
}

bool operator!=(const RowRequest& lhd, const RowRequest& rhd)
{
    return !operator==(lhd, rhd);
}

SqlCacheTable& SyncSqlCache::GetTable(bool aSuspend)
{
    return aSuspend ? mSuspendedItemsTable : mTable;
}
