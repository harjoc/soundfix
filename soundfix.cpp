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
#include <QTcpSocket>
#include <QTime>

//#define USE_MIDOMI

SoundFix::SoundFix(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SoundFix)
{
    ui->setupUi(this);

    QTime time = QTime::currentTime();
    qsrand((uint)time.msec());

    sock = new QTcpSocket(this);
    connect(sock, SIGNAL(readyRead()), this, SLOT(sockReadyRead()));
    connect(sock, SIGNAL(connected()), this, SLOT(sockConnected()));
    connect(sock, SIGNAL(error(QAbstractSocket::SocketError)),
                 this, SLOT(sockError(QAbstractSocket::SocketError)));

    speexTimer = new QTimer(this);
    connect(speexTimer, SIGNAL(timeout()), this, SLOT(sendSpeexChunk()));

    partners = "%7B%22installed%22%3A%5B%5D%7D";
    loadSession();

    recordingName = "data/kong.3gp";
    ui->videoEdit->setText(recordingName);
    identifyAudio();
}

void SoundFix::loadSession()
{
    QFile sessionFile("data/session.txt");
    if (!sessionFile.open(QFile::ReadOnly))
        return;

    printf("loading session\n");

    for (;;) {
        QString line = sessionFile.readLine(1024).trimmed();
        if (line.isEmpty()) break;

        int sp = line.indexOf(' ');
        if (sp<0) break;

        QString name = line.left(sp);
        QString value = line.right(line.length() - (sp+1));

        printf("%s is [%s]\n", name.toAscii().data(), value.toAscii().data());

        if      (name == "userAgent") userAgent = value;
        else if (name == "PHPSESSID") phpsessid = value;
        else if (name == "recent_searches_cookie_1") recent_searches = value;
        else if (name == "num_searches_cookie") num_searches = value;
        else if (name == "partners_cookie") partners = value;
        else printf("unknown session var: %s\n", name.toAscii().data());
    }

    sessionFile.close();
}

SoundFix::~SoundFix()
{
    delete ui;
}

void SoundFix::cleanupIdentification()
{
    speexTimer->stop();
    speexFile.close();
    sock->abort();
}

void SoundFix::on_browseBtn_clicked()
{
    cleanupIdentification();

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

void SoundFix::sockConnected()
{
    if (substep == IDENTIFY_GET_SESSION) {
        QString cookies = "partners_cookie=" + partners;

        QString req = QString(
                "GET /v2/?method=getAvailableCharts&from=charts HTTP/1.1\r\n"
                #ifdef USE_MIDOMI
                "Host: api.midomi.com:443\r\n"
                #else
                "Host: patraulea.com:80\r\n"
                #endif
                "Connection: Keep-Alive\r\n"
                "Cookie: %1\r\n"
                "Cookie2: $Version=1\r\n"
                "User-Agent: %2\r\n"
                "\r\n").arg(cookies, userAgent);

        contentLength = -1;
        sockBuf.truncate(0);
        sock->write(req.toAscii().data());
    } else if (substep == IDENTIFY_POST_SAMPLE) {            
        QString cookies = "partners_cookie=" + partners;
        cookies += "; PHPSESSID=" + phpsessid;

        if (!recent_searches.isEmpty())
            cookies += "; recent_searches_cookie_1=" + recent_searches;

        if (!recent_searches.isEmpty())
            cookies += "; num_searches_cookie=" + num_searches;

        QString req = QString(
                "POST /v2/?method=search&type=identify&url=sh_button&prebuffer=0 HTTP/1.1\r\n"
                #ifdef USE_MIDOMI
                "Host: api.midomi.com:443\r\n"
                "Transfer-Encoding: chunked\r\n"
                #else
                "Host: patraulea.com:80\r\n"
                "Content-Length: 0\r\n"
                #endif
                "User-Agent: %1\r\n"
                "Cookie: %2\r\n"
                "\r\n").arg(userAgent, cookies);

        contentLength = -1;
        sockBuf.truncate(0);
        sock->write(req.toAscii().data());

        printf("sending speex data\n");

        #ifdef USE_MIDOMI
        speexTimer->start(2150);
        #endif
    }
}

void SoundFix::sendSpeexChunk()
{
    if (!speexFile.isOpen()) {
        speexFile.setFileName("data/sample.spx");
        if (!speexFile.open(QFile::ReadOnly)) {
            error("Error opening audio sample", "Cannot open audio sample for identification.");
            cleanupIdentification();
            return;
        }
    }

    char burst[5*1024];
    int blen = speexFile.read(burst, sizeof(burst));
    if (blen < 0) blen = 0;

    printf("%d bytes to send\n", blen);
    if (blen==0)
        speexTimer->stop();

    while (blen > 0) {
        int clen = 2*1024;
        if (clen > blen)
            clen = blen;
        sock->write(QString("%1\r\n").arg(clen, 0, 16).toAscii().data());
        sock->write(burst, clen);
        sock->write("\r\n");
        memmove(burst, burst+clen, blen-clen);
        blen -= clen;
    }
}

void SoundFix::sockReadyRead()
{
    if (substep != IDENTIFY_GET_SESSION &&
        substep != IDENTIFY_POST_SAMPLE)
    {
        error("Network error", "Unexpected response from identification service.");
        cleanupIdentification();
        return;
    }

    QByteArray chunk = sock->readAll();
    //printf("received: %d\n", chunk.length());
    sockBuf.append(chunk);

    if (contentLength < 0) {
        int rnrn = sockBuf.indexOf("\r\n\r\n");
        if (rnrn < 0) return;

        if (!sockBuf.startsWith("HTTP/1.0 200 ") &&
            !sockBuf.startsWith("HTTP/1.1 200 "))
        {
            error("Song identification error", "Identification service returned an error.");
            cleanupIdentification();
            return;
        }

        int clenPos = sockBuf.indexOf("\r\nContent-Length: ", 0, Qt::CaseInsensitive);
        if (clenPos < 0 || clenPos > rnrn) {
            error("Song identification error", "Invalid response from identification service.");
            cleanupIdentification();
            return;
        }

        clenPos += strlen("\r\nContent-Length: ");
        int rn = sockBuf.indexOf("\r\n", clenPos);
        QString clenStr = sockBuf.mid(clenPos, rn-clenPos);

        contentLength = clenStr.toInt();
        //printf("content-length: %d\n", contentLength);

        headers = sockBuf.left(rnrn+2);
        sockBuf = sockBuf.right(sockBuf.length() - (rnrn+4));
    }

    //printf("have: %d\n", sockBuf.length());
    if (sockBuf.length() < contentLength)
        return;

    sockBuf.truncate(contentLength);
    printf("---\n%s\n---\n", sockBuf.toAscii().data());
    cleanupIdentification();

    if (substep == IDENTIFY_GET_SESSION)
        processSessionResponse();
    else if (substep == IDENTIFY_POST_SAMPLE)
        processSearchResponse();
}

void SoundFix::collectCookies()
{
    int hdrpos = 0;

    for (;;) {
        hdrpos = headers.indexOf("\r\nSet-Cookie: ", hdrpos, Qt::CaseInsensitive);
        if (hdrpos < 0) break;
        hdrpos += strlen("\r\nSet-Cookie: ");

        int prn = headers.indexOf("\r\n", hdrpos);

        int p0 = prn;
        int p1 = headers.indexOf(";", hdrpos);
        int p2 = headers.indexOf(" ", hdrpos);
        if (p1>0 && p1 < p0) p0 = p1;
        if (p2>0 && p2 < p1) p0 = p2;

        int peq = headers.indexOf("=", hdrpos);
        if (peq < 0) break;

        QString name  = headers.mid(hdrpos, peq-hdrpos);
        QString value = headers.mid(peq+1, p0-(peq+1));

        printf("%s is [%s]\n", name.toAscii().data(), value.toAscii().data());

        if (name == "PHPSESSID") phpsessid = value;
        if (name == "recent_searches_cookie_1") recent_searches = value;
        if (name == "num_searches_cookie") num_searches = value;

        hdrpos = prn;
    }

    QFile sessionFile("data/session.txt");
    if (!sessionFile.open(QFile::WriteOnly))
        return;

    sessionFile.write(QString("userAgent %1\n").arg(userAgent).toAscii());
    sessionFile.write(QString("PHPSESSID %1\n").arg(phpsessid).toAscii());
    sessionFile.write(QString("recent_searches_cookie_1 %1\n").arg(recent_searches).toAscii());
    sessionFile.write(QString("num_searches_cookie %1\n").arg(num_searches).toAscii());
    sessionFile.write(QString("partners_cookie %1\n").arg(partners).toAscii());

    sessionFile.close();
}

void SoundFix::processSessionResponse()
{
    collectCookies();

    if (phpsessid.isEmpty())
        { error("Song identification error", "Cannot get identification session."); return; }

    substep++;
    continueIdentification();
    return;
}

void SoundFix::processSearchResponse()
{
    collectCookies();

    cleanupIdentification();

    int trackPos  = sockBuf.indexOf("track_name=\"");
    int artistPos = sockBuf.indexOf("artist_name=\"");
    int trackEnd;
    int artistEnd;

    if (trackPos >= 0) {
        trackPos += strlen("track_name=\"");
        trackEnd = sockBuf.indexOf("\">", trackPos);
    }

    if (artistPos >= 0) {
        artistPos += strlen("artist_name=\"");
        artistEnd = sockBuf.indexOf("\">", artistPos);
    }

    if (trackPos<0 || artistPos<0 || trackEnd<0 || artistEnd<0) {
        error("Song identification not available", "Could not identify this song automatically.");
        return;
    }

    QString track  = sockBuf.mid(trackPos,  trackEnd-trackPos);
    QString artist = sockBuf.mid(artistPos, artistEnd-artistPos);

    ui->songEdit->setText(QString("%1 - %2").arg(artist, track));
}

void SoundFix::sockError(QAbstractSocket::SocketError)
{
    if (substep == IDENTIFY_GET_SESSION)
        error("Network error", "Cannot get song charts information.");
    else if (substep == IDENTIFY_GET_SESSION)
        error("Network error", "Cannot do automatic song identification.");

    cleanupIdentification();
}

void SoundFix::getSession()
{
    if (!phpsessid.isEmpty()) {
        substep++;
        continueIdentification();
        return;
    }

    printf("getting charts\n");

    QString uid;
    for (int i=0; i<16; i++)
        uid.append((qrand()%2) ? ('0' + qrand()%10) : ('a' + qrand()%6));

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

    sock->abort();

    #ifdef USE_MIDOMI
    sock->connectToHost("api.midomi.com", 443);
    #else
    sock->connectToHost("patraulea.com", 80);
    #endif
}

void SoundFix::postSample()
{
    printf("posting sample\n");

    sock->abort();

    #ifdef USE_MIDOMI
    sock->connectToHost("search.midomi.com", 443);
    #else
    sock->connectToHost("patraulea.com", 80);
    #endif
}
