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


#pragma warning(disable:4482) // nonstandard extension: enum used in qualified name

SoundFix::SoundFix(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SoundFix),
    sock(0)
{
    ui->setupUi(this);

    partners = "%7B%22installed%22%3A%5B%5D%7D";

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
    // TODO should abort first, based on substep

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

/*void SoundFix::collectCookies(QNetworkReply *reply)
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
}*/

/*void SoundFix::httpChartsFinished(QNetworkReply*reply)
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
*/

void SoundFix::sockConnected()
{
    if (substep == IDENTIFY_GET_SESSION) {
        QString cookies = "partners_cookie=" + partners;

        QString req = QString(
                "GET /v2/?method=getAvailableCharts&from=charts HTTP/1.1\r\n"
                "Host: patraulea.com:80\r\n"
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
                "Host: patraulea.com:80\r\n"
                //"Transfer-Encoding: chunked\r\n"
                "Content-Length: 0\r\n"
                "User-Agent: %1\r\n"
                "Cookie: %2\r\n"
                "\r\n").arg(userAgent, cookies);

        contentLength = -1;
        sockBuf.truncate(0);
        sock->write(req.toAscii().data());

        printf("sent search headers\n");
    }
}

void SoundFix::sockReadyRead()
{
    if (substep != IDENTIFY_GET_SESSION &&
        substep != IDENTIFY_POST_SAMPLE)
    {
        error("Network error", "Unexpected response from identification service.");
        sock->abort();
        return;
    }

    QByteArray chunk = sock->readAll();
    printf("received: %d\n", chunk.length());
    sockBuf.append(chunk);

    if (contentLength < 0) {
        int rnrn = sockBuf.indexOf("\r\n\r\n");
        if (rnrn < 0) return;

        int clenPos = sockBuf.indexOf("\r\nContent-Length: ", 0, Qt::CaseInsensitive);
        if (clenPos < 0 || clenPos > rnrn) {
            error("Song identification error", "Invalid response from identification service.");
            sock->abort();
            return;
        }

        clenPos += strlen("\r\nContent-Length: ");
        int rn = sockBuf.indexOf("\r\n", clenPos);
        QString clenStr = sockBuf.mid(clenPos, rn-clenPos);

        contentLength = clenStr.toInt();
        printf("content-length: %d\n", contentLength);

        headers = sockBuf.left(rnrn+2);
        sockBuf = sockBuf.right(sockBuf.length() - (rnrn+4));
    }

    printf("have: %d\n", sockBuf.length());
    if (sockBuf.length() < contentLength)
        return;

    sockBuf.truncate(contentLength);
    printf("---\n%s\n---\n", sockBuf.toAscii().data());
    sock->abort();

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
}

void SoundFix::sockError(QAbstractSocket::SocketError)
{
    if (substep == IDENTIFY_GET_SESSION)
        error("Network error", "Cannot get song charts information.");
    else if (substep == IDENTIFY_GET_SESSION)
        error("Network error", "Cannot do automatic song identification.");

    sock->abort();
}

void SoundFix::initSock()
{
    if (sock) {
        sock->abort();
    } else {
        sock = new QTcpSocket(this);
        connect(sock, SIGNAL(readyRead()), this, SLOT(sockReadyRead()));
        connect(sock, SIGNAL(connected()), this, SLOT(sockConnected()));
        connect(sock, SIGNAL(error(QAbstractSocket::SocketError)),
                     this, SLOT(sockError(QAbstractSocket::SocketError)));
    }
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

    initSock();
    sock->connectToHost("patraulea.com", 80);
}

void SoundFix::postSample()
{
    printf("posting sample\n");

    initSock();
    sock->connectToHost("patraulea.com", 80);
}
