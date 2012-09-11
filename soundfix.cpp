#include "soundfix.h"
#include "ui_soundfix.h"

#include <QMessageBox>
#include <QDir>
#include <QFileDialog>
#include <QProgressDialog>
#include <QProcess>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
//QMessageBox::information(this, "title", QDir::current().absolutePath(), QMessageBox::Ok);

#pragma warning(disable:4482) // nonstandard extension: enum used in qualified name

SoundFix::SoundFix(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SoundFix),
    httpChartsMgr(0),
    httpSearchMgr(0)
{
    ui->setupUi(this);

    partners = "{\"installed\":[]}";

    recordingName = "data/kong.3gp";
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

    substep = 0;
    identifyAudio();
}

void SoundFix::error(const QString &title, const QString &text)
{
    QMessageBox::warning(this, title, text, QMessageBox::Ok);
}

enum {
    IDENTIFY_EXTRACT_AUDIO=0,
    IDENTIFY_GET_SESSION,
    IDENTIFY_POST_SAMPLE
};

void SoundFix::identifyAudio()
{
    substep = IDENTIFY_EXTRACT_AUDIO;
    continueIdentification();
}

void SoundFix::continueIdentification()
{
    switch (substep) {
        case IDENTIFY_EXTRACT_AUDIO: extractAudio(); return;
        case IDENTIFY_GET_SESSION: getSession(); return;
        case IDENTIFY_POST_SAMPLE: postSample(); return;
    }
}

#define SAMPLE_MSEC (12*1000)

void SoundFix::extractAudio()
{
    printf("getting video info\n");

    QProcess ffmpegInfo;
    ffmpegInfo.start("tools/ffmpeg.exe", QStringList() << "-i" << recordingName);
    if (!ffmpegInfo.waitForFinished())
        { error("Video load error", "Cannot get video information."); return; }

    QByteArray info = ffmpegInfo.readAllStandardError();

    // Duration: 00:00:21.38,
    QRegExp re("\n *Duration: ([0-9]+):([0-9]+):([0-9]+).([0-9]+)");
    if (re.indexIn(info) == -1) {
        printf("---\n%s---\n", info);
        error("Video load error", "Cannot get video duration.");
        return;
    }

    int hours = re.cap(1).toInt();
    int mins  = re.cap(2).toInt();
    int secs  = re.cap(3).toInt();
    int hsecs = re.cap(4).toInt();

    int durationMsec = (hours*3600 + mins*60 + secs)*1000 + hsecs*10;
    printf("duration: %d\n", durationMsec);

    if (durationMsec < SAMPLE_MSEC) {
        error("Video is too short",
            QString("Video is too short (%1 seconds). At least %2 seconds are required.")
                     .arg(durationMsec/1000).arg(SAMPLE_MSEC/1000));
        return;
    }

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
            "-ar" << "44100" <<
            "data/sample.wav");
    if (!ffmpegWav.waitForFinished())
        { error("Audio load error", "Cannot extract audio sample."); return; }

    QProcess ffmpegOgg;
    ffmpegOgg.start("tools/ffmpeg.exe", QStringList() << "-i" << "data/sample.wav" <<
            "-acodec" << "libspeex" <<
            "-ac" << "1" <<
            "-ar" << "8000" <<
            "-ss" << QString::number(sampleOffset/1000) <<
            "-t" << QString::number(SAMPLE_MSEC/1000) <<
            "-cbr_quality" << "10" <<
            "-compression_level" << "10" <<
            "data/sample.ogg");
    if (!ffmpegOgg.waitForFinished())
        { error("Audio conversion error", "Cannot compress audio sample."); return; }

    // ---

    printf("converting to raw speex\n");

    QFile ogg("data/sample.ogg");
    QFile spx("data/sample.spx");
    if (!ogg.open(QIODevice::ReadOnly))
        { error("Audio sample conversion error", "Cannot open sample."); return; }
    if (!spx.open(QIODevice::WriteOnly))
        { error("Audio sample conversion error", "Cannot create sample."); return; }

    bool headers=true;
    bool first=true;

    for (;;) {
        unsigned char ogghdr[27];
        unsigned char segtab[255];
        unsigned char seg[80];

        size_t ret = ogg.read((char*)ogghdr, sizeof(ogghdr));
        if (ret==0) break;

        if (ret != sizeof(ogghdr) || memcmp(ogghdr, "OggS", 4))
            { error("Audio sample conversion error", "Cannot read header."); return; }

        unsigned char nsegs = ogghdr[26];
        if (nsegs==0)
            { error("Audio sample conversion error", "Cannot read audio segments."); return; }

        ret = ogg.read((char*)segtab, nsegs);
        if (ret != nsegs)
            { error("Audio sample conversion error", "Cannot read audio segment table."); return; }

        if (nsegs > 1)
            headers = false;

        if (headers) {
            if (nsegs != 1)
                { error("Audio sample conversion error", "Unsupported audio format."); return; }

            ret = ogg.read((char*)seg, segtab[0]);
            if (ret != segtab[0])
                { error("Audio sample conversion error", "Error reading audio."); return; }

            if (first) {
                unsigned char speex_header_bin[] =
                    "\x53\x70\x65\x65\x78\x20\x20\x20\x73\x70\x65\x65\x78\x2d\x31\x2e\x32\x62\x65\x74"
                    "\x61\x33\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x50\x00\x00\x00\x40\x1f\x00\x00"
                    "\x00\x00\x00\x00\x04\x00\x00\x00\x01\x00\x00\x00\xff\xff\xff\xff\xa0\x00\x00\x00"
                    "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00";

                 if (segtab[0] != 80)
                    { error("Audio sample conversion error", "Unsupported audio format."); return; }
                 spx.write((char*)speex_header_bin, 80);
            }
        } else {
            for (int s=0; s<nsegs; s++) {
                if (segtab[s] > 62 || (s<nsegs-1 && segtab[s] != 62))
                    { error("Audio sample conversion error", "Unsupported audio frame."); return; }

                ret = ogg.read((char*)seg, segtab[s]);
                if (ret != segtab[s])
                    { error("Audio sample conversion error", "Cannot read audio frame."); return; }

                unsigned char lenbuf[2] = {0, segtab[s]};
                spx.write((char*)lenbuf, 2);
                spx.write((char*)seg, segtab[s]);
            }
        }

        first = false;
    }

    ogg.close();
    spx.close();

    substep++;
    continueIdentification();
}

void SoundFix::collectCookies(QNetworkReply *reply)
{
    QVariant v = reply->header(QNetworkRequest::SetCookieHeader);
    QList<QNetworkCookie> headers = qvariant_cast<QList<QNetworkCookie> >(v);
    QList<QNetworkCookie>::const_iterator iter;

    for (iter = headers.begin(); iter != headers.end(); ++iter) {
        QString name = (*iter).name();
        QString value = (*iter).value();

        if (name == "PHPSESSID") phpsessid = value;
        if (name == "num_searches_cookie") num_searches = value;
        if (name == "recent_searches_cookie_1") recent_searches = value;
    }

    printf("phpsessid: %s\n", phpsessid.toAscii().data());
    printf("num_searches_cookie: %s\n", num_searches.toAscii().data());
    printf("recent_searches_cookie: %s\n", recent_searches.toAscii().data());
}

void SoundFix::httpChartsFinished(QNetworkReply*reply)
{
    QVariant statusCodeV = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    int code = statusCodeV.isNull() ? 0 : statusCodeV.toInt();
    printf("getCharts status: %d\n", code);
    if (code != 200) {
        error("Song identification error", QString("Charts service returned code %1").arg(code));
        return;
    }

    collectCookies(reply);
    if (phpsessid.isEmpty()) {
        error("Song identification error", "Could not obtain identification session.");
        return;
    }

    substep++;
    continueIdentification();
}

void SoundFix::httpSearchFinished(QNetworkReply*reply)
{
    QVariant statusCodeV = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    int code = statusCodeV.isNull() ? 0 : statusCodeV.toInt();
    printf("search status: %d\n", code);
    if (code != 200) {
        error("Song identification error", QString("Search service returned code %1").arg(code));
        return;
    }

    collectCookies(reply);

    //substep++;
    //continueIdentification();
}

void SoundFix::getSession()
{
    printf("getting charts\n");

    if (!phpsessid.isEmpty()) {
        substep++;
        continueIdentification();
        return;
    }

    QString uid;
    for (int i=0; i<16; i++)
        uid.append((rand()%2) ? ('0' + rand()%10) : ('a' + rand()%6));

    userAgent = QString(
        "AppNumber=31 "
        "AppVersion=5.1.7b "
        "APIVersion=2.0.0 "
        "DEV=GT-I9100_GT-I9100 "
        "UID=%1 "
        "FIRMWARE=2.3.4_eng.build.20120314.185218 "
        "LANG=en_US "
        "6IMSI=310260 "
        "COUNTRY=us "
        "NETWORK=WIFI")
        .arg(uid);

    QNetworkRequest req;
    req.setUrl(QUrl("http://patraulea.com:80/v2/?method=getAvailableCharts&from=charts"));
    req.setRawHeader("Accept-Encoding", QByteArray());
    req.setRawHeader("Accept-Language", QByteArray());
    req.setRawHeader("User-Agent", QByteArray(userAgent.toAscii()));

    QString cookies = "partners_cookie=" + QUrl::toPercentEncoding(partners);

    req.setRawHeader("Cookie", QByteArray(cookies.toAscii()));
    req.setRawHeader("Cookie2", "$Version=1");

    delete httpChartsMgr;
    httpChartsMgr = new QNetworkAccessManager(this);
    QObject::connect(httpChartsMgr, SIGNAL(finished(QNetworkReply*)),
             this, SLOT(httpChartsFinished(QNetworkReply*)));

    httpChartsMgr->get(req);
    // should keep the reply ptr so we can cancel it from the progress
}

void SoundFix::postSample()
{
    printf("posting sample\n");

    QString cookies;
    cookies += "partners_cookie=" + QUrl::toPercentEncoding(partners);
    cookies += "; PHPSESSID=" + QUrl::toPercentEncoding(phpsessid);

    if (!recent_searches.isEmpty())
        cookies += "; recent_searches_cookie_1=" + QUrl::toPercentEncoding(recent_searches);

    if (!recent_searches.isEmpty())
        cookies += "; num_searches_cookie=" + QUrl::toPercentEncoding(num_searches);

    QNetworkRequest req;
    req.setUrl(QUrl("http://patraulea.com:80/v2/?method=search&type=identify&url=sh_button&prebuffer=0"));
    req.setRawHeader("Accept-Encoding", QByteArray());
    req.setRawHeader("Accept-Language", QByteArray());
    req.setRawHeader("Transfer-Encoding", "chunked");
    req.setRawHeader("User-Agent", QByteArray(userAgent.toAscii()));
    req.setRawHeader("Cookie", QByteArray(cookies.toAscii()));

    delete httpSearchMgr;
    httpSearchMgr = new QNetworkAccessManager(this);
    QObject::connect(httpSearchMgr, SIGNAL(finished(QNetworkReply*)),
             this, SLOT(httpSearchFinished(QNetworkReply*)));

    // now the speex data

    bufferData.setRawData("salut", 5);
    buffer.setData(bufferData);
    httpSearchMgr->post(req, &buffer);
}
