#ifndef SOUNDFIX_H
#define SOUNDFIX_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>
#include <QFile>
#include <QProgressDialog>
#include <QProcess>
#include <QBuffer>

namespace Ui {
class SoundFix;
}

class QNetworkReply;
class QNetworkAccessManager;
class QButtonGroup;

namespace Phonon {
    class MediaObject;
    enum State;
};

class SoundFix : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit SoundFix(QWidget *parent = 0);
    ~SoundFix();

protected:
    void closeEvent(QCloseEvent *event);

private slots:
    void appReady();

    // identification
    void on_browseBtn_clicked();
    void sockConnected();
    void sockReadyRead();
    void sockError(QAbstractSocket::SocketError);
    void sendSpeexChunk();

    // youtube search
    void on_searchBtn_clicked();
    void youtubeReadyRead();
    void youtubeFinished(int exitCode);
    void youtubeError(QProcess::ProcessError err);
    void thumbnailFinished(QNetworkReply *reply);

    // youtube down
    void on_downloadBtn_clicked();
    void youtubeDownReadyRead();
    void youtubeDownError(QProcess::ProcessError);
    void youtubeDownFinished(int exitCode);

    // audio sync
    void playOffset();
    void playerStateChanged(Phonon::State);

    void on_saveBtn_clicked();

private:
    Ui::SoundFix *ui;

    void error(const QString &title, const QString &text);
    void information(const QString &title, const QString &text);

    // identification
    void loadSession();    
    void startIdentification();
    void continueIdentification();
    void cleanupIdentification();
    bool getVideoInfo();
    void extractAudio();
    bool loadCachedSongName();
    void collectCookies();
    void getSession();
    void postSample();
    void processSessionResponse();
    void processSearchResponse();

    // youtube search
    bool loadCachedYoutubeResults();
    void startYoutubeSearch();
    void cleanupYoutubeSearch();
    void youtubeUpdateProgress();
    void youtubeAddResult();
    void showThumb();
    void startThumbnail();
    bool saveThumb(const QByteArray &data);

    // youtube download
    void startYoutubeDown(const QString &videoId);
    void cleanupYoutubeDown();

    // audio sync
    void cleanupAudioSync();
    void runAudioSync();
    bool mixSyncAudio(int row);
    void playSyncAudio(int row);
    QPushButton *buttonFromOffsetRow(int row);

    public: int updateAudioSyncProgress(const char *step, int progress);
    private:

    // === members ===

    // identification
    int identSubstep;
    QProgressDialog identProgressBar;

    QString recordingPath;
    QString recordingName;
    int durationMsec;

    QNetworkAccessManager* httpChartsMgr;
    QNetworkAccessManager* httpSearchMgr;

    QString phpsessid;
    QString partners;
    QString recent_searches;
    QString num_searches;
    QString userAgent;

    QTcpSocket *sock;

    QString headers;
    QByteArray sockBuf;
    int contentLength;

    QTimer *speexTimer;
    QFile speexFile;    

    // youtube search
    QString cleanSongName;
    QProcess youtubeSearchProc;
    int youtubeLineNo;
    QString youtubeLines[3];

    int thumbsStarted;
    int thumbsFinished;
    QList<QString> thumbUrls;
    QNetworkAccessManager *thumbMgr;

    QButtonGroup *radioGroup;

    // youtube download
    QProcess youtubeDownProc;
    QString youtubeDownStdout;
    QString youtubeDownDestination;

    // audio sync
    QProgressDialog syncProgressBar;

    #define MAX_SYNC_OFFSETS 10
    int offsets[MAX_SYNC_OFFSETS];
    float confidences[MAX_SYNC_OFFSETS];
    int retOffsets;

    Phonon::MediaObject *player;
    int playingRow;
};

#endif // SOUNDFIX_H
