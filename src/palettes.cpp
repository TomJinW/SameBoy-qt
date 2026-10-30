#include "palettes.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtEndian>
#include <algorithm>
#include <cmath>

void UserPalette::recalculate()
{
    if (manual) { if(!distinctLcd) colors[4]=colors[3]; return; }
    const auto first=colors[0],last=colors[4],bias=QColor::fromHsvF(hue/360.0,1,1);
    const double power=1/std::pow(4,(brightness-128)/128.0);
    auto colorAt=[&](double position){position=std::pow(position,power);QColor result;
        auto channel=[&](double a,double b,double tint){return std::clamp(a+(b-a)*std::pow(position,1/std::pow(4,(tint*2-1)*strength/256.0)),0.0,1.0);};
        result.setRgbF(channel(first.redF(),last.redF(),bias.redF()),channel(first.greenF(),last.greenF(),bias.greenF()),channel(first.blueF(),last.blueF(),bias.blueF()));return result;};
    colors[1]=colorAt(distinctLcd?8/25.0:1/3.0);colors[2]=colorAt(distinctLcd?16/25.0:2/3.0);colors[3]=distinctLcd?colorAt(24/25.0):colors[4];
}
GB_palette_t UserPalette::corePalette() const {GB_palette_t result{};for(int i=0;i<5;++i)result.colors[i]={uint8_t(colors[i].red()),uint8_t(colors[i].green()),uint8_t(colors[i].blue())};return result;}
QByteArray UserPalette::exportSbp() const
{
    QByteArray bytes(32,'\0');bytes.replace(qsizetype(0),4,"LPBS",4);bytes[4]=char((manual?1:0)|(distinctLcd?2:0));
    for(int i=0;i<5;++i){bytes[5+i*3]=char(colors[i].red());bytes[6+i*3]=char(colors[i].green());bytes[7+i*3]=char(colors[i].blue());}
    qToLittleEndian<qint32>((brightness-128)*0x800000,reinterpret_cast<uchar *>(bytes.data()+20));
    qToLittleEndian<quint32>(quint32(std::llround(hue*(2147483648.0/360))),reinterpret_cast<uchar *>(bytes.data()+24));
    qToLittleEndian<quint32>(quint32(std::llround(strength*(2147483648.0/256))),reinterpret_cast<uchar *>(bytes.data()+28));
    if(manual)bytes.resize(distinctLcd?20:17);return bytes;
}
bool UserPalette::importSbp(const QByteArray &bytes,const QString &name,UserPalette &result)
{
    if(bytes.size()<5 || bytes.left(4)!="LPBS")return false;
    const auto flags=uint8_t(bytes[4]);const bool manual=flags&1,distinct=flags&2;
    if(flags&~3 || bytes.size()<(manual?(distinct?20:17):32))return false;
    UserPalette p;p.name=name;p.manual=manual;p.distinctLcd=distinct;
    for(int i=0;i<(distinct||!manual?5:4);++i)p.colors[i]=QColor(uint8_t(bytes[5+i*3]),uint8_t(bytes[6+i*3]),uint8_t(bytes[7+i*3]));
    if(!distinct)p.colors[4]=p.colors[3];
    if(!manual){p.brightness=std::clamp(int(std::lround(qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(bytes.constData()+20))/8388608.0+128)),0,256);
        p.hue=std::clamp(int(std::lround(qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()+24))*(360/2147483648.0))),0,360);
        p.strength=std::clamp(int(std::lround(qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(bytes.constData()+28))*(256/2147483648.0))),0,256);}
    result=std::move(p);return true;
}
QVector<UserPalette> defaultPalettes(){QVector<UserPalette> result;for(const auto &name:QDir(":/palettes").entryList({"*.sbp"},QDir::Files,QDir::Name)){
    QFile file(":/palettes/"+name);UserPalette palette;if(file.open(QIODevice::ReadOnly)&&UserPalette::importSbp(file.readAll(),QFileInfo(name).completeBaseName(),palette))result.append(palette);}return result;}
QByteArray serializePalettes(const QVector<UserPalette> &palettes){QJsonArray array;for(const auto &p:palettes){QJsonObject item{{"name",p.name},{"sbp",QString::fromLatin1(p.exportSbp().toBase64())}};array.append(item);}return QJsonDocument(array).toJson(QJsonDocument::Compact);}
bool deserializePalettes(const QByteArray &bytes,QVector<UserPalette> &palettes){QJsonParseError error;const auto document=QJsonDocument::fromJson(bytes,&error);if(error.error!=QJsonParseError::NoError||!document.isArray())return false;
    QVector<UserPalette> result;for(const auto &item:document.array()){const auto object=item.toObject();UserPalette p;if(!UserPalette::importSbp(QByteArray::fromBase64(object.value("sbp").toString().toLatin1()),object.value("name").toString(),p)||p.name.trimmed().isEmpty())return false;result.append(p);}palettes=std::move(result);return true;}
