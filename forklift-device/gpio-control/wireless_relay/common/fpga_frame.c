#include "fpga_frame.h"

uint8_t FpgaFrame_IsValid(const uint8_t frame[FPGA_FRAME_LEN])
{
    uint8_t checksum;

    if (frame[0] != FPGA_FRAME_HEADER) {
        return 0;
    }

    checksum = frame[0] ^ frame[1] ^ frame[2] ^ frame[3];
    return (checksum == frame[4]) ? 1 : 0;
}

uint8_t FpgaFrame_CutoffActive(const uint8_t frame[FPGA_FRAME_LEN])
{
    return (frame[3] & FPGA_FRAME_CUTOFF_BIT) ? 1 : 0;
}
