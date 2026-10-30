#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>
#include <QVector>
struct RestartWindow{QString rom;QByteArray geometry;};
inline QString encodeRestartSession(const QVector<RestartWindow>&windows){
 QJsonArray array;for(const auto&window:windows)array.append(QJsonObject{{"rom",window.rom},{"geometry",QString::fromLatin1(window.geometry.toBase64())}});
 return QString::fromLatin1(QJsonDocument(array).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding));
}
inline QVector<RestartWindow> decodeRestartSession(const QString&encoded){
 const auto document=QJsonDocument::fromJson(QByteArray::fromBase64(encoded.toLatin1(),QByteArray::Base64UrlEncoding));QVector<RestartWindow>windows;
 if(!document.isArray())return {};for(const auto&value:document.array()){const auto item=value.toObject();if(!item.value("rom").isString()||!item.value("geometry").isString())return {};windows.append({item.value("rom").toString(),QByteArray::fromBase64(item.value("geometry").toString().toLatin1())});}return windows;
}
