#include "oled.h"
#include "spi.h"
#include <string.h>
static uint8_t buffer[1024];
/* 5x7 uppercase ASCII glyphs (digits, A-Z, -, :, .). */
static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-:.%/+<>?";
static const uint8_t glyphs[][5] = {
 {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},
 {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
 {54,73,73,73,54},{6,73,73,41,30},
 {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
 {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
 {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
 {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
 {62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
 {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
 {7,8,112,8,7},{97,81,73,69,67},{8,8,8,8,8},{0,54,54,0,0},{0,96,96,0,0},
 {99,19,8,100,99},{32,16,8,4,2},{8,8,62,8,8},{8,20,34,65,0},
 {0,65,34,20,8},{2,1,81,9,6}
};
static HAL_StatusTypeDef send(int data, const uint8_t *bytes, uint16_t size)
{
    HAL_StatusTypeDef s;
    HAL_GPIO_WritePin(GPIOD, OLED_DC_PIN, data ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, OLED_CS_PIN, GPIO_PIN_RESET);
    s = HAL_SPI_Transmit(&hspi1, (uint8_t *)bytes, size, 50);
    HAL_GPIO_WritePin(GPIOD, OLED_CS_PIN, GPIO_PIN_SET);
    return s;
}
void OLED_Clear(void) { memset(buffer, 0, sizeof(buffer)); }
void OLED_Pixel(int x, int y, int on)
{
    uint8_t mask;
    if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
    mask = (uint8_t)(1U << (y & 7));
    if (on) buffer[(y/8)*128+x] |= mask;
    else buffer[(y/8)*128+x] &= (uint8_t)~mask;
}
void OLED_Box(int x, int y, int width, int height, int filled)
{
    int i,j;
    /* Bound loop costs even if caller supplies invalid coordinates. */
    if (width <= 0 || height <= 0 || width > 128 || height > 64) return;
    for (j=0;j<height;j++) for (i=0;i<width;i++)
        if (filled || i==0 || j==0 || i==width-1 || j==height-1) OLED_Pixel(x+i,y+j,1);
}
void OLED_Invert(int x, int y, int width, int height)
{
    int i,j;
    if (width <= 0 || height <= 0 || width > 128 || height > 64) return;
    for (j=0;j<height;j++) for (i=0;i<width;i++)
        if (x+i>=0 && x+i<128 && y+j>=0 && y+j<64)
            buffer[((y+j)/8)*128+x+i] ^= (uint8_t)(1U<<((y+j)&7));
}
void OLED_Text(int x, int y, const char *text, unsigned scale)
{
    const char *p; unsigned col,row,dx,dy;
    if (scale < 1 || scale > 2) return;
    while (*text && x<128) {
        p = strchr(alphabet,*text++);
        if (p) for (col=0;col<5;col++) for (row=0;row<7;row++)
            if (glyphs[p-alphabet][col] & (1U<<row))
                for (dy=0;dy<scale;dy++) for (dx=0;dx<scale;dx++)
                    OLED_Pixel(x+(int)(col*scale+dx),y+(int)(row*scale+dy),1);
        x += (int)(6*scale);
    }
}
void OLED_Center(int y, const char *text, unsigned scale)
{
    int width = (int)(strlen(text)*6*scale);
    OLED_Text((128-width)/2,y,text,scale);
}
void OLED_Line(unsigned page, const char *text)
{
    unsigned x = 0; const char *p;
    if (page >= 8) return;
    memset(buffer+page*128, 0, 128);
    while (*text && x+5 < 128) {
        p = strchr(alphabet, *text++);
        if (p) memcpy(buffer+page*128+x, glyphs[p-alphabet], 5);
        x += 6;
    }
}
HAL_StatusTypeDef OLED_Update(void)
{
    unsigned page; HAL_StatusTypeDef s;
    for (page = 0; page < 8; page++) {
        uint8_t command[3] = {(uint8_t)(0xB0+page), OLED_SH1106 ? 0x02 : 0x00, 0x10};
        s = send(0, command, 3); if (s != HAL_OK) return s;
        s = send(1, buffer+128*page, 128); if (s != HAL_OK) return s;
    }
    return HAL_OK;
}
HAL_StatusTypeDef OLED_Init(void)
{
    /* SSD1306 Rev1.1 sections9/10; SPI mode0,1MHz,128x64,page addressing.
     * Transfer success is NOT a display-presence or controller-identity test.
     */
    static const uint8_t commands[] = {0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,
#if OLED_SH1106
        0xAD,0x8B,
#else
        0x8D,0x14,0x20,0x02,
#endif
        0xA1,0xC8,0xDA,0x12,0x81,0x7F,0xD9,0xF1,0xDB,0x40,0xA4,0xA6};
    uint8_t on = 0xAF; HAL_StatusTypeDef s;
    HAL_GPIO_WritePin(GPIOD, OLED_RESET_PIN, GPIO_PIN_RESET); HAL_Delay(10);
    HAL_GPIO_WritePin(GPIOD, OLED_RESET_PIN, GPIO_PIN_SET); HAL_Delay(100);
    s = send(0, commands, sizeof(commands)); if (s != HAL_OK) return s;
    OLED_Clear(); OLED_Line(0,"TRIPLE 1S CHARGER"); OLED_Line(2,"STARTING ADC...");
    OLED_Line(6,"ISET2 LIMIT 100MA"); OLED_Line(7,"SSD1306 128X64");
    s = OLED_Update(); if (s != HAL_OK) return s;
    return send(0, &on, 1);
}
