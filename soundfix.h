#ifndef SOUNDFIX_H
#define SOUNDFIX_H

#include <QMainWindow>

namespace Ui {
class SoundFix;
}

class SoundFix : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit SoundFix(QWidget *parent = 0);
    ~SoundFix();
    
private slots:
    void on_browseBtn_clicked();

private:
    Ui::SoundFix *ui;

    void identifyAudio();
    bool SoundFix::extractAudio();

    QString recordingName;
};

#endif // SOUNDFIX_H
