#pragma once

#include <QString>

// An operator's role gates which console actions they can take -- currently
// only the demo/injection panel (Ctrl+Shift+D), since forcing synthetic risk
// events must stay out of reach during live monitoring.
enum class OperatorRole {
    Operator,
    Supervisor
};

QString operatorRoleToString(OperatorRole role);
OperatorRole operatorRoleFromString(const QString &value);

// Static description of an operator account as read from config/operators.json.
// pinHash is a salted SHA-256 digest (see AuthService::hashPin) -- the raw
// PIN is never stored.
struct OperatorAccount {
    QString operatorId;
    QString displayName;
    OperatorRole role = OperatorRole::Operator;
    QString pinHash;
};
