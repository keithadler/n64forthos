/* pak.c -- the Controller Pak: 32 KiB of battery-backed SRAM that plugs into
 * the back of a controller, and the only writable storage a stock N64 has.
 *
 * It is reached over the same joybus as the buttons.  A read asks for 32
 * bytes at an address and gets them back with a CRC; a write sends 32 bytes
 * and gets the CRC of what arrived.  The address carries a five-bit CRC of
 * its own in its low bits, which is why every transfer is 32 bytes on a
 * 32-byte boundary.  Both checks are done here, so a loose pak or a bad
 * contact is an error rather than a corrupt file.
 *
 * Only the controller on channel 1 is used.  Paks with more than one bank
 * (third-party 1 MiB ones) are treated as the standard 32 KiB.
 */
#include "n64.h"

#define CMD_PAK_READ   0x02
#define CMD_PAK_WRITE  0x03

/* The five-bit CRC the controller expects in the bottom of the address. */
static u16 address_crc(u16 addr)
{
    static const u8 table[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x1F,
                                  0x0B, 0x16, 0x19, 0x07, 0x0E, 0x1C, 0x0D,
                                  0x1A, 0x01 };
    u16 crc = 0;
    int i;

    addr &= (u16)~0x1F;
    for (i = 15; i >= 5; i--)
        if ((addr >> i) & 1)
            crc ^= table[i];
    return (u16)(addr | (crc & 0x1F));
}

/* CRC-8, polynomial 0x85, over the 32 data bytes and one of zeroes. */
u8 pak_data_crc(const u8 *data)
{
    u8 crc = 0;
    int i, j;

    for (i = 0; i <= 32; i++)
        for (j = 7; j >= 0; j--) {
            u8 top = (u8)(crc & 0x80);

            crc = (u8)(crc << 1);
            if (i < 32 && (data[i] >> j) & 1)
                crc |= 1;
            if (top)
                crc ^= 0x85;
        }
    return crc;
}

int pak_present(void)
{
    return input_accessory(0);
}

/* Lay out a single transfer for channel 1 and run it.  Returns the offset
 * of the reply in the block. */
static int transfer(u8 op, u16 addr, const u8 *data)
{
    u8 *p = input_block();
    int i, at = 0;
    u16 a = address_crc(addr);

    for (i = 0; i < 64; i++)
        p[i] = 0;
    if (op == CMD_PAK_WRITE) {
        p[at++] = 35;                   /* command, address, 32 bytes */
        p[at++] = 1;                    /* the CRC comes back */
    } else {
        p[at++] = 3;
        p[at++] = 33;                   /* 32 bytes and their CRC */
    }
    p[at++] = op;
    p[at++] = (u8)(a >> 8);
    p[at++] = (u8)a;
    if (op == CMD_PAK_WRITE)
        for (i = 0; i < 32; i++)
            p[at++] = data[i];
    i = at;
    at += (op == CMD_PAK_WRITE) ? 1 : 33;
    p[at++] = 0xFE;
    p[63] = 1;
    input_exchange();
    if (p[1] & 0xC0)
        return -1;                      /* nothing answered on channel 1 */
    return i;
}

int pak_read(u16 addr, u8 *out)
{
    int r = transfer(CMD_PAK_READ, addr, 0), i;
    u8 *p = input_block();

    if (r < 0)
        return -1;
    for (i = 0; i < 32; i++)
        out[i] = p[r + i];
    return pak_data_crc(out) == p[r + 32] ? 0 : -2;
}

int pak_write(u16 addr, const u8 *data)
{
    int r = transfer(CMD_PAK_WRITE, addr, data);

    if (r < 0)
        return -1;
    return pak_data_crc(data) == input_block()[r] ? 0 : -2;
}
