#include "TableModels/SqlQueryUtils.h"
#include "ColumnManager.h"

#include <QDateTime>

#include <stdexcept>

std::atomic_int64_t SqlQueryUtils::mInstanceCounter { 0 };

const FieldTypeName SqlQueryUtils::FIELD_TYPE_NAMES[] =
{
    { SqlFieldType::String, "TEXT" },
    { SqlFieldType::StringCollateNoCase, "TEXT COLLATE NOCASE" },
    { SqlFieldType::Integer, "INTEGER" },
    { SqlFieldType::Double, "REAL" },
    { SqlFieldType::DateTime, "TEXT" },
    { SqlFieldType::Bool, "INTEGER" },
};

SqlQueryUtils::TSortOrder SqlQueryUtils::NormalizeSortOrder(const TSortOrder& aSortOrder)
{
    TSortOrder result;
    for (const auto& sequence : aSortOrder)
    {
        result.push_back(std::vector<int> {});
        for (auto column : sequence)
        {
            result.back().push_back(ColumnManager::Enum2Index(column));
        }
    }
    return result;
}

QString SqlQueryUtils::GetFieldTypeName(const SqlFieldType& aType)
{
    for (auto& fieldTypeName : FIELD_TYPE_NAMES)
    {
        if (aType == fieldTypeName.mFieldType)
        {
            return fieldTypeName.mFieldTypeName;
        }
    }

    throw std::runtime_error("field type name not found");
}

QString SqlQueryUtils::MakeUniqueName(const QString& aName)
{
    int64_t num = mInstanceCounter.load();
    while (!mInstanceCounter.compare_exchange_strong(num, num + 1)) {}
    return QString("%1%2").arg(aName).arg(num + 1);
}

QString SqlQueryUtils::GetCommonFilter(const TextFilter& aFilter, const QString& aColumnExpression)
{
    QString res;

    if (aFilter.Mode.contains(FilterMode::WholeWords))
    {
        res = "%1 REGEXP '\\b%2\\b'";
    }
    else if (aFilter.Mode.contains(FilterMode::RegExp))
    {
        res = "%1 REGEXP '%2'";
    }
    else
    {
        res = "%1 GLOB '*%2*'";
    }

    QString column = !aFilter.Mode.contains(FilterMode::CaseSensitive)
            ? QString("LOWER(%1)").arg(aColumnExpression)
            : aColumnExpression;

    QString filter = !aFilter.Mode.contains(FilterMode::CaseSensitive)
            ? aFilter.Filter.toLower()
            : aFilter.Filter;

    res = res.arg(column, filter);

    return res;
}

QString SqlQueryUtils::GetInstrumentFilter(const QString& aInstrument, bool aIsWithStandardContractSize)
{
    QString filter;

    if (!aIsWithStandardContractSize)
    {
        const QString instrumentFilter = "instrument = %1";
        filter = instrumentFilter.arg(EscapeField(aInstrument));
    }
    else
    {
        const QString instrumentFilter = "(instrument = %1 OR instrument LIKE %2)";
        filter = instrumentFilter.arg(EscapeField(aInstrument), EscapeField(QString("% ") + aInstrument));
    }

    return filter;
}

QString SqlQueryUtils::EscapeField(const QString& aField)
{
    QString result = aField;
    result.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + result + QLatin1Char('\'');
}

void SqlQueryUtils::BindFieldsToQuery(const QVariant* fields, int size, SqlQueryLockWrapper& query, int& pos)
{
    auto bind = [&query, &pos](const QVariant& value) { query.bindValue(pos++, value); };
    for (int i = 0; i < size; ++i)
    {
        bind(fields[i]);
    }
}

void SqlQueryUtils::SpecifyQueryString(
    QString& outSql,
    const QString& aTableName,
    const QString& aFields,
    const QString& aFilter)
{
    outSql.replace(TablePlaceholder, aTableName);
    outSql.replace(FieldsPlaceholder, aFields);
    auto filter = aFilter.isEmpty() ? "TRUE" : aFilter;
    outSql.replace(FilterPlaceholder, filter);
}

QString SqlQueryUtils::GetFullTextSearchValue(const QVariantList& aValues, const std::set<int>& aIndexes)
{
    QString common;
    for (const auto i : aIndexes)
    {
        common.append(']');
        common.append(aValues[i].toString());
    }
    return common;
}

QString SqlQueryUtils::GetFullTextSearchValue(const QVariantList& aValues)
{
    std::set<int> indexes;
    for (int i = 0; i < static_cast<int>(aValues.size()); ++i)
    {
        indexes.insert(i);
    }
    return GetFullTextSearchValue(aValues, indexes);
}

QVariantList SqlQueryUtils::Record2Fields(const QSqlRecord& aRecord)
{
    QVariantList rowValues;
    for (int j = 0; j < aRecord.count(); ++j)
    {
        rowValues.push_back(aRecord.value(j));
    }
    return rowValues;
}

TimerOperation::TimerOperation(std::function<void()> aHandler, QObject* aParent)
: QObject(aParent)
, mTimer(new QTimer(aParent))
{
    mTimer->callOnTimeout(aHandler);
    mTimer->setSingleShot(true);
}

bool TimerOperation::CheckAndPrepare()
{
    if (mIsNeeded && mIsAllowed)
    {
        mOperationStartTime = QDateTime::currentDateTime().toMSecsSinceEpoch();
        return true;
    }
    else
    {
        mOperationStartTime = 0;
        return false;
    }
}

void TimerOperation::ProcessComplete()
{
    mTimeout = MinTimeout;
    if (mOperationStartTime)
    {
        auto diff = (QDateTime::currentDateTime().toMSecsSinceEpoch() - mOperationStartTime) * 2;
        if (diff > mTimeout.count())
        {
            mTimeout = qMin(
                std::chrono::milliseconds {diff},
                MaxTimeout);
        }
    }
    mOperationStartTime = 0;
    mIsNeeded = false;
    GetTracer("TimerOperation").Info(QString("ProcessComplete: %1").arg(mTimeout.count()));
}

void TimerOperation::Request()
{
    mIsNeeded = true;
    TryStartTimer();
}

bool TimerOperation::Allow(bool aIsAllowed)
{
    if (mIsAllowed == aIsAllowed)
    {
        return false;
    }
    mIsAllowed = aIsAllowed;
    if (aIsAllowed)
    {
        mTimeout = MinTimeout;
        TryStartTimer();
    }
    else
    {
        mTimer->stop();
    }

    return true;
}

bool TimerOperation::IsAllowed() const
{
    return mIsAllowed;
}

void TimerOperation::TryStartTimer()
{
    if (mIsAllowed && mIsNeeded)
    {
        mTimer->start(mTimeout);
    }
}

bool RowRange::IsValid() const
{
    return Top >= 0 && Bottom >= 0 && Bottom >= Top;
}

bool RowRange::Intersects(const RowRange &aOther) const
{
    return (Contains(aOther.Top)
        || Contains(aOther.Bottom)
        || aOther.Contains(Top)
        || aOther.Contains(Bottom));
}

bool RowRange::Contains(int aRow) const
{
    return (aRow >= Top && aRow <= Bottom);
}

bool RowRange::Contains(const RowRange& aOther, int aPadding) const
{
    Q_ASSERT(aOther.IsValid());

    if (!Contains(aOther.Top) || !Contains(aOther.Bottom))
    {
        return false;
    }

    if (aOther.Top - Top < aPadding)
    {
        return false;
    }

    if (Bottom - aOther.Bottom < aPadding)
    {
        return false;
    }

    return true;
}

QVector<RowRange> RowRange::Union(const RowRange &aOther) const
{
    QVector<RowRange> result;

    if (Intersects(aOther))
    {
        RowRange unitedRange;
        unitedRange.Top = qMin(Top, aOther.Top);
        unitedRange.Bottom = qMax(Bottom, aOther.Bottom);
        result.push_back(unitedRange);
    }
    else
    {
        const auto* minRange = this;
        const auto* maxRange = &aOther;
        if (aOther.Top < Top)
        {
            minRange = &aOther;
            maxRange = this;
        }
        result.push_back(*minRange);
        result.push_back(*maxRange);
    }
    return result;
}

int RowRange::Distance(int aRow) const
{
    if (!IsValid())
    {
        return std::numeric_limits<int>::max();
    }

    if (Contains(aRow))
    {
        return 0;
    }

    if (aRow < Top)
    {
        return Top - aRow;
    }

    if (aRow > Bottom)
    {
        return aRow - Bottom;
    }

    Q_ASSERT(false);
    return 0;
}

int RowRange::Distance() const
{
    if (!IsValid())
    {
        return 0;
    }

    return Bottom - Top;
}

int RowRange::NearestRow(int aRow) const
{
    if (!IsValid() || Contains(aRow))
    {
        return aRow;
    }

    if (aRow < Top)
    {
        return Top;
    }

    if (aRow > Bottom)
    {
        return Bottom;
    }

    Q_ASSERT(false);
    return aRow;
}

RowRange RowRange::Expand(int aOffset) const
{
    RowRange newRange;
    newRange.Top = qMax(0, Top - aOffset);
    newRange.Bottom = qMax(newRange.Top, Bottom + aOffset);

    return newRange;
}

int RowRange::Count() const
{
    if (!IsValid())
    {
        return 0;
    }

    return Bottom - Top + 1;
}

RowRange RowRange::ScrollTo(int aRow) const
{
    int dst = 0;

    if (!IsValid())
    {
        dst = 0;
    }
    else if (aRow < Top)
    {
        dst = aRow - Top;
    }
    else if (aRow > Bottom)
    {
        dst = aRow - Bottom;
    }

    return RowRange { Top + dst, Bottom + dst };
}

RowRange RowRange::ScrollToWithCorrection(
    const int aRow,
    const bool aTopIsFullVisible,
    const bool aBottomIsFullVisible,
    const bool aBottomIsEnd) const
{
    int dst = 0;

    if (aRow < Top)
    // Если строка вышла вверх из-за диапазона, то скроллируем на неё.
    // Т.к. всегда возможно выставить Top на целую строку, то корректировать больше нечего.
    {
        dst = aRow - Top;
    }
    else if (aRow > Bottom)
    // Если строка вышла вниз из-за диапазона, то скроллируем на неё.
    // Т.к. при таком скролле Bottom сохранит свойство aBottomIsFullVisible, то корректируем Bottom после.
    {
        dst = aRow - Bottom;
    }

    // промежуточный
    const RowRange tmpRange { Top + dst, Bottom + dst };

    int dstTop = 0;
    int dstBottom = 0;

    // Теперь:
    // либо корректируем Bottom с предыдущего скролла,
    // либо предыдущих скроллов не было и мы пришли на Bottom:
    // в обоих случаях сдвигаем диапазон вниз на 1, кроме случая, когда сдвигать некуда - 
    // тогда двигаем только Top (правильно сдвинется VerticalScrollBar).
    if ((aRow == tmpRange.Bottom) && !aBottomIsFullVisible)
    {
        dstTop = 1;
        dstBottom = aBottomIsEnd ? 0 : 1;
    }

    return RowRange { qMax(0, tmpRange.Top + dstTop), tmpRange.Bottom + dstBottom };
}

bool RowRange::operator ==(const RowRange& aOther) const
{
    return Top == aOther.Top
        && Bottom == aOther.Bottom;
}

bool RowRange::operator !=(const RowRange& aOther) const
{
    return !operator==(aOther);
}

QVector<RowRange> RowRange::ItemSelection2Ranges(const QItemSelection& aSelection)
{
    QVector<RowRange> result;
    std::transform(
        aSelection.begin(),
        aSelection.end(),
        std::back_inserter(result), [](const auto& aSelection)
    {
        return RowRange { aSelection.top(), aSelection.bottom() };
    });
    return result;
}

QString ToString(const RowRange& aRange)
{
    return QString("RowRange {%1; %2}").arg(aRange.Top).arg(aRange.Bottom);
}

QString ToString(const QVector<RowRange>& aRanges)
{
    QStringList rangeList;
    for (const auto& range : aRanges)
    {
        rangeList.append(ToString(range));
    }
    return rangeList.join(", ");
}
