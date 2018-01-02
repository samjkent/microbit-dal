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

// Instance of MBFlash
MicroBitFlash flash;

uint8_t MicroBitPartialFlashService::writeStatus = 0;  // access static var
uint8_t *MicroBitPartialFlashService::data = 0;         // access static var
uint8_t MicroBitPartialFlashService::flashControlCharacteristicBuffer[20];  // access static var
uint32_t MicroBitPartialFlashService::baseAddress = 0x30000;

int packet = 0;

uint32_t packetNum = 0; 
uint32_t packetCount = 0; 
uint32_t blockPacketCount = 0;
    
uint32_t block[16];
uint8_t  blockNum = 0;
uint16_t  offset   = 0;

/**
  * Constructor.
  * Create a representation of the PartialFlashService
  * @param _ble The instance of a BLE device that we're running on.
  * @param _memoryMap An instance of MicroBitMemoryMap to interface with.
  */
MicroBitPartialFlashService::MicroBitPartialFlashService(BLEDevice &_ble, MicroBitMemoryMap &_memoryMap, EventModel &_messageBus) :
        ble(_ble), memoryMap(_memoryMap), messageBus(_messageBus),
    mapCharacteristic(MicroBitPartialFlashServiceMapUUID, (uint8_t *)&mapCharacteristicBuffer, 0, sizeof(mapCharacteristicBuffer),
        GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ),
    flashCharacteristic(MicroBitPartialFlashServiceFlashUUID, (uint8_t *) flashCharacteristicBuffer, 0,
        sizeof(flashCharacteristicBuffer), GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE_WITHOUT_RESPONSE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ),
    flashControlCharacteristic(MicroBitPartialFlashServiceFlashControlUUID, (uint8_t *) flashControlCharacteristicBuffer, 0,
        sizeof(flashControlCharacteristicBuffer), GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_NOTIFY)
{
    // Create the data structures that represent each of our characteristics in Soft Device.
    //GattCharacteristic  rwPolicyCharacteristic(MicroBitPartialFlashServiceRWPolicyUUID, (uint8_t *) rwPolicyCharacteristicBuffer, 0,
    //sizeof(rwPolicyCharacteristicBuffer), GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ);

    // Auth Callbacks 
    mapCharacteristic.setReadAuthorizationCallback(this, &MicroBitPartialFlashService::onDataRead);
    flashCharacteristic.setReadAuthorizationCallback(this, &MicroBitPartialFlashService::onDataRead);
    flashControlCharacteristic.setReadAuthorizationCallback(this, &MicroBitPartialFlashService::onDataRead);

    // Set default security requirements
    mapCharacteristic.requireSecurity(SecurityManager::MICROBIT_BLE_SECURITY_LEVEL);
    flashCharacteristic.requireSecurity(SecurityManager::MICROBIT_BLE_SECURITY_LEVEL);
    flashControlCharacteristic.requireSecurity(SecurityManager::MICROBIT_BLE_SECURITY_LEVEL);

    GattCharacteristic *characteristics[] = {&mapCharacteristic, &flashCharacteristic, &flashControlCharacteristic}; // , &endAddressCharacteristic, &hashCharacteristic, &rwPolicyCharacteristic};
    GattService         service(MicroBitPartialFlashServiceUUID, characteristics, sizeof(characteristics) / sizeof(GattCharacteristic*) );

    ble.addService(service);

    mapCharacteristicBuffer[0] = 0x00;
    flashCharacteristicBuffer[0] = 0x00;
    flashControlCharacteristicBuffer[0] = 0x00;

    mapCharacteristicHandle = mapCharacteristic.getValueHandle();
    flashCharacteristicHandle = flashCharacteristic.getValueHandle();
    flashControlCharacteristicHandle = flashControlCharacteristic.getValueHandle();

    ble.onDataWritten(this, &MicroBitPartialFlashService::onDataWritten);

    // Set up listener for SD writing
    messageBus.listen(MICROBIT_ID_PFLASH_NOTIFICATION, MICROBIT_EVT_ANY, writeEvent);

    // Set up fast BLE
    Gap::ConnectionParams_t fast;
    ble.getPreferredConnectionParams(&fast);
    fast.minConnectionInterval = 6;  // 7.5 ms
    fast.maxConnectionInterval = 16; // 20  ms
    fast.slaveLatency = 0;
    ble.setPreferredConnectionParams(&fast);
}


/**
  * Callback. Invoked when any of our attributes are written via BLE.
  */
void MicroBitPartialFlashService::onDataWritten(const GattWriteCallbackParams *params)
{
    data = (uint8_t *)params->data;
    
    if(params->handle == mapCharacteristicHandle && params->len > 0 && params->len < 6)
    {
        /* Data : 0xFF Returns list of Region Names
                  0x?? Returns Region ?? data   
        */
        ROI = data[0];
        packet = 0;

        baseAddress = memoryMap.memoryMapStore.memoryMap[ROI].startAddress & 0xFFFF0000; // Offsets are 16 bit
            
    } else if(params->handle == flashCharacteristicHandle && params->len > 0){

        // Receive 16 bytes per packet
        // Buffer 8 packets - 32 uint32_t // 128 bytes per block
        // When buffer is full trigger writeEvent
        // When write is complete notify app and repeat

        // Check packet count
        packetNum = ((data[18] << 8) | data[19]);
        if(packetNum != ++packetCount)
        {
            flashControlCharacteristicBuffer[0] = 0xAA;
            packetCount = blockPacketCount;
        }

        // Add to block
        for(int x = 0; x < 4; x++)
            block[(4*blockNum) + x] = data[(4*x)] | data[(4*x)+1] << 8 | data[(4*x)+2] << 16 | data[(4*x)+3] << 24;
        
        // If packet num is 0xFFFF end transmission 
        if(packetNum == 0xFFFF)
            blockNum = 255;

        // Actions
        switch(blockNum) {
            // blockNum is 0, set up offset
            case 0:
                {
                    offset = ((data[16] << 8) | data[17]);
                    blockPacketCount = packetNum;
                    blockNum++;
                    flashControlCharacteristicBuffer[0] = 0x00;
                    break;
                }
            // blockNum is 7, block is full
            case 3:
                {
                    // Fire write event
                    MicroBitEvent evt(MICROBIT_ID_PFLASH_NOTIFICATION, params->len ,CREATE_AND_FIRE);
                    // Reset blockNum
                    blockNum = 0;
                    break;
                }
            // blockNum is 255, end transmission
            case 255:
                {
                    MicroBitEvent evt(MICROBIT_ID_PFLASH_NOTIFICATION, params->len ,CREATE_AND_FIRE);
                }
            default:
                {
                    blockNum++;
                    break;
                }
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
    flashControlCharacteristicBuffer[0] = 0xFF;
}


/**
  * Callback. Invoked when any of our attributes are read via BLE.
  */
void MicroBitPartialFlashService::onDataRead(GattReadAuthCallbackParams *params)
{
    if(params->handle == mapCharacteristicHandle)
    {
       if(ROI == 0xFF){ 
            // Returns list of region names
            int j = 0; // Counter
            for(int i = 0; i < NUMBER_OF_REGIONS; i++) { 
                mapCharacteristicBuffer[j  ] = memoryMap.memoryMapStore.memoryMap[i].name[0]; 
                mapCharacteristicBuffer[j+1] = memoryMap.memoryMapStore.memoryMap[i].name[1]; 
                mapCharacteristicBuffer[j+2] = memoryMap.memoryMapStore.memoryMap[i].name[2];
                j = j + 3;
            }


            ble.gattServer().write(mapCharacteristicHandle, (const uint8_t *)&mapCharacteristicBuffer, 3 * NUMBER_OF_REGIONS);
        } else {
            if(packet == 0){
                // Return Region Start / End
                mapCharacteristicBuffer[0]  = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress & 0x000000FF);
                mapCharacteristicBuffer[1]  = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress & 0x0000FF00) >>  8;
                mapCharacteristicBuffer[2]  = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress & 0x00FF0000) >> 16;
                mapCharacteristicBuffer[3]  = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress & 0xFF000000) >> 24;
                mapCharacteristicBuffer[4]  = 0;
                mapCharacteristicBuffer[5]  = 0;
                mapCharacteristicBuffer[6]  = 0;
                mapCharacteristicBuffer[7]  = 0;
                mapCharacteristicBuffer[8]  = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress & 0x000000FF);
                mapCharacteristicBuffer[9]  = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress & 0x0000FF00) >>  8;
                mapCharacteristicBuffer[10] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress & 0x00FF0000) >> 16;
                mapCharacteristicBuffer[11] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress & 0xFF000000) >> 24;
                mapCharacteristicBuffer[12] = 0;
                mapCharacteristicBuffer[13] = 0;
                mapCharacteristicBuffer[14] = 0;
                mapCharacteristicBuffer[15] = 0;
                mapCharacteristicBuffer[16] = 0;
                mapCharacteristicBuffer[17] = 0;
                mapCharacteristicBuffer[18] = ROI;
                mapCharacteristicBuffer[19] = 0;

                // Increment Packet Count
                packet = 1;
        
            } else {
                // Return Hash
                for(int i = 0; i < 16; ++i)
                    mapCharacteristicBuffer[i] = memoryMap.memoryMapStore.memoryMap[ROI].hash[i];
                mapCharacteristicBuffer[16] = 0;
                mapCharacteristicBuffer[17] = 0;
                mapCharacteristicBuffer[18] = ROI;
                mapCharacteristicBuffer[19] = 1;

                // Set packet count to 0
                packet = 0;
            }
           

            ble.gattServer().write(mapCharacteristicHandle, (const uint8_t *)&mapCharacteristicBuffer, sizeof(mapCharacteristicBuffer));
            }
    } else 
    if(params->handle == flashCharacteristicHandle)
    {   // Writes Region
        ble.gattServer().write(flashCharacteristicHandle, (const uint8_t *)flashCharacteristicBuffer, sizeof(flashCharacteristicBuffer));
    } else
    if(params->handle == flashControlCharacteristicHandle)
    {
        ble.gattServer().write(flashControlCharacteristicHandle, (const uint8_t *)flashControlCharacteristicBuffer, sizeof(flashControlCharacteristicBuffer));
    }
}


const uint8_t  MicroBitPartialFlashServiceUUID[] = {
    0xe9,0x7d,0xd9,0x1d,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};

const uint8_t  MicroBitPartialFlashServiceMapUUID[] = {
    0xe9,0x7d,0x3b,0x10,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};

const uint8_t  MicroBitPartialFlashServiceFlashUUID[] = {
    0xe9,0x7f,0xaa,0x6d,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};

const uint8_t  MicroBitPartialFlashServiceFlashControlUUID[] = {
    0xe9,0x7f,0xab,0x6d,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};
