#pragma once

#include <QMutex>

#include <QtSql/QSqlError>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlRecord>
#include <QtSql/QSqlTableModel>

#include "database/DatabaseConnections.h"

class SqlTableModelLockWrapper;

class SqlQueryLockWrapper : public DataBaseMutex, private QSqlQuery
{
public:
    SqlQueryLockWrapper(const DataBaseMutex& aOther);
    SqlQueryLockWrapper(std::weak_ptr<DataBaseConnections> aConnections);

    bool exec();
    bool exec(const QString& aQuery);
    bool execBatch(QSqlQuery::BatchExecutionMode mode = ValuesAsRows);
    QSqlRecord record() const;
    void addBindValue(const QVariant &val, QSql::ParamType paramType = QSql::In);
    void clear();

    bool prepare(const QString& aSql);
    void bindValue(int pos, const QVariant& val, QSql::ParamType type = QSql::In);
    void bindValue(const QString& placeholder, const QVariant& val, QSql::ParamType type = QSql::In);

    bool first();
    bool last();
    bool next();
    bool previous();

    bool seek(int index, bool relative = false);


    QVariant value(int i) const;

    int numRowsAffected() const;

    QSqlError lastError() const;

    int size() const;
    bool isSelect() const;
    bool isActive() const;

    bool isForwardOnly() const;
    void setForwardOnly(bool aIsForwardOnly);

    void PerformSql(QString aSql, const QVariantList& aParams);

    friend class SqlTableModelLockWrapper;
};

class SqlTableModelLockWrapper : public DataBaseMutex, public QSqlTableModel
{
public:
    explicit SqlTableModelLockWrapper(std::weak_ptr<DataBaseConnections> aConnections, QObject *parent = Q_NULLPTR);
    explicit SqlTableModelLockWrapper(const DataBaseMutex& aOther, QObject *parent = Q_NULLPTR);

public:
    void fetchMore(const QModelIndex &parent = QModelIndex()) override;
    bool canFetchMore(const QModelIndex &parent = QModelIndex()) const override;

    void sort(int column, Qt::SortOrder order) override;
    QVariant data(const QModelIndex &idx, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QSqlRecord record(int row) const;

    virtual bool insertRowIntoTable(const QSqlRecord& aValues) override;
    virtual void setFilter(const QString &filter) override;
    virtual void setTable(const QString &tableName) override;
    virtual void setSort(int column, Qt::SortOrder order) override;
    virtual bool select() override;
    void clear() override;

    SqlQueryLockWrapper GetQuery() const;

};


