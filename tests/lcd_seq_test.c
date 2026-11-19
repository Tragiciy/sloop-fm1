/* SPDX-License-Identifier: GPL-3.0-only */
/* lcd_init's command stream (firmware/src/lcd.c) on a recording SPI: the init tables walk to their exact ends, every
 * parameter list fits lcd_small, and the panel gets M-VAVE's own setup (the table of the stock V15 / V16 firmware) after
 * COLMOD, MADCTL and INVON and before DISPON, once. Built with -DLCD_PANEL_ON=0 the stream is SLOOP 2.8.5's: no panel
 * register at all. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t nrec, ncmd;
static uint32_t baud;

/* hal/fm1_lcd_hw.h, recorded: 0x100 | cmd for a command, the byte for data */
static uint16_t ev[240u * 240u * 2u + 4096u];   /* the clear screen of lcd_init too */
static uint32_t nev;
static void fm1_lcd_hw_init(void) {}
static void fm1_lcd_baud(uint32_t b) { baud = b; }
static void fm1_lcd_wait(void) {}
static void fm1_lcd_deselect(void) {}
static void fm1_lcd_send_cmd(uint8_t c) { ev[nev++] = (uint16_t)(0x100u | c); ncmd++; }
static void fm1_lcd_send_data(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    assert(nev + n <= sizeof ev / sizeof ev[0]);
    for (uint32_t i = 0; i < n; i++)
        ev[nev++] = b[i];
    nrec += n;
}

#include "../firmware/src/lcd.c"

/* the stock table (V15 app.bin @0x4f76c, V16 app.bin @0x44b59), the registers lcd_init does not set otherwise */
static const uint8_t STOCK[] = {
    0xB2, 5, 0x0C, 0x0C, 0x0C, 0x00, 0x33,
    0xB7, 1, 0x56,
    0xBB, 1, 0x18,
    0xC0, 1, 0x2C,
    0xC2, 1, 0x01,
    0xC3, 1, 0x1F,
    0xC4, 1, 0x20,
    0xC6, 1, 0x0F,
    0xD0, 2, 0xA6, 0xA1,
    0xE0, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,
    0xE1, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,
};

static uint32_t walk(const uint8_t *s, uint32_t n)         /* the entries, as lcd_seq walks them */
{
    uint32_t r, k = 0;
    for (r = 0; r < n; r += 2u + s[r + 1u], k++) {
        assert(r + 1u < n);
        assert(s[r + 1u] <= sizeof lcd_small);
        assert(r + 2u + s[r + 1u] <= n);
    }
    assert(r == n);                                         /* ends exactly at the table's end */
    return k;
}

static int find_cmd(uint8_t c, uint32_t from)             /* index in ev of command c, -1 if none */
{
    for (uint32_t i = from; i < nev; i++)
        if (ev[i] == (0x100u | c))
            return (int)i;
    return -1;
}

int main(void)
{
    walk(LCD_SEQ, sizeof LCD_SEQ);
    assert(walk(LCD_PANEL, sizeof LCD_PANEL) == 11u);
    assert(sizeof LCD_PANEL == sizeof STOCK && !memcmp(LCD_PANEL, STOCK, sizeof STOCK));

    lcd_init();
    lcd_sync();
    assert(baud == LCD_BAUD);
    assert(ev[nev - 1u] == 0x129u);                         /* DISPON last */
    int inv = find_cmd(0x21, 0), madctl = find_cmd(0x36, 0), colmod = find_cmd(0x3A, 0), on = find_cmd(0x29, 0);
    assert(inv >= 0 && madctl >= 0 && colmod >= 0 && on == (int)nev - 1);
    assert(ev[colmod + 1] == 0x55 && ev[madctl + 1] == 0x00);
    for (uint32_t r = 0; r < sizeof STOCK; r += 2u + STOCK[r + 1u]) {
        int at = find_cmd(STOCK[r], 0);
#if LCD_PANEL_ON
        assert(at > inv && at < on);                        /* after the base setup, before the display is on */
        assert(find_cmd(STOCK[r], (uint32_t)at + 1u) < 0);  /* once */
        for (uint32_t x = 0; x < STOCK[r + 1u]; x++)
            assert(ev[at + 1 + (int)x] == STOCK[r + 2u + x]);
        assert(ev[at + 1 + STOCK[r + 1u]] & 0x100u);        /* no extra parameter byte */
#else
        assert(at < 0);
#endif
    }
    printf("lcd init: %u commands, %u data bytes, panel setup %s\n", ncmd, nrec, LCD_PANEL_ON ? "on" : "off");
    return 0;
}
