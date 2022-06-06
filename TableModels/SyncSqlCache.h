#pragma once

#include <functional>
#include <QFont>
#include <QItemSelection>
#include <QFutureWatcher>
#include <QTimer>
#include <QPointer>

#include <optional>

#include "TextFilter/TextFilter.h"
#include "TableOperationHandlerBase.h"
#include "export/Exporter.h"
#include "SqlQueryUtils.h"
#include "SqlCacheTable.h"

using TNewItemsBuffer = std::vector<QVariantList>;
using TNewItemsBufferPtr = QSharedPointer<TNewItemsBuffer>;

using TSelectedIds = std::optional<std::set<qlonglong>>;

enum class LoadingStatus
{
    NotChanged,
    Finished,
    Started,
};

struct SortParameters
{
    int Column;
    int Order;
};

// посказка видимости строк на границе диапазона
enum class EdgeRowHintType
{
    Part,   // строка частично видима
    Full    // строка полностью видима
};

// подсказка положения текущей строки выделения
// как при скролле клавишами, так и при выделении
enum class ScrollHintType
{
    NoHint,         // 
    EnsureVisible   // тек.строка выделения не должна выйти за диапазон
};

using TSortParametersArg = std::optional<SortParameters>;
using TFilterParametersArg = std::optional<QString>;

struct RowChanges
{
    RowRange RemovedRows;
    QVector<RowRange> ChangedRows;
    RowRange NewRows;
};

struct RowRequest
{
    RowRange RowWindow;
    RowRange RowWindowVisible;
    qint64 Version = 0;

    auto GetTuple() const
    {
        return std::tie(
            RowWindow,
            RowWindowVisible,
            Version);
    };
};

bool operator==(const RowRequest& lhd, const RowRequest& rhd);
bool operator!=(const RowRequest& lhd, const RowRequest& rhd);

struct SelectionRequest
{
    QVector<RowRange> Selection;
    int CurrentRow = -1;
    qint64 Version = 0;
};

struct HintsRequest
{
    ScrollHintType ScrollHint = ScrollHintType::NoHint;
    EdgeRowHintType TopRowHint = EdgeRowHintType::Full;
    EdgeRowHintType BottomRowHint = EdgeRowHintType::Full;
};


struct ViewWindowValues
{
    QList<QVariantList> Data;

    /// Количество строк, удовлетворяющее фильтрам
    int RecordsCount = 0;

    // RowWindow
    RowRange Rows;
    RowRange RowsVisible;

    // Selection
    QVector<RowRange> Selection;
    int CurrentRow = -1;

    // Hints
    ScrollHintType ScrollHint = ScrollHintType::NoHint;
    EdgeRowHintType TopRowHint = EdgeRowHintType::Full;
    EdgeRowHintType BottomRowHint = EdgeRowHintType::Full;

    qint64 Version = 0;
    qint64 RequestId = -1;

    QVariant ExtraData;

    const QVariantList* GetRow(int aRow) const;
    QVariantList* GetRow(int aRow);

    RowRange PrepareRemoveRows(int aRecordsCount) const;
    void RemoveRows(int aRecordsCount);
    QVector<RowRange> PrepareChangeRows(const ViewWindowValues& aNewValues) const;
    void ChangeRows(const ViewWindowValues& aNewValues);
    RowRange PrepareAddRows(int aRecordsCount) const;
    void AddRows(int aRecordsCount);
    void SetData(
        const QList<QVariantList>& aData,
        const RowRange& aRows,
        const RowRange& aRowsVisiable,
        const int aRecordsCount);
};

using TCommonIndexesRanges = std::map<int, std::set<int>>;

class SyncSqlCache : public QObject
{
    Q_OBJECT

private:
    struct IdsInfo
    {
        std::vector<qlonglong> Ids;
        mutable std::unordered_map<qlonglong, size_t> IdPositions;

        QVariant GetId(int aI) const;
        bool IsOutOfRange(int aI) const;

        std::optional<size_t> GetRow(const QVariant& aId) const;

        void AddId(const QVariant& aId);
    };

public:
    SyncSqlCache(
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
        const QPointer<TableOperationHandlerBase>& aHandler);

    virtual ~SyncSqlCache() override;

    /// Потокобезопасные методы для вызова из front-потока /////////////////////////

    const QString& GetTableName() const;
    void StopExport();
    
    ////////////////////////////////////////////////////////////////////////////////
    /// Синхронные методы для обмена информацией с пользовательскими плагинами /////

    QSqlQuery PerformSqlUnsafe(
        const QString& aSql,
        const QVariantList& aParams) noexcept(false);
    QVariantList GetItemValues(const QVariant& aId);
    QSqlRecord GetRecord(int aRow);
    /// Набор идентификаторов выбранных строк.
    /// Используем маппинг строк на id из базовой модели.
    const std::tuple<
        std::optional<std::set<qlonglong>>,
        std::optional<qlonglong>> GetSelectedIds() const;

    ////////////////////////////////////////////////////////////////////////////////
                                                        
public slots:
    /// Обработчики событий от front-потока ////////////////////////////////////////

    void InitDbTable();
    void ClearTable(bool aIsFianal);
    void ProcessHeavyAction(
        const qint64 aRequestId,
        const TNewItemsBufferPtr& aValues,
        const LoadingStatus aLoadingStatus,
        const TSortParametersArg aSorting,
        const TFilterParametersArg aFilter,
        bool aReportSelected,
        bool aSuspendUpdates);
    void ProcessEasyAction(
        const qint64 aRequestId,
        const RowRequest& aRowRequest,
        const SelectionRequest& aSelectionRequest,
        const HintsRequest& aHintsRequest);
    void ConfirmVersion(qint64 aVersion);
    void On_PerformSelect(QString aSql, QVariantList aParams);
    void On_SetAutoScroll(bool aIsAutoScroll);
    void OnExport(
        const QString& aExportFileName,
        const ColumnsExportInfo& aColumns);
    ////////////////////////////////////////////////////////////////////////////////

signals:
    /// События, отправляемые во front-поток ///////////////////////////////////////

    void InitializationCompleted();
    void OperationCompleted(
        const QVariant& aSelectionDuration, 
        const QVariant& aDbRowCount,
        const QVariant& aSuspendedUpdatesCount,
        const ViewWindowValues& aValues, 
        bool aUpdated,
        TSelectedIds aSelectedIds);
    void ClearCompleted();
    void UserQueryPerformed(QVariantList aResults);
    void ExportFinished(const QString& aError);
    void ExportProgressChanged(int);
    void ErrorOccured(const QString& aErrorMessage);
    void PendingUpdatesProgressChanged(int);

    ////////////////////////////////////////////////////////////////////////////////

private:

    struct RowTransformator
    {
        const IdsInfo& Old;
        const IdsInfo& New;

        int Transform(int aRow) const;
        RowRange Transform(const RowRange& aRowRange) const;
    };

    TCommonIndexesRanges mCommonFieldsIndexes;

    QString mFilter;
    int mSortColumn = -1;
    Qt::SortOrder mSortOrder = Qt::SortOrder::AscendingOrder;

    RowRange mRequestedRowRange;
    RowRange mRequestedRowRangeVisible;
    bool mIsAutoScroll = true;
    bool mIsSelectionAllowed = false;

    /// Приблизительные оценки операций, выполненных с таблицами
    size_t mTableOperationsCounter = 0;
    size_t mSuspendedRecordsCounter = 0;

    std::map<qint64, IdsInfo> mVersionedIds;

    QPointer<TableOperationHandlerBase> mOperationHandler;

    const SqlQueryUtils::TSortOrder mDefaultSortOrder;
    const Qt::SortOrder mDefaultSortDirection;

    ViewWindowValues mViewWindowValues;

    mutable DataBaseMutex mDbConnection;
    SqlCacheTable mTable;
    /// Таблица для хранения данных, применение которых приостановленно.
    SqlCacheTable mSuspendedItemsTable;
    QSet<qlonglong> mSuspendedDeletedIds;

    TracerGuiWrapper mSqlCacheTracer;

    std::atomic_bool mStopExport {false};
    
private:
    /// Методы инициализации, вызываемые в конструкторе ////////////////////////////

    void SetOperationHandler(const QPointer<TableOperationHandlerBase>& aHandler);
    static SqlQueryUtils::TSortOrder MakeDefaultSortOrder(
        const SqlQueryUtils::TSortOrder& aSortOrder,
        int aMaxColumn,
        int aIdColumn);

    ////////////////////////////////////////////////////////////////////////////////
    /// Обертки над SqlCacheTable //////////////////////////////////////////////////

    QSqlQuery PerformSqlSafe(
        const QString& aSql,
        const QVariantList& aParams) noexcept;
    QSqlRecord GetItem(const QVariant& aId);
    qlonglong GetDbRowCount();
    qlonglong GetSuspendDbRowCount();

    ////////////////////////////////////////////////////////////////////////////////
    /// Методы работы с версионнным кэшом данных в ОП //////////////////////////////

    int GetRecordsCount() const;
    const IdsInfo* GetIdMapping() const;
    std::optional<RowTransformator> GetRowTransformation(qint64 aVersion) const;

    ////////////////////////////////////////////////////////////////////////////////
    /// Методы для обработки EasyAction ////////////////////////////////////////////
    bool SetRowWindow(const RowRequest& aRowRequest);
    bool SetSelection(const SelectionRequest& aSelectionRequest);

    ////////////////////////////////////////////////////////////////////////////////
    /// Методы для обработки HeavyAction ///////////////////////////////////////////

    void SetUpdatesFromDbAllowed(LoadingStatus aLoadingStatus);
    /// В зависимости от режима хранилища возвращает точное или приблизительное
    /// количество записей в основной таблице
    std::pair<std::optional<int>, std::optional<int>> EstimateDbRowCount(
        bool aMainTableUpdated,
        bool aIsSuspend);
    
    std::optional<int> TryPerformSelection(
        bool aMainTableUpdated,
        const TSortParametersArg aSorting,
        const TFilterParametersArg aFilter);
    void PerformSelection();
    void LogHeavyAction(
        std::optional<std::pair<qint64, qint64>> insertionDuration,
        std::optional<int> selectionDuration,
        std::optional<int> dbRecordCount,
        std::optional<int> rowCountingDuration,
        size_t aValuesSize);

    void ProcessDataPopulation(QSqlQuery& aQuery);
    void UpdateRowWindow();
    void UpdateIdMapping(QSqlQuery& aQuery);

    void SetSorting(const TSortParametersArg& aSorting);
    void SetFilter(const TFilterParametersArg& aFilter);
    
    ////////////////////////////////////////////////////////////////////////////////
    /// Методы добавления данных в хранилище////////////////////////////////////////

    /// Сохраняет данные в БД.
    /// Если контейнер не пустой, возвращает длительность выполнения транзакции
    /// и пользовательского кода плагина в миллисекундах.
    /// Выполняет изменения в БД транзакционно.
    std::optional<std::pair<qint64, qint64>> TryStoreItemsToDb(
        const TNewItemsBufferPtr& aValues,
        bool aSuspend) noexcept;
    void StoreItemsToDb(
        const TNewItemsBufferPtr& aValues,
        bool aSuspend) noexcept(false);
    void InsertOrReplace(
        const QVariantList& aFields,
        bool aSuspend) noexcept(false);
    bool AddPendingValue(QVariantList& aValues);
    void DeleteRecord(qlonglong aId, bool aSuspend) noexcept(false);
    void ResumeSuspendedItems() noexcept(false);
    
    ////////////////////////////////////////////////////////////////////////////////
    /// Методы обновления ViewWindowValues /////////////////////////////////////////

    void UpdateViewWindowValues(bool aRefreshAll);
    void UpdateViewWindowValuesInternal(bool aRefreshAll);
    void UpdateExtraData();
    
    ////////////////////////////////////////////////////////////////////////////////
    /// Методы работы с выделенными строками и видимым диапазоном

    void TransformSelection(
        qint64 aVersion,
        QVector<RowRange>& outSelection,
        int& outCurrentRow);
    SelectionRequest TransformSelection(const SelectionRequest& aSelectionRequest); 
    static bool TransformRowRange(
        const RowTransformator& aTransformator,
        RowRange& outRange,
        RowRange& outRangeVisible);
    std::optional<RowRequest> TransformRowRange(const RowRequest& aRowRequest);
    
    ////////////////////////////////////////////////////////////////////////////////
    /// Вспомогательные функциии общего назначения

    /// Логирование и отправка сигнала об ошибке
    void ReportError(const QString& aContext);
    /// Формирование ORDER BY строки для sql запроса
    QString OrderByClause() const;
    SqlCacheTable& GetTable(bool aSuspend);
    static QVariantList Record2List(const QSqlRecord& aRecord);
};

Q_DECLARE_METATYPE(RowRange)
Q_DECLARE_METATYPE(TSelectedIds)
Q_DECLARE_METATYPE(ViewWindowValues)
Q_DECLARE_METATYPE(ColumnExportInfo)
Q_DECLARE_METATYPE(ColumnsExportInfo)
Q_DECLARE_METATYPE(TNewItemsBufferPtr)
Q_DECLARE_METATYPE(LoadingStatus)
Q_DECLARE_METATYPE(TSortParametersArg)
Q_DECLARE_METATYPE(TFilterParametersArg)
Q_DECLARE_METATYPE(RowRequest)
Q_DECLARE_METATYPE(SelectionRequest)
Q_DECLARE_METATYPE(HintsRequest)
Q_DECLARE_METATYPE(ScrollHintType)
Q_DECLARE_METATYPE(EdgeRowHintType)
