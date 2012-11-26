class Thr : public QThread {
public: static void msleep(unsigned long msecs) { QThread::msleep(msecs); }
};


/*    QProgressDialog progress("Analyzing audio track...", "Cancel", 0, 10, this);
    for (int i=0; i<=10; i++) {
        Thr::msleep(500);
        progress.setValue(i);
        QApplication::processEvents();
        if (progress.wasCanceled())
            break;
    }
}*/


    /*QList<QNetworkReply::RawHeaderPair> headers = reply->rawHeaderPairs();
    QList<QNetworkReply::RawHeaderPair>::const_iterator iter;
    for (iter = headers.begin(); iter != headers.end(); ++iter) {
        QString name = (*iter).first;
        QString value = (*iter).second;

        if (name.compare("Set-Cookie", Qt::CaseInsensitive) != 0) continue;

        int p = value.length();
        int p1 = value.indexOf(';');
        int p2 = value.indexOf(' ');
        if (p1 > 0) p = p1;
        if (p2 > 0 && p2 < p) p = p2;
        QString trimmed = value.left(p);

        if (value.startsWith("PHPSESSID=")) phpsessid = trimmed;
        if (value.startsWith("num_searches_cookie=")) num_searches = trimmed;
        if (value.startsWith("recent_searches_cookie_1=")) recent_searches = trimmed;
    }*/


        QList<QNetworkCookie> cookies;
    cookies.append(QNetworkCookie(QByteArray("partners_cookie"), partners.toAscii()));

    QVariant var;
    var.setValue(cookies);
    req.setHeader(QNetworkRequest::KnownHeaders::CookieHeader, var);


    int rpos = url.lastIndexOf('/');
    if (rpos)
    int lpos = rpos-1;
    while (lpos>0 && url[lpos] != '/')
        lpos--;
    
    if (/bwFdr9VaFJw/




    //QTableWidgetItem *item = new QTableWidgetItem(link);





    for (int o = 0; o<npoints-range*2; o++) {
			for (int f=0; f<period/2; f++) {
				if (mags[f]) {
					int x = amp1[o*period/2+f] * 100 / mags[f];
					int y = amp2[o*period/2+f] * 100 / mags[f];
					int z = amp3[o*period/2+f] * 100 / mags[f];
					int c = x*y*z/50000;
					score += c;
				}
			}
		}