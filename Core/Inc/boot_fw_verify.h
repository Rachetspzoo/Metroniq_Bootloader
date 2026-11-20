#ifndef BOOT_FW_VERIFY_H
#define BOOT_FW_VERIFY_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Offsets are taken from fw_postbuild.py and the existing DeviceName check. */
#define BOOT_FWINFO_OFFSET       0x2CCu
#define BOOT_FWINFO_SIZE         0x1Cu
#define BOOT_FWINFO_SIZE_OFFSET  0x14u
#define BOOT_FWINFO_CRC_OFFSET   0x18u
#define BOOT_FW_APP_BASE        0x08020000u

typedef enum {
    BOOT_FW_OK = 0,
    BOOT_FW_BAD_ARGUMENT,
    BOOT_FW_BAD_NAME,
    BOOT_FW_BAD_SIZE,
    BOOT_FW_SIZE_MISMATCH,
    BOOT_FW_BAD_VECTOR,
    BOOT_FW_BAD_CRC
} BootFWVerifyResult;

static inline uint32_t BootFW_ReadLE32(const volatile uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Reflected CRC-32/ISO-HDLC (zlib.crc32): poly 0xEDB88320,
   init 0xFFFFFFFF, xorout 0xFFFFFFFF. */
static inline uint32_t BootFW_CRC32Byte(uint32_t crc, uint8_t byte)
{
    static const uint32_t lut16[16] = {
        0x00000000u,0x1DB71064u,0x3B6E20C8u,0x26D930ACu,
        0x76DC4190u,0x6B6B51F4u,0x4DB26158u,0x5005713Cu,
        0xEDB88320u,0xF00F9344u,0xD6D6A3E8u,0xCB61B38Cu,
        0x9B64C2B0u,0x86D3D2D4u,0xA00AE278u,0xBDBDF21Cu
    };
    crc ^= byte;
    crc = (crc >> 4) ^ lut16[crc & 0x0Fu];
    crc = (crc >> 4) ^ lut16[crc & 0x0Fu];
    return crc;
}

/* image points to the FIRST byte of the raw application BIN in memory-mapped OSPI.
   capacity is the reserved firmware slot size, not the total OSPI capacity.
   expected_received_size: 0 if unavailable; otherwise DeviceSettings.NewFWSize.
   Call only after OSPI memory-mapped read mode has been enabled. */
static inline BootFWVerifyResult BootFW_VerifyExternal(
    const volatile uint8_t *image, uint32_t capacity,
    uint32_t expected_received_size, uint32_t *verified_size)
{
    const uint32_t info = BOOT_FWINFO_OFFSET;
    const uint32_t crc_pos = info + BOOT_FWINFO_CRC_OFFSET;
    const uint32_t min_size = info + BOOT_FWINFO_SIZE;
    uint32_t size, stored_crc, crc, sp, reset;

    if (verified_size) *verified_size = 0u;
    if (image == NULL || capacity < min_size) return BOOT_FW_BAD_ARGUMENT;

    static const uint8_t name[8] = {'M','e','t','r','o','n','i','Q'};
    for (uint32_t i = 0; i < sizeof(name); ++i)
        if (image[info + i] != name[i]) return BOOT_FW_BAD_NAME;

    size = BootFW_ReadLE32(image + info + BOOT_FWINFO_SIZE_OFFSET);
    stored_crc = BootFW_ReadLE32(image + crc_pos);
    if (size < min_size || size > capacity) return BOOT_FW_BAD_SIZE;
    if (expected_received_size != 0u && size != expected_received_size)
        return BOOT_FW_SIZE_MISMATCH;

    /* The application vector table is at the beginning of the raw BIN. */
    sp = BootFW_ReadLE32(image);
    reset = BootFW_ReadLE32(image + 4u);
    if ((sp & 7u) != 0u || sp < 0x20000000u || sp >= 0x40000000u ||
        (reset & 1u) == 0u ||
        (reset & ~1u) < BOOT_FW_APP_BASE ||
        ((uint64_t)(reset & ~1u) >= (uint64_t)BOOT_FW_APP_BASE + size))
        return BOOT_FW_BAD_VECTOR;

    crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < size; ++i) {
        /* fw_postbuild.py zeroes FWCRC bytes before zlib.crc32. */
        uint8_t b = (i >= crc_pos && i < crc_pos + 4u) ? 0u : image[i];
        crc = BootFW_CRC32Byte(crc, b);
    }
    crc ^= 0xFFFFFFFFu;
    if (crc != stored_crc) return BOOT_FW_BAD_CRC;

    if (verified_size) *verified_size = size;
    return BOOT_FW_OK;
}

#endif /* BOOT_FW_VERIFY_H */
