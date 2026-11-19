/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ST7789-class 240x240 panel on SPI1: PC9 CLK, PC10 DO, PC7 CS, PC8 D/C,
 * backlight PA2 active low. Polled DMA transfers; every source buffer must be
 * in RAM. A pixel transfer is left running (lcd_busy): the next LCD access, or
 * a write to its source buffer (lcd_sync), waits for it, so the main loop works
 * while the last strip of a frame goes out. */
/* pins and SPI1: hal/fm1_lcd_hw.h */
#ifndef LCD_BAUD
#define LCD_BAUD 4u                /* lsb/(BAUD+1): 4 = 12 MHz */
#endif

static uint8_t lcd_small[64];
static uint8_t lcd_busy;           /* a lcd_data DMA may still run; CS is low */

static void lcd_spin(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++)
        ;
}

static void lcd_sync(void)          /* finish the running transfer (before touching its buffer) */
{
    if (!lcd_busy)
        return;
    fm1_lcd_wait();
    fm1_lcd_deselect();
    lcd_busy = 0;
}

static void lcd_cmd(uint8_t c)
{
    lcd_sync();
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}

static void lcd_data(const void *p, uint32_t n)
{
    if (!n)
        return;                    /* SPI_CNT = 0 never completes */
    lcd_sync();
    fm1_lcd_send_data(p, n);
    lcd_busy = 1;                  /* completed by lcd_sync */
}

static void lcd_window(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    lcd_sync();
    lcd_small[0] = (uint8_t)(x0 >> 8);
    lcd_small[1] = (uint8_t)x0;
    lcd_small[2] = (uint8_t)(x1 >> 8);
    lcd_small[3] = (uint8_t)x1;
    lcd_cmd(0x2A);
    lcd_data(lcd_small, 4);
    lcd_small[4] = (uint8_t)(y0 >> 8);       /* own bytes: the x transfer may still read [0..3] */
    lcd_small[5] = (uint8_t)y0;
    lcd_small[6] = (uint8_t)(y1 >> 8);
    lcd_small[7] = (uint8_t)y1;
    lcd_cmd(0x2B);
    lcd_data(lcd_small + 4, 4);
    lcd_cmd(0x2C);
}

static uint16_t lcd_fillbuf[240];

static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n, k, sw = (uint16_t)((c >> 8) | (c << 8));
    if (!w || !h || x >= 240u || y >= 240u)
        return;
    if (x + w > 240u)
        w = 240u - x;
    if (y + h > 240u)
        h = 240u - y;
    lcd_sync();
    k = 240u / w * w;                       /* whole rows per transfer (narrow fills: one DMA) */
    for (i = 0; i < k; i++)
        lcd_fillbuf[i] = (uint16_t)sw;
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    for (n = w * h; n; n -= k) {
        if (k > n)
            k = n;
        lcd_data(lcd_fillbuf, k * 2u);
    }
}

static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (!w || !h)
        return;
    lcd_window(x, y, x + w - 1u, y + h - 1u);
    lcd_data(px, w * h * 2u);
}

/* ST7789V init: reset, then 16-bit colour, scan direction and IPS inversion
 * (LCD_SEQ), then the panel's own voltages and gamma (LCD_PANEL). Format:
 * cmd, n, n data bytes; cmd 0x00 = wait (data byte: ~ms). */
static const uint8_t LCD_SEQ[] = {
    0x01, 0,                     /* SWRESET */
    0x00, 1, 150,
    0x11, 0,                     /* SLPOUT */
    0x00, 1, 120,
    0x3A, 1, 0x55,               /* COLMOD: RGB565 */
    0x36, 1, 0x00,               /* MADCTL: top-left origin, RGB order */
    0x21, 0,                     /* INVON: IPS panel */
    0x13, 0,                     /* NORON */
};

/* The panel setup of M-VAVE's own firmware: the same table in V15 and V16
 * (their app.bin, after SLPOUT; COLMOD, MADCTL and INVON as above). Left at the
 * ST7789V's reset values (VCOMS 0x20, GCTRL 0x35, VRHS 0x0B, its default gamma)
 * the panel of a 2026 FM-1 (UC25HQ80 flash) reaches full level within the first
 * 2 of 32 steps: greys turn white, the dark shades of one hue run together;
 * with them its ramps are smooth, as on the stock firmware (tested on the unit).
 * Sent on every FM-1, as the stock firmware does. */
static const uint8_t LCD_PANEL[] = {
    0xB2, 5, 0x0C, 0x0C, 0x0C, 0x00, 0x33,   /* PORCTRL: porch */
    0xB7, 1, 0x56,                           /* GCTRL: VGH / VGL */
    0xBB, 1, 0x18,                           /* VCOMS */
    0xC0, 1, 0x2C,                           /* LCMCTRL */
    0xC2, 1, 0x01,                           /* VDVVRHEN */
    0xC3, 1, 0x1F,                           /* VRHS */
    0xC4, 1, 0x20,                           /* VDVS */
    0xC6, 1, 0x0F,                           /* FRCTRL2: 60 Hz */
    0xD0, 2, 0xA6, 0xA1,                     /* PWCTRL1: AVDD, AVCL, VDS */
    0xE0, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,   /* PVGAMCTRL */
    0xE1, 14, 0xD0, 0x0D, 0x14, 0x0B, 0x0B, 0x07, 0x3A, 0x44, 0x50, 0x08, 0x13, 0x13, 0x2D, 0x32,   /* NVGAMCTRL */
};
#ifndef LCD_PANEL_ON
#define LCD_PANEL_ON 1             /* 0: the controller's reset values (SLOOP 2.8.5 and before) */
#endif

static void lcd_seq(const uint8_t *s, uint32_t n)
{
    uint32_t r, x;
    for (r = 0; r < n; r += 2u + s[r + 1u]) {
        if (s[r] == 0x00u) {                             /* pseudo command: wait */
            lcd_spin(s[r + 2u] * 25000u);                /* ~1 ms per unit (lcd_spin(3000000) ~ 120 ms) */
            continue;
        }
        lcd_cmd(s[r]);
        for (x = 0; x < s[r + 1u]; x++)
            lcd_small[x] = s[r + 2u + x];
        if (s[r + 1u])
            lcd_data(lcd_small, s[r + 1u]);
    }
}

static void lcd_init(void)
{
    fm1_lcd_hw_init();
    lcd_spin(2000000u);
    lcd_seq(LCD_SEQ, sizeof LCD_SEQ);
    if (LCD_PANEL_ON)
        lcd_seq(LCD_PANEL, sizeof LCD_PANEL);
    fm1_lcd_baud(LCD_BAUD);
    lcd_fill(0, 0, 240, 240, 0);
    lcd_cmd(0x29);
}
