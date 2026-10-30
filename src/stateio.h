#pragma once
#include "boundedfile.h"
#include <Core/gb.h>
#include <QByteArray>
inline int loadStatePreservingOnFailure(GB_gameboy_t*g,const QByteArray&data){
    // Core/save_state.c reads some backing RAM before all sections validate.
    QByteArray previous(GB_get_save_state_size(g),0);
    GB_save_state_to_buffer(g,reinterpret_cast<uint8_t*>(previous.data()));
    const int error=GB_load_state_from_buffer(g,reinterpret_cast<const uint8_t*>(data.constData()),data.size());
    if(error)GB_load_state_from_buffer(g,reinterpret_cast<const uint8_t*>(previous.constData()),previous.size());
    return error;
}

#include <QFile>
inline QString readStateFile(QFile&file,QByteArray&data){
    if(!file.open(QIODevice::ReadOnly))return file.errorString();
    // Native/BESS states are small; leave room for older layouts and SGB data.
    if(file.size()<8||file.size()>16*1024*1024)return QStringLiteral("Invalid save state file size");
    if(!readBoundedFile(file,data,16*1024*1024,8))return QStringLiteral("Could not read the complete save state");
    return {};
}
