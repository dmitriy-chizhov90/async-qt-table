#include "SqlCacheTable.h"

#include <stdexcept>

SqlCacheTable::SqlCacheTable(
    QSqlDatabase& aDatabase,
    const QString& aTableName,
    const SqlFieldDescription* aFieldList,
    size_t aFieldListSize,
    const QString& aPrimaryKey)
    : mDatabase(aDatabase)
    , mTableName(aTableName)
{
    InitFieldStrings(aFieldList, aFieldListSize, aPrimaryKey);
}

void SqlCacheTable::InitFieldStrings(
    const SqlFieldDescription* aFieldList,
    size_t aFieldListSize,
    const QString& aPrimaryKey)
{
    mFieldList.clear();
    QStringList fieldTypes;

    for (size_t i = 0; i < aFieldListSize; ++i)
    {
        const SqlFieldDescription& fieldDescription = aFieldList[i];
        mFieldList.append(fieldDescription.mName);
        QString fieldTypeName = QString("%1 %2")
            .arg(
                fieldDescription.mName,
                SqlQueryUtils::GetFieldTypeName(fieldDescription.mType));

        if (aPrimaryKey == fieldDescription.mName)
        {
            fieldTypeName += " PRIMARY KEY";
        }

        fieldTypes.append(fieldTypeName);
    }
    mFieldsWithTypes = fieldTypes.join(",");
    mFields = mFieldList.join(",");

    auto insertParametersList = CreateParameters(mFieldList.size());
    mInsertItemQuery = QString("INSERT OR REPLACE INTO %1 VALUES (%2)")
        .arg(mTableName, insertParametersList);
    mDeleteItemQuery = QString("DELETE FROM %1 WHERE id = ?;").arg(mTableName);
    mSelectItemQuery = QString("SELECT %1 FROM %2 WHERE id = ?")
        .arg(mFields)
        .arg(mTableName);
    mCreateTableQuery = QString("CREATE TABLE %1 (%2);")
        .arg(mTableName, mFieldsWithTypes);
    // We make delete instead of drop. From sqlite docs:
    // It is illegal to drop a table if any cursors are open on the
    // database. This is because in auto-vacuum mode the backend may
    // need to move another root-page to fill a gap left by the deleted
    // root page. If an open cursor was using this page a problem would
    // occur.
    mClearTableQuery = QString("DELETE FROM %1;").arg(mTableName);

    if (mFieldList.size() >= SqlQueryUtils::SQLITE_MAX_VARIABLE_NUMBER)
    {
        throw std::runtime_error(
            QString("%1: Fields count more than Max")
                .arg(Q_FUNC_INFO).toStdString());
    }
}

QString SqlCacheTable::CreateParameters(int aSize)
{
    QStringList parameterList;
    for (int i = 0; i < aSize; ++i)
    {
        parameterList.append("?");
    }
    return parameterList.join(",");
}

void SqlCacheTable::PerformSql(
    const QString& aSql,
    const QVariantList& aParams,
    const QString& aFilter,
    bool aIsForwardOnly)
{
    auto sql = aSql;
    SqlQueryUtils::SpecifyQueryString(sql, mTableName, mFields, aFilter);
    PerformSqlInternal(sql, aParams, aIsForwardOnly);
}

void SqlCacheTable::PerformAction(Action aAction, const QVariant& aItem)
{
    QString sql;
    /// Для select и delete считаем, что это Id
    
    auto params = (aAction == Action::Select || aAction == Action::Delete)
        ? (QVariantList {} << aItem)
        : aItem.toList();
    
    switch (aAction)
    {
    case Action::Create: sql = mCreateTableQuery; break;
    case Action::Clear: sql = mClearTableQuery; break;
    case Action::Select: sql = mSelectItemQuery; break;
    case Action::Delete: sql = mDeleteItemQuery; break;
    case Action::Insert: sql = mInsertItemQuery; break;
    default: break;
    }
    
    PerformSqlInternal(sql, params);
}

QSqlQuery& SqlCacheTable::GetLastQuery()
{
    return mLastQuery;
}

QString SqlCacheTable::GetLastError() const
{
    return mLastQuery.lastError().text();
}

const QString& SqlCacheTable::GetName() const
{
    return mTableName;
}

const QString& SqlCacheTable::GetColumnName(int aColumn) const
{
    return mFieldList[aColumn];
}

qlonglong SqlCacheTable::GetColumnCount() const
{
    return static_cast<qlonglong>(mFieldList.size());
}

qlonglong SqlCacheTable::GetRowCount()
{
    auto sql = QString("SELECT count(1) FROM %1").arg(mTableName);
    PerformSqlInternal(sql, {});

    if (!mLastQuery.next())
    {
        throw std::runtime_error("GetRowCount: query is empty");
    }

    const auto& record = mLastQuery.record();
    return record.value(0).toLongLong();
}

void SqlCacheTable::PerformSqlInternal(
    const QString& aSql,
    const QVariantList& aParams,
    bool aIsForwardOnly)
{
    mLastQuery = QSqlQuery { mDatabase };
    mLastQuery.setForwardOnly(aIsForwardOnly);
    if (!mLastQuery.prepare(aSql))
    {
        Throw();
    }
    for (int i = 0; i < aParams.size(); ++i)
    {
        mLastQuery.bindValue(i, aParams[i]);
    }
    if (!mLastQuery.exec())
    {
        Throw();
    }
}

void SqlCacheTable::Throw() noexcept(false)
{
    throw std::runtime_error(GetLastError().toStdString());
}



