QT += quick opengl
CONFIG += c++11
DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
        image_view.cpp \
        main.cpp \
        vr_render.cpp

RESOURCES += \
    Demo.qrc

# Additional import path used to resolve QML modules in Qt Creator's code model
QML_IMPORT_PATH =

# Additional import path used to resolve QML modules just for Qt Quick Designer
QML_DESIGNER_IMPORT_PATH =

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    image_view.h \
    vr_render.h

INCLUDEPATH += $$PWD/openvr/headers

win32 {
        LIBS += -L$$PWD/openvr/lib/win64/ \
                -lopenvr_api -lopengl32
    CONFIG(debug, debug|release) {
        DESTDIR = $$PWD/../debug/
    } else {
        DESTDIR = $$PWD/../release/
    }
    openvr.files += $$PWD/openvr/bin/win64/openvr_api.dll
    openvr.path = $$DESTDIR
    COPIES += openvr
}
