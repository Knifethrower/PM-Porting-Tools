/* IMA ADPCM encoder producing FMOD's FSB5 "IMAADPCM" layout (Xbox IMA):
 *   per channel and block: int16 first sample, int8 step index, 1 reserved byte, then 32 bytes
 *   of nibbles (low nibble first) for samples 1..63; the 64th nibble is padding. 64 samples/block.
 *   Mono: 36-byte blocks. Stereo: 72-byte blocks = both 4-byte headers, then alternating 4-byte
 *   chunks (8 samples) of the left and right channel.
 * Built as a plain shared library (no libc calls) and used from audio.py through ctypes.
 * MIT licence, part of the Gone Home PortMaster port. */
#include <stdint.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

static const int step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66,
    73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408,
    449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
    9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
static const int index_table[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

#define BLOCK_SAMPLES 64

/* encode one sample; the predictor follows exactly what the decoder will compute */
static int encode_nibble(int sample, int *pred, int *index) {
    int step = step_table[*index];
    int diff = sample - *pred;
    int nibble = 0, delta = step >> 3;
    if (diff < 0) { nibble = 8; diff = -diff; }
    if (diff >= step) { nibble |= 4; diff -= step; delta += step; }
    if (diff >= (step >> 1)) { nibble |= 2; diff -= step >> 1; delta += step >> 1; }
    if (diff >= (step >> 2)) { nibble |= 1; delta += step >> 2; }
    *pred += (nibble & 8) ? -delta : delta;
    if (*pred > 32767) *pred = 32767;
    if (*pred < -32768) *pred = -32768;
    *index += index_table[nibble];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;
    return nibble;
}

/* Bytes needed for `frames` sample frames of `channels` (1 or 2) channels. */
EXPORT long ima_size(long frames, int channels) {
    long blocks = (frames + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES;
    return blocks * 36L * channels;
}

/* pcm: interleaved int16 frames. state: int[4] = {pred, index} per channel, carried between
 * calls so a long clip can be encoded in pieces (frames must then be a multiple of 64 except for
 * the last piece). Returns bytes written. */
EXPORT long ima_encode(const int16_t *pcm, long frames, int channels, uint8_t *out, int *state) {
    long blocks = (frames + BLOCK_SAMPLES - 1) / BLOCK_SAMPLES, o = 0;
    for (long b = 0; b < blocks; b++) {
        long base = b * BLOCK_SAMPLES;
        uint8_t nib[2][32];
        for (int c = 0; c < channels; c++) {
            int *pred = &state[2 * c], *index = &state[2 * c + 1];
            long first = base < frames ? base : frames - 1;
            int s0 = pcm[first * channels + c];
            *pred = s0;                                     /* header sample is exact */
            uint8_t *h = out + o + 4 * c;
            h[0] = (uint8_t)(s0 & 0xff); h[1] = (uint8_t)((s0 >> 8) & 0xff);
            h[2] = (uint8_t)*index; h[3] = 0;
            for (int i = 0; i < 32; i++) nib[c][i] = 0;
            for (int i = 1; i < BLOCK_SAMPLES; i++) {
                long f = base + i;
                if (f >= frames) f = frames - 1;           /* pad the last block with its last sample */
                int n = encode_nibble(pcm[f * channels + c], pred, index);
                nib[c][(i - 1) >> 1] |= (uint8_t)(((i - 1) & 1) ? n << 4 : n);
            }
        }
        o += 4 * channels;
        if (channels == 1) {
            for (int i = 0; i < 32; i++) out[o++] = nib[0][i];
        } else {
            for (int k = 0; k < 8; k++)
                for (int c = 0; c < 2; c++)
                    for (int i = 0; i < 4; i++) out[o++] = nib[c][4 * k + i];
        }
    }
    return o;
}
