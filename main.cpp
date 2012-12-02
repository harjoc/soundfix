#include <QtGui/QApplication>
#include "soundfix.h"

#pragma warning(disable:4996)

int main(int argc, char *argv[])
{
    freopen("data/soundfix.log", "a+b", stdout);
    setbuf(stdout, NULL);

    QApplication a(argc, argv);
    SoundFix w;
    w.show();
    
    return a.exec();
}
