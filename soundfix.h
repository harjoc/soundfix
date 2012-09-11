#ifndef SOUNDFIX_H
#define SOUNDFIX_H

#include <QMainWindow>
#include <QByteArray>
#include <QTcpSocket>

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
    
private slots:
    void on_browseBtn_clicked();
    void sockConnected();
    void sockReadyRead();
    void sockError(QAbstractSocket::SocketError);

private:
    Ui::SoundFix *ui;

    void error(const QString &title, const QString &text);
    void identifyAudio();
    void continueIdentification();
    void extractAudio();
    void initSock();
    void collectCookies();
    void getSession();
    void postSample();
    void processSessionResponse();
    void processSearchResponse();

    int substep;
    QString recordingName;
    QNetworkAccessManager* httpChartsMgr;
    QNetworkAccessManager* httpSearchMgr;

    // cookies
    QString phpsessid;
    QString partners;
    QString recent_searches;
    QString num_searches;
    QString userAgent;

    QTcpSocket *sock;

    QString headers;
    QString sockBuf;
    int contentLength;
};

#endif // SOUNDFIX_H
