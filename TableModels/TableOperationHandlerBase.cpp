#include "TableOperationHandlerBase.h"

#include "SyncSqlCache.h"

#include "Tracer.h"

TableOperationHandlerBase::TableOperationHandlerBase(QObject* aParent)
    : QObject(aParent)
    , mTracer(GetTracer(modelTblAdmGen))
{}

void TableOperationHandlerBase::SetTableModel(SyncSqlCache* aModel)
{
    mModel = aModel;
}

void TableOperationHandlerBase::MakeExtraData(ViewWindowValues& /*outValues*/) {}

std::string TableOperationHandlerBase::GetLastError() const
{
    return mLastError;
}

bool TableOperationHandlerBase::AddPendingValue(const QVariantList& /*aValues*/)
{
    return true;
}

void TableOperationHandlerBase::DeletePendingValue(const QVariant& /*aId*/) {}

bool TableOperationHandlerBase::ProcessDataInserted() noexcept(false)
{
    return true;
}

bool TableOperationHandlerBase::IsInsertionNeeded() const
{
    return false;
}

void TableOperationHandlerBase::ProcessDataSelected() {}

void TableOperationHandlerBase::ProcessClear() {}
