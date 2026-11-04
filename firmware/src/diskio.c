// FatFs glue for the microSD card (MCU-A).
#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "sd_spi.h"

DSTATUS disk_status(BYTE pdrv) { return pdrv || !sd_ready() ? STA_NOINIT : 0; }
DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv) return STA_NOINIT;
    if (!sd_ready()) sd_init();
    return sd_ready() ? 0 : STA_NOINIT;
}
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    return sd_read((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    return sd_write((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv || !sd_ready()) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: return sd_sync() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = sd_sectors(); return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
