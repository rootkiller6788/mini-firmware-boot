#ifndef SPI_NOR_H
#define SPI_NOR_H

#include <stdbool.h>
#include <stdint.h>

#define SPI_CMD_READ    0x03
#define SPI_CMD_WREN    0x06
#define SPI_CMD_SE      0xD8
#define SPI_CMD_RDSR    0x05

#define SR_BIT_BUSY     0x01
#define SR_BIT_WEL      0x02

typedef struct {
    uint32_t jedec_id;
    uint32_t capacity;
    uint32_t page_size;
    uint8_t  status_reg;
    uint8_t *data;
} SPIFlash;

bool     spi_init(SPIFlash *flash, uint32_t jedec_id, uint32_t capacity);
uint32_t spi_read_jedec_id(const SPIFlash *flash);
bool     spi_read(const SPIFlash *flash, uint32_t addr, uint8_t *buf, uint32_t len);
bool     spi_write_enable(SPIFlash *flash);
bool     spi_sector_erase(SPIFlash *flash, uint32_t addr);
bool     spi_page_program(SPIFlash *flash, uint32_t addr, const uint8_t *buf, uint32_t len);
uint8_t  spi_read_status(const SPIFlash *flash);

#endif
