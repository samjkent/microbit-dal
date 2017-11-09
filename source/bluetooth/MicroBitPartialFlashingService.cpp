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

uint8_t MicroBitPartialFlashService::writeStatus = 0;  // access static var
uint8_t *MicroBitPartialFlashService::data = 0;         // access static var
uint32_t MicroBitPartialFlashService::offset = 0;
uint32_t MicroBitPartialFlashService::baseAddress = 0;


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
        sizeof(flashCharacteristicBuffer), GattCharacteristic::GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ)
{
    // Create the data structures that represent each of our characteristics in Soft Device.
    //GattCharacteristic  rwPolicyCharacteristic(MicroBitPartialFlashServiceRWPolicyUUID, (uint8_t *) rwPolicyCharacteristicBuffer, 0,
    //sizeof(rwPolicyCharacteristicBuffer), GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_WRITE | GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_READ);

    // Auth Callbacks 
    mapCharacteristic.setReadAuthorizationCallback(this, &MicroBitPartialFlashService::onDataRead);
    flashCharacteristic.setReadAuthorizationCallback(this, &MicroBitPartialFlashService::onDataRead);

    // Set default security requirements
    mapCharacteristic.requireSecurity(SecurityManager::MICROBIT_BLE_SECURITY_LEVEL);
    flashCharacteristic.requireSecurity(SecurityManager::MICROBIT_BLE_SECURITY_LEVEL);

    GattCharacteristic *characteristics[] = {&mapCharacteristic, &flashCharacteristic}; // , &endAddressCharacteristic, &hashCharacteristic, &rwPolicyCharacteristic};
    GattService         service(MicroBitPartialFlashServiceUUID, characteristics, sizeof(characteristics) / sizeof(GattCharacteristic*) );

    ble.addService(service);

    mapCharacteristicBuffer[0] = 0x00;
    flashCharacteristicBuffer[0] = 0x00;

    mapCharacteristicHandle = mapCharacteristic.getValueHandle();
    flashCharacteristicHandle = flashCharacteristic.getValueHandle();

    ble.onDataWritten(this, &MicroBitPartialFlashService::onDataWritten);

    // Set up listener for SD writing
    messageBus.listen(MICROBIT_ID_PFLASH_NOTIFICATION, MICROBIT_EVT_ANY, writeEvent);

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
        ROI = *data;
        baseAddress = memoryMap.memoryMapStore.memoryMap[ROI].startAddress;
            
    } else if(params->handle == flashCharacteristicHandle && params->len > 0){
       
        // Use event model
        MicroBitEvent evt(MICROBIT_ID_PFLASH_NOTIFICATION, params->len ,CREATE_AND_FIRE);

        flashCharacteristicBuffer[1] = 0x0F; // Indicates received
    }

}

/**
  * Write Event 
  * Used the write data to the flash outside of the BLE ISR
  */
void MicroBitPartialFlashService::writeEvent(MicroBitEvent e){

    uint32_t *scratchPointer = (uint32_t *)(NRF_FICR->CODEPAGESIZE * (NRF_FICR->CODESIZE - 19));
    uint32_t *flashPointer   = (uint32_t *) baseAddress; // memoryMap.memoryMapStore.memoryMap[2].startAddress;

    MicroBitFlash flash;     
    uint32_t len = 32;  

    writeStatus = 0x00; // Start flash

    // offset
    offset = (data[16] << 8) | data[17];
    offset = offset / 4;

    for(int i = 0; i < 4; i++){
        uint8_t block[4];
        block[3] = data[i*4];
        block[2] = data[(i*4)+1];
        block[1] = data[(i*4)+2];
        block[0] = data[(i*4)+3];
        flash.flash_write(flashPointer + offset + i, block, sizeof(block), scratchPointer);
    }
    writeStatus = 0xFF; // Indicates flash write complete
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
            // Return Region
            /*
             * mapCharacteristicBuffer[0] = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress && 0x000000FF);
            mapCharacteristicBuffer[1] = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress && 0x0000FF00) >>  8;
            mapCharacteristicBuffer[2] = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress && 0x00FF0000) >> 16;
            mapCharacteristicBuffer[3] = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress && 0xFF000000) >> 24;

            mapCharacteristicBuffer[4] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress && 0x000000FF);
            mapCharacteristicBuffer[5] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress && 0x0000FF00) >>  8;
            mapCharacteristicBuffer[6] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress && 0x00FF0000) >> 16;
            mapCharacteristicBuffer[7] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress && 0xFF000000) >> 24;
            */

           
            mapCharacteristicBuffer[0] = ROI;

            /*for(int i = 0; i < 16; ++i)
             *
             */
                // mapCharacteristicBuffer[2] |= memoryMap.memoryMapStore.memoryMap[ROI].hash;
                //mapCharacteristicBuffer[4+i] = memoryMap.memoryMapStore.memoryMap[ROI].hash[i];*/

            ble.gattServer().write(mapCharacteristicHandle, (const uint8_t *)&mapCharacteristicBuffer, sizeof(mapCharacteristicBuffer));
        }
        
    } else 
    {
    if(params->handle == flashCharacteristicHandle)
    {   // Writes Region
        // memcpy(regionCharacteristicBuffer, &memoryMap.memoryMapStore.memoryMap[ROI], sizeof(memoryMap.memoryMapStore.memoryMap[*data]));
        flashCharacteristicBuffer[0] = writeStatus;
        ble.gattServer().write(flashCharacteristicHandle, (const uint8_t *)flashCharacteristicBuffer, sizeof(flashCharacteristicBuffer));
    } 
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

const uint8_t  MicroBitPartialFlashServiceEndAddressUUID[] = {
    0xe9,0x7d,0x1b,0x4f,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};

const uint8_t  MicroBitPartialFlashServiceHashUUID[] = {
    0xe9,0x7d,0x6a,0xad,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};

const uint8_t  MicroBitPartialFlashServiceRWPolicyUUID[] = {
    0xe9,0x7d,0x88,0x2f,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};

const uint8_t  MicroBitPartialFlashServiceFlashWriteUUID[] = {
    0xe9,0x7d,0x2d,0x9e,0x25,0x1d,0x47,0x0a,0xa0,0x62,0xfa,0x19,0x22,0xdf,0xa9,0xa8
};
