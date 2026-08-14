#include "OperatorAccount.h"

QString operatorRoleToString(OperatorRole role)
{
    switch (role) {
    case OperatorRole::Supervisor: return QStringLiteral("supervisor");
    case OperatorRole::Operator: return QStringLiteral("operator");
    }
    return QStringLiteral("operator");
}

OperatorRole operatorRoleFromString(const QString &value)
{
    if (value == QStringLiteral("supervisor"))
        return OperatorRole::Supervisor;
    return OperatorRole::Operator;
}
