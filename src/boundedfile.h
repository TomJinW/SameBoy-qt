#pragma once
#include <QFile>
#include <QByteArray>
#include <limits>
// Only publish a complete, size-checked snapshot; keep the previous output on failure.
inline bool readBoundedFile(QFile&file,QByteArray&data,qint64 maximum,qint64 minimum=0){
    if(minimum<0||maximum<minimum||maximum==std::numeric_limits<qint64>::max())return false;
    if(!file.isOpen()&&!file.open(QIODevice::ReadOnly))return false;
    if(file.isSequential()||(file.pos()!=0&&!file.seek(0)))return false;
    const auto expected=file.size();if(expected<minimum||expected>maximum)return false;
    auto candidate=file.read(expected+1);
    if(file.error()!=QFileDevice::NoError||candidate.size()!=expected||file.size()!=expected)return false;
    data.swap(candidate);return true;
}
