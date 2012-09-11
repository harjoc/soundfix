#ifndef SOUNDFIX_H
#define SOUNDFIX_H

#include <QMainWindow>
#include <QBuffer>

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
    void httpChartsFinished(QNetworkReply*);
    void httpSearchFinished(QNetworkReply*);

private:
    Ui::SoundFix *ui;

    void error(const QString &title, const QString &text);
    void identifyAudio();
    void continueIdentification();
    void extractAudio();
    void collectCookies(QNetworkReply *reply);
    void getSession();
    void postSample();

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

    QBuffer buffer;
    QByteArray bufferData;


};

#endif // SOUNDFIX_H
