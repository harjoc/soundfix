#pragma once

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

bool read_wav(const wchar_t *fname, short **data, int *len);
