#pragma once

#include "SqlQueryUtils.h"

/// @class SqlCacheTable
/// @brief Выполняет Sql-запросы к таблице в БД.
/// Также позволяет создать Sql-таблицу.
/// Удаление таблицы не поддерживается. Вместо этого можно очистить таблицу
/// и затем использовать её заново.
/// Методы, выполняющие Sql-запросы выбрасывают std::runtime_error в случае ошибки.
class SqlCacheTable
{
public:
    enum class Action
    {
        Create,
        Clear,
            
        Select,
        Insert,
        Delete
    };
    
    SqlCacheTable(
        QSqlDatabase& aDatabase,
        const QString& aTableName,
        const SqlFieldDescription* aFieldList,
        size_t aFieldListSize,
        const QString& aPrimaryKey);

   void PerformSql(
        const QString& aSql,
        const QVariantList& aParams,
        const QString& aFilter,
        bool aIsForwardOnly = false) noexcept(false);
   void PerformAction(
       Action aAction,
       const QVariant& aItem = QVariantList {}) noexcept(false);

    QSqlQuery& GetLastQuery();
    QString GetLastError() const;
    const QString& GetName() const;
    const QString& GetColumnName(int aColumn) const;

    qlonglong GetColumnCount() const;
    qlonglong GetRowCount() noexcept(false);
    
private:
    QSqlDatabase& mDatabase;

    /// Входные параметры
    QString mTableName;
    QString mFields;
    QStringList mFieldList;
    QString mFieldsWithTypes;
    
    /// Запросы для выполнения стандартных действий
    QString mInsertItemQuery;
    QString mDeleteItemQuery;
    QString mSelectItemQuery;
    QString mCreateTableQuery;
    QString mClearTableQuery;

    /// Последний исполненный запрос
    QSqlQuery mLastQuery;

    void InitFieldStrings(
        const SqlFieldDescription* aFieldList,
        size_t aFieldListSize,
        const QString& aPrimaryKey);
    void PerformSqlInternal(
        const QString& aSql,
        const QVariantList& aParams,
        bool aIsForwardOnly = false) noexcept(false);
    [[ noreturn ]] void Throw() noexcept(false);

    static QString CreateParameters(int aSize);
};
