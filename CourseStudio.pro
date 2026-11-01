QT       += core gui xml openglwidgets webenginewidgets webchannel concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets network multimedia multimediawidgets

CONFIG += c++20
QMAKE_CXXFLAGS += /std:c++17 /await
RC_ICONS = E:/live/version26/UI/QSS/icon/48.ico

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    CourseStudio.cpp

HEADERS += \
    CourseStudio.h

include($$PWD/UI/UI.pri)
include($$PWD/VFX/VFX.pri)
include($$PWD/Net/Net.pri)
include($$PWD/Core/Core.pri)
include($$PWD/Model/Model.pri)
include($$PWD/Record/Record.pri)



INCLUDEPATH += $(FFMPEG_7_HOME)/include
INCLUDEPATH += $(OPENCV_HOME)/include
LIBS += -lDbgHelp -lpsapi
LIBS += -L$(FFMPEG_7_HOME)/lib -lavcodec -lavdevice -lavformat -lavutil -lswresample -lswscale
LIBS += -L$(OPENCV_HOME)/x64/vc17/lib -lopencv_world4130

# 2. 强制 Release 模式也生成调试符号 (非常重要！)
QMAKE_CXXFLAGS_RELEASE += /Zi
QMAKE_LFLAGS_RELEASE += /DEBUG /OPT:REF /OPT:ICF

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    res.qrc


win32-msvc* {
    # 开启生成调试信息
    QMAKE_CXXFLAGS_RELEASE += /Zi
    # 告知链接器生成 .pdb 文件
    QMAKE_LFLAGS_RELEASE += /DEBUG /OPT:REF /OPT:ICF
}
