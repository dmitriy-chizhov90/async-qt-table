#pragma once

#include "TableModels/SqlTableModelLockWrapper.h"

#include "TextFilter/TextFilter.h"

#include <QString>
#include <QTimer>
#include <QItemSelection>

#include <functional>
#include <chrono>
#include <set>

enum class SqlFieldType
{
    String,                        ///< Любая String
    StringCollateNoCase,           ///< Вспомогательный String, нужен для хранения и работы с case insensitive строками
    Integer,                       ///< Любой Int
    Double,                        ///< Любое Double значение. DEPRECATED: Старайтесь использовать Real
    DateTime,                      ///< Дата и время
};

inline bool IsBoolType(SqlFieldType aType)
{
    return aType == SqlFieldType::Bool || aType == SqlFieldType::IsFok;
}

struct SqlFieldDescription
{
    SqlFieldDescription() = default;
    SqlFieldDescription(const char* aName, SqlFieldType aType)
        : mName(aName)
        , mType(aType)
    {}

    const char* mName;
    SqlFieldType mType{SqlFieldType::String};
};

struct FieldTypeName
{
    SqlFieldType mFieldType;
    const char* mFieldTypeName;
};

struct CopyTableResults
{
    QString TableName;
    QString Filter;
    QString FieldsWithTypes;
    int SortColumn;
    int SortOrder;
    int Size;
};

struct SqlQueryUtils
{
    using TSortSequence = std::vector<int>;
    using TSortOrder = std::vector<TSortSequence>;

    static constexpr char TablePlaceholder[] = "$table$";
    static constexpr char FieldsPlaceholder[] = "$fields$";
    static constexpr char FilterPlaceholder[] = "$filter$";

    static constexpr int SQLITE_MAX_VARIABLE_NUMBER = 999; // from sqlite3.c: maximum number of SQL variables
    static constexpr int RowWindowOffset = 50;

    static const FieldTypeName FIELD_TYPE_NAMES[];

    static TSortOrder NormalizeSortOrder(const TSortOrder& aSortOrder);

    static QString GetFieldTypeName(const SqlFieldType& aType);
    static QString MakeUniqueName(const QString& aName);

    static QString GetCommonFilter(const TextFilter& aFilter, const QString& aColumnExpression = "common");

    static QString GetInstrumentFilter(const QString& aInstrument, bool aIsWithStandardContractSize = false);

    static QString EscapeField(const QString& aField);

    static void BindFieldsToQuery(const QVariant* fields, int size, SqlQueryLockWrapper& query, int& pos);

    static void SpecifyQueryString(
        QString &outSql,
        const QString& aTableName,
        const QString& aFields,
        const QString& aFilter);

    static QString GetFullTextSearchValue(const QVariantList& aValues, const std::set<int>& aIndexes);
    static QString GetFullTextSearchValue(const QVariantList& aValues);

    static QVariantList Record2Fields(const QSqlRecord& aRecord);

private:
    static std::atomic_int64_t mInstanceCounter;
};

class TimerOperation : public QObject
{
    static constexpr std::chrono::milliseconds MinTimeout { 200 };
    static constexpr std::chrono::milliseconds MaxTimeout { 2000 };

    QTimer* mTimer;
    std::chrono::milliseconds mTimeout { MinTimeout };
    bool mIsNeeded = false;
    bool mIsAllowed = false;
    qint64 mOperationStartTime = 0;

public:
    TimerOperation(std::function<void()> aHandler, QObject* aParent);

    bool CheckAndPrepare();
    void ProcessComplete();

    void Request();

    bool Allow(bool aIsAllowed);
    bool IsAllowed() const;

private:
    void TryStartTimer();
};

struct RowRange
{
    int Top = -1;
    int Bottom = -1;

    bool IsValid() const;
    bool Intersects(const RowRange& aOther) const;
    bool Contains(int aRow) const;
    bool Contains(const RowRange& aOther, int aPadding) const;
    QVector<RowRange> Union(const RowRange& aOther) const;
    int Distance(int aRow) const;
    int Distance() const;// Bottom - Top
    int NearestRow(int aRow) const;
    RowRange Expand(int aOffset) const;
    int Count() const;
    RowRange ScrollTo(int aRow) const;
    RowRange ScrollToWithCorrection(
        const int aRow,
        const bool aTopIsFullVisible,
        const bool aBottomIsFullVisible,
        const bool aBottomIsEnd) const;

    bool operator ==(const RowRange& aOther) const;
    bool operator !=(const RowRange& aOther) const;

    static QVector<RowRange> ItemSelection2Ranges(const QItemSelection& aSelection);
};

QString ToString(const RowRange& aRange);
QString ToString(const QVector<RowRange>& aRanges);
