#-------------------------------------------------
#
# Project created by QtCreator 2012-09-09T14:24:09
#
#-------------------------------------------------

QT       += core gui network phonon

TARGET = SoundFix
TEMPLATE = app

SOURCES += main.cpp\
        soundfix.cpp \
    kiss_fft/kiss_fftr.c \
    kiss_fft/kiss_fft.c \
    wav.cpp \
    specpp.cpp

HEADERS  += soundfix.h \
    kiss_fft/kiss_fftr.h \
    kiss_fft/kiss_fft.h \
    kiss_fft/_kiss_fft_guts.h \
    wav.h \
    specpp.h

FORMS    += soundfix.ui

RC_FILE = soundfix.rc
