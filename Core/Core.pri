INCLUDEPATH += $$PWD

# 开启 MSVC 标准兼容模式 (对 WGC 至关重要)
QMAKE_CXXFLAGS += /permissive- /Zc:twoPhase-

HEADERS += \
    $$PWD/CameraCapture.h \
    $$PWD/DX11Context.h \
    $$PWD/DX11FilterBase.h \
    $$PWD/DX11Filters.h \
    $$PWD/FilterPipeline.h \
    $$PWD/HighPrecisionThread.h \
    $$PWD/IVideoSource.h \
    $$PWD/ImageFileSource.h \
    $$PWD/Logger.h \
    $$PWD/SceneCompositor.h \
    $$PWD/TexturePool.h \
    $$PWD/VideoFileSource.h \
    $$PWD/VideoUtils.h \
    $$PWD/WGCCapture.h \
    $$PWD/WasapiCapturer.h

SOURCES += \
    $$PWD/CameraCapture.cpp \
    $$PWD/DX11Context.cpp \
    $$PWD/ImageFileSource.cpp \
    $$PWD/SceneCompositor.cpp \
    $$PWD/TexturePool.cpp \
    $$PWD/VideoFileSource.cpp \
    $$PWD/VideoUtils.cpp \
    $$PWD/WGCCapture.cpp \
    $$PWD/WasapiCapturer.cpp

LIBS += -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -luser32 -lwindowsapp -lmmdevapi -lole32
