QT += widgets core

CONFIG += c++17

SOURCES += main.cpp md.cpp selftest.cpp

HEADERS += md.hpp evaluator.hpp selftest.hpp

LIBS += -ldwmapi -lpsapi

win32: {
    # 链接 MinGW 运行时库（如果需要）
    LIBS += -lole32 -lshell32
}

# 适应高DPI
DEFINES += QT_WIDGETS_HIGHDPI

# 如果使用 MSVC，可能需要额外设置
# QMAKE_CXXFLAGS += /std:c++17

TRANSLATIONS += \
    QT_zh_CN.ts