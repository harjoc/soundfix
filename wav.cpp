#include <stdio.h>

#include "wav.h"

#pragma warning(disable:4996)

struct WavHeader {
	int ChunkID;
	int ChunkSize;
	int Format;
};

struct WavFmt {
	int SubchunkID;
	int SubchunkSize;
	short AudioFormat;
	short NumChannels;
	int SampleRate;
	int ByteRate;
	short BlockAlign;
	short BitsPerSample;
};

struct WavData {
	int SubchunkID;
	int SubchunkSize;
};

bool read_wav(const char *fname, short **samples, int *len)
{
	FILE *f = NULL; 
	short *buf = NULL;

	WavHeader hdr;
	WavFmt fmt;
	WavData data;

	int line = 0;
	#define BREAK { line = __LINE__; break; }

	do {
		f = fopen(fname, "rb");
		if (!f) BREAK;

		if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr))
			BREAK;
		if (hdr.ChunkID != 0x46464952)
			BREAK;
		if (hdr.Format != 0x45564157)
			BREAK;

		if (fread(&fmt, 1, sizeof(fmt), f) != sizeof(fmt))
			BREAK;
		if (fmt.SubchunkID != 0x20746d66)
			BREAK;
		if (fmt.SubchunkSize != 16)
			BREAK;
		if (fmt.AudioFormat != 1)
			BREAK;
		if (fmt.NumChannels != 1)
			BREAK;
		if (fmt.SampleRate != 44100)
			BREAK;
		if (fmt.BitsPerSample != 16)
			BREAK;

		if (fread(&data, 1, sizeof(data), f) != sizeof(data))
			BREAK;
		if (data.SubchunkID != 0x61746164)
			BREAK;

		int count = data.SubchunkSize / 2;
		if (count <= 0 || count > 100000000)
			BREAK;
		
		buf = new short[count];
		if (!buf) BREAK;

		if (fread(buf, 1, count*2, f) != count*2)
			BREAK;

		fclose(f);
		*samples = buf;
		*len = count;
		return true;
	} while (0);

	delete buf;
	if (f) fclose(f);
	printf("%s: load failed at line %d\n", fname, line);
	return false;
}
