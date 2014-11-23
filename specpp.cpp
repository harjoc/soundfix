#include <windows.h>
#include <time.h>
#include <assert.h>

#include "kiss_fft/kiss_fftr.h"
#include "wav.h"

#include "specpp.h"

#pragma warning(disable:4996)

#define M_PI 3.14159265358979323846f

const int period = 512; // average down to this
const int fftsize = 2048;
const int step = 512; // how much to advance in 'substeps' substeps
const int substeps = 5;

kiss_fftr_cfg kcfg;
float *hann_fft=NULL;
float *hann_ws1=NULL;
float *hann_ws2=NULL;

const int h = period/2;

int maxthresh = 103;
int minthresh = 91;

int ws1 = 20;
int ws2 = ws1*35/10;
int band0 = h*1/40;
int band1 = h*38/40;

const int bspec_maxrange = 700;

SpecppCallback callback_fn = 0;
void* callback_arg = 0;

struct Score {
    int score;
    int ofs;
};

Score *scores=0;

struct Song {
    const wchar_t *fname;
    short *isamples;
    int nsamp;

    int npoints;
    int *points;
    int *avgs1;
    int *deriv;

    int *ampbuf;

    int *mags;
    int *bspec;
    int bspec_range;

    void cleanup() {
        delete isamples;
        delete points;
        delete avgs1;
        delete deriv;
        delete ampbuf;
        delete mags;
        delete bspec;
    }

    bool load();

    void get_peaks();
    void get_bspec();
    bool bspec_confidence();
    void process();

    void scale_tempo(double ratio);
};

Song s1;
Song s2;

int int_cmp(const void *a, const void *b)
{
    int x = *(int *)a;
    int y = *(int *)b;
    return y-x;
}

bool Song::load()
{    
    if (!read_wav(fname, &isamples, &nsamp))
        return false;

    npoints = (nsamp-fftsize-1)/step;
    points = new int[npoints];
    avgs1 = new int[npoints];

    ampbuf = new int[period/2*npoints];

    deriv = new int[npoints];

    mags = new int[period/2];

    bspec = new int[bspec_maxrange];

    memset(ampbuf, 0, sizeof(int)*period/2*npoints);
    memset(deriv, 0, sizeof(int)*npoints);
    memset(mags, 0, sizeof(int)*period/2);
    memset(bspec, 0, sizeof(int)*bspec_maxrange);

    for (int pos=0; pos<npoints; pos++) {
        for (int substep=0; substep<substeps; substep++) {
            static kiss_fft_cpx fft[fftsize/2+1];

            static float chunk[fftsize];

            float *ptr_f = chunk;
            short *ptr_i = isamples + pos*step + step*substep/substeps;

            for (int i=0; i<fftsize; i++)
                *(ptr_f++) = *(ptr_i++) * hann_fft[i];

            kiss_fftr(kcfg, chunk, fft);

            int *amp = ampbuf+pos*period/2;
            for (int f=0; f<fftsize/2; f++)
                amp[(period/2)*f/(fftsize/2)] += (int)((fft[f+1].i*fft[f+1].i + fft[f+1].r*fft[f+1].r)/100000000);
        }
    }

    for (int f=0; f<period/2; f++) {
        long long mag = 0;
        for (int p=0; p<npoints; p++)
            mag += ampbuf[p*period/2+f];

        mags[f] = (int)(1 + mag/npoints);
    }

    return true;
}

void Song::get_peaks()
{
    for (int p=0; p<npoints; p++) {
        int power = 0;
        int *amp = ampbuf+p*period/2;
        for (int f=band0; f<band1; f++)
            power += amp[f];
        points[p] = power;
    }

    for (int p=ws2/2; p<npoints-ws2/2; p++) {
        float avg1 = 0;

        for (int o=-ws1/2; o<ws1/2; o++)
            avg1 += points[p+o]*hann_ws1[o+ws1/2];

        float sc1 = 0.5f*(float)(ws1-1);

        avgs1[p] = (int)(avg1/sc1);
    }

    for (int p=ws2/2; p<npoints-ws2/2; p++)
        deriv[p] = (avgs1[p] > avgs1[p-1]) ? 1 : -1;
}

void Song::get_bspec()
{
    const int range = bspec_range = min(bspec_maxrange, npoints/3);

    // don't correlate over more than 90 secs
    const int correl_range = min(npoints-range*2, /*debug*/ 90*44100/step);

    printf("range=%d correl_range=%d\n", range, correl_range);

    for (int ofs=0; ofs < range; ofs++)
    {
        int *amp1 = ampbuf;
        int *amp2 = ampbuf+period/2*(ofs);
        int *amp3 = ampbuf+period/2*(ofs*2);

        long long score=0;

        for (int o = 0; o<correl_range; o++) {
            long long oscore=0;
            for (int f=0; f<period/2; f++) {
                int x = amp1[o*period/2+f] * 100 / mags[f];
                int y = amp2[o*period/2+f] * 100 / mags[f];
                int z = amp3[o*period/2+f] * 100 / mags[f];
                int c = x*y*z/50000;
                oscore += c;
            }
            //printf("%llu\t", oscore);
            score += oscore;
        }        

        //printf("\n");

        bspec[ofs] = (int)(score / correl_range);

        static char stars[] = "**************************************************";
        int nstars = min((int)sqrt((double)bspec[ofs]), sizeof(stars)-1);
        printf("%d: %.*s\n", bspec[ofs], nstars, stars);
    }
    printf("\n\n");
}

bool Song::bspec_confidence()
{
    int lastmax = 0;
    int lastmin = 0;
    double dists[bspec_maxrange];
    double growths[bspec_maxrange];
    int dists_len = 0;

    for (int i=50; i<bspec_range-4; i++) {
        int c = i+2;

        bool ismin=true;
        bool ismax=true;
        for (int j=i; j<i+5; j++) {
            ismin &= (bspec[c] <= bspec[j]);
            ismax &= (bspec[c] >= bspec[j]);
        }

        if (ismin) {
            int dmin = c-lastmin;
            if (dmin > 5)
                lastmin = c;
        }

        if (ismax) {
            int dmax = c-lastmax;
            if (dmax > 5) {
                lastmax = c;
                dists[dists_len] = dmax;
                double g = bspec[lastmax]-bspec[lastmin];
                g /= 1+bspec[lastmin];
                if (g<0) g=0;
                growths[dists_len] = g*g;
                dists_len++;
            }
        }
    }

    memmove(dists, dists+1, (dists_len-1)*sizeof(dists[0]));
    memmove(growths, growths+1, (dists_len-1)*sizeof(dists[0]));
    dists_len--;

    if (dists_len==0) return false;

    double avg = 0;
    for (int i=0; i<dists_len; i++)
        avg += dists[i];
    avg /= dists_len;

    double avg2 = 0;
    int avg2_len = 0;
    for (int i=0; i<dists_len; i++)
        if (dists[i]/avg >= 0.7 && dists[i]/avg < 1.3)
            { avg2 += dists[i]; avg2_len++; }
    avg2 /= avg2_len;

    double var2 = 0;
    for (int i=0; i<dists_len; i++)
        if (dists[i]/avg >= 0.7 && dists[i]/avg < 1.3)
            var2 += (dists[i]-avg2)*(dists[i]-avg2);
    var2 /= avg2_len;

    double gravg = 0;
    for (int i=0; i<dists_len; i++)
        gravg += growths[i];
    gravg /= dists_len;

    printf("confidence: %.2f\n", var2/gravg);
    return true;
}

void Song::process()
{
    get_peaks();
    get_bspec();
}

void Song::scale_tempo(double ratio)
{
    int new_npoints = (int)(npoints/ratio);
    int *new_points = new int[new_npoints];
    int *new_deriv = new int[new_npoints];

    memset(new_points, 0, sizeof(int)*new_npoints);
    memset(new_deriv, 0, sizeof(int)*new_npoints);

    for (int p=0; p<new_npoints; p++) {
        new_points[p] = points[(int)(p*ratio)];
        new_deriv[p] = deriv[(int)(p*ratio)];
    }

    delete points;
    delete deriv;

    npoints = new_npoints;
    points = new_points;
    deriv = new_deriv;
}

// TODO make threading dynamic

#define THREAD_NUM 4

#if THREAD_NUM>1
HANDLE thread_handles[THREAD_NUM];
HANDLE thread_ev_data[THREAD_NUM];
HANDLE thread_ev_done[THREAD_NUM];
#endif

DWORD WINAPI match_func(VOID *param)
{    
    int tid = (int)param;

    for (;;) {
        #if THREAD_NUM>1
        if (WaitForSingleObject(thread_ev_data[tid], INFINITE) != WAIT_OBJECT_0)
            break;
        #endif

        int nscores = s2.npoints-1 + s1.npoints-1;
        int start = nscores*tid/THREAD_NUM;
        int end   = nscores*(tid+1)/THREAD_NUM;

        int a1 = ws2/2;
        int b1 = s1.npoints-ws2/2;

        for (int ofsnum=start; ofsnum < end; ofsnum++) {    
            int ofs = ofsnum - (s2.npoints-1);
                    
            int a2 = ofs+ws2/2;
            int b2 = ofs+s2.npoints-ws2/2;

            int a = max(a1,a2);
            int b = min(b1,b2);

            int score = 0;

            for (int p=a; p<b; p++) {
                assert(p >= 0 && p < s1.npoints);
                assert(-ofs+p >= 0 && -ofs+p <s2.npoints);

                int c = s1.deriv[p]*s2.deriv[-ofs+p];
                assert(c == 1 || c == -1 || c == 0);
                score += c;
            }

            scores[ofsnum].ofs = ofs;
            scores[ofsnum].score = score;
        }
    
        #if THREAD_NUM>1
        SetEvent(thread_ev_done[tid]);
        #else
        break;
        #endif
    }

    return 0;
}

void match()
{
    #if THREAD_NUM>1
    for (int i=0; i<THREAD_NUM; i++)
        SetEvent(thread_ev_data[i]);
    for (int i=0; i<THREAD_NUM; i++)
        WaitForSingleObject(thread_ev_done[i], INFINITE);
    #else
    match_func(0);
    #endif

    printf("match threads finished\n");

    int nscores = s2.npoints-1 + s1.npoints-1;
    qsort(scores, nscores, sizeof(Score), int_cmp);

    int s=1;
    while (abs(scores[0].ofs - scores[s].ofs) < 20)
        s++;

    int headroom = 100 * (scores[0].score - scores[s].score) / scores[0].score;
    int offset = abs(scores[0].ofs - scores[s].ofs);

    printf("%s\theadroom=%d\t offset=%d\n", s1.fname, headroom, offset);
}

void print_scores()
{
    for (int s=0; s<20; s++) {
        int c = (s < 10) ? ('0'+s) : ('a'+s-10);
        printf("%c: %d\t%d\t%d%%\n", c, scores[s].ofs, scores[s].score, 
                scores[s].score*100/scores[0].score);
    }
    printf("\n");
}

void init_match_workers()
{
    #if THREAD_NUM>1
    for (int i=0; i<THREAD_NUM; i++) {
        thread_ev_data[i] = CreateEvent(0, FALSE, FALSE, NULL);
        thread_ev_done[i] = CreateEvent(0, FALSE, FALSE, NULL);

        DWORD threadid;
        thread_handles[i] = CreateThread(0, 0, match_func, (void *)i, 0, &threadid);
        if (thread_handles[i] == NULL)
            abort();
    }
    #endif
}

void gen_ws_hannings()
{
    delete hann_ws1;
    hann_ws1 = new float[ws1];
    for (int n=0; n<ws1; n++)
        hann_ws1[n] = 0.5f * (1.0f - cos(2.0f * M_PI * n / (ws1-1)));

    delete hann_ws2;
    hann_ws2 = new float[ws2];
    for (int n=0; n<ws2; n++)
        hann_ws2[n] = 0.5f * (1.0f - cos(2.0f * M_PI * n / (ws2-1)));

}

void specpp_init()
{
    hann_fft = new float[fftsize];
    for (int n=0; n<fftsize; n++)
        hann_fft[n] = 0.5f * (1.0f - cos(2.0f * M_PI * n / (fftsize-1)));

    gen_ws_hannings();

    kcfg = kiss_fftr_alloc(fftsize, 0, 0, 0);

    #if THREAD_NUM>1
    init_match_workers();
    #endif
}

void specpp_cleanup()
{
    // TODO
}

void clusterScores(int minOffsets, int maxOffsets, int minConfidence, int *retOffsets, int *offsets, float *confidences)
{
    if (scores[0].score==0) {
        *retOffsets = 1;
        offsets[0] = 0;
        confidences[0] = 0;
        return;
    }

    int nscores = s2.npoints-1 + s1.npoints-1;
    if (nscores > 10000)
        nscores = 10000;

    int nret=0;

    for (;;) {
        if ((nret == maxOffsets) || (nret >= minOffsets && confidences[nret-1] < minConfidence))
            break;

        int p;
        for (p = 0; p<nscores; p++) {
            int ofs = scores[p].ofs*step;

            int l;
            for (l=0; l<nret; l++)
                if (abs(ofs-offsets[l]) < 20*step)
                    break;

            if (l==nret) {
                offsets[nret] = ofs;
                confidences[nret] = scores[p].score*100.0f/scores[0].score;
                nret++;
                break;
            }
        }

        if (p==nscores)
            break;
    }

    *retOffsets = nret;
}


inline int sgn(int x) { return !x ? 0 : ((x < 0) ? -1 : 1); }

double bspec_ratio()
{
    double bestRatio = 0;
    double bestScore = 0;

    int n = min(s1.bspec_range, s2.bspec_range);

    for (double m=0.7; m < 1.4; m += 0.001) {
        int score = 0;
        int xmax = min(n, (int)(n/m));
        for (int x1=50; x1<xmax; x1++) {
            int x2 = (int)(x1*m);
            int d1 = sgn(s1.bspec[x1] - s1.bspec[x1-1]);
            int d2 = sgn(s2.bspec[x2] - s2.bspec[x2-1]);
            score += d1*d2;
        }

        if (bestScore < score) {
            bestScore = score;
            bestRatio = m;
        }

        char stars[] = "**************************************************";

        double scorev = max(score, 0);
        int scorep = (int)sqrt(scorev);
        printf("%.1f\t%d\t%.*s\n", m*100.0, score, min(sizeof(stars)-1, scorep), stars);
    }

    printf("best ratio: %.1f\n\n",bestRatio*100.0);

    if (fabs((1.0 - bestRatio)*100.0) <= 0.21)
        bestRatio = 1.0;

    return bestRatio;
}

// five steps
bool specpp_compare(const wchar_t *fname1, const wchar_t *fname2, SpecppCallback cb, void *cb_arg,
        int minOffsets, int maxOffsets, int minConfidence, int *retOffsets, int *offsets, float *confidences,
        double *tempoRatio)
{
    *retOffsets = 0;
    callback_fn = cb;
    callback_arg = cb_arg;

    s1.cleanup();
    s2.cleanup();
    s1.fname = fname1;
    s2.fname = fname2;

    if (callback_fn(callback_arg, "Loading YouTube audio track...", 0))
        return true;

    if (!s1.load()) return false;

    if (callback_fn(callback_arg, "Loading recorded audio track...", 300))
        return true;

    if (!s2.load()) return false;

    if (callback_fn(callback_arg, "Processing YouTube audio track...", 400))
        return true;

    s1.process();

    if (callback_fn(callback_arg, "Processing recorded audio track...", 500))
        return true;

    s2.process();

    bool conf1 = s1.bspec_confidence();
    bool conf2 = s2.bspec_confidence();
    /*if (conf1 && conf2) {
        *tempoRatio = bspec_ratio();
        if (*tempoRatio)
            s2.scale_tempo(*tempoRatio);
    } else */{
        *tempoRatio = 1.0;
    }

    delete scores;
    scores = new Score[s2.npoints-1 + s1.npoints-1];

    if (callback_fn(callback_arg, "Synchronizing audio tracks...", 580))
        return true;

    match();

    if (callback_fn(callback_arg, "Done!", 1000))
        return true;

    print_scores();

    clusterScores(minOffsets, maxOffsets, minConfidence, retOffsets, offsets, confidences);
    
    return true;
}

bool specpp_mix(int ofs, const wchar_t *fname_tempo, const char *fname_out)
{
    // s1, tempo-adjusted version
    short *s1t_isamples=0;
    int s1t_nsamp=0;

    if (!read_wav(fname_tempo, &s1t_isamples, &s1t_nsamp))
        return false;

    FILE *f = fopen(fname_out, "wb");
    if (!f) {
        printf("can't create %s\n", fname_out);
        delete s1t_isamples;
        return false;
    }

    // reference system is with 0 at s1t.0
    // begin, end
    int b1 = 0;
    int e1 = s1t_nsamp;
    int b2 = ofs;
    int e2 = ofs + s2.nsamp;

    int b = b1;
    if (b2 > b) b = b2;

    int e = e1;
    if (e2 < e) e = e2;

    int nsamples = e-b;

    WavHeader hdr = {};
    hdr.ChunkID = 0x46464952;
    hdr.ChunkSize = sizeof(WavFmt)-2*sizeof(int) + sizeof(WavFmt) + sizeof(WavData) + nsamples*2;
    hdr.Format = 0x45564157;

    WavFmt fmt = {};
    fmt.SubchunkID = 0x20746d66;
    fmt.SubchunkSize = sizeof(WavFmt)-2*sizeof(int);
    fmt.AudioFormat = 1;
    fmt.NumChannels = 1;
    fmt.SampleRate = 44100;
    fmt.ByteRate = 44100*2;
    fmt.BlockAlign = 2;
    fmt.BitsPerSample = 16;

    WavData data = {};
    data.SubchunkID = 0x61746164;
    data.SubchunkSize = nsamples*2;

    fwrite(&hdr,  1, sizeof(hdr),  f);
    fwrite(&fmt,  1, sizeof(fmt),  f);
    fwrite(&data, 1, sizeof(data), f);

    for (int p=b; p<e; ) {
        short buf[1024];
        int buflen = e-p;
        if (buflen > sizeof(buf)/2)
            buflen = sizeof(buf)/2;

        // play tracks alternately in 3 second intervals

        if ((p-b)/44100 % 6 < 3)
            for (int i=0; i<buflen; i++)
                buf[i] = s1t_isamples[p+i];
        else
            for (int i=0; i<buflen; i++)
                buf[i] = s2.isamples[-ofs+p+i]/2;

        fwrite(buf, 1, buflen*2, f);

        p += buflen;
    }

    fclose(f);
    delete s1t_isamples;

    return true;
}
