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


/**
  * Constructor.
  * Create a representation of the PartialFlashService
  * @param _ble The instance of a BLE device that we're running on.
  * @param _memoryMap An instance of MicroBitMemoryMap to interface with.
  */
MicroBitPartialFlashService::MicroBitPartialFlashService(BLEDevice &_ble, MicroBitMemoryMap &_memoryMap) :
        ble(_ble), memoryMap(_memoryMap),
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

    mapCharacteristicHandle = mapCharacteristic.getValueHandle();
    flashCharacteristicHandle = flashCharacteristic.getValueHandle();

    ble.onDataWritten(this, &MicroBitPartialFlashService::onDataWritten);
}


/**
  * Callback. Invoked when any of our attributes are written via BLE.
  */
void MicroBitPartialFlashService::onDataWritten(const GattWriteCallbackParams *params)
{
    uint8_t *data = (uint8_t *)params->data;
    
    if(params->handle == mapCharacteristicHandle && params->len > 0 && params->len < 6)
    {
        /* Data : 0xFF Returns list of Region Names
                  0x?? Returns Region ?? data   
        */
        ROI = *data;
            
    } else if(params->handle == flashCharacteristicHandle && params->len > 0){
        
    }

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
            mapCharacteristicBuffer[0] = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress & 0xFF00) >> 4;
            mapCharacteristicBuffer[1] = (memoryMap.memoryMapStore.memoryMap[ROI].startAddress & 0x00FF);
            
            mapCharacteristicBuffer[2] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress & 0xFF00) >> 4;
            mapCharacteristicBuffer[3] = (memoryMap.memoryMapStore.memoryMap[ROI].endAddress & 0x00FF);

            for(int i = 0; i < 16; i++)
                mapCharacteristicBuffer[4+i] = memoryMap.memoryMapStore.memoryMap[ROI].hash[i];

            ble.gattServer().write(mapCharacteristicHandle, (const uint8_t *)&mapCharacteristicBuffer, 20);
        }
        
    } else 
    {
    if(params->handle == flashCharacteristicHandle)
    {   // Writes Region
        // memcpy(regionCharacteristicBuffer, &memoryMap.memoryMapStore.memoryMap[ROI], sizeof(memoryMap.memoryMapStore.memoryMap[*data]));
        ble.gattServer().write(flashCharacteristicHandle, (const uint8_t *)&memoryMap.memoryMapStore.memoryMap[0], sizeof(&memoryMap.memoryMapStore.memoryMap[0]));
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
