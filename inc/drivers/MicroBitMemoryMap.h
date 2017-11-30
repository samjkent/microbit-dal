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

#ifndef MICROBIT_MEMORY_MAP_H
#define MICROBIT_MEMORY_MAP_H

#include "mbed.h"
#include "MicroBitConfig.h"
#include "ManagedString.h"
#include "ErrorNo.h"
#include "md5.h"

#define MICROBIT_MEMORY_MAP_MAGIC       0xCA6E

#define MICROBIT_MEMORY_MAP_PAGE_OFFSET            21 
#define MICROBIT_MEMORY_MAP_SCRATCH_PAGE_OFFSET    19      //Use the page just below the BLE Bond Data.

#define NUMBER_OF_REGIONS 5

enum RWPolicy { EMPTY, PartialFlash, FullFlash, USB };

struct Region 
{
    uint32_t startAddress;
    uint32_t endAddress;
    char name[3];
    unsigned char hash[16];
    RWPolicy rwPolicy;

    Region(uint32_t startAddress, uint32_t endAddress, char name[4], char hash[16], RWPolicy rwPolicy)
    {
        this->startAddress = startAddress;
        this->endAddress = endAddress;
        strcpy( this->name, name );
        memcpy( this->hash, hash, 16 );
        this->rwPolicy = rwPolicy;
    }

    Region()
    {
        this->startAddress = 0;
        this->endAddress = 0;
        strcpy( this->name, "" );
        memcpy( this->hash, "", 16 );
        this->rwPolicy = EMPTY;
    }

};

struct MemoryMapStore
{
    uint32_t magic;
    Region memoryMap[NUMBER_OF_REGIONS];           
};


/**
  * Class definition for the MicroBitMemoryMap class.
  * This allows reading and writing of regions within the memory map.
  * 
  * This class maps the different regions used on the flash memory to allow 
  * a region to updated independently of the others AKA Partial Flashing.
  */
class MicroBitMemoryMap
{
uint32_t pg_size = NRF_FICR->CODEPAGESIZE;
uint32_t pg_num  = NRF_FICR->CODESIZE - MICROBIT_MEMORY_MAP_PAGE_OFFSET;
uint32_t *flashBlockPointer = (uint32_t *)(pg_size * pg_num);

    /**
    * Method for erasing a page in flash.
    *
    * @param page_address Address of the first word in the page to be erased.
    */
    void flashPageErase(uint32_t * page_address);

    /**
      * Method for writing a word of data in flash with a value.
      *
      * @param address Address of the word to change.
      *
      * @param value Value to be written to flash.
      */
    void flashWordWrite(uint32_t * address, uint32_t value);

    /**
      * Function for copying words from one location to another.
      *
      * @param from the address to copy data from.
      *
      * @param to the address to copy the data to.
      *
      * @param sizeInWords the number of words to copy
      */
    void flashCopy(uint32_t* from, uint32_t* to, int sizeInWords);

    /**
    * Function for populating the scratch page with a MemoryMapStore
    *
    * @param store the MemoryMapStore struct to write to the scratch page.
    */
    void scratchMemoryMapStore(MemoryMapStore store);

    /**
    * Function to update the flash with the current MemoryMapStore
    *
    * @param memoryMapStore The memory map to write to flash
    */
    void updateFlash(MemoryMapStore store);
    
    /**
    * Function to get the MD5 hash of a region of data
    * 
    * @param data A pointer to the start of the block(Region.startAddress)
    *
    * @param size The size of the region
    *
    * @param hash Pointer to the char array used to store the MD5 hash of the region
    */
    void getHash(uint32_t* startAddress, unsigned long length, char* hash);


    public:

    MemoryMapStore memoryMapStore;

    /**
      * Default constructor.
      *
      * Creates an instance of MicroBitMemoryMap
      */
    MicroBitMemoryMap();

    /**
     * Function for adding a Region to the end of the MemoryMap
     *
     * @param region The Region to add to the MemoryMap
     *
     * @return MICROBIT_OK on success
     *
     */
    int pushRegion(Region region);
    
    /**
      * Function for updating a Region of the MemoryMap
      *
      * @param region The Region to update in the MemoryMap. The name is used as the selector.
      *
      * @return MICROBIT_OK success, MICROBIT_NO_DATA if the region is not found
      */
    int updateRegion(Region region);

    /**
     * Function to fetch hashes from PXT build
     *
     * @return int  Boolean result of the search. 1 = Hashes Found; 0 = No Hash Found
     */
    int findHashes();
};

#endif
