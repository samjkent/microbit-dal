
/*
The MIT License (MIT)

Copyright (c) 2016 British Broadcasting Corporation.
This software is provided by Lancaster University by arrangement with the BBC.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

/**
  * Class definition for the custom MicroBit Partial Flashing service.
  * Provides a BLE service to remotely write the user program to the device.
  */
#include "MicroBitConfig.h"
#include "ble/UUID.h"
#include "MicroBitPartialFlashingService.h"

#define REGION_INFO 0x00
#define FLASH_DATA  0x01

// Instance of MBFlash
MicroBitFlash flash;

uint32_t baseAddress = 0x30000;

int packet = 0;

uint8_t packetNum = 0;
uint32_t packetCount = 0;
uint32_t blockPacketCount = 0;

uint32_t block[16];
uint8_t  blockNum = 0;
uint16_t  offset   = 0;

/**
  * Constructor.
  * Create a representation of the PartialFlashService
  * @param _ble The instance of a BLE device that we're running on.
  */
MicroBitPartialFlashService::MicroBitPartialFlashService(BLEDevice &_ble, EventModel &_messageBus) :
        ble(_ble), messageBus(_messageBus), memoryMap()
{
    uint8_t initCharacteristicValue = 0x00;
    GattCharacteristic partialFlashCharacteristic(MicroBitPartialFlashServiceCharacteristicUUID, &initCharacteristicValue, sizeof(initCharacteristicValue),
    20, GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE_WITHOUT_RESPONSE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY);

    // Set default security requirements
    partialFlashCharacteristic.requireSecurity(SecurityManager::MICROBIT_BLE_SECURITY_LEVEL);

    GattCharacteristic *characteristics[] = {&partialFlashCharacteristic}; // , &endAddressCharacteristic, &hashCharacteristic, &rwPolicyCharacteristic};
    GattService         service(MicroBitPartialFlashServiceUUID, characteristics, sizeof(characteristics) / sizeof(GattCharacteristic*) );

    ble.addService(service);

    partialFlashCharacteristicHandle = partialFlashCharacteristic.getValueHandle();

    ble.onDataWritten(this, &MicroBitPartialFlashService::onDataWritten);

    // Set up listener for SD writing
    messageBus.listen(MICROBIT_ID_PFLASH_NOTIFICATION, MICROBIT_EVT_ANY, this, &MicroBitPartialFlashService::writeEvent);

}


/**
  * Callback. Invoked when any of our attributes are written via BLE.
  */
void MicroBitPartialFlashService::onDataWritten(const GattWriteCallbackParams *params)
{
    data = (uint8_t *)params->data;

    if(params->handle == partialFlashCharacteristicHandle && params->len > 0)
    {
      // Switch CONTROL byte
      switch(data[0]){
        case REGION_INFO:
        {
          uint8_t buffer[18];
          buffer[0] =  0x00;
          buffer[1] =  data[1];

          buffer[2] = (memoryMap.memoryMapStore.memoryMap[data[1]].startAddress & 0xFF000000) >> 24;
          buffer[3] = (memoryMap.memoryMapStore.memoryMap[data[1]].startAddress & 0x00FF0000) >> 16;
          buffer[4] = (memoryMap.memoryMapStore.memoryMap[data[1]].startAddress & 0x0000FF00) >>  8;
          buffer[5] = (memoryMap.memoryMapStore.memoryMap[data[1]].startAddress & 0x000000FF);

          buffer[6] = (memoryMap.memoryMapStore.memoryMap[data[1]].endAddress & 0xFF000000) >> 24;
          buffer[7] = (memoryMap.memoryMapStore.memoryMap[data[1]].endAddress & 0x00FF0000) >> 16;
          buffer[8] = (memoryMap.memoryMapStore.memoryMap[data[1]].endAddress & 0x0000FF00) >>  8;
          buffer[9] = (memoryMap.memoryMapStore.memoryMap[data[1]].endAddress & 0x000000FF);

          for(int i = 0; i < 8; ++i)
              buffer[10+i] = memoryMap.memoryMapStore.memoryMap[data[1]].hash[i];

          ble.gattServer().notify(partialFlashCharacteristicHandle, (const uint8_t *)buffer, sizeof(buffer));

          // Set offset for writing
          baseAddress = memoryMap.memoryMapStore.memoryMap[data[1]].startAddress & 0xFFFF0000; // Offsets are 16 bit
          break;
        }
        case FLASH_DATA:
        {
          flashData(data);
          break;
        }
    }
  }
}



/**
  * @param data - A pointer to the data to process
  *
  */
void MicroBitPartialFlashService::flashData(uint8_t *data)
{
        // Receive 16 bytes per packet
        // Buffer 8 packets - 32 uint32_t // 128 bytes per block
        // When buffer is full trigger writeEvent
        // When write is complete notify app and repeat
        // +-----------+----------+---------+---------+
        // | 1 Byte    | 16 Bytes | 2 Bytes | 1 Byte  |
        // +-----------+----------+---------+---------+
        // | COMMAND   | DATA     | OFFSET  | PACKET# |
        // +-----------+----------+---------+---------+

        // If offset = 0x1234 restart current packet
        if(((data[17] << 8) | data[18]) == 0x1234)
        {
          packetCount = ((data[18] << 8) | data[19]);
          blockNum = 0;
          return;
        }

        // Check packet count
        packetNum = data[19];
        if(packetNum != ++packetCount)
        {
            uint8_t flashNotificationBuffer[] = {FLASH_DATA, 0xAA};
            ble.gattServer().notify(partialFlashCharacteristicHandle, (const uint8_t *)flashNotificationBuffer, sizeof(flashNotificationBuffer));
            packetCount = blockPacketCount;
        }

        // Add to block
        for(int x = 0; x < 4; x++)
            block[(4*blockNum) + x] = data[(4*x) + 1] | data[(4*x) + 2] << 8 | data[(4*x) + 3] << 16 | data[(4*x) + 4] << 24;

        // If packet num is 0xFFFF end transmission
        if(packetNum == 0xFFFF)
            blockNum = 255;

        // Actions
        switch(blockNum) {
            // blockNum is 0, set up offset
            case 0:
                {
                    offset = ((data[17] << 8) | data[18]);
                    blockPacketCount = packetNum;
                    blockNum++;
                    break;
                }
            // blockNum is 7, block is full
            case 3:
                {
                    // Fire write event
                    MicroBitEvent evt(MICROBIT_ID_PFLASH_NOTIFICATION, 0 ,CREATE_AND_FIRE);
                    // Reset blockNum
                    blockNum = 0;
                    break;
                }
            // blockNum is 255, end transmission
            case 255:
                {
                    MicroBitEvent evt(MICROBIT_ID_PFLASH_NOTIFICATION, 0 ,CREATE_AND_FIRE);
                }
            default:
                {
                    blockNum++;
                    break;
                }
        }
}


/**
  * Write Event
  * Used the write data to the flash outside of the BLE ISR
  */
void MicroBitPartialFlashService::writeEvent(MicroBitEvent e)
{

    // Flash Pointer
    uint32_t *flashPointer   = (uint32_t *) (baseAddress + offset);

    // If the pointer is on a page boundary erase the page
    if(!((uint32_t)flashPointer % 0x400))
        flash.erase_page(flashPointer);

    // Create a pointer to the data block
    uint32_t *blockPointer;
    blockPointer = block;

    flash.flash_burn(flashPointer, blockPointer, 16);

    // Update flash control buffer to send next packet
    uint8_t flashNotificationBuffer[] = {FLASH_DATA, 0xFF};
    ble.gattServer().notify(partialFlashCharacteristicHandle, (const uint8_t *)flashNotificationBuffer, sizeof(flashNotificationBuffer));
}

const uint8_t  MicroBitPartialFlashServiceUUID[] = {
    0xe9,0x7d,0xd9,0x1d,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};

const uint8_t  MicroBitPartialFlashServiceCharacteristicUUID[] = {
    0xe9,0x7d,0x3b,0x10,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};
