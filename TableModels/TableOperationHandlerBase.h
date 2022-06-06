#pragma once

#include "ColumnManager.h"

#include "TracerGuiWrapper.h"
#include "SqlQueryUtils.h"

#include "uiobjects/UiObjHelpers.h"
#include "uiobjects/admin/adm_uihelpers.h"
#include "uiobjects/Instrument.h"
#include "uiobjects/TableQuote.h"
#include "uiobjects/PriceLevel.h"
#include "UiValueDate.h"

#include <set>
#include <memory>
#include <vector>
#include <variant>
#include <QVariant>
#include <QSet>
#include <QDateTime>
#include <QtSql/QSqlRecord>

class SyncSqlCache;

struct ViewWindowValues;

struct FieldDescription
{
    int Column {};            ///< Идентификатор колонки. Соответствует enum-у колонок конкретной таблицы.
    std::string SqlFieldName; ///< Имя колонки в базе данных.
    SqlFieldType Type {};     ///< Тип колонки.
    QString HeaderName;       ///< Отображаемый заголовок колонки.
};

class TableOperationHandlerBase : public QObject
{
public:
    using TEnum = enum class _ {};
    inline static const std::map<int, FieldDescription> FieldDesc {};

public:
    TableOperationHandlerBase(QObject* aParent = nullptr);

    void SetTableModel(SyncSqlCache* aModel);

    virtual void MakeExtraData(ViewWindowValues& outValues);
    std::string GetLastError() const;
    virtual bool AddPendingValue(const QVariantList& aValues);
    virtual void DeletePendingValue(const QVariant& aId);
    /// Данный метод может использоваться для обновления записей в БД.
    /// Изменения будут выполнены в одной транзакции
    /// со стандартным добавлением записей.
    /// Внутри ProcessDataInserted должен вызываться метод PerformSqlUnsafe.
    /// В случае проблем с запросом метод выкинет исключение,
    /// которое будет использовано для отката транзакции.
    virtual bool ProcessDataInserted() noexcept(false);
    virtual bool IsInsertionNeeded() const;

    virtual void ProcessDataSelected();

    virtual void ProcessClear();

    ///
    /// Helpers
    ///
    template<typename TRecord, typename TColumns>
    static QVariant ExtractRowData(const TRecord& aRecord, TColumns aCol)
    {
        static_assert (std::is_same<TRecord, QSqlRecord>::value
            || std::is_same<TRecord, QVariantList>::value,
            "Unsupported record type");

        auto col = ColumnManager::Enum2Index(aCol);
        if (col < 0 || col >= aRecord.count())
        {
            return {};
        }
        return aRecord.value(col);
    }

    template<typename TRecord, typename T, typename TConvert, typename TColumns>
    static void ExtractData(const TRecord& aRecord, TColumns aCol, TConvert aTransform, T& aOut)
    {
        QVariant value = ExtractRowData(aRecord, aCol);
        if (!value.isNull())
        {
            aOut = aTransform(value);
        }
    }


    template<class T, template <class> class O>
    static typename std::enable_if_t<
        std::is_same_v<O<T>, std::optional<T>>
        || std::is_same_v<O<T>, std::optional<T>>, QVariant>
    TransformToVariant(const O<T>& aObj)
    {
        return aObj ? toVariant(*aObj) : QVariant {};
    }

    template<class T>
    static QVariant TransformToVariant(const T& aObj)
    {
        return toVariant(aObj);
    }

    template<typename T>
    static QVariant EncodeData(const T& aValue)
    {
        return TransformToVariant(aValue);
    }

    template<typename T, typename TColumns>
    static void SetData(QVariantList& outRecord, TColumns aCol, const T& aIn)
    {
        auto col = static_cast<int>(aCol) - static_cast<int>(StartColumnEnumValue);
        if (col < 0 || col >= outRecord.size())
        {
            return;
        }

        outRecord[col] = QVariant::fromValue(aIn);
    }

    template <typename TBackendInfo>
    static std::optional<TBackendInfo> GetSummary(const QVariant& aData)
    {
        if (aData.isNull() || !aData.canConvert<TBackendInfo>())
        {
            return std::nullopt;
        }
        return aData.value<TBackendInfo>();
    }

    template <typename T>
    static auto toVariant(const T& v) { return QVariant {v}; }
    static auto toVariant(bool v) { return QVariant {v}; }
    static auto toVariant(int64_t v) { return QVariant {static_cast<qlonglong>(v)}; }
    static auto toVariant(const UiReal& v) { return QVariant {static_cast<double>(v)}; }
    static auto toVariant(const QDate& v) { return QVariant { ::ToString(v) }; }
    static auto toVariant(const QDateTime& v) { return QVariant { ::ToString(v) }; }

    static auto toString(const QVariant& v) { return v.toString(); }   
    static bool toHasValue(const QVariant& v) { return !v.toString().isEmpty(); }

    static auto toDouble(const QVariant& v) { return v.toDouble(); }
    static auto toDateTime(const QVariant& v) { return v.toDateTime(); }
    static auto toDate(const QVariant& v) { return v.toDate(); }
    static auto toLongLong(const QVariant& v) { return v.toLongLong(); }
    static auto toInt(const QVariant& v) { return v.toInt(); }
    static auto toBool(const QVariant& v) { return v.toBool(); }
    static auto toQSetLongLong(const QVariant& v)
    {
        auto ids = v.toString().split(",", Qt::SkipEmptyParts);
        QSet<long long> result;
        std::for_each(ids.begin(), ids.end(), [&](auto& s) { result.insert(s.toLongLong()); });
        return result;
    }

protected:

    SyncSqlCache* mModel = nullptr;
    std::string mLastError;

    TracerGuiWrapper mTracer;
};

