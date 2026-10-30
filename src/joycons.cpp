#include "controllers.h"
#include <QSet>
#include <QByteArray>
#include <algorithm>
#include <QDataStream>
#include <QScopedValueRollback>
#include <QIODevice>
namespace {
constexpr quint32 bit(int n){return 1u<<n;}
bool paired(const AppSettings&s,const QString&id){return s.joyConPairs.contains(id)||std::any_of(s.joyConPairs.cbegin(),s.joyConPairs.cend(),[&](const QString&right){return right==id;});}
PadSnapshot single(PadSnapshot p,const AppSettings&s){
    p.sources={p.id};
    if(p.joyCon!=JoyConType::Left&&p.joyCon!=JoyConType::Right)return p;
    p.horizontal=s.joyConGrips.value(p.id,s.joyConHorizontal?1:0)==1;
    const bool left=p.joyCon==JoyConType::Left;
    const float x=left?p.x:p.rightX,y=left?p.y:p.rightY;
    p.x=x;p.y=y;
    // A single right Joy-Con still supplies the primary stick used by the emulator.
    if(!left&&(p.buttons&bit(8))){p.buttons&=~bit(8);p.buttons|=bit(7);p.rightX=p.rightY=0;}
    if(!p.horizontal)return p;
    const auto buttons=p.buttons;
    const quint32 faces=bit(0)|bit(1)|bit(2)|bit(3);
    const quint32 dpad=bit(11)|bit(12)|bit(13)|bit(14);
    p.buttons&=~(faces|dpad|bit(9)|bit(10));
    auto move=[&](int from,int to){if(buttons&bit(from))p.buttons|=bit(to);};
    if(left){
        p.x=y;p.y=-x;
        move(13,0);move(12,1);move(11,2);move(14,3);
        // SL / SR become the two shoulder buttons when held sideways.
        move(19,9);move(17,10);
        p.buttons&=~bit(4);move(4,6); // Minus acts as Start, without also pressing Select.
        p.motionX=p.motionUp;
    }else{
        p.x=-y;p.y=x;
        move(1,0);move(3,1);move(0,2);move(2,3);
        move(16,9);move(18,10);
        p.motionX=-p.motionUp;
    }
    // Preserve the outer shoulder as an assignable paddle in horizontal mode.
    if(buttons&bit(left?9:10))p.buttons|=bit(left?17:16);
    return p;
}
}
QString joyConPairId(const QString&left,const QString&right){
    auto encode=[](const QString&id){return QString::fromLatin1(id.toUtf8().toBase64(QByteArray::Base64UrlEncoding|QByteArray::OmitTrailingEquals));};
    return "joycon-pair:"+encode(left)+":"+encode(right);
}
QVector<PadSnapshot> arrangeJoyCons(const QVector<PadSnapshot>&raw,const AppSettings&s){
    QVector<PadSnapshot> result;QSet<QString> consumed;
    for(const auto &p:raw){
        if(consumed.contains(p.id))continue;
        QString left,right;
        if(p.joyCon==JoyConType::Left&&s.joyConPairs.contains(p.id)){left=p.id;right=s.joyConPairs.value(left);}
        else if(p.joyCon==JoyConType::Right){for(auto it=s.joyConPairs.begin();it!=s.joyConPairs.end();++it)if(it.value()==p.id){left=it.key();right=p.id;break;}}
        if(left.isEmpty()){result.append(single(p,s));consumed.insert(p.id);continue;}
        const PadSnapshot *l=nullptr,*r=nullptr;
        for(const auto&candidate:raw){if(candidate.id==left&&candidate.joyCon==JoyConType::Left)l=&candidate;if(candidate.id==right&&candidate.joyCon==JoyConType::Right)r=&candidate;}
        PadSnapshot pair;pair.id=joyConPairId(left,right);pair.sources={left,right};pair.joyCon=JoyConType::Pair;
        pair.name=QObject::tr("Joy-Con (L + R)");if(!l||!r)pair.name+=QObject::tr(" — %1 disconnected").arg(!l?"L":"R");
        if(l){pair.buttons|=l->buttons;pair.x=l->x;pair.y=l->y;pair.slow=l->slow;}
        if(r){pair.buttons|=r->buttons;pair.rightX=r->rightX;pair.rightY=r->rightY;pair.turbo=r->turbo;
            // Like Cocoa, ignore the left sensor when two Joy-Cons are combined.
            pair.sensor=r->sensor;pair.motionX=r->motionX;pair.motionY=r->motionY;pair.motionUp=r->motionUp;}
        consumed.insert(left);consumed.insert(right);result.append(pair);
    }
    return result;
}
bool pairJoyCons(AppSettings&s,const PadSnapshot&a,const PadSnapshot&b){
    const PadSnapshot *l=&a,*r=&b;if(a.joyCon==JoyConType::Right)std::swap(l,r);
    if(l->joyCon!=JoyConType::Left||r->joyCon!=JoyConType::Right||l->id.isEmpty()||r->id.isEmpty()||l->id==r->id||paired(s,l->id)||paired(s,r->id))return false;
    s.joyConPairs.insert(l->id,r->id);s.joyConSingles.removeAll(l->id);s.joyConSingles.removeAll(r->id);
    const auto id=joyConPairId(l->id,r->id);bool assigned=false;
    for(auto&device:s.controllers)if(device==l->id||device==r->id){device=assigned?QString("none"):id;assigned=true;}
    return true;
}
bool separateJoyCons(AppSettings&s,const QString&id){
    for(auto it=s.joyConPairs.begin();it!=s.joyConPairs.end();++it)if(joyConPairId(it.key(),it.value())==id){
        const auto left=it.key(),right=it.value();s.joyConPairs.erase(it);
        if(!s.joyConSingles.contains(left))s.joyConSingles.append(left);if(!s.joyConSingles.contains(right))s.joyConSingles.append(right);
        for(auto&device:s.controllers)if(device==id)device=left;
        return true;
    }return false;
}
bool autoPairJoyCons(const QVector<PadSnapshot>&raw,AppSettings&s){
    if(!s.joyConAutoPair)return false;bool changed=false;
    for(const auto&l:raw){if(l.joyCon!=JoyConType::Left||paired(s,l.id)||s.joyConSingles.contains(l.id))continue;
        for(const auto&r:raw)if(r.joyCon==JoyConType::Right&&!paired(s,r.id)&&!s.joyConSingles.contains(r.id)){
            changed=pairJoyCons(s,l,r)||changed;break;
        }
    }return changed;
}

bool JoyConAutoPairing::update(const QVector<PadSnapshot>&raw,const AppSettings&current,const std::function<bool(const AppSettings&)>&apply){
    if(applying_)return false;
    bool left=false,right=false;
    if(current.joyConAutoPair)for(const auto&pad:raw){
        if(paired(current,pad.id)||current.joyConSingles.contains(pad.id))continue;
        left|=pad.joyCon==JoyConType::Left;right|=pad.joyCon==JoyConType::Right;
    }
    if(!left||!right){failedInput_.clear();return false;}
    auto candidate=current;
    if(!autoPairJoyCons(raw,candidate)){failedInput_.clear();return false;}
    QMap<QString,int> devices;for(const auto&pad:raw)devices.insert(pad.id,int(pad.joyCon));
    auto singles=current.joyConSingles;singles.sort();
    QByteArray input;QDataStream stream(&input,QIODevice::WriteOnly);stream<<devices<<current.joyConPairs<<singles;
    for(const auto&controller:current.controllers)stream<<controller;
    if(input==failedInput_)return false;
    QScopedValueRollback<bool> guard(applying_,true);
    if(!apply(candidate)){failedInput_=input;return false;}
    failedInput_.clear();return true;
}
