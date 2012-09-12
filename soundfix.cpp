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
#include <QThread>
#include <QCloseEvent>
#include <QRadioButton>
#include <QTextDocument>

//#define USE_MIDOMI

#define TEST_IDENT_SRV "localhost"

SoundFix::SoundFix(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::SoundFix),
    radioGroup(new QButtonGroup(this))
{
    ui->setupUi(this);

    // identification

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

    progressBar.setCancelButtonText("Cancel");

    // youtube search

    thumbMgr = new QNetworkAccessManager(this);

    ui->youtubeTable->setHorizontalHeaderLabels(
            QStringList() << "Use" << "Play" << "Sample" << "Title");

    ui->youtubeTable->setColumnWidth(0, 0); //30);
    ui->youtubeTable->setColumnWidth(1, 0); //40);
    ui->youtubeTable->setColumnWidth(2, 90);

    //ui->youtubeTable->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft);
    //ui->youtubeTable->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignLeft);
    //ui->youtubeTable->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignLeft);
    ui->youtubeTable->horizontalHeaderItem(3)->setTextAlignment(Qt::AlignLeft);

    // sync

    ui->offsetsTable->setHorizontalHeaderLabels(
            QStringList() << "Use" << "Play" << "Offset");

    ui->offsetsTable->setColumnWidth(0, 30);
    ui->offsetsTable->setColumnWidth(1, 40);

    //ui->offsetsTable->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft);
    //ui->offsetsTable->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignLeft);
    ui->offsetsTable->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignLeft);

    // test
    QTimer::singleShot(0, this, SLOT(appReady()));
}

void SoundFix::appReady()
{
    recordingName = "data/tefalta.3gp";
    ui->videoEdit->setText(recordingName);
    //startIdentification();
    ui->songEdit->setText("Calambuco - Te Falta Ritmo");

    startYoutubeDown("CU8V4BSuRKI");
}

void SoundFix::closeEvent(QCloseEvent *event)
{
    cleanupIdentification();
    cleanupYoutubeSearch();
    cleanupYoutubeDown();
    event->accept();
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
    printf("identification cleanup\n");

    speexTimer->stop();
    speexFile.close();
    sock->abort();

    progressBar.setValue(progressBar.maximum());
}

void SoundFix::on_browseBtn_clicked()
{
    cleanupIdentification();

    recordingName = QFileDialog::getOpenFileName(this, "Open Video", QString());
    if (recordingName.isNull())
        return;

    ui->videoEdit->setText(QDir::toNativeSeparators(recordingName));

    startIdentification();
}

void SoundFix::error(const QString &title, const QString &text)
{
    QMessageBox::warning(this, title, text, QMessageBox::Ok);
}

void SoundFix::information(const QString &title, const QString &text)
{
    QMessageBox::information(this, title, text, QMessageBox::Ok);
}

enum {
    IDENTIFY_EXTRACT_AUDIO=0,
    IDENTIFY_GET_SESSION,
    IDENTIFY_POST_SAMPLE
};

void SoundFix::startIdentification()
{
    progressBar.setMinimum(0);
    progressBar.setMaximum(100);
    progressBar.setValue(0);
    progressBar.setMinimumDuration(1500);

    substep = IDENTIFY_EXTRACT_AUDIO;
    continueIdentification();
}

class Thr : public QThread {
    public: static void msleep(unsigned long msecs) { QThread::msleep(msecs); }
};

void SoundFix::continueIdentification()
{
    switch (substep) {
        case IDENTIFY_EXTRACT_AUDIO:
            progressBar.setLabelText("Analyzing audio track...");
            progressBar.setValue(25);
            QApplication::processEvents();

            ("Analyzing audio track...", "Cancel", 0, 10, this);
            extractAudio();
            return;

        case IDENTIFY_GET_SESSION:
            progressBar.setLabelText("Starting audio identification session...");
            progressBar.setValue(50);
            QApplication::processEvents();

            getSession();
            return;

        case IDENTIFY_POST_SAMPLE:
            progressBar.setLabelText("Identifying audio track...");
            progressBar.setValue(75);
            QApplication::processEvents();

            #ifdef USE_MIDOMI
            Thr::msleep(2500);
            #endif
            postSample();
            return;
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
                "Host: " TEST_IDENT_SRV ":80\r\n"
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
                "Host: " TEST_IDENT_SRV ":80\r\n"
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
    sock->connectToHost(TEST_IDENT_SRV, 80);
    #endif
}

void SoundFix::postSample()
{
    printf("posting sample\n");

    sock->abort();

    #ifdef USE_MIDOMI
    sock->connectToHost("search.midomi.com", 443);
    #else
    sock->connectToHost(TEST_IDENT_SRV, 80);
    #endif
}

void SoundFix::on_searchBtn_clicked()
{
    // TODO check that step 1 completed

    QByteArray songName = ui->songEdit->text().toAscii();
    QString cleanSongName;
    QString songQuery;

    int lc = 0;
    for (int i=0; i<songName.length(); i++) {
        int c = songName[i];
        if ((c < 'a' || c > 'z') &&
            (c < 'A' || c > 'Z') &&
            (c < '0' || c > '9'))
            c = ' ';

        if (c == ' ' && lc == ' ') continue;

        cleanSongName.append(c);
        lc = c;

        int q = (c==' ') ? '+' : c;
        songQuery.append(q);
    }

    ui->songLabel->setText(QString("<a href=\"http://www.youtube.com/results?search_query=%1\">%2</a>")
            .arg(songQuery).arg(cleanSongName));

    startYoutubeSearch(cleanSongName);
}

#define YOUTUBE_RESULTS 10
// TODO there may not be 10 results ... update maxpos at youtube-dl eof

void SoundFix::startYoutubeSearch(const QString &cleanSongName)
{
    cleanupYoutubeSearch();

    printf("\nyoutube search\n");

    progressBar.setLabelText("Starting YouTube video search...");
    progressBar.setMinimum(0);
    progressBar.setMaximum(2*YOUTUBE_RESULTS);
    progressBar.setValue(0);
    progressBar.setMinimumDuration(500);

    ui->youtubeTable->setRowCount(0);

    connect(&youtubeSearchProc, SIGNAL(readyReadStandardOutput()), this, SLOT(youtubeReadyRead()));
    connect(&youtubeSearchProc, SIGNAL(finished(int)), this, SLOT(youtubeFinished(int)));
    connect(&youtubeSearchProc, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(youtubeError(QProcess::ProcessError)));

    connect(thumbMgr, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(thumbnailFinished(QNetworkReply*)));

    cleanSongName.isEmpty();

    //youtubeSearchProc.start("tools/youtube-dl.exe", QStringList() <<
    //        QString("ytsearch10:%1").arg(cleanSongName) <<
    //        "--cookies" << "data/cookies.txt" <<
    //        "-g" << "-e" << "--get-thumbnail");
    youtubeSearchProc.start("python.exe tools/delay.py tools/search.txt 0");

    ui->youtubeTable->setFocus();
}

void SoundFix::cleanupYoutubeSearch()
{
    printf("youtube search cleanup\n");

    disconnect(&youtubeSearchProc, SIGNAL(readyReadStandardOutput()), this, SLOT(youtubeReadyRead()));
    disconnect(&youtubeSearchProc, SIGNAL(finished(int)), this, SLOT(youtubeFinished(int)));
    disconnect(&youtubeSearchProc, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(youtubeError(QProcess::ProcessError)));

    disconnect(thumbMgr, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(thumbnailFinished(QNetworkReply*)));

    youtubeSearchProc.kill();

    thumbUrls.clear();
    //while (!thumbMgrs.isEmpty())
    //     delete thumbMgrs.takeFirst();

    youtubeLineNo = 0;
    thumbsStarted = 0;
    thumbsFinished = 0;

    progressBar.setValue(progressBar.maximum());
}

void SoundFix::youtubeUpdateProgress()
{
    progressBar.setValue(youtubeLineNo/3 + thumbsFinished);
}

// thumbUrl = "..../CU8V4BSuRKI/default.jpg"

QString getVideoId(const QString &thumbUrl)
{
    int rpos = thumbUrl.lastIndexOf('/');
    if (rpos<0) return QString();

    int lpos = thumbUrl.lastIndexOf('/', rpos-1);
    if (lpos<0) return QString();

    return thumbUrl.mid(lpos+1, rpos-1-lpos);
}

void SoundFix::youtubeAddResult()
{    
    int row = ui->youtubeTable->rowCount();
    ui->youtubeTable->insertRow(row);

    ui->youtubeTable->verticalHeader()->resizeSection(row, 60);

    // col 0

    /*QWidget* w0 = new QWidget;
    QRadioButton *radio = new QRadioButton(this);
    radioGroup->addButton(radio);*/

    // slot for selected, to deselect others
    /*QHBoxLayout* layout0 = new QHBoxLayout(w0);
    layout0->addWidget(radio);
    layout0->setAlignment(Qt::AlignCenter);
    layout0->setSpacing(0);
    layout0->setMargin(0);
    w0->setLayout(layout0);
    ui->youtubeTable->setCellWidget(row, 0, w0);*/

    if (row==0) ui->youtubeTable->selectRow(0);

    // col 1

    /*QWidget* w1 = new QWidget;
    QPushButton *button = new QPushButton(w1);
    button->setIcon(QIcon("play.png"));
    QHBoxLayout* layout1 = new QHBoxLayout(w1);
    layout1->addWidget(button);
    layout1->setAlignment(Qt::AlignCenter);
    layout1->setSpacing(0);
    layout1->setMargin(0);
    w1->setLayout(layout1);
    ui->youtubeTable->setCellWidget(row, 1, w1);*/

    // col 3

    QString videoId = getVideoId(youtubeLines[2]);

    QString link = videoId.isEmpty() ? youtubeLines[0] :
             QString("<a href=\"http://localhost/watch?v=%1\">%2</a>")
                    .arg(videoId).arg(Qt::escape(youtubeLines[0]));

    QLabel *href = new QLabel(link, this);
    href->setOpenExternalLinks(true);

    QFont font = href->font();
    font.setPointSize(11);
    font.setBold(true);
    href->setFont(font);

    ui->youtubeTable->setCellWidget(row, 3, href);
}

void SoundFix::showThumb()
{
    // col 2
    QWidget* w2 = new QWidget;
    QLabel *label = new QLabel(w2);

    QPixmap pixmap(QString("data/%1.jpg").arg(thumbsFinished));
    QPixmap scaledPix = pixmap.scaled(90, 60);
    label->setPixmap(scaledPix);

    QHBoxLayout* layout2 = new QHBoxLayout(w2);
    layout2->addWidget(label);
    layout2->setAlignment(Qt::AlignCenter);
    layout2->setSpacing(0);
    layout2->setMargin(0);

    w2->setLayout(layout2);

    ui->youtubeTable->setCellWidget(thumbsFinished, 2, w2);
}

void SoundFix::youtubeReadyRead()
{
    while (youtubeSearchProc.canReadLine()) {
        QString line = youtubeSearchProc.readLine().trimmed();
        if (line.isEmpty()) continue;

        youtubeLines[youtubeLineNo++ % 3] = line;

        if (youtubeLineNo % 3 == 0) {
            youtubeUpdateProgress();

            printf("title: [%s]\n",     youtubeLines[0].toAscii().data());
            printf("url: [%s]\n",       youtubeLines[1].toAscii().data());
            printf("thumbnail: [%s]\n", youtubeLines[2].toAscii().data());
            printf("\n");

            youtubeAddResult();

            thumbUrls.append(youtubeLines[2]);
            if (thumbsFinished == thumbsStarted)
                startThumbnail();
        }
    }
}

void SoundFix::startThumbnail()
{
    printf("starting thumbnail %d\n", thumbsStarted);

    /*QNetworkAccessManager *mgr = new QNetworkAccessManager(this);
    thumbMgrs.append(mgr);

    connect(mgr, SIGNAL(finished(QNetworkReply*)),
            this, SLOT(thumbnailFinished(QNetworkReply*)));*/

    thumbMgr->get(QNetworkRequest(QUrl(thumbUrls[thumbsStarted])));

    thumbsStarted++;
}

bool SoundFix::saveThumb(const QByteArray &data)
{
    QFile f(QString("data/%1.jpg").arg(thumbsFinished));
    if (!f.open(QFile::WriteOnly))
        return false;

    f.write(data);
    f.close();
    return true;
}

void SoundFix::thumbnailFinished(QNetworkReply *reply)
{
    printf("thumbnail finished %d\n", thumbsFinished);

    if (saveThumb(reply->readAll()))
        showThumb();

    reply->deleteLater();

    thumbsFinished++;
    youtubeUpdateProgress();

    if (thumbUrls.length() > thumbsFinished)
        startThumbnail();

    if (!youtubeSearchProc.isOpen())
        cleanupYoutubeSearch();
}

void SoundFix::youtubeError(QProcess::ProcessError)
{
    error("YouTube search error", "Could not start YouTube search.");
    cleanupYoutubeSearch();
}

void SoundFix::youtubeFinished(int exitCode)
{
    printf("youtube search finished\n");

    if (exitCode != 0) {
        error("YouTube search error", "YouTube search returned an error.");
        cleanupYoutubeSearch();
        return;
    }

    // TODO update progress, etc with actual number of results

    if (thumbsFinished == thumbUrls.length())
        cleanupYoutubeSearch();
}

void SoundFix::on_downloadBtn_clicked()
{
    QList<QModelIndex> rows = ui->youtubeTable->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        information("No video is selected", "Please select a video to download from the list.");
        return;
    }

    // get videoId from row
    //startYoutubeDown();
}

// 60KB/s, ETA 00:59

void SoundFix::startYoutubeDown(const QString &videoId)
{
    cleanupYoutubeDown();

    printf("\nyoutube down\n");

    connect(&youtubeDownProc, SIGNAL(readyReadStandardOutput()), this, SLOT(youtubeDownReadyRead()));
    connect(&youtubeDownProc, SIGNAL(finished(int)), this, SLOT(youtubeDownFinished(int)));
    connect(&youtubeDownProc, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(youtubeDownError(QProcess::ProcessError)));

    youtubeDownStdout.clear();
    youtubeDownDestination.clear();

    videoId.isEmpty();

    //youtubeDownProc.start("tools/youtube-dl.exe", QStringList() <<
    //        QString("http://www.youtube.com/watch?v=%1").arg(videoId) <<
    //        "--cookies" << "data/cookies.txt");
    youtubeDownProc.start("python.exe tools/delay.py tools/download.txt 0.02");
}

void SoundFix::cleanupYoutubeDown()
{
    printf("youtube down cleanup\n");

    disconnect(&youtubeDownProc, SIGNAL(readyReadStandardOutput()), this, SLOT(youtubeDownReadyRead()));
    disconnect(&youtubeDownProc, SIGNAL(finished(int)), this, SLOT(youtubeDownFinished(int)));
    disconnect(&youtubeDownProc, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(youtubeDownError(QProcess::ProcessError)));

    youtubeDownProc.kill();

    ui->downloadProgress->setValue(0);
}

void SoundFix::youtubeDownReadyRead()
{
    youtubeDownStdout.append(youtubeDownProc.readAll());

    for (;;) {
        // it separates progress lines with \r and other lines with \n
        int cr = youtubeDownStdout.indexOf('\r');
        int lf = youtubeDownStdout.indexOf('\n');
        if (cr<0 && lf<0) break;

        int sep = (cr<0 || (lf>0 && lf<cr)) ? lf : cr;
        int nonsep = sep;

        QString sepChars("\r\n");
        while (nonsep < youtubeDownStdout.length() && sepChars.contains(youtubeDownStdout.at(nonsep)))
            nonsep++;

        QString line = youtubeDownStdout.left(sep);
        youtubeDownStdout = youtubeDownStdout.right(youtubeDownStdout.length() - nonsep);

        // get destination file

        QRegExp rxd("\\[download\\] +Destination: +(.+)");
        if (rxd.indexIn(line)>=0) {
            youtubeDownDestination = rxd.cap(1);
            printf("destination: %s\n", youtubeDownDestination.toAscii().data());
        }

        // get progress

        QRegExp rxp("\\[download\\] +(.+)%.* at (.+)/s +ETA +([^ ]+)");
        if (rxp.indexIn(line)>=0) {
            QString percent = rxp.cap(1);
            QString speed = rxp.cap(2);
            QString eta = rxp.cap(3);

            printf("percent=%s speed=%s eta=%s\n",
                   percent.toAscii().data(),
                   speed.toAscii().data(),
                   eta.toAscii().data());

            float fpercent = percent.toFloat()*10;
            ui->downloadProgress->setValue(fpercent);

            ui->progressLabel->setText(QString("%1/s, ETA %2").arg(speed).arg(eta));
        }
    }
}

void SoundFix::youtubeDownError(QProcess::ProcessError)
{
    error("YouTube download error", "Could not download YouTube video.");
    cleanupYoutubeSearch();
}

void SoundFix::youtubeDownFinished(int exitCode)
{
    printf("youtube down finished\n");

    if (exitCode != 0) {
        error("YouTube download error", "YouTube download was interrupted.");
        cleanupYoutubeSearch();
        return;
    }

    ui->progressLabel->setText("Download complete.");

    if (youtubeDownDestination.isEmpty()) {
        error("YouTube download error", "Could not determine YouTube download location.");
        cleanupYoutubeSearch();
        return;
    }

    syncAudio();
}

void SoundFix::syncAudio()
{

}

