#pragma once

#include "SyncSqlCache.h"
#include "UiHelpers.h"

#include <QAbstractTableModel>
#include <QThread>

#include <memory>

class AsyncSqlTableModelBase;
struct State;
class AsyncSqlTableEventProcessing;

struct ItemsSummary
{
    int Count = 0;
    int ReceivedCount = 0;
    int SelectedCount = 0;
    QString Error;
};

class AsyncSqlTableModelBase : public QAbstractTableModel
{
public:
    enum Command
    {
        DoNothing,
        SendUserActionRequest,
        SendUpdateRequest,
        SendUserQuery
    };

    /// события
    enum class Event
    {
        // тяжелые операции пользователя
        SortOperation,                  ///< Операция сортировки.
        FilterOperation,                ///< Операция фильтрации.
        DeleteOperation,                ///< Операция удаления выбранных локальных данных.

        // легкие операции пользователя
        WindowOperation,                ///< Операция смещения окна.
        SelectionOperation,             ///< Операция выделения.
        SelectionAndWindowOperation,    ///< Операция выделения и смещения окна.

        // тяжелые операции (не пользователя)
        NewDataPackReceived,    ///< Приход новых данных.

        FrontEndStateChanged,

        // backend
        BackEndStateChanged,

        LoadingFinished,        ///< получен последний пакет

        TimerExpired,           ///< период накопления операций истек

        ClearCompleted,         ///< хранилище завершило очистку

        UserQueryRequested,     ///< пользователь запросил данные из хранилища
        UserQueryCompleted,     ///< хранилище завершило выполнение пользовательского запроса

        ErrorOccured,           ///< произошла ошибка, хранилище в неправильном состоянии

        UpdateSuspensionFlagChanged, ///< пользователь изменил флаг остановки изменений
    };

private:

    Q_OBJECT

    QPointer<SyncSqlCache> mSyncTableModel;
    QThread mDbThread;
    
    static constexpr size_t mSkipLoggingSize = 1000;

    std::unique_ptr<State> mState;

protected:
    // Счетчик операций
    qint64 mOperationId = 0;

    /// Курсор
    std::unique_ptr<WaitCursorKeeper> mCursorKeeper;
    QTimer mCursorKeeperTimer;

    /// Данные
    ViewWindowValues mViewData;

    /// Экспорт
    bool mIsPendingExport = false;

    QPointer<TableOperationHandlerBase> mBackendHandler;

    TracerGuiWrapper mAsyncTableTracer;

    int mDbRecordsCount = 0;
    QString mError;

    std::set<Event> mBlockedUserActions;
    bool mPendingViewWindowUpdate = false;

    SqlQueryUtils::TSortOrder mDefaultSortOrder;
    Qt::SortOrder mDefaultSortDirection;

    /// Переменная отражает желаемое поведение асинхронного хранилища
    /// и соответствует состоянию переключателя в интерфейсе.
    /// Флаг передается в асинхронное хранилище в каждом HeavyAction-запросе.
    /// При изменении этого флага также отправляется HeavyAction запрос.
    bool mSuspendUpdates = false;

public:
    AsyncSqlTableModelBase(
        const std::weak_ptr<DataBaseConnections>& aConnections,
        const QString& aTableName,
        const SqlFieldDescription* aFieldList,
        size_t aFieldListSize,
        const SqlQueryUtils::TSortOrder& aDefaultSortOrder,
        Qt::SortOrder aDefaultSortDirection,
        const QString& aPrimaryKey,
        const TCommonIndexesRanges& aCommonIndexRanges,
        int aIdColumn,
        bool aUseFileStorage,
        QObject* aParent,
        const QPointer<TableOperationHandlerBase>& aHandler = nullptr);
    ~AsyncSqlTableModelBase() override;

    int rowCount(const QModelIndex &aParent = QModelIndex()) const override;
    QVariant data(const QModelIndex &aIndex, int aRole = Qt::DisplayRole) const override;

    // SetRowWindow - задает окно + хинты, если нужно
    // SetSelection - задает выделение + хинты, если нужно
    // SetSelectionAndRowWindow - задает вообще всё

    void SetRowWindow(
        const int aTopRow,
        const int aBottomRow,
        const ScrollHintType aScrollHint = ScrollHintType::NoHint,
        const EdgeRowHintType aTopRowHint = EdgeRowHintType::Full,
        const EdgeRowHintType aBottomRowHint = EdgeRowHintType::Full);

    /// aCustomEvent парметр отражает, что выделение изменилось из пользовательского кода, а не из стандартного виджета.
    /// Например, при нажатии клавиши.
    /// По-умолчанию он false. true для него выставляется из eventFilter
    void SetSelection(
        const QItemSelection& aSelection,
        const int aCurrentRow,
        const bool aCustomEvent = false,
        const ScrollHintType aScrollHint = ScrollHintType::NoHint,
        const EdgeRowHintType aTopRowHint = EdgeRowHintType::Full,
        const EdgeRowHintType aBottomRowHint = EdgeRowHintType::Full);

    void SetSelectionAndRowWindow(
        const QItemSelection& aSelection,
        const int aCurrentRow,
        const bool aCustomEvent,
        const int aTopRow,
        const int aBottomRow,
        const ScrollHintType aScrollHint = ScrollHintType::NoHint,
        const EdgeRowHintType aTopRowHint = EdgeRowHintType::Full,
        const EdgeRowHintType aBottomRowHint = EdgeRowHintType::Full);

    bool IsIndexVisible(const QModelIndex& aIndex) const;
    bool IsDataLoaded(const QModelIndex& aIndex) const;
    bool StartExport(const QString &aExportFileName, const ColumnsExportInfo &aColumns);
    bool AbortExport();
    void StopThread();
    void SetLoadingFinished(bool aFinished);
    const ViewWindowValues& GetViewData() const;
    void PrepareRemovingModel();
    ItemsSummary GetSummary() const;
    bool IsBusy() const;
    std::optional<std::pair<int, Qt::SortOrder> > GetDefaultSortIndicator() const;
    bool IsThreadCompletelyStopped() const;
    /// Удаление выбранных записей из локального хранилища.
    void ReportSelected();

    void SetSuspendUpdates(bool aSuspend);

protected:
    void Clear(bool aIsFinal = false);
    /// Очистка данных в наследниках.
    virtual void ClearCustomData() {}
    bool PerformUserQuery(const QString& aSql, const QVariantList& aParams);
    void PrepareSortOperation(int aColumn, int aOrder);
    void PrepareFilterOperation(const QString& aFilter);
    void ProcessNewChunkCompleted();
    QString GetTableName() const;
    void UpdateBufferLogSize();
    TNewItemsBuffer& GetNewItemsBuffer();

signals:
    /// Model --> SyncCache
    void InitDbTableAsync();

    void ConfirmVersionAsync(qint64 aVersion);

    void StartExportAsync(const QString &aExportFileName, const ColumnsExportInfo &aColumns);
    void SetAutoScrollAsync(bool aIsAutoScroll);

    void ClearTableAsync(bool aIsFinal);
    void PerformUserQueryAsync(QString aSql, QVariantList aParams);

    void ProcessEasyActionAsync(
        const qint64 aRequestId, 
        const RowRequest& aRowRequest,
        const SelectionRequest& aSelectionRequest,
        const HintsRequest& aHintsRequest);

    void ProcessHeavyActionAsync(
        const qint64 aRequestId,
        const TNewItemsBufferPtr& aValues,
        const LoadingStatus aLoadingStatus,
        const TSortParametersArg aSorting,
        const TFilterParametersArg aFilter,
        bool aReportSelected,
        bool aSuspendUpdates);

    /// Model --> Ui
    void ExportFinished(const QVariant& aResult);
    void ExportProgressChanged(int);
    void SelectionUpdated(const QItemSelection& aSelection, int aCurrentRow);
    void ViewWindowValuesChanged();
    void DbRecordsCountChanged();
    void UserQueryPerformed(const QVariantList& aResults);
    void SelectedIdsReported(const std::set<qlonglong>& aSelectedIds);
    
    void ReportSuspendedUpdatesCount(size_t aCount);
    void ReportIsBusy(bool aIsBusy);
    void PendingUpdatesProgressChanged(int);
                                                
private slots:
    void OnViewWindowValuesChanged(
        const QVariant& aSelectionDuration,
        const QVariant& aDbRowCount,
        const QVariant& aSuspendedUpdatesCount,
        const ViewWindowValues& aValues, 
        bool aIsUpdated,
        const TSelectedIds& aSelectedIds);
    void OnCleared();
    void OnUserQueryPerformed(QVariantList aResults);
    void OnExportFinished(const QString& aError);
    void OnCursorKeeperTimeout();
    void OnErrorOccured(const QString& aErrorMessage);

private:
    void TryEngageCursor();
    void TryRestoreCursor();
    std::pair<QVector<RowRange>, int> CorrectSelection(const QVector<RowRange>& aSelection, int aCurrentRow) const;
    void SetRowWindowInternal(
        const int aTopRow,
        const int aBottomRow,
        const ScrollHintType aScrollHint = ScrollHintType::NoHint,
        const EdgeRowHintType aTopRowHint = EdgeRowHintType::Full,
        const EdgeRowHintType aBottomRowHint = EdgeRowHintType::Full);
    void SetSelectionInternal(
        const QItemSelection& aSelection,
        const int aCurrentRow,
        const bool aCustomEvent = false,
        const ScrollHintType aScrollHint = ScrollHintType::NoHint,
        const EdgeRowHintType aTopRowHint = EdgeRowHintType::Full,
        const EdgeRowHintType aBottomRowHint = EdgeRowHintType::Full);

    RowRequest GetRowRequest() const;
    SelectionRequest GetSelectionRequest() const;
    HintsRequest GetHintsRequest() const;

    Command ProcessEvent(
        const Event aEvent,
        const bool aIsSupressLogging = false);

    friend class AsyncSqlTableEventProcessing;
};
