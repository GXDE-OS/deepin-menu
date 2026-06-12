#-------------------------------------------------
#
# Project created by QtCreator 2014-08-14T14:55:09
#
#-------------------------------------------------

QT       += core gui dbus

greaterThan(QT_MINOR_VERSION, 7): QT += gui-private
else: QT += platformsupport-private

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = deepin-menu
TEMPLATE = app

CONFIG += c++11 link_pkgconfig
PKGCONFIG += dtkwidget

# Wayland下使用layer-shell-qt5
INCLUDEPATH += /usr/include/LayerShellQt
LIBS += -lLayerShellQtInterface

# Wayland走org_kde_kwin_blur作为模糊协议
PKGCONFIG += wayland-client

SOURCES += src/main.cpp \
    src/ddesktopmenu.cpp \
    src/utils.cpp \
    src/dmenucontent.cpp \
    src/dbus_manager_adaptor.cpp \
    src/dbus_menu_adaptor.cpp \
    src/manager_object.cpp \
    src/menu_object.cpp \
    src/ddockmenu.cpp \
    src/dmenuapplication.cpp \
    src/dabstractmenu.cpp \
    src/waylandhelper.cpp \
    src/kde-blur-protocol.c \
    src/treeland-personalization-protocol.c \
    src/treeland-dde-shell-protocol.c

HEADERS  += \
    src/ddesktopmenu.h \
    src/utils.h \
    src/dmenucontent.h \
    src/dbus_manager_adaptor.h \
    src/dbus_menu_adaptor.h \
    src/manager_object.h \
    src/menu_object.h \
    src/ddockmenu.h \
    src/dmenuapplication.h \
    src/dabstractmenu.h \
    src/waylandhelper.h \
    src/kde-blur-client-protocol.h \
    src/treeland-personalization-client-protocol.h \
    src/treeland-dde-shell-client-protocol.h

dbus.path = /usr/share/dbus-1/services
dbus.files = data/com.deepin.menu.service

RESOURCES += \
    images.qrc

target.path = /usr/bin
INSTALLS += target dbus
