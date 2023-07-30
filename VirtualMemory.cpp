#include "VirtualMemory.h"
#include "PhysicalMemory.h"


word_t ceil(float num){
    return (num - (word_t)num) == 0 ? (word_t) num : (word_t) (num + 1);
}

void unlinkChildFromTable(uint64_t parentFrameIndex, uint64_t childFrameIndex){
    uint64_t startAddr = parentFrameIndex * PAGE_SIZE;
    word_t val;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        PMread(startAddr + i, &val);
        if ((uint64_t)val == childFrameIndex) {
            PMwrite(startAddr + i , 0);
        }
    }
}

void cleanFrame(uint64_t frameIndex) {
    uint64_t frameAddr = frameIndex * PAGE_SIZE;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        PMwrite(frameAddr + i, 0);
    }
}

bool isFrameEmpty(uint64_t frameIndex) {
    uint64_t startAddr = frameIndex * PAGE_SIZE;
    word_t val;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        PMread(startAddr + i, &val);
        if (val != 0) {
            return false;
        }
    }
    return true;
}

int absoluteValue(int num){
    if(num < 0){
        return - num;
    }
    return num;
}

uint64_t cyclicalDistance(uint64_t pageSwappedIn, uint64_t p) {
    if ((NUM_PAGES - absoluteValue((int)pageSwappedIn - (int)p)) <  absoluteValue((int)pageSwappedIn - (int)p)) {
        return (NUM_PAGES - absoluteValue((int)pageSwappedIn - (int)p));
    }
    return absoluteValue((int)pageSwappedIn - (int)p);
}

void traversTree(uint64_t frameIndex, uint64_t partialAddr, uint64_t currDepth, uint64_t parent,
                 uint64_t * emptyTableFrameIndex,uint64_t * emptyTableParent, uint64_t * maxInUseFrameIndex,
                 uint64_t * maxDisPageFrameIndex, uint64_t * maxDisPageIndex, uint64_t * maxDisPageParent,
                 uint64_t frameToSkip, uint64_t newPageIndex) {
    if(frameIndex > *maxInUseFrameIndex){
        *maxInUseFrameIndex = frameIndex;
    }

    // checks if we reached max tree depth meaning that curr frame is page
    if (currDepth == TABLES_DEPTH) {
        uint64_t currDist = cyclicalDistance(newPageIndex , partialAddr);
        uint64_t maxDist = cyclicalDistance(newPageIndex , *maxDisPageIndex);
        if ((currDist > maxDist) || (*maxDisPageIndex == 0)){
            *maxDisPageFrameIndex = frameIndex;
            *maxDisPageIndex = partialAddr;
            *maxDisPageParent = parent;
        }
        return;
    }

    // iterate over rows of table that are linked to other frames
    uint64_t frameAddr = frameIndex * PAGE_SIZE;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        word_t childFrame;
        PMread(frameAddr + i, &childFrame);
        if (childFrame != 0) {
            traversTree((uint64_t)childFrame, (partialAddr << OFFSET_WIDTH) + i,
                        currDepth + 1 ,frameIndex, emptyTableFrameIndex,
                        emptyTableParent, maxInUseFrameIndex, maxDisPageFrameIndex, maxDisPageIndex,
                        maxDisPageParent, frameToSkip, newPageIndex);
        }
    }

    // checks if curr frame empty and can be used
    if (isFrameEmpty(frameIndex) && (frameIndex != 0) && (frameIndex != frameToSkip)){
        if(*emptyTableFrameIndex == 0) {
            *emptyTableFrameIndex = frameIndex;
            *emptyTableParent = parent;
        }
    }
}

uint64_t getAvAvailableFrame(uint64_t newPageIndex, uint64_t frameToSkip, bool isPage){
    uint64_t emptyTableFrameIndex = 0;
    uint64_t emptyTableParent = 0;
    uint64_t maxInUseFrameIndex = 0;
    uint64_t maxDisPageFrameIndex = 0;
    uint64_t maxDisPageIndex = 0;
    uint64_t maxDisPageParent = 0;
    traversTree(0, 0, 0, 0,
                &emptyTableFrameIndex, &emptyTableParent , &maxInUseFrameIndex,
                &maxDisPageFrameIndex, &maxDisPageIndex, &maxDisPageParent , frameToSkip, newPageIndex);
    if(emptyTableFrameIndex != 0){
        unlinkChildFromTable(emptyTableParent, emptyTableFrameIndex);
        cleanFrame(emptyTableFrameIndex);

        return emptyTableFrameIndex;
    }

    if(maxInUseFrameIndex + 1 < NUM_FRAMES){
        if(!isPage){
            cleanFrame(maxInUseFrameIndex + 1);
        }

        return maxInUseFrameIndex + 1;
    }

    PMevict(maxDisPageFrameIndex, maxDisPageIndex);
    if(!isPage){
        cleanFrame(maxDisPageFrameIndex);
    }

    unlinkChildFromTable(maxDisPageParent, maxDisPageFrameIndex);

    return maxDisPageFrameIndex;
}

uint64_t getPhysicalAddrFromVirtualAddr(uint64_t virtualAddr){
    word_t mask = PAGE_SIZE - 1;
    uint64_t pageIndex = (uint64_t) virtualAddr / PAGE_SIZE ;
    word_t counter = ceil( (float )VIRTUAL_ADDRESS_WIDTH / (float)OFFSET_WIDTH ) -1;
    uint64_t currAddr = 0;
    word_t nextAddr = 0;
    bool isPathToPageExists = true;

    for (int i = counter; i > 0; --i) {
        PMread((currAddr * PAGE_SIZE) + ((virtualAddr >> (i * OFFSET_WIDTH)) & mask), &nextAddr);
        if(nextAddr == 0){
            isPathToPageExists = false;
            nextAddr = getAvAvailableFrame(pageIndex, currAddr, (i == 1));
            PMwrite((currAddr * PAGE_SIZE) + ((virtualAddr >> (i * OFFSET_WIDTH)) & mask), nextAddr);
        }
        currAddr = nextAddr;
    }

    if(!isPathToPageExists){
        PMrestore(currAddr, pageIndex);
    }

    return  (currAddr * PAGE_SIZE) + ((virtualAddr >> (0 * OFFSET_WIDTH)) & mask);
}


/*
 * Initialize the virtual memory.
 */
void VMinitialize() {
    cleanFrame(0);
}

/* Reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t* value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }

    uint64_t physicalAddr = getPhysicalAddrFromVirtualAddr(virtualAddress);
    PMread(physicalAddr, value);

    return 1;
}

/* Writes a word to the given virtual address.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMwrite(uint64_t virtualAddress, word_t value) {
    if (virtualAddress >= VIRTUAL_MEMORY_SIZE) {
        return 0;
    }

    uint64_t physicalAddr = getPhysicalAddrFromVirtualAddr(virtualAddress);
    PMwrite(physicalAddr, value);

    return 1;
}
