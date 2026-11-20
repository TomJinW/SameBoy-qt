#include "../src/filterrenderer.h"
#include "../src/palettes.h"
#include <QGuiApplication>
#include <QCryptographicHash>
#include <QSet>
#include <QDir>
#include <QFile>
#include <rhi/qshader.h>
#include <cstdio>
#include <cmath>

int main(int argc,char **argv){QGuiApplication app(argc,argv);QImage frame(32,32,QImage::Format_RGB32);
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)frame.setPixel(x,y,(x==y||x==31-y)?qRgb(255,255,255):qRgb(x*8,y*8,(x^y)*8));
    frame.setPixel(0,0,qRgb(255,0,0));frame.setPixel(31,31,qRgb(0,0,255));
    for(const auto&name:QDir(":/filters").entryList({"*.qsb"},QDir::Files)){
        QFile file(":/filters/"+name);if(!file.open(QIODevice::ReadOnly))return 1;
        const auto shader=QShader::fromSerialized(file.readAll());bool metal=false,desktop=false,gl31=false;
        for(const auto&key:shader.availableShaders()){
            if(key.source()==QShader::MslShader)metal=true;
            if(key.source()==QShader::GlslShader){
                const auto version=key.sourceVersion();
                if(version.version()==150&&!version.flags().testFlag(QShaderVersion::GlslEs))desktop=true;
                if(version.version()==140&&!version.flags().testFlag(QShaderVersion::GlslEs))gl31=true;
            }
        }
        if(!metal){std::fprintf(stderr,"Missing Metal shader: %s\n",qPrintable(name));return 1;}
        if(!desktop||!gl31){std::fprintf(stderr,"Missing desktop GLSL 140/150 shader: %s\n",qPrintable(name));return 1;}
    }
    QSet<QByteArray> hashes;
    for(int filter=0;filter<16;++filter){QString error;const auto image=renderFilteredImage(frame,{},QSize(160,160),filter,0,&error);
        if(image.size()!=QSize(160,160)){std::fprintf(stderr,"Filter %d: %s\n",filter,qPrintable(error));return 1;}
        if(filter==0&&(image.pixelColor(2,2).red()<250||image.pixelColor(157,157).blue()<250))return 1;
        hashes.insert(QCryptographicHash::hash(QByteArray::fromRawData(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256));
        if(argc>1&&!image.save(QString::fromLocal8Bit(argv[1])+QString("/filter-%1.png").arg(filter)))return 1;
    }
    if(hashes.size()<8)return 1;
    QImage white(8,8,QImage::Format_RGB32),black(8,8,QImage::Format_RGB32);white.fill(Qt::white);black.fill(Qt::black);
    const auto simple=renderFilteredImage(white,black,QSize(8,8),0,1);
    const auto even=renderFilteredImage(white,black,QSize(8,8),0,2),odd=renderFilteredImage(white,black,QSize(8,8),0,3);
    if(simple.isNull()||even.isNull()||odd.isNull()||std::abs(qRed(simple.pixel(0,0))-186)>2||qRed(even.pixel(0,0))<=qRed(even.pixel(0,1))||even.pixel(0,0)!=odd.pixel(0,1))return 1;
    const auto palettes=defaultPalettes();if(palettes.size()!=14)return 1;
    for(const auto &p:palettes){UserPalette copy;if(!UserPalette::importSbp(p.exportSbp(),p.name,copy)||copy.colors!=p.colors||copy.manual!=p.manual||copy.distinctLcd!=p.distinctLcd)return 1;}
    UserPalette generated;generated.colors[0]=Qt::black;generated.colors[4]=Qt::white;generated.distinctLcd=false;generated.strength=0;generated.recalculate();
    if(generated.colors[1]!=QColor(85,85,85)||generated.colors[2]!=QColor(170,170,170)||generated.colors[3]!=generated.colors[4])return 1;
    std::printf("16 original GPU filters (%lld distinct outputs), gamma-correct simple/accurate blending, 14 palettes and SBP round trips passed.\n",static_cast<long long>(hashes.size()));return 0;
}
