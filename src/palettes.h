#pragma once
#include <Core/gb.h>
#include <QColor>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <array>

struct UserPalette {
    QString name;
    std::array<QColor,5> colors{Qt::black,QColor(85,85,85),QColor(170,170,170),Qt::white,Qt::white};
    bool manual = false, distinctLcd = true;
    int brightness = 128, hue = 0, strength = 128;
    void recalculate();
    GB_palette_t corePalette() const;
    QByteArray exportSbp() const;
    static bool importSbp(const QByteArray &bytes, const QString &name, UserPalette &result);
};
QVector<UserPalette> defaultPalettes();
QByteArray serializePalettes(const QVector<UserPalette> &palettes);
bool deserializePalettes(const QByteArray &bytes, QVector<UserPalette> &palettes);
