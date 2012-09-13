#pragma once

typedef int (*SpecppCallback)(void *arg, const char *step, int progress);

void specpp_init();
void specpp_cleanup();

bool specpp_compare(const char *fname1, const char *fname2, SpecppCallback cb, void *cb_arg,
        int minOffsets, int maxOffsets, int minConfidence, int *retOffsets, int *offsets, float *confidences);

bool specpp_mix(int ofs, const char *fname);
