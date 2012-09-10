#include <QtGui/QApplication>
#include "soundfix.h"

#pragma warning(disable:4996)
FILE *logFile=0;

void logToFile(QtMsgType , const char *msg)
{
    fputs(msg, logFile);
    fflush(logFile);
}

int main(int argc, char *argv[])
{
    logFile = fopen("soundfix.log", "wb");
    qInstallMsgHandler(logToFile);

    QApplication a(argc, argv);
    SoundFix w;
    w.show();
    
    return a.exec();
}
