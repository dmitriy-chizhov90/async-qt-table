#include "SqlTableModelLockWrapper.h"

#include <QUuid>

#include <stdexcept>

SqlQueryLockWrapper::SqlQueryLockWrapper(const DataBaseMutex& aOther)
    : DataBaseMutex { aOther }
    , QSqlQuery(GetDatabase())
{
}

SqlQueryLockWrapper::SqlQueryLockWrapper(std::weak_ptr<DataBaseConnections> aConnections)
    : DataBaseMutex { aConnections }
    , QSqlQuery(GetDatabase())
{
}

bool SqlQueryLockWrapper::exec()
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::exec();
}

bool SqlQueryLockWrapper::exec(const QString& aQuery)
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::exec(aQuery);
}

bool SqlQueryLockWrapper::execBatch(QSqlQuery::BatchExecutionMode mode)
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::execBatch(mode);
}

QSqlRecord SqlQueryLockWrapper::record() const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::record();
}

void SqlQueryLockWrapper::addBindValue(const QVariant& val, QSql::ParamType paramType)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlQuery::addBindValue(val, paramType);
}

void SqlQueryLockWrapper::clear()
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlQuery::clear();
}

bool SqlQueryLockWrapper::prepare(const QString& aSql)
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::prepare(aSql);
}

QSqlError SqlQueryLockWrapper::lastError() const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::lastError();
}

int SqlQueryLockWrapper::size() const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::size();
}

bool SqlQueryLockWrapper::isSelect() const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::isSelect();
}

bool SqlQueryLockWrapper::isActive() const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::isActive();
}

bool SqlQueryLockWrapper::isForwardOnly() const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::isForwardOnly();
}

void SqlQueryLockWrapper::setForwardOnly(bool aIsForwardOnly)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlQuery::setForwardOnly(aIsForwardOnly);
}

void SqlQueryLockWrapper::PerformSql(QString aSql, const QVariantList& aParams)
{
    if (!prepare(aSql))
    {
        throw std::runtime_error("PerformSql prepare: " + lastError().text().toStdString());
    }
    for (int i = 0; i < aParams.size(); ++i)
    {
        bindValue(i, aParams[i]);
    }
    if (!exec())
    {
        throw std::runtime_error("PerformSql: " + lastError().text().toStdString());
    }
}

void SqlQueryLockWrapper::bindValue(int pos, const QVariant& val, QSql::ParamType type)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlQuery::bindValue(pos, val, type);
}

void SqlQueryLockWrapper::bindValue(const QString& placeholder, const QVariant& val, QSql::ParamType type)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlQuery::bindValue(placeholder, val, type);
}

bool SqlQueryLockWrapper::first()
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::first();
}

bool SqlQueryLockWrapper::last()
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::last();
}

bool SqlQueryLockWrapper::next()
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::next();
}

bool SqlQueryLockWrapper::previous()
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::previous();
}

bool SqlQueryLockWrapper::seek(int index, bool relative)
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::seek(index, relative);
}

QVariant SqlQueryLockWrapper::value(int i) const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::value(i);
}

int SqlQueryLockWrapper::numRowsAffected() const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlQuery::numRowsAffected();
}


SqlTableModelLockWrapper::SqlTableModelLockWrapper(std::weak_ptr<DataBaseConnections> aConnections, QObject *parent)
    : DataBaseMutex { aConnections }
    , QSqlTableModel { parent, GetDatabase() }
{
}

SqlTableModelLockWrapper::SqlTableModelLockWrapper(const DataBaseMutex& aOther, QObject *parent)
    : DataBaseMutex { aOther }
    , QSqlTableModel { parent, GetDatabase() }
{
}

void SqlTableModelLockWrapper::fetchMore(const QModelIndex &parent)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlTableModel::fetchMore(parent);
}

bool SqlTableModelLockWrapper::canFetchMore(const QModelIndex &parent) const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::canFetchMore(parent);
}

void SqlTableModelLockWrapper::sort(int column, Qt::SortOrder order)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlTableModel::sort(column, order);
}

QVariant SqlTableModelLockWrapper::data(const QModelIndex &idx, int role) const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::data(idx, role);
}

QModelIndex SqlTableModelLockWrapper::index(int row, int column, const QModelIndex& parent) const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::index(row, column, parent);
}

int SqlTableModelLockWrapper::rowCount(const QModelIndex& parent) const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::rowCount(parent);
}

QSqlRecord SqlTableModelLockWrapper::record(int row) const
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::record(row);
}

bool SqlTableModelLockWrapper::insertRowIntoTable(const QSqlRecord &aValues)
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::insertRowIntoTable(aValues);
}

void SqlTableModelLockWrapper::setFilter(const QString &filter)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlTableModel::setFilter(filter);
}

void SqlTableModelLockWrapper::setTable(const QString &tableName)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlTableModel::setTable(tableName);
}

void SqlTableModelLockWrapper::setSort(int column, Qt::SortOrder order)
{
    QMutexLocker locker(mDataBaseMutex.get());
    QSqlTableModel::setSort(column, order);
}

bool SqlTableModelLockWrapper::select()
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::select();
}

void SqlTableModelLockWrapper::clear()
{
    QMutexLocker locker(mDataBaseMutex.get());
    return QSqlTableModel::clear();
}

SqlQueryLockWrapper SqlTableModelLockWrapper::GetQuery() const
{
    return SqlQueryLockWrapper { *this };
}
