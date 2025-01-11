#ifndef litepcie_ext_h
#define litepcie_ext_h

#include <stdbool.h>

#include "csr.h"

#define CSR_TO_OFFSET(addr) ((addr)-CSR_BASE)

enum LitePCIeMessageType {
    LITEPCIE_CONFIG_DMA_READER_CHANNEL,
    LITEPCIE_CONFIG_DMA_WRITER_CHANNEL,
    LITEPCIE_READ_CSR,
    LITEPCIE_WRITE_CSR,
    LITEPCIE_ICAP,
    LITEPCIE_FLASH,
	LITEPCIE_CONFIG_DMA,
	LITEPCIE_CONFIG_DMA_LOCK
};

enum LitePCIeMemoryType {
    LITEPCIE_DMA_READER = 0x00010000,
    LITEPCIE_DMA_WRITER = 0x00020000,
    LITEPCIE_DMA_COUNTS = 0x00040000,
};

#define LITEPCIE_DMA_MEMORY(type, dma_channel) ((uint64_t)(type & dma_channel))

typedef struct DMACounts {
    uint64_t hwReaderCountTotal;
    uint64_t hwReaderCountPrev;
    uint64_t hwReaderLost;
    uint64_t hwWriterCountTotal;
    uint64_t hwWriterCountPrev;
    uint64_t hwWriterLost;
} __attribute__((packed)) DMACounts;

typedef struct LitePCIeConfigDmaChannelData {
    uint32_t enable;
    uint32_t channel;
	int64_t hw_count;
	int64_t sw_count;
	int64_t lost_count;
} __attribute__((packed)) LitePCIeConfigDmaChannelData;

typedef struct LitePCIeFlashCallData {
    uint32_t tx_len; /* 8 to 40 */
    uint64_t tx_data; /* 8 to 40 bits */
    uint64_t rx_data; /* 40 bits */
} __attribute__((packed)) LitePCIeFlashCallData;

typedef struct LitePCIeICAPCallData {
    uint8_t addr;
    uint32_t data;
} __attribute__((packed)) LitePCIeICAPCallData;

typedef struct LitePCIeDmaLoopbackData {
    uint8_t loop_en;
    uint8_t channel;
} __attribute__((packed)) LitePCIeDmaLoopbackData;

typedef struct LitePCIeDmaLockData {
	uint8_t dma_reader_request;
	uint8_t dma_writer_request;
	uint8_t dma_reader_release;
	uint8_t dma_writer_release;
	uint8_t dma_reader_status;
	uint8_t dma_writer_status;
} __attribute__((packed)) LitePCIeDmaLockData;
#endif /* litepcie_ext_h */
