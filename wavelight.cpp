#define NOMINMAX

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

#include <opencv2/opencv.hpp>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define SAMPLE_RATE 44100
#define PI 3.14159265358979323846f

#define NOTE_G4 392.00f  // Rot
#define NOTE_D4 293.66f  // Gelb
#define NOTE_E4 329.63f  // Blau

// Globale Steuerung für den Sound
float freq1 = 0.0f, freq2 = 0.0f, freq3 = 0.0f;
float phase1 = 0.0f, phase2 = 0.0f, phase3 = 0.0f;

// Parameter mit geglätteten Zielwerten (Gegen Stottern)
float currentPitchShift = 1.0f, targetPitchShift = 1.0f;
float currentVolumeGain = 0.2f, targetVolumeGain = 0.2f;
float currentPulseSpeed = 0.0f, targetPulseSpeed = 0.0f;

float pulsePhase = 0.0f;

// Audio-Callback (Wird unabhängig auf separatem High-Priority-Thread ausgeführt)
void audio_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    float* pOutputF32 = (float*)pOutput;

    for (ma_uint32 i = 0; i < frameCount; ++i) {
        if (freq1 > 0.0f) {
            // Sanftes Angleichen der Werte im Audio-Thread (Keine Ruckler/Knackser)
            currentPitchShift += (targetPitchShift - currentPitchShift) * 0.005f;
            currentVolumeGain += (targetVolumeGain - currentVolumeGain) * 0.005f;
            currentPulseSpeed += (targetPulseSpeed - currentPulseSpeed) * 0.005f;

            float pulseEnvelope = 1.0f;
            if (currentPulseSpeed > 1.0f) {
                pulsePhase += (2.0f * PI * currentPulseSpeed) / SAMPLE_RATE;
                if (pulsePhase > 2.0f * PI) pulsePhase -= 2.0f * PI;
                pulseEnvelope = (std::sin(pulsePhase) > -0.2f) ? 1.0f : 0.15f;
            }

            float currentFreq1 = freq1 * currentPitchShift;
            float currentFreq2 = freq2 * currentPitchShift;
            float currentFreq3 = freq3 * currentPitchShift;

            phase1 += (2.0f * PI * currentFreq1) / SAMPLE_RATE;
            phase2 += (2.0f * PI * currentFreq2) / SAMPLE_RATE;
            phase3 += (2.0f * PI * currentFreq3) / SAMPLE_RATE;

            if (phase1 > 2.0f * PI) phase1 -= 2.0f * PI;
            if (phase2 > 2.0f * PI) phase2 -= 2.0f * PI;
            if (phase3 > 2.0f * PI) phase3 -= 2.0f * PI;

            float sineSample = (std::sin(phase1) + std::sin(phase2) + std::sin(phase3)) / 3.0f;

            pOutputF32[i] = sineSample * currentVolumeGain * pulseEnvelope;
        }
        else {
            pOutputF32[i] = 0.0f;
        }
    }
}

void updateSoundFromPixel(uint8_t r, uint8_t g, uint8_t b) {
    cv::Mat bgrPixel(1, 1, CV_8UC3, cv::Scalar(b, g, r));
    cv::Mat hsvPixel;
    cv::cvtColor(bgrPixel, hsvPixel, cv::COLOR_BGR2HSV);

    uint8_t h = hsvPixel.at<cv::Vec3b>(0, 0)[0];
    uint8_t s = hsvPixel.at<cv::Vec3b>(0, 0)[1];
    uint8_t v = hsvPixel.at<cv::Vec3b>(0, 0)[2];

    if (s < 20) {
        if (v < 60) {
            freq1 = 185.00f; freq2 = 220.00f; freq3 = 277.18f; // Schwarz -> Fis-Moll
        }
        else if (v > 190) {
            freq1 = 440.00f; freq2 = 554.37f; freq3 = 659.25f; // Weiß -> A-Dur
        }
        else {
            freq1 = freq2 = freq3 = 220.00f;                  // Grau
        }
        return;
    }

    float octaveMult = 1.0f;
    if (v < 90)       octaveMult = 0.5f;
    else if (v > 180) octaveMult = 2.0f;

    float note = NOTE_G4;
    if ((h >= 0 && h <= 12) || (h >= 160 && h <= 180)) note = 392.00f;
    else if (h > 12 && h <= 38) note = 293.66f;
    else if (h > 38 && h <= 85) note = 440.00f;
    else if (h > 85 && h <= 140) note = 329.63f;
    else note = 349.23f;

    freq1 = freq2 = freq3 = note * octaveMult;
}

int main() {
    // 1. Audio-Hardware initialisieren mit größerem Puffer gegen Stottern
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = SAMPLE_RATE;
    deviceConfig.dataCallback = audio_callback;
    deviceConfig.periodSizeInFrames = 512; // Stabiler Puffer!

    ma_device device;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        std::cerr << "Fehler beim Initialisieren der Soundkarte!" << std::endl;
        return -1;
    }
    ma_device_start(&device);

    cv::VideoCapture cap(0, cv::CAP_ANY);
    if (!cap.isOpened()) {
        std::cerr << "Fehler: Kamera konnte nicht geöffnet werden!" << std::endl;
        return -1;
    }

    cv::Mat frame;
    const int boxSize = 10;
    const int radarZoneSize = 160;
    float previousSize = 0.0f;

    std::cout << "waveLight (Performance-Optimiert) läuft..." << std::endl;

    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        int centerX = frame.cols / 2;
        int centerY = frame.rows / 2;

        // --- SPEED-BOOST: PERFORMANCE RADAR ---
        int radarX = (std::max)(0, centerX - radarZoneSize / 2);
        int radarY = (std::max)(0, centerY - radarZoneSize / 2);
        cv::Rect radarRect(radarX, radarY, radarZoneSize, radarZoneSize);
        cv::Mat radarROI = frame(radarRect);

        // 1. Auf die Hälfte verkleinern für 4-fache Rechengeschwindigkeit!
        cv::Mat smallROI, grayROI, edgesROI;
        cv::resize(radarROI, smallROI, cv::Size(), 0.5, 0.5, cv::INTER_NEAREST);
        cv::cvtColor(smallROI, grayROI, cv::COLOR_BGR2GRAY);
        cv::Canny(grayROI, edgesROI, 50, 150);

        float currentSize = (float)cv::countNonZero(edgesROI);

        if (previousSize > 0.0f) {
            float delta = currentSize - previousSize;

            if (delta > 25.0f) {
                // Annäherung -> Ziele für den Audio-Thread setzen
                targetPitchShift = (std::min)(1.4f, targetPitchShift + 0.04f);
                targetVolumeGain = (std::min)(0.30f, targetVolumeGain + 0.02f);
                targetPulseSpeed = (std::min)(10.0f, 2.0f + (delta / 20.0f));
            }
            else if (delta < -25.0f) {
                // Entfernung
                targetPitchShift = (std::max)(0.80f, targetPitchShift - 0.03f);
                targetVolumeGain = (std::max)(0.12f, targetVolumeGain - 0.01f);
                targetPulseSpeed = 1.2f;
            }
            else {
                // Stillstand -> Sanfte Rückkehr
                targetPitchShift = targetPitchShift * 0.9f + 1.0f * 0.1f;
                targetVolumeGain = targetVolumeGain * 0.9f + 0.2f * 0.1f;
                targetPulseSpeed *= 0.7f;
            }
        }
        previousSize = currentSize;
        // ----------------------------------------------------

        // Farb-Ton aus der 10x10 Mitte-Box berechnen
        int startX = (std::max)(0, centerX - boxSize / 2);
        int startY = (std::max)(0, centerY - boxSize / 2);
        cv::Rect scanBoxRect(startX, startY, boxSize, boxSize);
        cv::Mat scanBox = frame(scanBoxRect);

        cv::Scalar avgColor = cv::mean(scanBox);
        updateSoundFromPixel((uint8_t)avgColor[2], (uint8_t)avgColor[1], (uint8_t)avgColor[0]);

        // UI Feedback
        cv::rectangle(frame, radarRect, cv::Scalar(100, 255, 100), 1);
        cv::line(frame, cv::Point(centerX - 15, centerY), cv::Point(centerX + 15, centerY), cv::Scalar(0, 255, 0), 1);
        cv::line(frame, cv::Point(centerX, centerY - 15), cv::Point(centerX, centerY + 15), cv::Scalar(0, 255, 0), 1);

        cv::Scalar boxColor = (targetPulseSpeed > 2.0f) ? cv::Scalar(0, 165, 255) : cv::Scalar(0, 0, 255);
        cv::rectangle(frame, scanBoxRect, boxColor, 2);

        cv::imshow("waveLight - Visualizer", frame);

        // Minimalste Wartezeit, um die FPS hochzuhalten
        if (cv::waitKey(1) == 'q') break;
    }

    ma_device_uninit(&device);
    cap.release();
    cv::destroyAllWindows();

    return 0;
}