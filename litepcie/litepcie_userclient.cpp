#include <os/log.h>

#include <DriverKit/IOLib.h>
#include <DriverKit/IOMemoryMap.h>
#include <DriverKit/IOMemoryDescriptor.h>
#include <DriverKit/IOTimerDispatchSource.h>
#include <DriverKit/IOUserClient.h>
#include <DriverKit/IOUserServer.h>
#include <DriverKit/OSData.h>

#include <PCIDriverKit/PCIDriverKit.h>

#include "config.h"
#include "litepcie.h"
#include "litepcie_int.h"
#include "litepcie_ext.h"
#include "litepcie_userclient.h"

#define Log(fmt, ...) os_log(OS_LOG_DEFAULT, "litepcie_userclient::%s - " fmt "\n", __FUNCTION__, ##__VA_ARGS__)

struct litepcie_userclient_IVars {
    litepcie* litepcie = nullptr;
    IOBufferMemoryDescriptor* rdma[16] = {nullptr};
    IOBufferMemoryDescriptor* wdma[16] = {nullptr};
    IOBufferMemoryDescriptor* cdma[16] = {nullptr};
    uint8_t dmaWriterLocks[DMA_CHANNEL_COUNT] = {0};
    uint8_t dmaReaderLocks[DMA_CHANNEL_COUNT] = {0};
};

bool litepcie_userclient::init(void)
{
    bool result = false;

    Log("entered");

    result = super::init();
    if (result != true) {
        Log("super::init failed.");
        goto Exit;
    }

    ivars = IONewZero(litepcie_userclient_IVars, 1);
    if (ivars == nullptr) {
        Log("failed to allocate memory for ivars");
        goto Exit;
    }

    Log("finished.");
    return true;

Exit:
    return false;
}

kern_return_t
IMPL(litepcie_userclient, Start)
{
    kern_return_t ret = kIOReturnSuccess;

    Log("entered");

    ret = super::Start(provider, SUPERDISPATCH);
    if (ret != kIOReturnSuccess) {
        Log("super::Start failed with error: 0x%08x", ret);
        goto Exit;
    }

    // try to cast the provider object to a PCI device because thats what it should be
    ivars->litepcie = OSDynamicCast(litepcie, provider);
    if (ivars->litepcie == NULL) {
        Log("failed to cast provider litepcie driver");
        ret = kIOReturnNoDevice;
        goto Exit;
    }

Exit:
    Log("finished");
    return ret;
}

kern_return_t
IMPL(litepcie_userclient, Stop)
{
    kern_return_t ret = kIOReturnSuccess;

    Log("entered");
    
    for (int i = 0; i < 16; i += 1) {
        if (ivars->rdma[i] != nullptr) {
            ivars->rdma[i]->release();
        }

        if (ivars->wdma[i] != nullptr) {
            ivars->wdma[i]->release();
        }
        
        if (ivars->cdma[i] != nullptr) {
            ivars->cdma[i]->release();
        }
    }

    Log("finished");

    return ret;
}

void litepcie_userclient::free(void)
{
    Log("free() entered");

    IOSafeDeleteNULL(ivars, litepcie_userclient_IVars, 1);

    super::free();

    Log("free() finished");
}

kern_return_t litepcie_userclient::ExternalMethod(uint64_t selector, IOUserClientMethodArguments* arguments, const IOUserClientMethodDispatch* dispatch, OSObject* target, void* reference)
{
    kern_return_t ret = kIOReturnSuccess;
    Log("ExternalMethod() entered");
    Log("ExternalMethod() selector: %lli", selector);

    switch (selector) {
    case LITEPCIE_CONFIG_DMA_READER_CHANNEL: {
        ret = HandleConfigDmaChannel(arguments, true);
    } break;
    case LITEPCIE_CONFIG_DMA_WRITER_CHANNEL: {
        ret = HandleConfigDmaChannel(arguments, false);
    } break;
    case LITEPCIE_READ_CSR: {
        ret = HandleReadCSR(arguments);
    } break;
    case LITEPCIE_WRITE_CSR: {
        ret = HandleWriteCSR(arguments);
    } break;
    case LITEPCIE_ICAP: {
        ret = HandleICAP(arguments);
    } break;
    case LITEPCIE_FLASH: {
        ret = HandleFlash(arguments);
    } break;
    case LITEPCIE_CONFIG_DMA: {
        ret = HandleDMA(arguments);
    } break;
    case LITEPCIE_CONFIG_DMA_LOCK: {
        ret = HandleDMALock(arguments);
    } break;
    case LITEPCIE_DMA_READ: {
        ret = HandleDMARead(arguments);
    } break;
    case LITEPCIE_DMA_WRITE: {
        ret = HandleDMAWrite(arguments);
    } break;

    default:
        break;
    }

Exit:
    Log("ExternalMethod() finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleConfigDmaChannel(IOUserClientMethodArguments* arguments, bool is_reader)
{
    Log("entered DMA Config");
    kern_return_t ret = kIOReturnSuccess;

    LitePCIeConfigDmaChannelData* input;
    LitePCIeConfigDmaChannelData output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeConfigDmaChannelData*)arguments->structureInput->getBytesNoCopy();
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (input == nullptr) {
        Log("input struct was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    output.channel = input->channel;
    output.enable = input->enable;
    output.sw_count = input->sw_count;
    output.lost_count = 0;
    
    if (is_reader) {
        if (ivars->litepcie->IsDMAReaderChannelEnabled(input->channel) != input->enable) {
            if (input->enable){
                ivars->litepcie->SetupDMAReaderChannel(input->channel, input->interrupt_count);
                ivars->litepcie->StartDMAReaderChannel(input->channel, true);
            } else {
                ivars->litepcie->StopDMAReaderChannel(input->channel);
            }
            output.sw_count = 0;
        }
        output.hw_count = ivars->litepcie->GetDmaReaderCount(input->channel);
    } else {
        if (ivars->litepcie->IsDMAWriterChannelEnabled(input->channel) != input->enable) {
            if (input->enable){
                ivars->litepcie->SetupDMAWriterChannel(input->channel, input->interrupt_count);
                ivars->litepcie->StartDMAWriterChannel(input->channel, true);
            } else {
                ivars->litepcie->StopDMAWriterChannel(input->channel);
            }
            output.sw_count = 0;
        }
        output.hw_count = ivars->litepcie->GetDmaWriterCount(input->channel);
    }

    // send our output out using osdata
    if(arguments->structureOutputDescriptor == nullptr)
    {
        arguments->structureOutput = OSData::withBytes(&output, sizeof(LitePCIeConfigDmaChannelData));
        Log("DMA Struct Output with OSData, counts %llu", output.hw_count);
    }
    else if(arguments->structureOutputMaximumSize < sizeof(LitePCIeConfigDmaChannelData))
    {
        Log("Invalid DMA Config output data size");
    }
    else
    {
        IOMemoryMap* outMap;
        arguments->structureOutputDescriptor->CreateMapping(0, 0, 0, 0, 0, &outMap);
        memcpy((uint8_t*)outMap->GetAddress(), &output, sizeof(LitePCIeConfigDmaChannelData));
        OSSafeReleaseNULL(outMap);
        Log("DMA Struct Output with Descriptor, counts %llu", output.hw_count);
    }

Exit:
    Log("finished %d", ret);
    return ret;
}

kern_return_t litepcie_userclient::HandleFlash(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;

    LitePCIeFlashCallData* input;
    LitePCIeFlashCallData output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeFlashCallData*)arguments->structureInput->getBytesNoCopy();
        output.tx_len = input->tx_len;
        output.tx_data = input->tx_data;
        output.rx_data = input->rx_data;
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (input == nullptr) {
        Log("input struct was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }
    
    if (input->tx_len < 8 || input->tx_len > 40) {
        Log("tx_len not >= 8 or <= 40");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

#ifdef CSR_FLASH_SPI_MOSI_ADDR
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MOSI_ADDR), input->tx_data >> 32);
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MOSI_ADDR) + 4, (uint32_t)(input->tx_data & 0xFF'FF'FF'FF));
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_CONTROL_ADDR), SPI_CTRL_START | (input->tx_len * SPI_CTRL_LENGTH));
    IODelay(16);
    for (int i = 0; i < SPI_TIMEOUT; i += 1) {
        uint32_t val;
        ivars->litepcie->ReadMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MOSI_ADDR), &val);
        if(val & SPI_STATUS_DONE) {
            break;
        }
        IODelay(1);
    }
    uint32_t lsb, msb;
    ivars->litepcie->ReadMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MISO_ADDR), &msb);
    ivars->litepcie->ReadMemory(CSR_TO_OFFSET(CSR_FLASH_SPI_MISO_ADDR) + 4, &lsb);
    output.rx_data = ((uint64_t)msb << 32) | lsb;
#endif
    
    // send our output out using osdata
    arguments->structureOutput = OSData::withBytes(&output, sizeof(LitePCIeFlashCallData));

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleICAP(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;

    LitePCIeICAPCallData* input;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeICAPCallData*)arguments->structureInput->getBytesNoCopy();
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (input == nullptr) {
        Log("input struct was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_ICAP_ADDR_ADDR), input->addr);
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_ICAP_DATA_ADDR), input->data);
    ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_ICAP_WRITE_ADDR), 1);

Exit:
    Log("finished");
    return ret;
}
kern_return_t litepcie_userclient::HandleDMA(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    
    LitePCIeDmaLoopbackData* input;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeDmaLoopbackData*)arguments->structureInput->getBytesNoCopy();
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }
#ifdef CSR_PCIE_DMA0_LOOPBACK_ENABLE_ADDR
    if (input->channel == 0) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA0_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA1_LOOPBACK_ENABLE_ADDR
    if (input->channel == 1) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA1_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA2_LOOPBACK_ENABLE_ADDR
    if (input->channel == 2) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA2_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA3_LOOPBACK_ENABLE_ADDR
    if (input->channel == 3) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA3_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA4_LOOPBACK_ENABLE_ADDR
    if (input->channel == 4) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA4_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA5_LOOPBACK_ENABLE_ADDR
    if (input->channel == 5) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA5_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA6_LOOPBACK_ENABLE_ADDR
    if (input->channel == 6) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA6_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif
#ifdef CSR_PCIE_DMA7_LOOPBACK_ENABLE_ADDR
    if (input->channel == 7) {
        ivars->litepcie->WriteMemory(CSR_TO_OFFSET(CSR_PCIE_DMA7_LOOPBACK_ENABLE_ADDR), input->loop_en ? 1 : 0);
    }
#endif

Exit:
    Log("finished");
    return ret;
}
kern_return_t litepcie_userclient::HandleDMALock(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    LitePCIeDmaLockData* input;
    LitePCIeDmaLockData output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeDmaLockData*)arguments->structureInput->getBytesNoCopy();
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    output.dma_reader_status = 1;
    if (input->dma_reader_request && ivars->dmaReaderLocks[0] == 0) {
        Log("DMA Reader Request");
        ivars->dmaReaderLocks[0] = ivars->litepcie->DmaChannelGetReaderLock(0);
    }
    else if (input->dma_reader_release && ivars->dmaReaderLocks[0] != 0) {
        Log("DMA Reader Release");
        ivars->dmaReaderLocks[0] = ivars->litepcie->DmaChannelReleaseReaderLock(0);
    }
    
    output.dma_writer_status = 1;
    if (input->dma_writer_request && ivars->dmaWriterLocks[0] == 0) {
        Log("DMA Writer Request");
        ivars->dmaWriterLocks[0] = ivars->litepcie->DmaChannelGetWriterLock(0);
    }
    else if (input->dma_writer_release && ivars->dmaWriterLocks[0] != 0) {
        Log("DMA Writer Release");
        ivars->dmaWriterLocks[0] = ivars->litepcie->DmaChannelReleaseWriterLock(0);
    }

    arguments->structureOutput = OSData::withBytes(&output, sizeof(LitePCIeDmaLockData));

Exit:
    Log("finished");
    return ret;
}
kern_return_t litepcie_userclient::HandleReadCSR(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    
    const uint64_t* input;
    uint64_t output;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->scalarInput != nullptr && arguments->scalarInputCount == 1) {
        input = arguments->scalarInput;
    } else {
        Log("scalarInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    ivars->litepcie->ReadMemory(input[0], (uint32_t*)&output);

    arguments->scalarOutput[0] = output;

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleWriteCSR(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    
    const uint64_t* input;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->scalarInput != nullptr && arguments->scalarInputCount == 2) {
        input = arguments->scalarInput;
    } else {
        Log("scalarInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }
    
    ivars->litepcie->WriteMemory(input[0], (uint32_t)input[1]);

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleDMARead(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    LitePCIeDmaTransferData *input, output;
    IOBufferMemoryDescriptor *readBuffer;
    IOMemoryMap *readMem;
    IOAddressSegment readAddr;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    input = (LitePCIeDmaTransferData*)arguments->structureInput->getBytesNoCopy();

    readAddr.address = (uint64_t)input->buffer_addr;
    readAddr.length  = input->length;
    ret = IOUserClient::CreateMemoryDescriptorFromClient(kIOMemoryDirectionInOut, 1, &readAddr, (IOMemoryDescriptor**)&readBuffer);
    if(ret != kIOReturnSuccess || readBuffer == nullptr)
    {
        Log("unable to create readBuffer");
        goto Exit;
    }

    ret = readBuffer->CreateMapping(kIOMemoryMapCacheModeDefault, 0, 0, 0, 0, &readMem);
    if(ret != kIOReturnSuccess || readMem == nullptr)
    {
        Log("unable to create readMap");
        readBuffer->release();
        ret = kIOReturnNoMemory;
        goto Exit;
    }
    
    output.channel = input->channel;
    output.buffer_addr = input->buffer_addr;
    output.length =  (uint32_t)ivars->litepcie->DmaChannelRead((int)input->channel, readMem);

    arguments->structureOutput = OSData::withBytes(&output, sizeof(LitePCIeDmaTransferData));

    readBuffer->release();
    readMem->release();

Exit:
    Log("finished");
    return ret;
}

kern_return_t litepcie_userclient::HandleDMAWrite(IOUserClientMethodArguments* arguments)
{
    Log("entered");
    kern_return_t ret = kIOReturnSuccess;
    LitePCIeDmaTransferData* input;

    // bunch of checks to see if out input is valid on multiple levels
    if (arguments == nullptr) {
        Log("Arguments were null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    if (arguments->structureInput != nullptr) {
        input = (LitePCIeDmaTransferData*)arguments->structureInput->getBytesNoCopy();
    } else {
        Log("structureInput was null");
        ret = kIOReturnBadArgument;
        goto Exit;
    }

    // TODO
    Log("TODO: Write user data to channel %u", input->channel);


Exit:
    Log("finished");
    return ret;
}

kern_return_t IMPL(litepcie_userclient, CopyClientMemoryForType) //(uint64_t type, uint64_t *options, IOMemoryDescriptor **memory)
{
    Log("entered");

    kern_return_t ret = kIOReturnSuccess;
    
    uint8_t dma_channel = type & 0xF;
    
    if (type & LITEPCIE_DMA_READER) {
        if (ivars->rdma[dma_channel] != nullptr) {
            ivars->rdma[dma_channel]->retain();
            *memory = (IOMemoryDescriptor*)(ivars->rdma[dma_channel]);
        } else {
            ret = ivars->litepcie->CreateReaderBufferDescriptor(dma_channel, (IOMemoryDescriptor**)&(ivars->rdma[dma_channel]));
            if (ret != kIOReturnSuccess) {
                Log("litepcie::CreateReaderBufferDescriptor failed: 0x%x", ret);
            } else {
                ivars->rdma[dma_channel]->retain();
                *memory = (IOMemoryDescriptor*)(ivars->rdma[dma_channel]);
            }
        }
    } else if (type & LITEPCIE_DMA_WRITER) {
        if (ivars->wdma[dma_channel] != nullptr) {
            ivars->wdma[dma_channel]->retain();
            *memory = (IOMemoryDescriptor*)(ivars->wdma[dma_channel]);
        } else {
            ret = ivars->litepcie->CreateWriterBufferDescriptor(dma_channel, (IOMemoryDescriptor**)&(ivars->wdma[dma_channel]));
            if (ret != kIOReturnSuccess) {
                Log("litepcie::CreateWriterBufferDescriptor failed: 0x%x", ret);
            } else {
                ivars->wdma[dma_channel]->retain();
                *memory = (IOMemoryDescriptor*)(ivars->wdma[dma_channel]);
            }
        }
    } else if (type & LITEPCIE_DMA_COUNTS) {
        if (ivars->cdma[dma_channel] != nullptr) {
            ivars->cdma[dma_channel]->retain();
            *memory = (IOMemoryDescriptor*)(ivars->cdma[dma_channel]);
            *options |= kIOUserClientMemoryReadOnly;
        } else {
            ret = ivars->litepcie->GetDmaCountDescriptor(dma_channel, (IOMemoryDescriptor**)&(ivars->cdma[dma_channel]));
            if (ret != kIOReturnSuccess) {
                Log("litepcie::GetDmaCountDescriptor failed: 0x%x", ret);
            } else {
                ivars->cdma[dma_channel]->retain();
                *memory = (IOMemoryDescriptor*)(ivars->cdma[dma_channel]);
                *options |= kIOUserClientMemoryReadOnly;
            }
        }
    }  else {
        ret = this->CopyClientMemoryForType(type, options, memory, SUPERDISPATCH);
    }

    Log("finished");

    return ret;
}
