#include "ui.h"
#include "oled.h"
#include "settings.h"
#include <stdio.h>
/* 4.2V lithium voltage proxy only; no fuel gauge, OCV rest or capacity model.
 * Charging IR rise and chemistry differences can substantially bias this bar.
 * It never asserts battery presence, charge termination or measured SOC.
 */
int UI_VoltagePercent(uint32_t mv)
{
    static const uint16_t volts[] = {3000,3300,3500,3600,3700,3800,3900,4000,4100,4200};
    static const uint8_t percent[] = {0,5,10,20,35,55,70,80,90,99};
    unsigned i;
    if (mv < 2500 || mv > 4250) return -1;
    if (mv <= volts[0]) return 0;
    for (i=1;i<10;i++) if (mv <= volts[i])
        return percent[i-1]+(int)((mv-volts[i-1])*(percent[i]-percent[i-1])/(volts[i]-volts[i-1]));
    return 99; /* Voltage alone never proves 100% / termination. */
}
typedef enum { HOME, DETAIL, MENU, CURRENT, INFO } Page;
static Page page;
static unsigned selected, cursor, pending;
static uint32_t started;
static uint8_t booting, action_failed;

void UI_Init(uint32_t now)
{
    page=HOME; selected=cursor=pending=0; started=now; booting=1; action_failed=0;
}
void UI_Key(unsigned key)
{
    if (booting) { booting=0; return; } /* Skip animation, consume key. */
    action_failed=0;
    if (key==UI_BACK) {
        if (page==CURRENT || page==INFO) page=MENU;
        else if (page==MENU) page=DETAIL;
        else if (page==DETAIL) page=HOME;
        else { page=MENU; cursor=0; }
        return;
    }
    if (key==UI_UP || key==UI_DOWN) {
        unsigned delta = key==UI_UP ? 2U : 1U;
        if (page==HOME || page==DETAIL) selected=(selected+delta)%3U;
        else if (page==MENU) cursor=(cursor+delta)%3U;
        else if (page==CURRENT) pending=(pending+delta)%3U;
        return;
    }
    if (key!=UI_OK) return;
    if (page==HOME) page=DETAIL;
    else if (page==DETAIL) { page=MENU; cursor=0; }
    else if (page==MENU) {
        if (cursor==0) { pending=(unsigned)Board_GetCurrent(selected); page=CURRENT; }
        else if (cursor==1) {
            if (Settings_All100()!=BOARD_OK) action_failed=1;
            if (!action_failed) page=HOME;
        } else page=INFO;
    } else if (page==CURRENT) {
        if (Settings_SetCurrent(selected,(ChargeMode)pending)==BOARD_OK) page=DETAIL;
        else action_failed=1;
    }
}
static void header(const char *title, const char *right)
{
    OLED_Text(3,1,title,1); OLED_Text(92,1,right,1);
    OLED_Box(0,10,128,1,1);
}
static void footer(const char *text)
{
    OLED_Text(2,56,text,1); OLED_Invert(0,54,128,10);
}
static void battery(int x,int y,int w,int h,int percent,int flowing,unsigned phase)
{
    int fill, start;
    OLED_Box(x,y,w,h,0); OLED_Box(x+w,y+h/3,2,h/3,1);
    if (percent<0) { OLED_Text(x+w/2-3,y+(h-7)/2,"?",1); return; }
    fill=(w-4)*percent/100;
    if (fill>0) OLED_Box(x+2,y+2,fill,h-4,1);
    if (flowing) {
        start=x+2+(int)(phase%(unsigned)(w-4));
        OLED_Box(start,y+2,1,h-4,1);
        if (start<x+2+fill) OLED_Invert(start,y+2,1,h-4);
    }
}
static void voltage(char *text,unsigned size,uint32_t mv)
{
    snprintf(text,size,"%lu.%02luV",(unsigned long)(mv/1000),(unsigned long)((mv%1000)/10));
}
static void home(const BoardTelemetry *d,uint32_t now)
{
    unsigned i; char text[24],vin[12];
    if (d->valid) voltage(vin,sizeof(vin),d->input_mv); else snprintf(vin,sizeof(vin),"--.--V");
    header("V  I/SET MA",vin);
    for (i=0;i<3;i++) {
        int y=13+(int)i*13;
        int pct=d->valid ? UI_VoltagePercent(d->battery_mv[i]) : -1;
        OLED_Text(0,y+2,i==selected ? ">" : " ",1);
        snprintf(text,sizeof(text),"%u",i+1); OLED_Text(8,y+2,text,1);
        battery(17,y+1,17,10,pct,d->valid && pct>=0 && d->current_ma[i]>=20,now/120);
        if (d->valid) {
            snprintf(text,sizeof(text),"%lu.%02lu %4lu/%u",(unsigned long)(d->battery_mv[i]/1000),
                     (unsigned long)(d->battery_mv[i]%1000/10),(unsigned long)d->current_ma[i],Board_CurrentMA(i));
        } else snprintf(text,sizeof(text),"--.-- ----/%u",Board_CurrentMA(i));
        OLED_Text(37,y+2,text,1);
        if (i==selected) OLED_Invert(7,y,8,12);
    }
    footer(d->valid ? "V-EST OK:VIEW BK:MENU" : "ADC ERROR  BK:MENU");
}
static void detail(const BoardTelemetry *d,uint32_t now)
{
    char text[24],title[16]; int pct=d->valid ? UI_VoltagePercent(d->battery_mv[selected]) : -1;
    snprintf(title,sizeof(title),"BATTERY %u",selected+1); header(title,"DETAIL");
    battery(3,16,40,24,pct,d->valid && pct>=0 && d->current_ma[selected]>=20,now/100);
    if (pct>=0) snprintf(text,sizeof(text),"%d%%",pct); else snprintf(text,sizeof(text),"--%%");
    OLED_Text(50,14,text,2);
    if (d->valid) snprintf(text,sizeof(text),"%luMV %luMA",(unsigned long)d->battery_mv[selected],(unsigned long)d->current_ma[selected]);
    else snprintf(text,sizeof(text),"ADC DATA INVALID");
    OLED_Text(50,36,text,1);
    if (!d->valid) snprintf(text,sizeof(text),"CHECK ADC / UART");
    else if (d->input_mv<4400 || d->input_mv>5500) snprintf(text,sizeof(text),"CHECK INPUT VOLTAGE");
    else if (pct<0) snprintf(text,sizeof(text),"CHECK BATTERY VOLTAGE");
    else snprintf(text,sizeof(text),"V-EST");
    OLED_Text(3,44,text,1);
    /* Preset remains visible even when ADC/input/battery warnings are present. */
    snprintf(text,sizeof(text),"SET %uMA",Board_CurrentMA(selected));
    OLED_Text(50,28,text,1);
    footer("OK:MENU      BK:HOME");
}
static void menu(void)
{
    static const char *const items[]={"CHARGE CURRENT", "ALL TO 100MA", "ABOUT / STATUS"};
    unsigned i; char title[20]; snprintf(title,sizeof(title),"CH%u MENU",selected+1); header(title,"SETUP");
    for (i=0;i<3;i++) {
        OLED_Text(8,15+(int)i*12,items[i],1);
        if (i==cursor) OLED_Invert(3,13+(int)i*12,122,11);
    }
    footer(action_failed ? "SAVE FAILED SEE UART" : "UP/DN SELECT  OK:OPEN");
}
static void current(void)
{
    static const unsigned values[]={100,500,1000};
    unsigned i; char text[24];
    snprintf(text,sizeof(text),"CH%u CURRENT",selected+1); header(text,"LIMIT");
    for (i=0;i<3;i++) {
        snprintf(text,sizeof(text),"%4u MA %s",values[i],(unsigned)Board_GetCurrent(selected)==i ? "ACTIVE" : "");
        OLED_Text(12,15+(int)i*12,text,1);
        if (i==pending) OLED_Invert(3,13+(int)i*12,122,11);
    }
    footer(action_failed ? "SAVE FAILED SEE UART" : "OK:APPLY    BK:CANCEL");
}
static void boot(uint32_t elapsed)
{
    unsigned progress=elapsed>=1400 ? 100 : elapsed*100/1400;
    unsigned phase=elapsed/90;
    OLED_Center(4,"TRIPLE 1S",2);
    battery(14,24,25,13,(int)(phase%5)*24,0,0);
    battery(50,24,25,13,(int)((phase+1)%5)*24,0,0);
    battery(86,24,25,13,(int)((phase+2)%5)*24,0,0);
    OLED_Box(13,43,102,5,0); if (progress) OLED_Box(15,45,(int)(progress*98/100),1,1);
    OLED_Center(54,"CHARGE STATION",1);
}
void UI_Draw(const BoardTelemetry *data,uint32_t now)
{
    OLED_Clear();
    if (booting && (uint32_t)(now-started)<1500U) { boot(now-started); return; }
    booting=0;
    if (page==HOME) home(data,now);
    else if (page==DETAIL) detail(data,now);
    else if (page==MENU) menu();
    else if (page==CURRENT) current();
    else {
        header("ABOUT","1.0"); OLED_Text(3,15,"SOC: VOLTAGE ESTIMATE",1);
        OLED_Text(3,27,"NO FULL DETECT / ETA",1); OLED_Text(3,39,"4.2V LI-ION 3 SLOTS",1);
        footer("BK:MENU");
    }
}
