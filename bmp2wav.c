#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#define PI 3.14159265358979323846
#define SAMPLE_RATE 44100

// Frequenzen für die Grundnoten (in Hz)
#define NOTE_G4 392.00f  // ROT
#define NOTE_E4 329.63f  // BLAU
#define NOTE_D4 293.66f  // GELB

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType; uint32_t bfSize; uint16_t bfReserved1; uint16_t bfReserved2;
    uint32_t bfOffBits; uint32_t biSize; int32_t biWidth; int32_t biHeight;
    uint16_t biPlanes; uint16_t biBitCount; uint32_t biCompression;
    uint32_t biSizeImage; int32_t biXPelsPerMeter; int32_t biYPelsPerMeter;
    uint32_t biClrUsed; uint32_t biClrImportant;
} BMPHeader;

typedef struct {
    char riffChunkID[4]; uint32_t chunkSize; char format[4];
    char fmtChunkID[4]; uint32_t fmtChunkSize; uint16_t audioFormat;
    uint16_t numChannels; uint32_t sampleRate; uint32_t byteRate;
    uint16_t blockAlign; uint16_t bitsPerSample;
    char dataChunkID[4]; uint32_t dataChunkSize;
} WAVHeader;
#pragma pack(pop)

// Rechnet RGB-Pixel in eine Frequenz basierend auf Rot (G), Gelb (D), Blau (E) um
float rgbToFrequency(uint8_t r, uint8_t g, uint8_t b) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    // Einfache Gewichtung der Grundnoten basierend auf Farbanteilen
    float totalWeight = rf + gf + bf;
    if (totalWeight < 0.05f) return 0.0f; // Schwarz / Dunkel -> Stumm

    // Rot dominiert -> Richtung G4
    // Gelb (Rot + Grün) -> Richtung D4
    // Blau dominiert -> Richtung E4
    float freq = (rf * NOTE_G4 + gf * NOTE_D4 + bf * NOTE_E4) / totalWeight;
    return freq;
}

int main() {
    FILE* f_bmp = fopen("bild.bmp", "rb");
    if (!f_bmp) return 1;

    BMPHeader bmp;
    fread(&bmp, sizeof(BMPHeader), 1, f_bmp);

    int width = bmp.biWidth;
    int height = abs(bmp.biHeight);
    int rowPadding = (4 - (width * 3) % 4) % 4;

    float lineDuration = 0.05f; // 50ms pro Zeile
    int samplesPerLine = (int)(SAMPLE_RATE * lineDuration);
    int totalSamples = samplesPerLine * height;
    uint32_t dataSize = totalSamples * sizeof(int16_t);

    WAVHeader wav = {
        .riffChunkID = {'R','I','F','F'}, .chunkSize = 36 + dataSize, .format = {'W','A','V','E'},
        .fmtChunkID = {'f','m','t',' '}, .fmtChunkSize = 16, .audioFormat = 1,
        .numChannels = 1, .sampleRate = SAMPLE_RATE, .byteRate = SAMPLE_RATE * 2,
        .blockAlign = 2, .bitsPerSample = 16, .dataChunkID = {'d','a','t','a'}, .dataChunkSize = dataSize
    };

    FILE* f_wav = fopen("ausgabe.wav", "wb");
    fwrite(&wav, sizeof(WAVHeader), 1, f_wav);

    float* phases = (float*)calloc(width, sizeof(float));

    for (int y = height - 1; y >= 0; y--) { // Top-Down
        fseek(f_bmp, bmp.bfOffBits + y * (width * 3 + rowPadding), SEEK_SET);
        uint8_t* row = (uint8_t*)malloc(width * 3);
        fread(row, 3, width, f_bmp);

        for (int s = 0; s < samplesPerLine; s++) {
            float sampleValue = 0.0f;
            int activePixels = 0;

            for (int x = 0; x < width; x++) {
                uint8_t b = row[x * 3];
                uint8_t g = row[x * 3 + 1];
                uint8_t r = row[x * 3 + 2];

                // Nur zeichnen/vertonen, wenn die Linie nicht weiß ist
                if (r < 240 || g < 240 || b < 240) {
                    float freq = rgbToFrequency(r, g, b);

                    if (freq > 0.0f) {
                        phases[x] += (2.0f * PI * freq) / SAMPLE_RATE;
                        if (phases[x] > 2.0f * PI) phases[x] -= 2.0f * PI;

                        sampleValue += sinf(phases[x]);
                        activePixels++;
                    }
                }
            }

            if (activePixels > 0) sampleValue /= sqrtf((float)activePixels);

            sampleValue *= 8000.0f;
            if (sampleValue > 32767.0f) sampleValue = 32767.0f;
            if (sampleValue < -32768.0f) sampleValue = -32768.0f;

            int16_t pcmSample = (int16_t)sampleValue;
            fwrite(&pcmSample, sizeof(int16_t), 1, f_wav);
        }
        free(row);
    }

    free(phases);
    fclose(f_bmp);
    fclose(f_wav);
    return 0;
}