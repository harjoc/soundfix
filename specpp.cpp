#include <windows.h>
#include <time.h>
#include <assert.h>

#include "kiss_fft/kiss_fftr.h"
#include "wav.h"

#pragma warning(disable:4996)

#define M_PI 3.14159265358979323846f

const int period = 1024;
const int step = 100;

kiss_fftr_cfg kcfg;
float *hann_fft;
float *hann_ws1;
float *hann_ws2;

int maxthresh = 103;
int minthresh = 91;
int ws1 = 65;
int ws2 = 90;
int band0 = period/2*0/5;
int band1 = period/2*5/5;

struct Score {
	int score;
	int ofs;
};

Score *scores=0;

struct Song {
	const char *fname;
	short *isamples;
	float *samples;
	int nsamp;

	int npoints;
	float *points;	
	float *avgs1;
	//float *avgs2;
	int *deriv;

	float **amps;
	float *ampbuf;

	int *peaks;
	int npeaks;
	
	void cleanup() {
		delete isamples;
		delete samples;
		delete points;
		delete avgs1;
		//delete avgs2;
		delete deriv;
		delete ampbuf;
		delete peaks;
	}

	bool load();
	void process();
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

	samples = new float[nsamp];

	float *ptr_f = samples + nsamp;
	short *ptr_i = isamples + nsamp;

	for (int pos=nsamp; pos-- > 0;)
		*(ptr_f--) = *(ptr_i--);

	npoints = (nsamp-period-1)/step;
	points = new float[npoints];		
	avgs1 = new float[npoints];
	//avgs2 = new float[npoints];

	deriv = new int[npoints];

	amps = new float*[npoints];
	ampbuf = new float[period/2*npoints];

	peaks = new int[npoints*3];

	for (int pos=0; pos<npoints; pos++) {
		static kiss_fft_cpx fft[period/2+1];

		static float chunk[period];
		memcpy(chunk, samples+pos*step, period*sizeof(float));
		for (int p=0; p<period; p++)
			chunk[p] *= hann_fft[p];

		kiss_fftr(kcfg, chunk, fft);

		float *amp = ampbuf+period/2*pos;
		for (int f=0; f<period/2; f++)
			amp[f] = sqrt(fft[f+1].i*fft[f+1].i + fft[f+1].r*fft[f+1].r);

		amps[pos] = amp;
	}	

	return true;
}

void Song::process()
{
	for (int p=1; p<npoints; p++) {
		float power = 0;
		float *amp = amps[p];
		for (int f=band0; f<band1; f++)
			power += amp[f];
		points[p] = power;
	}

	for (int p=ws2/2; p<npoints-ws2/2; p++) {		
		float avg1 = 0;
        //float avg2 = 0;

		for (int o=-ws1/2; o<ws1/2; o++)
			avg1 += points[p+o]*hann_ws1[o+ws1/2];
		
		/*for (int o=-ws2/2; o<ws2/2; o++)
			avg2 += points[p+o]*hann_ws2[o+ws2/2];*/

		float sc1 = 0.5f*(float)(ws1-1);
        //float sc2 = 0.5f*(float)(ws2-1);
		avgs1[p] = avg1/sc1;
		//avgs2[p] = avg2/sc2;
	}

	npeaks = 0;

	for (int p=ws2/2; p<npoints-ws2/2; p++) {		
		float dl = avgs1[p] - avgs1[p-1];
		float dr = avgs1[p] - avgs1[p+1];
		if (dl*dr > 0) peaks[npeaks++] = p;

		deriv[p] = (dl>0) ? 1 : -1;
	}
}

// TODO make threading dynamic

#define THREAD_NUM 1

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
		//int start=s2.npoints-1, end=start+1;
		//int start=s2.npoints-1+-722, end=start+1;

		int a1 = ws2/2;
		int b1 = s1.npoints-ws2/2;

		for (int ofsnum=start; ofsnum < end; ofsnum++) {	
			int ofs = ofsnum - (s2.npoints-1);
					
			int a2 = ofs+ws2/2;
			int b2 = ofs+s2.npoints-ws2/2;

			int a = max(a1,a2);
			int b = min(b1,b2);

			int score = 0;
			
			/*for (int p1=0; p1<s1.npeaks-1; p1++) {
				int pp1a = s1.peaks[p1];
				int pp1b = s1.peaks[p1+1];
				if (pp1a < a || pp1a > b ||
					pp1b < a || pp1b > b)
					continue;
				
				int m = (pp1a+pp1b)/2;

				float d1 = (s1.avgs1[m] - s1.avgs1[m-1])/10000;
				float d2 = (s2.avgs1[-ofs+m] - s2.avgs1[-ofs+m-1])/10000;

				float c = d1*d2;
				score += c;
			}*/
			
			for (int p=a; p<b; p++) {
				/*float d1 = s1.avgs1[p] - s1.avgs1[p-1];
				float d2 = s2.avgs1[-ofs+p] - s2.avgs1[-ofs+p-1];
				float c = d1*d2/100000000;
				score += c;*/

				int c = s1.deriv[p]*s2.deriv[-ofs+p];
				assert(c == 1 || c == -1);
				score += c;
			}

			scores[ofsnum].ofs = ofs;
			scores[ofsnum].score = (int)score;
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

bool specpp_compare(const char *fname1, const char *fname2)
{
	//printf("=== %s %s ===\n", args[0], args[1]);

	s1.cleanup();
	s2.cleanup();
    s1.fname = fname1;
    s2.fname = fname2;
	if (!s1.load()) return false;
	if (!s2.load()) return false;

	delete scores;
	scores = new Score[s2.npoints-1 + s1.npoints-1];

	s1.process();
	s2.process();
	match();

	print_scores();
	
	return true;
}

void specpp_init()
{
	hann_fft = new float[period];
	for (int n=0; n<period; n++)
		hann_fft[n] = 0.5f * (1.0f - cos(2.0f * M_PI * n / (period-1)));

	gen_ws_hannings();

	kcfg = kiss_fftr_alloc(period, 0, 0, 0);
	
	#if THREAD_NUM>1
	init_match_workers();
    #endif
}
