#pragma once

#include "TestTableModel.h"

#include <QTest>

class TableModelTest : public QObject
{
Q_OBJECT

private slots:
    void TestRemove ()
    {
        TestTableModel table;
        QVERIFY(table.rowCount(QModelIndex()) == 0);
        table.Add10();
        QVERIFY(table.rowCount(QModelIndex()) == 10);
        table.TestClearStaleRecords();
        QVERIFY(table.rowCount(QModelIndex()) == 2);
        table.clear();
        QVERIFY(table.rowCount(QModelIndex()) == 0);
    }
};

