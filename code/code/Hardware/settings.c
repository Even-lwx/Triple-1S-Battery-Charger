#include "settings.h"
/* STM32G031 的擦除页为 2 KB，编程宽度为 64 位；仅允许主循环调用。
 * 每条 16 字节记录只编程一次，先写数据，最后写 CRC 和提交标志。
 * 在另一页成功提交替代记录之前，不得擦除
 * 包含最新有效记录的页面。无效或写入中断的记录槽不再复用。
 */
#define TAG 0x43530100UL /* 格式 1，低字节中打包三个 2 位模式 */
#define COMMIT 0x434F4D31UL
#define RECORD_SIZE 16U
typedef struct { uint32_t sequence, payload, crc, commit; } Record;
typedef struct { uint32_t address, sequence, packed; uint8_t found; } Latest;
static uint32_t crc_record(uint32_t seq,uint32_t payload)
{
    uint32_t crc=0xFFFFFFFFUL; unsigned i,bit;
    for (i=0;i<8;i++) {
        crc ^= (i<4 ? seq>>(8*i) : payload>>(8*(i-4))) & 255U;
        for (bit=0;bit<8;bit++) crc=(crc>>1)^((crc&1U) ? 0xEDB88320UL : 0U);
    }
    return ~crc;
}
static int valid(const Record *r)
{
    uint32_t p=r->payload; unsigned i;
    if ((p&0xFFFFFFC0UL)!=TAG || r->commit!=COMMIT || r->crc!=crc_record(r->sequence,p)) return 0;
    for (i=0;i<3;i++) if (((p>>(2*i))&3U)>CHARGE_1000MA) return 0;
    return 1;
}
static Latest scan(void)
{
    Latest last={0}; uint32_t a;
    for (a=SETTINGS_BASE;a<SETTINGS_END;a+=RECORD_SIZE) {
        const Record *r=(const Record *)a;
        if (valid(r) && (!last.found || (int32_t)(r->sequence-last.sequence)>0)) {
            last.address=a; last.sequence=r->sequence; last.packed=r->payload&63U; last.found=1;
        }
    }
    return last;
}
static int blank(uint32_t address)
{
    const uint32_t *p=(const uint32_t *)address;
    return p[0]==0xFFFFFFFFUL && p[1]==0xFFFFFFFFUL && p[2]==0xFFFFFFFFUL && p[3]==0xFFFFFFFFUL;
}
static HAL_StatusTypeDef save(uint32_t packed)
{
    Latest last=scan(); uint32_t page,addr,erase_error=0,seq=last.found ? last.sequence+1 : 1;
    HAL_StatusTypeDef s; FLASH_EraseInitTypeDef erase={0}; Record record;
    if (last.found && last.packed==packed) return HAL_OK;
    page=last.found ? last.address&~(FLASH_PAGE_SIZE-1U) : SETTINGS_BASE;
    /* 从最新记录之后开始写入，失败记录也占用一个槽位。 */
    for (addr=last.found ? last.address+RECORD_SIZE : page;addr<page+FLASH_PAGE_SIZE;addr+=RECORD_SIZE)
        if (blank(addr)) break;
    s=HAL_FLASH_Unlock(); if (s!=HAL_OK) return s;
    if (addr>=page+FLASH_PAGE_SIZE) {
        page=page==SETTINGS_BASE ? SETTINGS_BASE+FLASH_PAGE_SIZE : SETTINGS_BASE;
        erase.TypeErase=FLASH_TYPEERASE_PAGES; erase.Banks=FLASH_BANK_1;
        erase.Page=(page-FLASH_BASE)/FLASH_PAGE_SIZE; erase.NbPages=1;
        s=HAL_FLASHEx_Erase(&erase,&erase_error); addr=page;
    }
    record.sequence=seq; record.payload=TAG|packed;
    record.crc=crc_record(seq,record.payload); record.commit=COMMIT;
    if (s==HAL_OK) s=HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,addr,
                                      ((uint64_t)record.payload<<32)|record.sequence);
    if (s==HAL_OK) s=HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,addr+8,
                                      ((uint64_t)record.commit<<32)|record.crc);
    if (s==HAL_OK && (!valid((const Record *)addr) || ((const Record *)addr)->sequence!=seq)) s=HAL_ERROR;
    if (HAL_FLASH_Lock()!=HAL_OK) s=HAL_ERROR;
    Board_Log("[FLASH] SAVE %s code=%u hal=0x%lX page_error=%lu\r\n",s==HAL_OK ? "OK" : "FAIL",(unsigned)s,
              (unsigned long)HAL_FLASH_GetError(),(unsigned long)erase_error);
    return s;
}
HAL_StatusTypeDef Settings_Restore(void)
{
    Latest last=scan(); unsigned i; HAL_StatusTypeDef s=HAL_OK;
    Board_Log("[FLASH] LOAD %s sequence=%lu\r\n",last.found ? "VALID" : "DEFAULT 100mA (empty/invalid)",
              (unsigned long)last.sequence);
    for (i=0;i<3;i++)
        if (Board_SetCurrent(i,last.found ? (ChargeMode)((last.packed>>(2*i))&3U) : CHARGE_100MA)!=BOARD_OK) s=HAL_ERROR;
    Board_InitResult("FLASH SETTINGS RESTORE",s);
    return s;
}
static BoardResult apply(uint32_t packed)
{
    unsigned i;
    /* 先保存：保存失败时保持 GPIO 和设置不变，并停留在编辑界面。 */
    if (save(packed)!=HAL_OK) return BOARD_ERROR;
    for (i=0;i<3;i++) if (Board_SetCurrent(i,(ChargeMode)((packed>>(2*i))&3U))!=BOARD_OK) return BOARD_ERROR;
    return BOARD_OK;
}
BoardResult Settings_SetCurrent(unsigned channel,ChargeMode mode)
{
    uint32_t packed=0; unsigned i;
    if (channel>=3 || (unsigned)mode>CHARGE_1000MA) return BOARD_ERROR;
    for (i=0;i<3;i++) packed|=(uint32_t)(i==channel ? mode : Board_GetCurrent(i))<<(2*i);
    return apply(packed);
}
BoardResult Settings_All100(void) { return apply(0); }
