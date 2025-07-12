#include "my_pio.pio.h"
#include "pico/stdlib.h"

#include "RKGReader.hpp"

constexpr uint32_t GC_DATA_PIN = 28;

constexpr uint32_t MICROSECONDS_IN_SECOND = 1000000;
constexpr float FRAME_RATE = 59.939469f;
constexpr float MICROSECONDS_PER_FRAME = MICROSECONDS_IN_SECOND / FRAME_RATE;

// binary ghost data included from file.S
extern uint8_t g_rkg[];

pio_sm_config g_config;
uint32_t g_offset;

RKGReader g_rkgReader = RKGReader(g_rkg);
uint32_t g_frame;
uint64_t g_time;

// PIO Shifts to the right by default
// In: pushes batches of 8 shifted left, i.e we get [0x40, 0x03, rumble (the end bit is never
// pushed)] Out: We push commands for a right shift with an enable pin, ie 5 (101) would be
// 0b11'10'11 So in doesn't need post processing but out does
static void convertToPio(const uint8_t *command, const uint32_t len, uint32_t *result,
        uint32_t &resultLen) {
    if (len == 0) {
        resultLen = 0;
        return;
    }

    resultLen = len / 2 + 1;

    for (uint32_t i = 0; i < resultLen; i++) {
        result[i] = 0;
    }
    for (uint32_t i = 0; i < len; i++) {
        for (uint32_t j = 0; j < 8; j++) {
            result[i / 2] += 1 << (2 * (8 * (i % 2) + j) + 1);
            result[i / 2] += (!!(command[i] & (0x80u >> j))) << (2 * (8 * (i % 2) + j));
        }
    }
    // End bit
    result[len / 2] += 3 << (2 * (8 * (len % 2)));
}

static GCPadStatus GetGCPadStatus() {
    if (g_time == 0) {
        g_frame = 0;
        g_time = time_us_64();
    } else {
        uint64_t timeDiff = time_us_64() - g_time;
        g_frame = static_cast<uint32_t>(timeDiff / MICROSECONDS_PER_FRAME);
    }

    return g_rkgReader.CalcFrame(g_frame);
}

static void sendData(uint32_t *result, uint32_t resultLen) {
    pio_sm_set_enabled(pio0, 0, false);
    pio_sm_init(pio0, 0, g_offset + save_offset_outmode, &g_config);
    pio_sm_set_enabled(pio0, 0, true);

    for (uint32_t i = 0; i < resultLen; i++) {
        pio_sm_put_blocking(pio0, 0, result[i]);
    }
}

static void probe() {
    uint8_t probeResponse[3] = {0x09, 0x00, 0x03};
    uint32_t result[2];
    uint32_t resultLen;
    convertToPio(probeResponse, 3, result, resultLen);
    sleep_us(6); // 3.75us into the bit before end bit => 6.25 to wait if the end-bit is 5us long

    sendData(result, resultLen);
}

static void origin() {
    uint8_t originResponse[10] = {0x00, 0x80, 128, 128, 128, 128, 0, 0, 0, 0};
    uint32_t result[6];
    uint32_t resultLen;
    convertToPio(originResponse, 10, result, resultLen);
    // Here we don't wait because convertToPio takes time

    sendData(result, resultLen);
}

static void poll() {
    uint8_t buffer;
    buffer = pio_sm_get_blocking(pio0, 0);
    buffer = pio_sm_get_blocking(pio0, 0);

    GCPadStatus pad = GetGCPadStatus();

    uint32_t result[5];
    uint32_t resultLen;
    convertToPio(reinterpret_cast<uint8_t *>(&pad), sizeof(GCPadStatus), result, resultLen);

    sendData(result, resultLen);
}

static void fail() {
    pio_sm_set_enabled(pio0, 0, false);
    sleep_us(400);
    pio_sm_init(pio0, 0, g_offset + save_offset_inmode, &g_config);
    pio_sm_set_enabled(pio0, 0, true);
}

static void init() {
    g_frame = 0;
    g_time = 0;

    gpio_init(GC_DATA_PIN);
    gpio_set_dir(GC_DATA_PIN, GPIO_IN);
    gpio_pull_up(GC_DATA_PIN);

    sleep_us(100); // Stabilize voltages

    pio_gpio_init(pio0, GC_DATA_PIN);
    g_offset = pio_add_program(pio0, &save_program);

    g_config = save_program_get_default_config(g_offset);
    sm_config_set_in_pins(&g_config, GC_DATA_PIN);
    sm_config_set_out_pins(&g_config, GC_DATA_PIN, 1);
    sm_config_set_set_pins(&g_config, GC_DATA_PIN, 1);
    sm_config_set_clkdiv(&g_config, 5);
    sm_config_set_out_shift(&g_config, true, false, 32);
    sm_config_set_in_shift(&g_config, false, true, 8);

    pio_sm_init(pio0, 0, g_offset, &g_config);
    pio_sm_set_enabled(pio0, 0, true);
}

int main() {
    init();

    while (true) {
        uint8_t buffer = pio_sm_get_blocking(pio0, 0);
        switch (buffer) {
        case 0x00:
            probe();
            break;
        case 0x41:
            origin();
            break;
        case 0x40:
            poll();
            break;
        default:
            fail();
        }
    }
}
