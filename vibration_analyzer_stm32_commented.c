/* =============================================================
 *  Real-Time Vibration Analyzer — STM32F401 DSP Node (v2)
 *  Board   : STM32F401 BlackPill (ARM Cortex-M4 @ 84 MHz)
 *  Sensor  : MPU-6050 MEMS accelerometer (I2C, addr 0x68)
 *  Output  : UART2 @ 115200 baud → ESP32 display node
 *  DSP     : CMSIS-DSP arm_rfft_fast_f32 (1024-point FFT)
 *
 *  Why STM32 for DSP in v2?
 *  In v1, the ESP32 handled sampling, FFT, and HTTP serving
 *  simultaneously on one core. CPU contention between the
 *  1024-point FFT and active HTTP requests could disrupt the
 *  analysis window. The STM32F401's ARM Cortex-M4 has a
 *  dedicated hardware FPU with DSP instructions. CMSIS-DSP's
 *  arm_rfft_fast_f32 uses those hardware instructions directly,
 *  running FFT faster and with zero network interference.
 *  ESP32 becomes a dedicated display node — one job only.
 * ============================================================= */

#include "stm32f4xx_hal.h"
#include "arm_math.h"
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef  hi2c1;
extern UART_HandleTypeDef huart2;

#define MPU6050_ADDR         (0x68 << 1)
#define MPU6050_PWR_MGMT_1   0x6B
#define MPU6050_ACCEL_XOUT_H 0x3B
#define ACCEL_SCALE          16384.0f     /* LSB/g at ±2g range */

/* ─── FFT configuration ─────────────────────────────────────── 
 *  1024 points chosen for two reasons:
 *  1. Frequency resolution = sample_rate / FFT_size.
 *     1024 pts → 0.977 Hz/bin. Finer resolution is critical
 *     for distinguishing closely spaced mechanical fault
 *     frequencies (e.g. BPFO, BPFI in bearing analysis).
 *  2. FFT requires power-of-2 sizes for the Cooley-Tukey
 *     algorithm. 1024 is optimal between resolution and
 *     computation time on Cortex-M4 hardware.
 * ──────────────────────────────────────────────────────────── */
#define SAMPLES          1024
#define SAMPLE_RATE_HZ   1000
#define FREQ_RESOLUTION  ((float)SAMPLE_RATE_HZ / (float)SAMPLES)

#define FAULT_MAG_THRESHOLD  0.1f   /* g   — empirically derived from motor testing */
#define FAULT_FREQ_THRESHOLD 5.0f   /* Hz  — healthy motor stays below 3 Hz         */

static float32_t samples[SAMPLES];
static float32_t fft_input[SAMPLES];
static float32_t fft_output[SAMPLES];
static float32_t mag_spectrum[SAMPLES / 2];
static float32_t hamming[SAMPLES];

static arm_rfft_fast_instance_f32 fft_instance;

static void MPU6050_Init(void);
static void MPU6050_ReadSamples(void);
static void GenerateHammingWindow(void);
static void RunFFT(float *dominant_freq, float *dominant_mag, uint8_t *fault);
static void SendUART(float freq, float mag, uint8_t fault);

void VibrationAnalyzer_Run(void)
{
    arm_rfft_fast_init_f32(&fft_instance, SAMPLES);
    GenerateHammingWindow();
    MPU6050_Init();

    float   dominant_freq;
    float   dominant_mag;
    uint8_t fault_flag;

    while (1)
    {
        MPU6050_ReadSamples();
        RunFFT(&dominant_freq, &dominant_mag, &fault_flag);
        SendUART(dominant_freq, dominant_mag, fault_flag);
    }
}

static void MPU6050_Init(void)
{
    uint8_t data[2] = { MPU6050_PWR_MGMT_1, 0x00 };
    HAL_I2C_Master_Transmit(&hi2c1, MPU6050_ADDR, data, 2, HAL_MAX_DELAY);
    HAL_Delay(100);
}

static void MPU6050_ReadSamples(void)
{
    uint8_t raw[2];
    int16_t ax_raw;

    for (int i = 0; i < SAMPLES; i++)
    {
        uint8_t reg = MPU6050_ACCEL_XOUT_H;
        HAL_I2C_Master_Transmit(&hi2c1, MPU6050_ADDR, &reg, 1, HAL_MAX_DELAY);
        HAL_I2C_Master_Receive (&hi2c1, MPU6050_ADDR, raw,  2, HAL_MAX_DELAY);

        ax_raw     = (int16_t)(raw[0] << 8 | raw[1]);
        samples[i] = (float32_t)ax_raw / ACCEL_SCALE;

        HAL_Delay(1);  /* 1ms interval = 1000 Hz */
    }
}

/* Hamming window: w(n) = 0.54 - 0.46 * cos(2*pi*n / (N-1))
 *
 * Why Hamming and not rectangular (no window)?
 * Taking 1024 samples abruptly truncates the signal at both ends.
 * A rectangular window assumes the signal is zero outside those
 * 1024 samples — never true for a continuously vibrating motor.
 * This abrupt cutoff creates spectral leakage: energy from the
 * dominant frequency spreads into adjacent bins, generating
 * artificial frequency components that can mimic fault signatures.
 *
 * Hamming tapers the signal smoothly to zero at both ends before
 * the FFT. This eliminates the abrupt truncation, dramatically
 * reducing leakage and producing a cleaner magnitude spectrum.
 * Pre-computed once at startup for efficiency. */
static void GenerateHammingWindow(void)
{
    for (int i = 0; i < SAMPLES; i++)
    {
        hamming[i] = 0.54f - 0.46f * cosf(2.0f * PI * i / (SAMPLES - 1));
    }
}

static void RunFFT(float *dominant_freq, float *dominant_mag, uint8_t *fault)
{
    /* Apply Hamming window — element-wise multiply signal by coefficients */
    arm_mult_f32(samples, hamming, fft_input, SAMPLES);

    /* arm_rfft_fast_f32: optimised real FFT using Cortex-M4 hardware FPU.
     * Significantly faster than software FFT on ESP32 in v1. */
    arm_rfft_fast_f32(&fft_instance, fft_input, fft_output, 0);

    /* Convert complex output to magnitude spectrum */
    arm_cmplx_mag_f32(fft_output, mag_spectrum, SAMPLES / 2);

    /* Skip bin 0 (DC offset — static gravity component) */
    float32_t peak_val = 0.0f;
    uint32_t  peak_bin = 1;
    arm_max_f32(&mag_spectrum[1], (SAMPLES / 2) - 1, &peak_val, &peak_bin);
    peak_bin += 1;

    *dominant_freq = (float)peak_bin * FREQ_RESOLUTION;
    *dominant_mag  = peak_val / (SAMPLES / 2);

    /* Fault thresholds derived from DC motor testing:
     * Healthy: freq < 3 Hz, mag < 0.005g
     * Imbalance fault: dominant spike at 50-100 Hz, mag > 0.1g
     * Either condition triggers fault flag. */
    *fault = (*dominant_mag > FAULT_MAG_THRESHOLD ||
              *dominant_freq > FAULT_FREQ_THRESHOLD) ? 1 : 0;
}

/* UART format: "FREQ:73.24,MAG:0.3841,FAULT:1\n"
 * ESP32 parses this string and updates the dashboard.
 * Transmitting pre-computed results (3 values) rather than
 * raw samples (1024 floats) keeps UART bandwidth minimal. */
static void SendUART(float freq, float mag, uint8_t fault)
{
    char buf[64];
    int  len = snprintf(buf, sizeof(buf),
                        "FREQ:%.2f,MAG:%.4f,FAULT:%d\n",
                        freq, mag, (int)fault);

    HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, HAL_MAX_DELAY);
}
