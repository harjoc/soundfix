#include "soundfix.h"
#include "ui_soundfix.h"

#include <QMessageBox>
#include <QDir>
#include <QFileDialog>
#include <QProgressDialog>
#include <QThread>
#include <QProcess>

//QMessageBox::information(this, "title", QDir::current().absolutePath(), QMessageBox::Ok);

SoundFix::SoundFix(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SoundFix)
{
    ui->setupUi(this);

    recordingName = "data/VIDEO0074.3gp";
    ui->videoEdit->setText(recordingName);
    identifyAudio();
}

SoundFix::~SoundFix()
{
    delete ui;
}

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

bool SoundFix::error(const QString &title, const QString &text)
{
    QMessageBox::warning(this, title, text, QMessageBox::Ok);
    return false;
}

#define SAMPLE_MSEC (12*1000)

bool SoundFix::extractAudio()
{
    printf("getting video info\n");

    QProcess ffmpegInfo;
    ffmpegInfo.start("tools/ffmpeg.exe", QStringList() << "-i" << recordingName);
    if (!ffmpegInfo.waitForFinished())
        return error("Video load error", "Cannot get video information.");

    QByteArray info = ffmpegInfo.readAllStandardError();

    // Duration: 00:00:21.38,
    QRegExp re("\n *Duration: ([0-9]+):([0-9]+):([0-9]+).([0-9]+)");
    if (re.indexIn(info) == -1) {
        printf("---\n%s---\n", info);
        return error("Video load error", "Cannot get video duration.");
    }

    int hours = re.cap(1).toInt();
    int mins  = re.cap(2).toInt();
    int secs  = re.cap(3).toInt();
    int hsecs = re.cap(4).toInt();

    int durationMsec = (hours*3600 + mins*60 + secs)*1000 + hsecs*10;
    printf("duration: %d\n", durationMsec);

    if (durationMsec < SAMPLE_MSEC)
        return error("Video is too short",
            QString("Video is too short (%1 seconds). At least %2 seconds are required.")
                     .arg(durationMsec/1000).arg(SAMPLE_MSEC/1000));

    int sampleOffset = 0;
    if (durationMsec > SAMPLE_MSEC)
        sampleOffset = (durationMsec - SAMPLE_MSEC) / 2;

    // ---

    printf("extracting sample from offset: %d\n", sampleOffset/1000);

    QFile("data/sample.wav").remove();
    QFile("data/sample.ogg").remove();
    QFile("data/sample.spx").remove();

    QProcess ffmpegWav;
    ffmpegWav.start("tools/ffmpeg.exe", QStringList() << "-i" << recordingName <<
            "-f" << "wav" <<
            "-ac" << "1" <<
            "-ar" << "8000" <<
            "-ss" << QString::number(sampleOffset/1000) <<
            "-t" << QString::number(SAMPLE_MSEC/1000) <<
            "data/sample.wav");
    if (!ffmpegWav.waitForFinished())
        return error("Audio load error", "Cannot extract audio sample.");

    QProcess ffmpegOgg;
    ffmpegOgg.start("tools/ffmpeg.exe", QStringList() << "-i" << "data/sample.wav" <<
            "-acodec" << "libspeex" <<
            "-cbr_quality" << "10" <<
            "-compression_level" << "10" <<
            "data/sample.ogg");
    if (!ffmpegOgg.waitForFinished())
        return error("Audio conversion error", "Cannot compress audio sample.");

    // ---

    printf("converting to raw speex\n");

    QFile ogg("data/sample.ogg");
    QFile spx("data/sample.spx");
    if (!ogg.open(QIODevice::ReadOnly))
        return error("Audio sample conversion error", "Cannot open sample.");
    if (!spx.open(QIODevice::WriteOnly))
        return error("Audio sample conversion error", "Cannot create sample.");

    bool headers=true;
    bool first=true;

    for (;;) {
        unsigned char ogghdr[27];
        unsigned char segtab[255];
        unsigned char seg[80];

        size_t ret = ogg.read((char*)ogghdr, sizeof(ogghdr));
        if (ret==0) break;

        if (ret != sizeof(ogghdr) || memcmp(ogghdr, "OggS", 4))
            return error("Audio sample conversion error", "Cannot read header.");

        unsigned char nsegs = ogghdr[26];
        if (nsegs==0)
            error("Audio sample conversion error", "Cannot read audio segments.");

        ret = ogg.read((char*)segtab, nsegs);
        if (ret != nsegs)
            return error("Audio sample conversion error", "Cannot read audio segment table.");

        if (nsegs > 1)
            headers = false;

        if (headers) {
            if (nsegs != 1)
                return error("Audio sample conversion error", "Unsupported audio format.");

            ret = ogg.read((char*)seg, segtab[0]);
            if (ret != segtab[0])
                error("Audio sample conversion error", "Error reading audio.");

            if (first) {
                 if (segtab[0] != 80)
                     error("Audio sample conversion error", "Unsupported audio format.");
                 spx.write((char*)seg, 80);
            }
        } else {
            for (int s=0; s<nsegs; s++) {
                if (segtab[s] > 62 || (s<nsegs-1 && segtab[s] != 62))
                    return error("Audio sample conversion error", "Unsupported audio frame.");

                ret = ogg.read((char*)seg, segtab[s]);
                if (ret != segtab[s])
                        return error("Audio sample conversion error", "Cannot read audio frame.");

                unsigned char lenbuf[2] = {0, segtab[s]};
                spx.write((char*)lenbuf, 2);
                spx.write((char*)seg, segtab[s]);
            }
        }

        first = false;
    }

    ogg.close();
    spx.close();

    return true;
}
