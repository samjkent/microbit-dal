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
  * Class definition for the MicroBitMemoryMap class.
  * This allows reading and writing of regions within the memory map.
  *
  * This class maps the different regions used on the flash memory to allow
  * a region to updated independently of the others AKA Partial Flashing.
*/

#include "MicroBitConfig.h"
#include "MicroBitMemoryMap.h"
#include "MicroBitFlash.h"

/**
  * Default constructor.
  *
  * Creates an instance of MicroBitMemoryMap
  */
MicroBitMemoryMap::MicroBitMemoryMap()
{

    //read our data!
    memcpy(&memoryMapStore, flashBlockPointer, sizeof(memoryMapStore));

    //if we haven't used flash before, we need to configure it
    // Rebuild map everytime
    if(memoryMapStore.magic != MICROBIT_MEMORY_MAP_MAGIC || 1)
    {

        // SD
        pushRegion(Region(0x00, 0x00, 0x18000, 0x00));  // Soft Device

        // DAL
        pushRegion(Region(0x01, 0x18000, FLASH_PROGRAM_END, 0x00)); // micro:bit Device Abstractation Layer

        // PXT
        pushRegion(Region(0x02, FLASH_PROGRAM_END, 0x3e800, 0x00)); // micro:bit PXT

        // Find Hashes if PXT Built Program
        findHashes();

        // Update Magic
        memoryMapStore.magic = MICROBIT_MEMORY_MAP_MAGIC;

        // Update Flash
        // updateFlash(&memoryMapStore);

    }

}

/**
  * Function for adding a Region to the end of the MemoryMap
  *
  * @param region The Region to add to the MemoryMap
  *
  * @return MICROBIT_OK on success
  *
  */
int MicroBitMemoryMap::pushRegion(Region region)
{

    // Find next blank Region in map
    if(regionCount == NUMBER_OF_REGIONS){
        return MICROBIT_NO_DATA;
    } else {
        // Add data
        memoryMapStore.memoryMap[regionCount].startAddress = region.startAddress;
        memoryMapStore.memoryMap[regionCount].endAddress   = region.endAddress;
        memcpy(&memoryMapStore.memoryMap[regionCount].hash, &region.hash, 8);
        memoryMapStore.memoryMap[regionCount].regionId     = region.regionId;
        regionCount++;
        return MICROBIT_OK;
    }
}

/**
  * Function for updating a Region of the MemoryMap
  *
  * @param region The Region to update in the MemoryMap. The name is used as the selector.
  *
  * @return MICROBIT_OK success, MICROBIT_NO_DATA if the region is not found
  */
int MicroBitMemoryMap::updateRegion(Region region)
{
    // Find Region name in map
    int i = 0;
    while(memoryMapStore.memoryMap[i].regionId != region.regionId && i < NUMBER_OF_REGIONS) i++;

    if(i == NUMBER_OF_REGIONS){
        return MICROBIT_NO_DATA;
    } else {
        // Add data
        memoryMapStore.memoryMap[i] = region;
        updateFlash(&memoryMapStore);
        return MICROBIT_OK;
    }
}

/**
  * Function to update the flash with the current MemoryMapStore
  *
  * @param memoryMapStore The memory map to write to flash
  */
void MicroBitMemoryMap::updateFlash(MemoryMapStore *store)
{
  //calculate our various offsets
  uint32_t pg_size = NRF_FICR->CODEPAGESIZE;
  uint32_t *flashPointer = (uint32_t *)(pg_size * (NRF_FICR->CODESIZE - MICROBIT_MEMORY_MAP_PAGE_OFFSET));
  int wordsToWrite = sizeof(MemoryMapStore) / 4;

  MicroBitFlash flash;
  flash.flash_write(store, flashPointer, wordsToWrite);
}

/*
 * Function to fetch the hashes from a PXT generated build
 */
void MicroBitMemoryMap::findHashes()
{

    // Iterate through pages to find magic
    for(int x = 0; x < NRF_FICR->CODESIZE - 1; x++)
    {

        uint32_t volatile *magicAddress  = (uint32_t *)(0x400 * x);
        uint32_t magicValue = *magicAddress;

        // Check for first 32 bits of Magic
        if(magicValue == 0x923b8e70)
        {
            // Check remaining magic
            if(
               *(uint32_t *)(magicAddress + 0x1) == 0x41A815C6 &&
               *(uint32_t *)(magicAddress + 0x2) == 0xC96698C4 &&
               *(uint32_t *)(magicAddress + 0x3) == 0x9751EE75
              )
            {
                // If the magic has been found use the hashes follow
                magicAddress = (uint32_t *)(magicAddress + 0x4);

                memoryMapStore.memoryMap[1].hash[0] = (*magicAddress & 0xFF);
                memoryMapStore.memoryMap[1].hash[1] = (*magicAddress & 0xFF00)     >>  8;
                memoryMapStore.memoryMap[1].hash[2] = (*magicAddress & 0xFF0000)   >> 16;
                memoryMapStore.memoryMap[1].hash[3] = (*magicAddress & 0xFF000000) >> 24;

                magicAddress = (uint32_t *)(magicAddress + 0x1);

                memoryMapStore.memoryMap[1].hash[4] = (*magicAddress & 0xFF);
                memoryMapStore.memoryMap[1].hash[5] = (*magicAddress & 0xFF00)     >>  8;
                memoryMapStore.memoryMap[1].hash[6] = (*magicAddress & 0xFF0000)   >> 16;
                memoryMapStore.memoryMap[1].hash[7] = (*magicAddress & 0xFF000000) >> 24;

                magicAddress = (uint32_t *)(magicAddress + 0x1);

                memoryMapStore.memoryMap[2].hash[0] = (*magicAddress & 0xFF);
                memoryMapStore.memoryMap[2].hash[1] = (*magicAddress & 0xFF00)     >>  8;
                memoryMapStore.memoryMap[2].hash[2] = (*magicAddress & 0xFF0000)   >> 16;
                memoryMapStore.memoryMap[2].hash[3] = (*magicAddress & 0xFF000000) >> 24;

                magicAddress = (uint32_t *)(magicAddress + 0x1);

                memoryMapStore.memoryMap[2].hash[4] = (*magicAddress & 0xFF);
                memoryMapStore.memoryMap[2].hash[5] = (*magicAddress & 0xFF00)     >>  8;
                memoryMapStore.memoryMap[2].hash[6] = (*magicAddress & 0xFF0000)   >> 16;
                memoryMapStore.memoryMap[2].hash[7] = (*magicAddress & 0xFF000000) >> 24;

                return;

            }

        }

    }

}
