#include "AsyncSqlTableModelBase.h"
#include "Tracer.h"
#include <Common/Finally.hpp>

#include <cmath>

class AsyncSqlTableEventProcessing : public QObject
{
public:
    static constexpr int MinUserTimerDurationMs = 0;
    static constexpr int CursorTimerDurationMs = 1000;

    AsyncSqlTableEventProcessing(
        AsyncSqlTableModelBase* aModel,
        State* aState)
        : QObject(aModel)
        , mModel(aModel)
        , mState(aState)
    {
        mTimerState.UserActionTimer.setSingleShot(true);
        mTimerState.UserActionTimer.callOnTimeout(this, &AsyncSqlTableEventProcessing::OnTimerExpired);
    }

    AsyncSqlTableModelBase::Command ProcessEvent(const AsyncSqlTableModelBase::Event aEvent);
    bool IsBusy() const;

    static std::string ToString(const AsyncSqlTableModelBase::Event aValue);
    static std::string ToString(const AsyncSqlTableModelBase::Command aValue);

    void SetLastUpdateDurationMs(int aMs);

private:
    void OnTimerExpired();
    void StartTimer(bool aForce);
    void ProcessEventInternal(const AsyncSqlTableModelBase::Event aEvent);
    AsyncSqlTableModelBase::Command GetCommand() const;

private:
    AsyncSqlTableModelBase* mModel;
    State* mState;

    struct TimerState
    {
        QTimer UserActionTimer;
        bool IsOperationSendAllowed = false;

        int LastUpdateDurationMs = MinUserTimerDurationMs;
    } mTimerState;
    bool mIsErrorOccured = false;
};

struct State
{
    State(AsyncSqlTableModelBase* aModel)
        : mEventProcessing(aModel, this)
    {
    }

    struct FrontendState
    {
        bool mIsFrontendReady = false;

        bool IsFrontendReady() const
        {
            return mIsFrontendReady;
        }

    } mFrontEndState;

    struct BackendState
    {
        TNewItemsBufferPtr WritingNewItemsBuffer = TNewItemsBufferPtr::create();
        std::optional<qint64> PendingUpdate;
        bool IsPendingClear = false;
        bool IsPendingUserQuery = false;

        bool IsBackendReady() const
        {
            return WritingNewItemsBuffer->empty()
                && !PendingUpdate
                && !IsPendingClear
                && !IsPendingUserQuery;
        }

    } mBackEndState;

    struct PendingDataIncomingState
    {
        TNewItemsBufferPtr PendingNewItemsBuffer = TNewItemsBufferPtr::create();
        LoadingStatus PendingLoadStatus = LoadingStatus::NotChanged;
        bool ResumeUpdates = false;

        bool IsUpdateOperationNeeded() const
        {
            return !PendingNewItemsBuffer->empty()
                || PendingLoadStatus != LoadingStatus::NotChanged
                || ResumeUpdates;
        }

    } mPendingDataIncomingState;

    struct PendingUserHeavyActionState
    {
        TSortParametersArg mPendingSorting;
        TFilterParametersArg mPendingFilter;
        bool mReportSelected {};

        bool IsUpdateOperationNeeded() const
        {
            return mPendingSorting || mPendingFilter || mReportSelected;
        }
    } mPendingUserHeavyActionState;

    struct PendingUserEasyActionState
    {
        std::optional<RowRequest> RequestedRows;
        std::optional<SelectionRequest> RequestedSelection;
        std::optional<HintsRequest> RequestedHints;

        bool IsNeeded() const
        {
            return RequestedRows || RequestedSelection || RequestedHints;
        }
    } mPendingUserEasyActionState;

    struct PendingUserQueryActionState
    {
        std::optional<std::pair<QString, QVariantList>> Query;

        bool IsNeeded() const
        {
            return static_cast<bool>(Query);
        }
    } mPendingUserQueryActionState;

    /// Для логирования очереди.
    size_t mPreviousLoggedSize = 0;

    /// Для логирования turnaround.
    QDateTime mLastUpdateRequestTime;

    AsyncSqlTableEventProcessing mEventProcessing;
};

AsyncSqlTableModelBase::Command
    AsyncSqlTableEventProcessing::ProcessEvent(const AsyncSqlTableModelBase::Event aEvent)
{
    ProcessEventInternal(aEvent);
    return GetCommand();
}

AsyncSqlTableModelBase::Command AsyncSqlTableEventProcessing::GetCommand() const
{
    if (mIsErrorOccured)
    {
        return AsyncSqlTableModelBase::Command::DoNothing;
    }
    
    if (mState->mBackEndState.IsBackendReady())
    {
        if (mState->mFrontEndState.IsFrontendReady())
        {
            if (mState->mPendingUserQueryActionState.IsNeeded())
            {
                return AsyncSqlTableModelBase::Command::SendUserQuery;
            }
            else if (mState->mPendingUserEasyActionState.IsNeeded())
            {
                return AsyncSqlTableModelBase::Command::SendUserActionRequest;
            }
            else if (mTimerState.IsOperationSendAllowed && mState->mPendingUserHeavyActionState.IsUpdateOperationNeeded())
            {
                return AsyncSqlTableModelBase::Command::SendUpdateRequest;
            }
        }
        if (mTimerState.IsOperationSendAllowed && mState->mPendingDataIncomingState.IsUpdateOperationNeeded())
        {
            return AsyncSqlTableModelBase::Command::SendUpdateRequest;
        }
    }

    return AsyncSqlTableModelBase::Command::DoNothing;
}

bool AsyncSqlTableEventProcessing::IsBusy() const
{
    if (!mState->mFrontEndState.IsFrontendReady())
    {
        return false;
    }

    if (!mState->mBackEndState.IsBackendReady()
        || mState->mPendingUserHeavyActionState.IsUpdateOperationNeeded()
        || mState->mPendingDataIncomingState.IsUpdateOperationNeeded())
    {
        return true;
    }
    return false;
}

void AsyncSqlTableEventProcessing::SetLastUpdateDurationMs(int aMs)
{
    mTimerState.LastUpdateDurationMs = aMs;
}

void AsyncSqlTableEventProcessing::ProcessEventInternal(const AsyncSqlTableModelBase::Event aEvent)
{
    if (mIsErrorOccured)
    {
        return;
    }
    
    switch(aEvent)
    {
    case AsyncSqlTableModelBase::Event::LoadingFinished:
    case AsyncSqlTableModelBase::Event::NewDataPackReceived:
    case AsyncSqlTableModelBase::Event::UpdateSuspensionFlagChanged:
        StartTimer(false);
        return;
    case AsyncSqlTableModelBase::Event::SortOperation:
    case AsyncSqlTableModelBase::Event::FilterOperation:
    case AsyncSqlTableModelBase::Event::DeleteOperation:
    case AsyncSqlTableModelBase::Event::WindowOperation:
    case AsyncSqlTableModelBase::Event::SelectionOperation:
    case AsyncSqlTableModelBase::Event::SelectionAndWindowOperation:
        StartTimer(true);
        return;
    case AsyncSqlTableModelBase::Event::FrontEndStateChanged:
    case AsyncSqlTableModelBase::Event::BackEndStateChanged:
    case AsyncSqlTableModelBase::Event::TimerExpired:
    case AsyncSqlTableModelBase::Event::ClearCompleted:
    case AsyncSqlTableModelBase::Event::UserQueryRequested:
    case AsyncSqlTableModelBase::Event::UserQueryCompleted:
        return;
    case AsyncSqlTableModelBase::Event::ErrorOccured:
        mIsErrorOccured = true;
        return;
    }
    assert(false && "Unknown event");
}

void AsyncSqlTableEventProcessing::OnTimerExpired()
{
    mTimerState.IsOperationSendAllowed = true;
    mModel->ProcessEvent(AsyncSqlTableModelBase::Event::TimerExpired);
}

std::string AsyncSqlTableEventProcessing::ToString(const AsyncSqlTableModelBase::Event aValue)
{
    switch (aValue)
    {
    case AsyncSqlTableModelBase::Event::SortOperation:
        return "SortOperation";
    case AsyncSqlTableModelBase::Event::FilterOperation:
        return "FilterOperation";
    case AsyncSqlTableModelBase::Event::DeleteOperation:
        return "DeleteOperation";
    case AsyncSqlTableModelBase::Event::WindowOperation:
        return "WindowOperation";
    case AsyncSqlTableModelBase::Event::SelectionOperation:
        return "SelectionOperation";
    case AsyncSqlTableModelBase::Event::SelectionAndWindowOperation:
        return "SelectionAndWindowOperation";
    case AsyncSqlTableModelBase::Event::NewDataPackReceived:
        return "NewDataPackReceived";
    case AsyncSqlTableModelBase::Event::FrontEndStateChanged:
        return "FrontEndStateChanged";
    case AsyncSqlTableModelBase::Event::BackEndStateChanged:
        return "BackEndStateChanged";
    case AsyncSqlTableModelBase::Event::LoadingFinished:
        return "LoadingFinished";
    case AsyncSqlTableModelBase::Event::TimerExpired:
        return "TimerExpired";
    case AsyncSqlTableModelBase::Event::ClearCompleted:
        return "ClearCompleted";
    case AsyncSqlTableModelBase::Event::UserQueryRequested:
        return "UserQueryRequested";
    case AsyncSqlTableModelBase::Event::UserQueryCompleted:
        return "UserQueryCompleted";
    case AsyncSqlTableModelBase::Event::ErrorOccured:
        return "ErrorOccured";
    case AsyncSqlTableModelBase::Event::UpdateSuspensionFlagChanged:
        return "UpdateSuspensionFlagChanged";
    }
    return "";
}

std::string AsyncSqlTableEventProcessing::ToString(const AsyncSqlTableModelBase::Command aValue)
{
    switch (aValue)
    {
    case AsyncSqlTableModelBase::Command::DoNothing:
        return "DoNothing";
    case AsyncSqlTableModelBase::Command::SendUserActionRequest:
        return "SendUserActionRequest";
    case AsyncSqlTableModelBase::Command::SendUpdateRequest:
        return "SendUpdateRequest";
    case AsyncSqlTableModelBase::Command::SendUserQuery:
        return "SendUserQuery";
    }
    return "";
}

void AsyncSqlTableEventProcessing::StartTimer(bool aForce)
{
    if (aForce
        || (!mTimerState.IsOperationSendAllowed && !mTimerState.UserActionTimer.isActive()))
    {
        mTimerState.IsOperationSendAllowed = false;
        mTimerState.UserActionTimer.start(mTimerState.LastUpdateDurationMs);
    }
}

AsyncSqlTableModelBase::AsyncSqlTableModelBase(
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
    const QPointer<TableOperationHandlerBase>& aHandler)
    : QAbstractTableModel(aParent)
    , mSyncTableModel(
        new SyncSqlCache(
            aConnections,
            aTableName,
            aFieldList,
            aFieldListSize,
            aPrimaryKey,
            aCommonIndexRanges,
            aIdColumn,
            aDefaultSortOrder,
            aDefaultSortDirection,
            nullptr,
            aUseFileStorage,
            aHandler))
    , mState(std::make_unique<State>(this))
    , mBackendHandler(aHandler)
    , mAsyncTableTracer(GetTracer(QString("model.%1.async").arg(GetTableName()).toStdString().c_str()))
    , mDefaultSortOrder(aDefaultSortOrder)
    , mDefaultSortDirection(aDefaultSortDirection)
{
    mSyncTableModel->moveToThread(&mDbThread);

    //
    // To cache from model
    //
    connect(
        this, &AsyncSqlTableModelBase::InitDbTableAsync,
        mSyncTableModel, &SyncSqlCache::InitDbTable);
    connect(
        this, &AsyncSqlTableModelBase::ProcessHeavyActionAsync,
        mSyncTableModel, &SyncSqlCache::ProcessHeavyAction);
    connect(
        this, &AsyncSqlTableModelBase::ProcessEasyActionAsync,
        mSyncTableModel, &SyncSqlCache::ProcessEasyAction);
    connect(
        this, &AsyncSqlTableModelBase::ConfirmVersionAsync,
        mSyncTableModel, &SyncSqlCache::ConfirmVersion);
    connect(
        this, &AsyncSqlTableModelBase::StartExportAsync,
        mSyncTableModel, &SyncSqlCache::OnExport);
    connect(
        this, &AsyncSqlTableModelBase::ClearTableAsync,
        mSyncTableModel, &SyncSqlCache::ClearTable);
    connect(
        this, &AsyncSqlTableModelBase::PerformUserQueryAsync,
        mSyncTableModel, &SyncSqlCache::On_PerformSelect);
    connect(
        this, &AsyncSqlTableModelBase::SetAutoScrollAsync,
        mSyncTableModel, &SyncSqlCache::On_SetAutoScroll);

    //
    // From cache to model
    //
    connect(
        mSyncTableModel, &SyncSqlCache::OperationCompleted,
        this, &AsyncSqlTableModelBase::OnViewWindowValuesChanged);
    connect(
        mSyncTableModel, &SyncSqlCache::ClearCompleted,
        this, &AsyncSqlTableModelBase::OnCleared);
    connect(
        mSyncTableModel, &SyncSqlCache::UserQueryPerformed,
        this, &AsyncSqlTableModelBase::OnUserQueryPerformed);
    connect(
        mSyncTableModel, &SyncSqlCache::ExportFinished,
        this, &AsyncSqlTableModelBase::OnExportFinished);
    connect(
        mSyncTableModel, &SyncSqlCache::ExportProgressChanged,
        this, &AsyncSqlTableModelBase::ExportProgressChanged);
    connect(
        mSyncTableModel, &SyncSqlCache::PendingUpdatesProgressChanged,
        this, &AsyncSqlTableModelBase::PendingUpdatesProgressChanged);
    connect(
        mSyncTableModel, &SyncSqlCache::ErrorOccured,
        this, &AsyncSqlTableModelBase::OnErrorOccured);

    mDbThread.start();
    mCursorKeeperTimer.setSingleShot(true);
    mCursorKeeperTimer.setInterval(AsyncSqlTableEventProcessing::CursorTimerDurationMs);
    mCursorKeeperTimer.callOnTimeout(this, &AsyncSqlTableModelBase::OnCursorKeeperTimeout);
}

AsyncSqlTableModelBase::~AsyncSqlTableModelBase()
{
    StopThread();
}

int AsyncSqlTableModelBase::rowCount(const QModelIndex &aParent) const
{
    if (aParent.isValid())
    {
        return 0;
    }

    return mViewData.RecordsCount;
}

QVariant AsyncSqlTableModelBase::data(const QModelIndex &aIndex, int /*aRole*/) const
{
    if (!aIndex.isValid())
    {
        return QVariant();
    }

    auto rowPtr = mViewData.GetRow(aIndex.row());
    if (!rowPtr)
    {
        return QVariant();
    }
    const auto& row = *rowPtr;

    if (aIndex.column() >= row.size())
    {
        return QVariant();
    }

    return row[aIndex.column()];
}

void AsyncSqlTableModelBase::SetRowWindow(
    const int aTopRow,
    const int aBottomRow,
    const ScrollHintType aScrollHint,
    const EdgeRowHintType aTopRowHint,
    const EdgeRowHintType aBottomRowHint)
{
    if (mBlockedUserActions.count(Event::WindowOperation))
    {
        return;
    }

    SetRowWindowInternal(
        aTopRow,
        aBottomRow,
        aScrollHint,
        aTopRowHint,
        aBottomRowHint);

    ProcessEvent(AsyncSqlTableModelBase::Event::WindowOperation);
}

void AsyncSqlTableModelBase::SetRowWindowInternal(
    const int aTopRow,
    const int aBottomRow,
    const ScrollHintType aScrollHint,
    const EdgeRowHintType aTopRowHint,
    const EdgeRowHintType aBottomRowHint)
{
    RowRange newRange;
    {
        newRange.Top = qMax(0, aTopRow);
        newRange.Bottom = qMax(newRange.Top, aBottomRow);
    }

    RowRequest rowRequest;
    {
        rowRequest.RowWindowVisible = newRange;
        rowRequest.RowWindow = newRange.Expand(SqlQueryUtils::RowWindowOffset);
        rowRequest.Version = mViewData.Version;
    }
    mState->mPendingUserEasyActionState.RequestedRows = rowRequest;

    HintsRequest hintsRequest;
    {
        hintsRequest.ScrollHint = aScrollHint;
        hintsRequest.TopRowHint = aTopRowHint;
        hintsRequest.BottomRowHint = aBottomRowHint;
    }
    mState->mPendingUserEasyActionState.RequestedHints = hintsRequest;
}

const ViewWindowValues& AsyncSqlTableModelBase::GetViewData() const
{
    return mViewData;
}

void AsyncSqlTableModelBase::PrepareRemovingModel()
{
    if (mCursorKeeperTimer.isActive())
    {
        if (mCursorKeeper)
        {
            mCursorKeeper.reset();
        }
        mCursorKeeperTimer.stop();
    }
}

ItemsSummary AsyncSqlTableModelBase::GetSummary() const
{
    ItemsSummary result;

    result.Count = mViewData.RecordsCount;
    result.ReceivedCount = mDbRecordsCount;

    result.SelectedCount = 0;

    Q_FOREACH(const RowRange& range, mViewData.Selection)
    {
        result.SelectedCount += range.Count();
    }

    result.Error = mError;

    return result;
}

bool AsyncSqlTableModelBase::IsBusy() const
{
    if (!mState)
    {
        return false;
    }
    return mState->mEventProcessing.IsBusy();
}

bool AsyncSqlTableModelBase::IsThreadCompletelyStopped() const
{
    /// Поток считается полностью завершённым, если он остановился 
    /// и объекты, которые обрабатывались в его EventLoop, удалены.
    return !mSyncTableModel && !mBackendHandler && mDbThread.isFinished();
}

std::optional<std::pair<int, Qt::SortOrder>> AsyncSqlTableModelBase::GetDefaultSortIndicator() const
{
    if (mDefaultSortOrder.empty() || mDefaultSortOrder.at(0).empty())
    {
        return std::nullopt;
    }

    return std::make_pair(mDefaultSortOrder.at(0).at(0), mDefaultSortDirection);
}

void AsyncSqlTableModelBase::SetSelection(
    const QItemSelection& aSelection,
    const int aCurrentRow,
    const bool aCustomEvent,
    const ScrollHintType aScrollHint,
    const EdgeRowHintType aTopRowHint,
    const EdgeRowHintType aBottomRowHint)
{
    if (mBlockedUserActions.count(Event::SelectionOperation))
    {
        return;
    }

    SetSelectionInternal(
        aSelection,
        aCurrentRow,
        aCustomEvent,
        aScrollHint,
        aTopRowHint,
        aBottomRowHint);

    ProcessEvent(AsyncSqlTableModelBase::Event::SelectionOperation);
}

void AsyncSqlTableModelBase::SetSelectionInternal(
    const QItemSelection& aSelection,
    const int aCurrentRow,
    const bool aCustomEvent,
    const ScrollHintType aScrollHint,
    const EdgeRowHintType aTopRowHint,
    const EdgeRowHintType aBottomRowHint)
{
    QVector<RowRange> oneColSelection;
    foreach (const auto& s, aSelection)
    {
        oneColSelection.push_back(RowRange { s.top(), s.bottom() });
    }

    mAsyncTableTracer.Info(
        QString("AsyncSqlTableModelBase::SetSelection: selection: %1, curRow: %2")
            .arg(ToString(oneColSelection))
            .arg(aCurrentRow));

    const auto [correctedSelection, correctedRow] = CorrectSelection(oneColSelection, aCurrentRow);

    SelectionRequest selectionRequest;
    {
        selectionRequest.Selection = correctedSelection;
        selectionRequest.CurrentRow = correctedRow;
        selectionRequest.Version = mViewData.Version;
    }
    mState->mPendingUserEasyActionState.RequestedSelection = selectionRequest;

    HintsRequest hintsRequest;
    {
        hintsRequest.ScrollHint = aScrollHint;
        hintsRequest.TopRowHint = aTopRowHint;
        hintsRequest.BottomRowHint = aBottomRowHint;
    }
    mState->mPendingUserEasyActionState.RequestedHints = hintsRequest;

    mAsyncTableTracer.Info(
        QString("AsyncSqlTableModelBase::SetSelection: correctSelection: %1, curRow: %2")
            .arg(ToString(selectionRequest.Selection))
            .arg(selectionRequest.CurrentRow));

    if (!aCustomEvent)
    {
        mViewData.Selection = correctedSelection;
        mViewData.CurrentRow = correctedRow;
    }
}

void AsyncSqlTableModelBase::SetSelectionAndRowWindow(
    const QItemSelection& aSelection,
    const int aCurrentRow,
    const bool aCustomEvent,
    const int aTopRow,
    const int aBottomRow,
    const ScrollHintType aScrollHint,
    const EdgeRowHintType aTopRowHint,
    const EdgeRowHintType aBottomRowHint)
{
    Q_ASSERT(mBlockedUserActions.empty());

    SetSelectionInternal(
        aSelection,
        aCurrentRow,
        aCustomEvent,
        aScrollHint,
        aTopRowHint,
        aBottomRowHint);

    SetRowWindowInternal(
        aTopRow,
        aBottomRow,
        aScrollHint,
        aTopRowHint,
        aBottomRowHint);

    ProcessEvent(AsyncSqlTableModelBase::Event::SelectionAndWindowOperation);
}

bool AsyncSqlTableModelBase::IsIndexVisible(const QModelIndex& aIndex) const
{
    if (!aIndex.isValid())
    {
        return false;
    }

    return mViewData.Rows.Contains(aIndex.row());
}

bool AsyncSqlTableModelBase::IsDataLoaded(const QModelIndex &aIndex) const
{
    if (!aIndex.isValid())
    {
        return false;
    }

    return mViewData.GetRow(aIndex.row());
}

bool AsyncSqlTableModelBase::StartExport(const QString &aExportFileName, const ColumnsExportInfo &aColumns)
{
    if (mIsPendingExport)
    {
        return false;
    }

    mIsPendingExport = true;
    emit StartExportAsync(aExportFileName, aColumns);
    return true;
}

bool AsyncSqlTableModelBase::AbortExport()
{
    if (mIsPendingExport)
    {
        mSyncTableModel->StopExport();
        return true;
    }
    return false;
}

void AsyncSqlTableModelBase::StopThread()
{
    if (mDbThread.isRunning())
    {
        mSyncTableModel->setParent(nullptr);
        mSyncTableModel->deleteLater();
        disconnect(mSyncTableModel, nullptr, nullptr, nullptr);
        this->disconnect(mSyncTableModel);

        mDbThread.quit();
        int i = 0;
        while (!mDbThread.wait(500) || !IsThreadCompletelyStopped())
        {
            if (++i == 100)
            {
                mDbThread.terminate();
            }
            mAsyncTableTracer.Info("Wait for thread");
        }
    }
}

void AsyncSqlTableModelBase::SetLoadingFinished(bool aFinished)
{
    mState->mPendingDataIncomingState.PendingLoadStatus = aFinished
        ? LoadingStatus::Finished
        : LoadingStatus::Started;

    mState->mFrontEndState.mIsFrontendReady = aFinished;
    ProcessEvent(AsyncSqlTableModelBase::Event::FrontEndStateChanged, true);

    if (aFinished)
    {
        ProcessEvent(AsyncSqlTableModelBase::Event::LoadingFinished, true);
    }
}

void AsyncSqlTableModelBase::Clear(bool aIsFinal)
{
    mAsyncTableTracer.Info("Clear: " + QString::number(aIsFinal));

    if (mState->mBackEndState.IsPendingClear)
    {
        mAsyncTableTracer.Info("Clear skipped");
        return;
    }
    beginResetModel();
    {
        emit ClearTableAsync(aIsFinal);
        mState = std::make_unique<State>(this);
        mState->mBackEndState.IsPendingClear = true;
        mViewData = ViewWindowValues{};
        mDbRecordsCount = 0;
        mError.clear();
        mPendingViewWindowUpdate = false;
        TryRestoreCursor();
        emit DbRecordsCountChanged();
    }
    ClearCustomData();
    endResetModel();
}

bool AsyncSqlTableModelBase::PerformUserQuery(const QString& aSql, const QVariantList& aParams)
{
    mAsyncTableTracer.Info(QString("%1: %2, %3").arg(Q_FUNC_INFO).arg(aSql));

    if (mState->mPendingUserQueryActionState.IsNeeded()
        || mState->mBackEndState.IsPendingUserQuery)
    {
        Q_ASSERT(false);
        mAsyncTableTracer.Error(QString("Query is already being executed"));
        return false;
    }

    mState->mPendingUserQueryActionState.Query = std::make_pair(aSql, aParams);
    ProcessEvent(AsyncSqlTableModelBase::Event::UserQueryRequested);

    return true;
}

void AsyncSqlTableModelBase::ReportSelected()
{
    mState->mPendingUserHeavyActionState.mReportSelected = true;
    ProcessEvent(AsyncSqlTableModelBase::Event::DeleteOperation);
}

void AsyncSqlTableModelBase::SetSuspendUpdates(bool aSuspend)
{
    mSuspendUpdates = aSuspend;
    if (!mSuspendUpdates)
    {
        mState->mPendingDataIncomingState.ResumeUpdates = true;
    }
    /// Включение режима не требует обработки в хранилище.
    /// Флаг будет отправлен с будущими обновлениями, препятствуя их применению.
    /// Выключение должно быть отправлено явно.
    ProcessEvent(AsyncSqlTableModelBase::Event::UpdateSuspensionFlagChanged);
}

void AsyncSqlTableModelBase::PrepareSortOperation(int aColumn, int aOrder)
{
    mState->mPendingUserHeavyActionState.mPendingSorting = SortParameters { aColumn, aOrder };
    ProcessEvent(AsyncSqlTableModelBase::Event::SortOperation);
}

void AsyncSqlTableModelBase::PrepareFilterOperation(const QString& aFilter)
{
     mState->mPendingUserHeavyActionState.mPendingFilter = aFilter;
     ProcessEvent(AsyncSqlTableModelBase::Event::FilterOperation);
}

void AsyncSqlTableModelBase::ProcessNewChunkCompleted()
{
    UpdateBufferLogSize();
    ProcessEvent(AsyncSqlTableModelBase::Event::NewDataPackReceived, true);
}

QString AsyncSqlTableModelBase::GetTableName() const
{
    if (mSyncTableModel)
    {
        return mSyncTableModel->GetTableName();
    }
    else
    {
        return QString {};
    }
}

void AsyncSqlTableModelBase::OnViewWindowValuesChanged(
    const QVariant& aSelectionDuration,
    const QVariant& aDbRowCount,
    const QVariant& aSuspendedUpdatesCount,
    const ViewWindowValues& aValues, 
    bool aIsUpdated,
    const TSelectedIds& aSelectedIds)
{
    if (!mError.isEmpty())
    {
        return;
    }
    
    if (aSelectedIds)
    {
        emit SelectedIdsReported(*aSelectedIds);
    }

    if (mState->mBackEndState.IsPendingClear)
    {
        return;
    }

    const auto turnaroundMs = QDateTime::currentDateTime().toMSecsSinceEpoch()
        - mState->mLastUpdateRequestTime.toMSecsSinceEpoch();
    mAsyncTableTracer.Trace(
        QString("%1: OpId: %2, IsUpdated: %3, turnaround: %4 ms")
            .arg(Q_FUNC_INFO)
            .arg(aValues.RequestId)
            .arg(aIsUpdated)
            .arg(turnaroundMs));

    mState->mBackEndState.PendingUpdate.reset();
    mState->mBackEndState.WritingNewItemsBuffer->clear();

    if (aSelectionDuration.isValid())
    {
        mState->mEventProcessing.SetLastUpdateDurationMs((std::max)(
            aSelectionDuration.toInt(),
            AsyncSqlTableEventProcessing::MinUserTimerDurationMs));
    }

    if (aDbRowCount.isValid())
    {
        mDbRecordsCount = aDbRowCount.toInt();
        emit DbRecordsCountChanged();
    }

    if (aSuspendedUpdatesCount.isValid())
    {
        emit ReportSuspendedUpdatesCount(static_cast<size_t>(aSuspendedUpdatesCount.toULongLong()));
    }

    const auto executedCommand = ProcessEvent(
        AsyncSqlTableModelBase::Event::BackEndStateChanged);

    /// Если отправили новый реквест, то данные применять нельзя, т.к. они могут приводить к отображению предыдущего состояния.
    /// Если на стороне стора ничего не изменилось (!aIsUpdated), то данные применять нельзя, чтобы не было зацикливания.
    /// Если на стороне стора ничего не изменилось (!aIsUpdated), но есть mPendingViewWindowUpdate, то данные нужно применить.
    if (executedCommand == AsyncSqlTableModelBase::Command::SendUserActionRequest
        || (!aIsUpdated && !mPendingViewWindowUpdate))
    {
        mPendingViewWindowUpdate |= aIsUpdated;
        return;
    }

    mPendingViewWindowUpdate = false;
    mBlockedUserActions.insert(Event::SelectionOperation);
    if (mViewData.RecordsCount == aValues.RecordsCount)
    {
        mBlockedUserActions.insert(Event::WindowOperation);
    }

    auto removeRange = mViewData.PrepareRemoveRows(aValues.RecordsCount);
    if (removeRange.IsValid())
    {
        beginRemoveRows(QModelIndex(), removeRange.Top, removeRange.Bottom);
        mViewData.RemoveRows(aValues.RecordsCount);
        endRemoveRows();
    }

    auto changedRanges = mViewData.PrepareChangeRows(aValues);
    mViewData.ChangeRows(aValues);
    for (auto it = changedRanges.cbegin(); it != changedRanges.cend(); ++it)
    {
        if (it->IsValid())
        {
            emit dataChanged(index(it->Top, 0), index(it->Bottom, columnCount() - 1));
        }
    }

    auto newRange = mViewData.PrepareAddRows(aValues.RecordsCount);
    if (newRange.IsValid())
    {
        beginInsertRows(QModelIndex(), newRange.Top, newRange.Bottom);
        mViewData.AddRows(aValues.RecordsCount);
        endInsertRows();
    }

    if (mViewData.Selection != aValues.Selection
        || mViewData.CurrentRow != aValues.CurrentRow
        || mViewData.ScrollHint != aValues.ScrollHint
        || mViewData.TopRowHint != aValues.TopRowHint
        || mViewData.BottomRowHint != aValues.BottomRowHint)
    {
        mViewData.Selection = aValues.Selection;
        mViewData.CurrentRow = aValues.CurrentRow;
        mViewData.ScrollHint = aValues.ScrollHint;
        mViewData.TopRowHint = aValues.TopRowHint;
        mViewData.BottomRowHint = aValues.BottomRowHint;

        QItemSelection newSelection;
        foreach (const auto& s, aValues.Selection)
        {
            newSelection.push_back(QItemSelectionRange(createIndex(s.Top, 0), createIndex(s.Bottom, 0)));
        }

        emit SelectionUpdated(newSelection, mViewData.CurrentRow);
    }

    if (mViewData.ExtraData != aValues.ExtraData)
    {
        mViewData.ExtraData = aValues.ExtraData;
    }

    if (mViewData.Version != aValues.Version)
    {
        mViewData.Version = aValues.Version;
        emit ConfirmVersionAsync(aValues.Version);
    }

    mViewData.RequestId = aValues.RequestId;

    emit ViewWindowValuesChanged();

    mBlockedUserActions.clear();
}

void AsyncSqlTableModelBase::OnCleared()
{
    mState->mBackEndState.IsPendingClear = false;
    ProcessEvent(AsyncSqlTableModelBase::Event::ClearCompleted);
}

void AsyncSqlTableModelBase::OnUserQueryPerformed(QVariantList aResults)
{
    mState->mBackEndState.IsPendingUserQuery = false;
    emit UserQueryPerformed(aResults);
    ProcessEvent(AsyncSqlTableModelBase::Event::UserQueryCompleted);
}

void AsyncSqlTableModelBase::OnExportFinished(const QString& aError)
{
    mIsPendingExport = false;
    emit ExportFinished(QVariant(aError));
}

void AsyncSqlTableModelBase::OnCursorKeeperTimeout()
{
    if (!mError.isEmpty())
    {
        return;
    }
    mAsyncTableTracer.Trace(QString("%1: Engage--------------------").arg(Q_FUNC_INFO));
    mCursorKeeper = std::make_unique<WaitCursorKeeper>(Qt::BusyCursor);
}

void AsyncSqlTableModelBase::OnErrorOccured(const QString& aErrorMessage)
{
    mError = aErrorMessage;
    
    beginResetModel();
    {
        mState = std::make_unique<State>(this);
        ProcessEvent(AsyncSqlTableModelBase::Event::ErrorOccured);
        
        mViewData = ViewWindowValues{};
        mDbRecordsCount = 0;
        TryRestoreCursor();
        emit DbRecordsCountChanged();
    }
    endResetModel();
}

void AsyncSqlTableModelBase::TryEngageCursor()
{
    if (!mCursorKeeper && !mCursorKeeperTimer.isActive())
    {
        mCursorKeeperTimer.start();
    }
}

void AsyncSqlTableModelBase::TryRestoreCursor()
{
    if (mCursorKeeper || mCursorKeeperTimer.isActive())
    {
        mCursorKeeperTimer.stop();
        if (mCursorKeeper)
        {
            mAsyncTableTracer.Trace(QString("%1: Restore--------------------").arg(Q_FUNC_INFO));
            mCursorKeeper.reset();
        }
    }
}

std::pair<QVector<RowRange>, int> AsyncSqlTableModelBase::CorrectSelection(
    const QVector<RowRange>& aSelection,
    int aCurrentRow) const
{
    auto resultCurrentRow = aCurrentRow;
    int distance = std::numeric_limits<int>::max();

    foreach(const auto& range, aSelection)
    {
        auto newDistance = range.Distance(aCurrentRow);
        if (newDistance < distance)
        {
            resultCurrentRow = range.NearestRow(aCurrentRow);
            distance = newDistance;
        }
    }

    if (distance == std::numeric_limits<int>::max())
    {
        return std::make_pair(QVector<RowRange>(), -1);
    }

    return std::make_pair(aSelection, resultCurrentRow);
}

AsyncSqlTableModelBase::Command AsyncSqlTableModelBase::ProcessEvent(
    const AsyncSqlTableModelBase::Event aEvent,
    const bool aIsSupressLogging)
{
    emit ReportIsBusy(IsBusy());

    const auto command = mState->mEventProcessing.ProcessEvent(aEvent);

    QString traceMsgCommon;

    if (!aIsSupressLogging)
    {
        traceMsgCommon = QString("%1: event: '%2', command: '%3', OpId: %4")
            .arg("AsyncSqlTableModelBase::ProcessEvent")
            .arg(AsyncSqlTableEventProcessing::ToString(aEvent).c_str())
            .arg(AsyncSqlTableEventProcessing::ToString(command).c_str())
            .arg(mOperationId);
    }

    switch (command)
    {
    case AsyncSqlTableModelBase::Command::SendUserActionRequest:
    {
        mState->mBackEndState.PendingUpdate = ++mOperationId;

        const auto rowRequest = GetRowRequest();
        const auto selectionRequest = GetSelectionRequest();
        const auto hintsRequest = GetHintsRequest();

        emit ProcessEasyActionAsync(mOperationId, rowRequest, selectionRequest, hintsRequest);

        mState->mLastUpdateRequestTime = QDateTime::currentDateTime();

        if (!aIsSupressLogging)
        {
            mAsyncTableTracer.Trace(QString("%1, range: %2, range vis: %3")
                .arg(traceMsgCommon)
                .arg(ToString(rowRequest.RowWindow))
                .arg(ToString(rowRequest.RowWindowVisible)));
        }

        mState->mPendingUserEasyActionState = State::PendingUserEasyActionState {};

        break;
    }
    case AsyncSqlTableModelBase::Command::SendUpdateRequest:
    {
        mState->mBackEndState.WritingNewItemsBuffer.swap(
            mState->mPendingDataIncomingState.PendingNewItemsBuffer);

        mState->mBackEndState.PendingUpdate = ++mOperationId;

        emit ProcessHeavyActionAsync(
            mOperationId,
            mState->mBackEndState.WritingNewItemsBuffer,
            mState->mPendingDataIncomingState.PendingLoadStatus,
            mState->mPendingUserHeavyActionState.mPendingSorting,
            mState->mPendingUserHeavyActionState.mPendingFilter,
            mState->mPendingUserHeavyActionState.mReportSelected,
            mSuspendUpdates);

        mState->mPendingDataIncomingState = State::PendingDataIncomingState {};
        mState->mPendingUserHeavyActionState = State::PendingUserHeavyActionState {};

        mState->mLastUpdateRequestTime = QDateTime::currentDateTime();

        if (!aIsSupressLogging)
        {
            mAsyncTableTracer.Trace(QString("%1, size: %2")
                .arg(traceMsgCommon)
                .arg(mState->mBackEndState.WritingNewItemsBuffer->size()));
        }

        break;
    }
    case AsyncSqlTableModelBase::Command::SendUserQuery:
    {
        mState->mBackEndState.IsPendingUserQuery = true;

        const auto& query = mState->mPendingUserQueryActionState.Query;
        Q_ASSERT(query);

        emit PerformUserQueryAsync(query->first, query->second);

        mState->mPendingUserQueryActionState.Query = std::nullopt;

        break;
    }
    case AsyncSqlTableModelBase::Command::DoNothing:
        if (!aIsSupressLogging)
        {
            mAsyncTableTracer.Trace(QString("%1").arg(traceMsgCommon));
        }
        break;
    default:
        break;
    }

    if (mState->mEventProcessing.IsBusy())
    {
        TryEngageCursor();
    }
    else
    {
        TryRestoreCursor();
    }

    return command;
}

RowRequest AsyncSqlTableModelBase::GetRowRequest() const
{
    return mState->mPendingUserEasyActionState.RequestedRows.value_or(
        RowRequest{ 
            mViewData.Rows,
            mViewData.RowsVisible,
            mViewData.Version });
}

SelectionRequest AsyncSqlTableModelBase::GetSelectionRequest() const
{
    return mState->mPendingUserEasyActionState.RequestedSelection.value_or(
        SelectionRequest{ 
            mViewData.Selection,
            mViewData.CurrentRow,
            mViewData.Version });
}

HintsRequest AsyncSqlTableModelBase::GetHintsRequest() const
{
    return mState->mPendingUserEasyActionState.RequestedHints.value_or(
        HintsRequest{ 
            mViewData.ScrollHint,
            mViewData.TopRowHint,
            mViewData.BottomRowHint });
}

void AsyncSqlTableModelBase::UpdateBufferLogSize()
{
    auto& buf = mState->mPendingDataIncomingState.PendingNewItemsBuffer;
    const auto maxSize = (std::max)(buf->size(), mState->mPreviousLoggedSize);
    const auto minSize = (std::min)(buf->size(), mState->mPreviousLoggedSize);
    if ((maxSize - minSize) > mSkipLoggingSize)
    {
        mState->mPreviousLoggedSize = buf->size();
    }
}

TNewItemsBuffer& AsyncSqlTableModelBase::GetNewItemsBuffer()
{
    return *mState->mPendingDataIncomingState.PendingNewItemsBuffer;
}
