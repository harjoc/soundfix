#include "soundfix.h"
#include "ui_soundfix.h"

#include <QMessageBox>
#include <QDir>
#include <QFileDialog>
#include <QProgressDialog>
#include <QThread>
#include <QProcess>
#include <QtGlobal>
#include <QDebug>

//QMessageBox::information(this, "title", QDir::current().absolutePath(), QMessageBox::Ok);

SoundFix::SoundFix(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SoundFix)
{
    ui->setupUi(this);
}

SoundFix::~SoundFix()
{
    delete ui;
}

//%FFMPEG% -i %1 -f wav -ac 1 %1.wav

void SoundFix::on_browseBtn_clicked()
{
    recordingName = QFileDialog::getOpenFileName(this, "Open Video", QString());
    if (recordingName.isNull())
        return;

    ui->videoEdit->setText(QDir::toNativeSeparators(recordingName));

    identifyAudio();
}

class Thr : public QThread {
public: static void msleep(unsigned long msecs) { QThread::msleep(msecs); }
};

void SoundFix::identifyAudio()
{
    if (!extractAudio()) return;
}

/*    QProgressDialog progress("Analyzing audio track...", "Cancel", 0, 10, this);
    for (int i=0; i<=10; i++) {
        Thr::msleep(500);
        progress.setValue(i);
        QApplication::processEvents();
        if (progress.wasCanceled())
            break;
    }
}*/

bool SoundFix::extractAudio()
{
    QProcess ffmpeg;
    ffmpeg.start("tools/ffmpeg.exe", QStringList() << "-i" << recordingName);
    if (!ffmpeg.waitForFinished()) {
        QMessageBox::warning(this, "Video load error", "Cannot determine video duration",
                QMessageBox::Ok);
        return false;
    }



    return true;
}
