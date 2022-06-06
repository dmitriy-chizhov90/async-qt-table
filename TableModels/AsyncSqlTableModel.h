#pragma once

// Boost
#include <boost/range/adaptor/transformed.hpp>

#include "TableModels/AsyncSqlTableModelBase.h"
#include "TableModels/ConfTableModel.h"
#include "IDataController.h"
#include "TableStringFormatter.hpp"

/**
 * @class AsyncDataSqlTableModel
 * @brief Базовый класс для асинхронных SQL - моделей.
 */
class AsyncColumnSqlTableModel : public AsyncSqlTableModelBase, public ColumnManager
{
    Q_OBJECT
public:
    /**
     * @param aTableName - имя таблицы в Sql БД.
     * @param aDefaultSortOrder - порядок сортировки по-умолчанию. Можно задавать несколько колонок.
     * Используется в качестве дополнительного при пользовательской сортировке.
     * @param aPrimaryKey - имя колонки в БД, которая используется в качестве первичного ключа.
     * @param aSqlPrimaryKeyIndex - номер колонки, которая используется в качестве первичного ключа.
     * @param aDataIdIndex - номер колонки, которая в которой хранится уникальный идентификатор типа данных.
     * Может отличаться от первичного ключа.
     * @param aCommonIndexRanges - диапазон колонок для полнотекстового поиска.
     * @param aUseFileStorage - БД может быть на диске или в памяти.
     * @param aHandler - объект плагина для кэша.
     */
    AsyncColumnSqlTableModel(
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
        const QPointer<TableOperationHandlerBase>& aHandler = nullptr);
    virtual ~AsyncColumnSqlTableModel() override;

    virtual int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    virtual void sort(int aColumn, Qt::SortOrder aOrder) override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    QFont GetFont(int aColumn, const QSet<int>& aBooleanColumns) const;
    QSet<QString> GetBooleanFields() const;
    QModelIndex indexExtended(int row, int column, const QModelIndex &parent = QModelIndex()) const;

    virtual QString GetIdByIndex(const QModelIndex& aIndex) const = 0;

    /// Для отображения колонки могут использоваться несколько альтернативных колонок хранилища.
    /// Пользовательская модель может переключаться между несколькими колонками.
    virtual int AlterColumn(int aColumn) const;

    QModelIndex GetIndexById(const QString& aId) const;
    bool GetIndexById(const QString& aId, QModelIndex& outIndex) const;

protected:
    QFont mFont;
    QFont mCheckFont;
    QSet<int> mBooleanColumns;
    const int mFieldListSize;
    const int mDataIdIndex;
    int mSortColumn {0};
    Qt::SortOrder mSortOrder {Qt::AscendingOrder};
    IDataController *mDataControllerInt;
    TCommonIndexesRanges mCommonIndexes;

    void PrepareToSubscribe();
    QVariant GetValue(const QVariantList &aRow, int aColumn) const;
    QVariant GetDataByRowAndColumn(int aRow, int aColumn) const;
    std::optional<QModelIndex> Match(int aColumn, const QVariant &aValue) const;
    std::optional<QVariant> GetInvalidData(const QModelIndex& index, int role) const;

    virtual QStringList GetFilters() const;
public:
    void InitFilter();
};

/**
 * @class AsyncDataSqlTableModel
 * @brief Отвечает за наполнение хранилища данными.
 */
template <typename THandlerParam>
class AsyncDataSqlTableModel : public AsyncColumnSqlTableModel
{
public:
    using THandler = THandlerParam;
    using TData = typename THandler::TData;
    using TEnum = typename THandler::TEnum;
    using TIncomingDecoratedData = typename THandler::TIncomingDecoratedData;
    using TIncomingData = typename THandler::TIncomingData;

    template <typename ...TArgs>
    AsyncDataSqlTableModel(IDataController* aDataController, TArgs&& ...aArgs)
        : AsyncColumnSqlTableModel(aDataController, std::forward<TArgs>(aArgs)...)
    {}

    AsyncDataSqlTableModel(
        IDataController* aDataController,
        bool aUseFileStorage,
        QObject* aParent,
        QPointer<TableOperationHandlerBase> aHandler = nullptr)
        : AsyncColumnSqlTableModel(
            aDataController,
            THandler::TableName,
            THandler::FIELD_LIST.data(),
            THandler::FIELD_LIST.size(),
            ConvertArrayOfArraysOfEnumToTSortOrder(THandler::DefaultSortOrder),
            THandler::DefaultSortDirection,
            THandler::PrimaryKey,
            ColumnManager::Enum2Index(THandler::SqlPrimaryIndex),
            ColumnManager::Enum2Index(THandler::DataIdIndex),
            ConvertArrayOfTuplesOfEnumsToTCommonIndexesRanges(THandler::CommonIndexRanges),
            aUseFileStorage,
            aParent,
            aHandler)
    {
        FindBooleanColumns();
    }

    using AsyncColumnSqlTableModel::AsyncColumnSqlTableModel;

    virtual QVariant data(const QModelIndex& aIndex, int aRole = Qt::DisplayRole) const override
    {
        if (auto invalidData = GetInvalidData(aIndex, aRole))
        {
            return *invalidData;
        }
        const auto column = TEnum(GetColumnEnumByOffset(aIndex.column()));
        const auto columnInt = static_cast<int>(column);
        const auto& cell = ExtractRowData(aIndex.row(), column);
        if (aRole == Qt::DisplayRole
            || aRole == Qt::EditRole
            || aRole == Qt::TextAlignmentRole
            || aRole == Qt::BackgroundRole
            || aRole == Qt::ToolTipRole
            || aRole == Qt::ForegroundRole
            || aRole == Qt::UserRole
            || aRole == Qt::FontRole)
        {
            auto it = THandler::FieldDesc.find(columnInt);
            if (it == THandler::FieldDesc.end())
            {
                return {};
            }

            auto findDependencyByFieldName = [&](const std::string& aName, QVariantMap& outDependencies)
            {
                auto itType = std::find_if(
                    THandler::FieldDesc.begin(), THandler::FieldDesc.end(),
                    [aName](const auto& aItem)
                    {
                        return aItem.second.SqlFieldName == aName;
                    });
                if (itType == THandler::FieldDesc.end())
                {
                    return;
                }
                outDependencies.insert(QString::fromStdString(aName), ExtractRowData(aIndex.row(), itType->first));
            };

            QVariantMap dependencies;
            if (it->second.Type == SqlFieldType::Price
                || it->second.Type == SqlFieldType::HighlightedPrice
                || it->second.Type == SqlFieldType::CenteredPrice)
            {
                findDependencyByFieldName("side", dependencies);
                findDependencyByFieldName("price_precision", dependencies);
            }
            if (it->second.Type == SqlFieldType::Amount)
            {
                findDependencyByFieldName("amount_precision", dependencies);
            }
            if (it->second.Type == SqlFieldType::HighlightedStatusMessage)
            {
                findDependencyByFieldName("quote_status", dependencies);
            }

            return TableStringFormatter::ToString(
                it->second.Type, cell, TableStringFormatterArgs(
                    aRole,
                    columnInt,
                    mBooleanColumns,
                    dependencies));
        }
        return {};
    }

    virtual ModelHeaders GetModelHeaders() const override
    {
        ModelHeaders headers;

        for (const auto& index : mCommonIndexes)
        {
            for (const auto& column : index.second)
            {
                auto it = THandler::FieldDesc.find(StartColumnEnumValue + column);
                if (it != THandler::FieldDesc.end())
                {
                    auto desc = it->second;
                    bool isAllowed = false;

                    auto isClient = mDataControllerInt->GetNetWrapper()->GetCurrentUserRole() == uiobj::UserRole::RoleClient;
                    auto showOwnerFirm = mDataControllerInt->GetPermissions()->ValidatePermission(uiobj::GrantType::ShowOwnerFirm);
                    auto showLocations = mDataControllerInt->GetPermissions()->ValidatePermission(uiobj::PrincipalFeatureType::MultipleLocations);
                    auto hasRealSources = !mDataControllerInt->GetNetWrapper()->GetBaseContext()->GetSources().GetRealSources().empty();
                    auto tradeAccountsCount = mDataControllerInt->GetNetBaseContext()->GetTradeAccounts().size();

                    switch (desc.Type)
                    {
                    case SqlFieldType::Account:
                    case SqlFieldType::Target:
                    case SqlFieldType::Firm:
                    case SqlFieldType::Login:
                    case SqlFieldType::IsFok:
                        isAllowed = !isClient;
                        break;
                    case SqlFieldType::OwnerFirm:
                        isAllowed = !isClient && showOwnerFirm;
                        break;
                    case SqlFieldType::Location:
                        isAllowed = showLocations;
                        break;
                    case SqlFieldType::Source:
                        isAllowed = !isClient && hasRealSources;
                        break;
                    case SqlFieldType::AccountLink:
                        isAllowed = isClient && tradeAccountsCount > 1;
                        break;
                    default:
                        isAllowed = true;
                        break;
                    }

                    if (isAllowed)
                    {
                        headers.push_back(ModelHeader { desc.Column, desc.HeaderName });
                    }
                }
            }
        }

        return headers;
    }

    QPointer<THandler> GetBackendDataHelper()
    {
        return dynamic_cast<THandler*>(mBackendHandler.data());
    }

    template<typename TColumns>
    QVariant ExtractRowData(int aRecord, TColumns aCol) const
    {
        auto rowPtr = mViewData.GetRow(aRecord);
        if (rowPtr)
        {
            return THandler::ExtractRowData(*rowPtr, aCol);
        }
        return {};
    }

    TData GetRowData(const QVariantList& aRow) const
    {
        return THandler::GetRowData(aRow, GetRowDataAdditionalParameters());
    }

    TData GetRowData(int aRow) const
    {
        auto rowPtr = mViewData.GetRow(aRow);
        if (rowPtr)
        {
            return GetRowData(*rowPtr);
        }
        return TData {};
    }

    QString GetIdByIndex(const QModelIndex& aIndex) const override
    {
        return GetIdByRow(aIndex.row());
    }

    QString GetIdByRow(int aRow) const
    {
        auto rowData = GetRowData(aRow);
        return rowData.GetId();
    }

    template <typename TIncomingRawDataPack>
    void Merge(const TIncomingRawDataPack& aData, const std::vector<int64_t>& aDeletedIds = std::vector<int64_t> {})
    {
        if constexpr (std::is_same<TIncomingDecoratedData, TIncomingData>::value)
        {
            MergeDecorated(aData, aDeletedIds);
        }
        else
        {
            using boost::adaptors::transformed;

            const auto range =
                aData | transformed([&](const auto& aItem)
                {
                    return DecorateData(aItem);
                });

            const std::vector<TIncomingDecoratedData> result(std::begin(range), std::end(range));

            MergeDecorated(result, aDeletedIds);
        }
    }

    template <typename TContainer>
    static TCommonIndexesRanges ConvertArrayOfTuplesOfEnumsToTCommonIndexesRanges(const TContainer& aContainer)
    {
        using namespace boost::adaptors;

        const auto range = aContainer | transformed(
            [](const auto& aTuple) -> std::pair<int, std::set<int>>
            {
                return
                {
                    ColumnManager::Enum2Index(std::get<0>(aTuple)),
                    ColumnManager::GetIndexesByRange(
                        ColumnManager::Enum2Index(std::get<1>(aTuple)),
                        ColumnManager::Enum2Index(std::get<2>(aTuple)))
                };
            });
        return TCommonIndexesRanges(std::cbegin(range), std::cend(range));
    }

    template <typename TContainer>
    static SqlQueryUtils::TSortOrder ConvertArrayOfArraysOfEnumToTSortOrder(const TContainer& aContainer)
    {
        using namespace boost::adaptors;

        auto VectorToVector = [](const auto& aContainer)
        {
            const auto range = aContainer | transformed([](const auto& aValue){ return int(aValue); });
            return std::vector(std::cbegin(range), std::cend(range));
        };

        const auto range = aContainer | transformed([&](const auto& aContainerNested){ return VectorToVector(aContainerNested); });
        return SqlQueryUtils::TSortOrder(std::cbegin(range), std::cend(range));
    }

protected:
    /**
    * Преобразование входных данных в данные для локальной БД.
    * @return Возвращает QVariantList, если данные должны быть отправлены в БД, иначе - nullopt.
    * Если QVariantList состоит из одного элемента, 
    * то будет отправлен запрос на удаление строки с данным id.
    */
    virtual std::optional<QVariantList> AddPendingData(const TIncomingDecoratedData& aData)
    {
        return THandler::MakeRow(aData);
    }
    /**
    * Преобразование входных данных перед добавлением в модель.
    */
    virtual TIncomingDecoratedData DecorateData(const TIncomingData& aData) const
    {
        return TIncomingDecoratedData { aData };
    }
    /**
    * Дополнительные параметры для функции THandler::GetRowData
    */
    virtual QVariantList GetRowDataAdditionalParameters() const
    {
        return QVariantList {};
    }

    /**
    * Отправка входных данных в кэш.
    * @param aData - набор данных, полученный от сервера.
    * В зависимости от реализации AddPendingData каждый элемент может привести
    * к добавлению, изменению, удалению одной записи в кэше.
    * Кроме того, данные могут быть отфильтрованы.
    * @param aDeletedIds - набор данных для удаления из кэша.
    */
    template <typename TIncomingDataPack>
    void MergeDecorated(
        const TIncomingDataPack& aData,
        const std::vector<int64_t>& aDeletedIds = std::vector<int64_t> {})
    {
        if (!mError.isEmpty())
        {
            return;
        }
        
        for (const auto& data : aData)
        {
            if (const auto& item = AddPendingData(data))
            {
                GetNewItemsBuffer().push_back(*item);
            }
        }
        for (const auto id : aDeletedIds)
        {
            GetNewItemsBuffer().push_back(QVariantList() << QVariant { static_cast<qlonglong>(id) });
        }

        AsyncSqlTableModelBase::ProcessNewChunkCompleted();
    }

private:
    void FindBooleanColumns()
    {
        mBooleanColumns.clear();
        for (const auto& desc: THandler::FieldDesc)
        {
            if (IsBoolType(desc.second.Type))
            {
                mBooleanColumns.insert(desc.second.Column);
            }
        }
    }
};

/**
 * @class AsyncSqlTableModel
 * @brief Предоставляет базовую реализацию для интерфейса ConfTableModel.
 */
template <typename _THandler>
class AsyncSqlTableModel
    : public AsyncDataSqlTableModel<_THandler>
    , public ConfTableModel<typename _THandler::TConfig>
{
public:
    using THandler = _THandler;
private:
    using TBase = AsyncDataSqlTableModel<THandler>;
    using TConfBase = ConfTableModel<typename THandler::TConfig>;
public:
    template <typename ...TArgs>
    AsyncSqlTableModel(IDataController* aDataController, TArgs&& ...aArgs)
        : TBase(aDataController, std::forward<TArgs>(aArgs)...)
        , TConfBase(QString("model.%1.conf").arg(this->GetTableName()).toStdString().c_str(), aDataController)
    {
        QObject::connect(
            this, &AsyncSqlTableModelBase::ViewWindowValuesChanged,
            TConfBase::GetTableConfigurationUpdater(), &TableConfigurationUpdater::OnPrepareFinished);
    }

    /// Обработка ответа на подписку от сервера.
    void ReceiveCommandReply(const NTPro::Ecn::Common::MsgProcessor::CommandReply& aAck)
    {
        if(aAck.Result == NTPro::Ecn::Common::MsgProcessor::ResultCode::Ok)
        {
            TConfBase::mTracer.Debug("Subscription successfully fulfilled:" + QString::fromStdString(aAck.Message));

            #ifdef DEBUG_DATA
               GenerateDebugData();
            #endif
        }
        else
        {
            TConfBase::mTracer.Warning("Subscription result:" + QString::fromStdString(aAck.Message));
            TConfBase::GetTableConfigurationUpdater()->OnRejected();
            ClearLocalData();
        }
    }

protected:

    /// Подготовка к обновлению запроса в кэше
    virtual void RefreshCustomData() {}
    /// Можно создавать фейковый данные для тестирования
    virtual void GenerateDebugData() {}
    /// Пережиток от старой модели
    virtual void ClearStaleRecords(const QStringList&) override final
    {
        throw std::logic_error("Not implemented");
    }

    void ClearLocalData() override final
    {
        TBase::Clear();
    }

    void RefreshLocalData() override final
    {
        /// Сейчас используется только в таблице сделок для обновления mShowAggregated, хотя возможно,
        /// вместо этого можно было бы исользовать mConfig.ShowAggregated.
        RefreshCustomData();
        /// При обновлении конфига изменяем фильтр данных
        TBase::InitFilter();
        /// При изменении режима отображения колонка, по которой сортируем, может начать ссылаться на другое поле в хранилище.
        /// Так происходит в таблице сделок в связи с режимом агрегирования.
        /// Поэтому вызываем пересортировку.
        TBase::sort(TBase::mSortColumn, TBase::mSortOrder);
    }
};
