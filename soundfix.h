#ifndef SOUNDFIX_H
#define SOUNDFIX_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QTimer>
#include <QFile>
#include <QProgressDialog>
#include <QProcess>

namespace Ui {
class SoundFix;
}

class QNetworkReply;
class QNetworkAccessManager;

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

private:
    Ui::SoundFix *ui;

    void error(const QString &title, const QString &text);

    // identification
    void loadSession();    
    void startIdentification();
    void continueIdentification();
    void cleanupIdentification();
    void extractAudio();
    void collectCookies();
    void getSession();
    void postSample();
    void processSessionResponse();
    void processSearchResponse();

    // youtube search
    void startYoutubeSearch();
    void cleanupYoutubeSearch();

    int substep;

    QProgressDialog progressBar;

    // identification
    QString recordingName;
    QNetworkAccessManager* httpChartsMgr;
    QNetworkAccessManager* httpSearchMgr;

    QString phpsessid;
    QString partners;
    QString recent_searches;
    QString num_searches;
    QString userAgent;

    QTcpSocket *sock;

    QString headers;
    QString sockBuf;
    int contentLength;

    QTimer *speexTimer;
    QFile speexFile;    

    // youtube search
    QProcess youtubeProc;
    int youtubeLineNo;
    QString youtubeLines[3];
};

#endif // SOUNDFIX_H
