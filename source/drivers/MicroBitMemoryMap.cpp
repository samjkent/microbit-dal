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
#include "ManagedString.h"
#include "md5.h"

char sdHash[16] = "00000000";
char dalHash[16] = "00000000";
char pxtHash[16] = "00000000";



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
    if(memoryMapStore.magic != MICROBIT_MEMORY_MAP_MAGIC || 1 == 1)
    {
        
        // Add known details
        // Set Names to Empty rather than garbage so PushRegion works
        for(int i = 0; i < NUMBER_OF_REGIONS; i++) {
            memoryMapStore.memoryMap[i].name[0] = ' ';
            memoryMapStore.memoryMap[i].name[1] = ' ';
            memoryMapStore.memoryMap[i].name[2] = ' ';
        }

        // Find Hashes if PXT Built Program
        findHashes();
        
        // SD
        char sdName[4] = "SD ";
        pushRegion(Region(0x00, 0x18000, sdName, sdHash, USB));  // Soft Device
        
        // DAL
        char dalName[4] = "DAL";
        pushRegion(Region(0x18000, FLASH_PROGRAM_END, dalName, dalHash, USB)); // micro:bit Device Abstractation Layer
        
        // PXT
        char pxtName[4] = "PXT";
        pushRegion(Region(FLASH_PROGRAM_END, 0x3e800, pxtName, pxtHash, PartialFlash)); // micro:bit PXT
        
    
        memoryMapStore.magic = MICROBIT_MEMORY_MAP_MAGIC;
     
       
        //erase the scratch page and write our new MemoryMapStore
        flashPageErase((uint32_t *)(pg_size * (NRF_FICR->CODESIZE - MICROBIT_MEMORY_MAP_SCRATCH_PAGE_OFFSET)));
        scratchMemoryMapStore(memoryMapStore);
        
        //erase flash, and copy the scratch page over
        flashPageErase((uint32_t *)flashBlockPointer);
        flashCopy((uint32_t *)(pg_size * (NRF_FICR->CODESIZE - MICROBIT_MEMORY_MAP_SCRATCH_PAGE_OFFSET)), flashBlockPointer, pg_size/4);
       
    }

}

/**
  * Method for erasing a page in flash.
  *
  * @param page_address Address of the first word in the page to be erased.
  */  
void MicroBitMemoryMap::flashPageErase(uint32_t * page_address)
{
    MicroBitFlash flash;
    flash.erase_page(page_address);
}

/**
  * Function for copying words from one location to another.
  *
  * @param from the address to copy data from.
  *
  * @param to the address to copy the data to.
  *
  * @param sizeInWords the number of words to copy
  */
void MicroBitMemoryMap::flashCopy(uint32_t* from, uint32_t* to, int sizeInWords)
{
    MicroBitFlash flash;
    flash.flash_burn(to, from, sizeInWords);
}

/**
  * Method for writing a word of data in flash with a value.
  *
  * @param address Address of the word to change.
  *
  * @param value Value to be written to flash.
  */
void MicroBitMemoryMap::flashWordWrite(uint32_t * address, uint32_t value)
{
    flashCopy(&value, address, 1);
}

/**
  * Function for populating the scratch page with a MemoryMapStore
  *
  * @param store the MemoryMapStore struct to write to the scratch page.
  */
void MicroBitMemoryMap::scratchMemoryMapStore(MemoryMapStore store)
{
    //calculate our various offsets
    uint32_t *s = (uint32_t *) &store;
    uint32_t pg_size = NRF_FICR->CODEPAGESIZE;
    uint32_t *scratchPointer = (uint32_t *)(pg_size * (NRF_FICR->CODESIZE - MICROBIT_MEMORY_MAP_SCRATCH_PAGE_OFFSET));
    int wordsToWrite = sizeof(MemoryMapStore) / 4;

    flashCopy(s, scratchPointer, wordsToWrite);
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
    int i = 0;
    while(memoryMapStore.memoryMap[i].name[0] != ' '  && i < NUMBER_OF_REGIONS) i++;
    
    if(i == NUMBER_OF_REGIONS){
        return MICROBIT_NO_DATA;
    } else {
        // Add data 
        memoryMapStore.memoryMap[i] = region;
    

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
    while(memoryMapStore.memoryMap[i].name != region.name && i < NUMBER_OF_REGIONS) i++;

    if(i == NUMBER_OF_REGIONS){
        return MICROBIT_NO_DATA;
    } else {
        // Add data 
        memoryMapStore.memoryMap[i] = region;
        updateFlash(memoryMapStore);
        return MICROBIT_OK;
    }
}
   
/**
  * Function to update the flash with the current MemoryMapStore
  *
  * @param memoryMapStore The memory map to write to flash
  */
void MicroBitMemoryMap::updateFlash(MemoryMapStore store)
{ 
    //erase the scratch page and write our new MemoryMapStore
    flashPageErase((uint32_t *)(pg_size * (NRF_FICR->CODESIZE - MICROBIT_MEMORY_MAP_SCRATCH_PAGE_OFFSET)));
    scratchMemoryMapStore(store);

    //erase flash, and copy the scratch page over
    flashPageErase((uint32_t *)flashBlockPointer);
    flashCopy((uint32_t *)(pg_size * (NRF_FICR->CODESIZE - MICROBIT_MEMORY_MAP_SCRATCH_PAGE_OFFSET)), flashBlockPointer, pg_size/4);
}

/*
 * Function to fetch the hashes from a PXT generated build
 */
int MicroBitMemoryMap::findHashes(){
    uint32_t *endAddress = (uint32_t *)(FLASH_PROGRAM_END);
    uint32_t *magicAddress = (uint32_t *)(FLASH_PROGRAM_END + 0x400);
    uint32_t *hashAddress  = (uint32_t *)(FLASH_PROGRAM_END + 0x410);
    uint32_t magicValue = *magicAddress;

    // Copy Hash
    memcpy(sdHash, magicAddress, 16);
    memcpy(dalHash, hashAddress, 16);
    memcpy(pxtHash, endAddress, 16);

    // Check for Magic
    if(magicValue == 0x7D){
        // Magic found!
        return 1;
    } else {
        return 0;
    }

}


/*
* Function to get the MD5 hash of a region of data
*
* @param data A pointer to the start of the block(Region.startAddress)
*
* @param size The size of the region
*
* @param hash Pointer to the char array used to store the MD5 hash of the region
*/
void MicroBitMemoryMap::getHash(uint32_t* startAddress, unsigned long length, char* hash)
{
    
    MD5_CTX ctx;
    // Set up MD5
    MD5_Init(&ctx);

    uint32_t volatile * startAddr = (uint32_t volatile *) startAddress;

    // Iterate through data in chunks
    uint32_t blockRemaining = length;
    uint32_t i = 0;
    uint32_t chunkSize = sizeof(uint8_t);
    while(blockRemaining > chunkSize){
        uint32_t volatile * blockPtr = (uint32_t volatile *) (startAddr + (chunkSize * i));
        MD5_Update(&ctx, &blockPtr, chunkSize);        
        blockRemaining = blockRemaining - chunkSize;
        i++;
    }
    uint32_t volatile * blockPtr = (uint32_t volatile *) startAddr + (chunkSize * i);
    MD5_Update(&ctx, &blockPtr, blockRemaining);
    
    unsigned char result[16];
    // Result
    MD5_Final(result, &ctx);
    
    // Convert to string
    char md5string[33];
    for(int i = 0; i < 16; ++i)
        sprintf(&md5string[i*2], "%02x", (unsigned int)result[i]);

    memcpy(&hash, result, sizeof(result));
    
}

