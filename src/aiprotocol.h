#pragma once
#include <QCryptographicHash>
#include <QDir>
inline QString defaultAiEndpoint(){return "sameboy-qt-ai-"+QString::fromLatin1(QCryptographicHash::hash(QDir::homePath().toUtf8(),QCryptographicHash::Sha256).toHex().left(20));}
