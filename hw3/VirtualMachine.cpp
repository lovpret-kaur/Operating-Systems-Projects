#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>
#include <queue>
#include <vector>
#include <stdlib.h>
#include <iterator>
#include <string>
#include <string.h>
#include <list>

using namespace std;
extern "C" {
#define VM_THREAD_PRIORITY_IDLE                 ((TVMThreadPriority)0x00)

  volatile int TIMER;
  static const int HIGH = 3;
  static const int MED = 2;
  static const int LOW = 1;
  TMachineSignalState sigstate;

  volatile int threadCount = 1;
  volatile int mutexCount = 1;
  volatile int poolCount = 1;
  TVMMainEntry VMLoadModule(const char *module);
  TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);

  class TCB{
    public:
      TVMThreadID id;
      TVMThreadIDRef idref;
      TVMThreadPriority priority;
      TVMThreadState state;
      TVMMemorySize mmSize;
      TVMStatus status;
      TVMTick vmTick;
      TVMThreadEntry entryCB;
      void* param;
      SMachineContext context;
      int result;
  };

  struct LessThanByPriority{
    bool operator()(const TCB* lhs, const TCB* rhs) const{
      return lhs->priority < rhs->priority;
    }
  };

  typedef priority_queue<TCB*, vector<TCB*>, LessThanByPriority> pq;

  class Mutex{
    public:
      TVMMutexID id;
      bool islocked;
      TCB* owner;

      pq waiting; 
      void nextOwner(){
        owner = waiting.top();
        waiting.pop();
      }

  };

  const TVMMemoryPoolID VM_MEMORY_POOL_ID_SYSTEM = 1;
  void* dataPtr; //will be used to shared memory read/write allocation

  TVMMemoryPoolID memoryPoolCount = 0;
  class MemoryChunk{
    public:
      void *beginPtr;
      TVMMemorySize size;
  };

  class MemPool{
    public:
      TVMMemoryPoolID id;
      TVMMemorySize memSize;
      TVMMemorySize memoryAvailable;
      void* base;
      bool deleted;

      vector<MemoryChunk> PTRS_TO_FREE_BLOCKS;
      vector<MemoryChunk> PTRS_TO_USED_BLOCKS;
  };

  vector<MemPool*> memPools;
  MemPool* findMemoryPool(TVMMemoryPoolID idSought){
    vector<MemPool*>::iterator it;
    for(it = memPools.begin(); it != memPools.end(); it++){
      if(((*it)->id) == idSought){
        return *it;
      }
    }
  }

  // threads
  vector<TCB*> threadList;
  TCB* currentThread;

  pq readyThreads;
  vector<TCB*> sleepThreads;
  vector<Mutex*> mutexList;

  /*MemPool getMemPool(TVMMemoryPoolID id ){
    list<MemPool>::iterator iter;
    for(iter = memPools.begin(); iter != memPools.end(); iter++){
    if(id == (iter)id){
    return iter;
    }
    }
    return NULL;
    }*/

  //the base and size of the memory array are specified by base and size respectively
  //the memory pool identifier is put into the TVMMemoryPoolIDRef memory
  //upon successful creation, return VM_STATUS_SUCCESS
  //if the base or memory are NULL,or size is 0, return VM_STATUS_ERROR_INVALID_PARAMETER
  TVMStatus VMMemoryPoolCreate(void *base, TVMMemorySize size, TVMMemoryPoolIDRef memory){
    int validSizes = memoryPoolCount;
    if(size == 0 || base == NULL || memory == NULL){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    //else
    //create memory in memorypools
    //cout << "\nCreating Memory Pool\n";
        //cout << "Size: " << NEWBLOCK->size << endl;
    MemPool* NEWPOOL = new MemPool;
    //cout << "ID: " << memoryPoolCount << endl;
    *memory = memoryPoolCount;
    NEWPOOL->id = memoryPoolCount++;
    NEWPOOL->memSize = size;
    NEWPOOL->memoryAvailable = size;
    NEWPOOL->base = base;
    NEWPOOL->deleted = false;
    MemoryChunk* NEWBLOCK = new MemoryChunk;
    NEWBLOCK->beginPtr = base;
    NEWBLOCK->size = size;
    NEWPOOL->PTRS_TO_FREE_BLOCKS.push_back(*NEWBLOCK);
    memPools.push_back(NEWPOOL);
    return VM_STATUS_SUCCESS;
  }

  //deletes a memory pool that has no memory allocated from the pool, memory pool is specified by memory parameter
  //successful deletion: return VM_STATUS_SUCCESS
  //if memory is not a valid memory pool, return VM_STATUS_ERROR_INVALID_PARAMETER
  //if memory has been allocated and not deallocated, return VM_STATUS_ERROR_INVALID_STATE
  TVMStatus VMMemoryPoolDelete(TVMMemoryPoolID memory){
    int validSizes = memoryPoolCount;
    if(validSizes < memory) return VM_STATUS_ERROR_INVALID_PARAMETER;
    MemPool* CURRENT_MEMORY_POOL = findMemoryPool(memory);
    //if(memory is not a valid memory pool)
    //return VM_STATUS_ERROR_INVALID_PARAMETER;
    //if(memorypools[memory] has memory allocated)
    //return VM_STATUS_ERROR_INVALID_STATE;
    //else
    // DELETE MEMORY POOL
    if(CURRENT_MEMORY_POOL->memSize != CURRENT_MEMORY_POOL->memoryAvailable){
      return VM_STATUS_ERROR_INVALID_STATE;
    }
    if(CURRENT_MEMORY_POOL->deleted == false){
      CURRENT_MEMORY_POOL->deleted = true;
      return VM_STATUS_SUCCESS;
    }

    return VM_STATUS_SUCCESS;
  }

  //query a memory pool for the available memory left
  //the memory pool id is 'memory'
  //the space left unallocated in the memory pool is placed inside bytesleft
  TVMStatus VMMemoryPoolQuery(TVMMemoryPoolID memory, TVMMemorySizeRef bytesleft){
    int validSizes = memoryPoolCount;
    if(bytesleft == NULL || validSizes < memory){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    MemPool* CURRENT_MEMORY_POOL = findMemoryPool(memory);
    //cout << "Querying Pool: " << memory << endl;
    //if 'memory' is not a valid memory pool or bytesleft is NULL
    //return VM_STATUS_ERROR_INVALID_PARAMETER;
    *bytesleft = CURRENT_MEMORY_POOL->memoryAvailable;
    //cout << "Bytes left: " << *bytesleft << endl;
    //upon successful querying of the memory pool
    return VM_STATUS_SUCCESS;
  }

  //the memory pool to allocate is specified by the memory parameter
  //size is allocated by the size and the base of the allocated array specified by the pointer
  //the allocated size will be rounded to the next multiple of 64bytes that is greater than or equal to the size parameter
  TVMStatus VMMemoryPoolAllocate(TVMMemoryPoolID memory, TVMMemorySize size, void **pointer){
    int validSizes = memoryPoolCount;
    if(memory > validSizes || size == 0 || pointer == NULL){
      cout << "ERROR 1" << endl;
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    MemPool* CURRENT_MEMORY_POOL = findMemoryPool(memory);
    //if memory pool does not have sufficient memory to allocate array of size bytes
    if(CURRENT_MEMORY_POOL->memoryAvailable < size){
      //SOMEWHERE I DO NOT UPDATE CURRENT_MEMORY_POOL MEMORY AVAILABLE
      //FIXED WHEN AVAILABLE MEMORY WHEN NO BLOCKS FOUND IN DEALLOCATE IS ADDED
      //STATUS: FIXED
      return VM_STATUS_ERROR_INSUFFICIENT_RESOURCES;
    }
    //cout << "TRYING TO ALLOCATE: " << size << " FROM MEMORY POOL: " << memory << endl;
    unsigned int SIZE_NEEDED = (size + 0x3F)&(~0x3F);
    //cout << "ALLOCATION: " << SIZE_NEEDED << endl;
    //cycle through the free Spaces
    vector<MemoryChunk> *FREE_BLOCKS = &(CURRENT_MEMORY_POOL->PTRS_TO_FREE_BLOCKS);
    int NUMBER_OF_FREE_BLOCKS = FREE_BLOCKS->size();
    for(int i = 0; i < NUMBER_OF_FREE_BLOCKS; i++){
      void* NEWBASE = (*FREE_BLOCKS)[i].beginPtr;
      MemoryChunk *FREE_BLOCK = &((*FREE_BLOCKS)[i]);
      //cout << "Memory Chunk size: " << (*FREE_BLOCKS)[i].size << endl;
      //if memorychunk at location i > SIZE_NEEDED
      if(FREE_BLOCK->size >= SIZE_NEEDED){
        if(FREE_BLOCK->size == SIZE_NEEDED){
          //set base of first free array into pointer
          //beginning of the free block
          *pointer = NEWBASE;
          CURRENT_MEMORY_POOL->memoryAvailable = 0;
          //cout << "SIZE LEFT: " << CURRENT_MEMORY_POOL->memoryAvailable << endl;
          //put completed allocated space into the used blocks vector
          (CURRENT_MEMORY_POOL->PTRS_TO_USED_BLOCKS).push_back(*FREE_BLOCK);
          //remove this block from the free blocks vector
          //cout << "\nBEFORE ERASE FREE: " << (FREE_BLOCKS)->size() << endl;
          (*FREE_BLOCKS).erase((*FREE_BLOCKS).begin()+i);
          //cout << "AFTER ERASE FREE: " << (FREE_BLOCKS)->size() << endl;
          return VM_STATUS_SUCCESS;
        }
        // if it's not exactly equal to the size
        // split FREE PARTITION
        //         /       \
        //      NEW    OLD BLOCK PUSHED FORWARD
        MemoryChunk* NEW = new MemoryChunk;
        //set NEW to point to beginning of chunk
        //size of NEW will be size required for allocation
        NEW->beginPtr = NEWBASE;
        NEW->size = SIZE_NEEDED;

        //this chunk is now USED
        (CURRENT_MEMORY_POOL->PTRS_TO_USED_BLOCKS).push_back(*NEW);
        //send location back to machine
        *pointer = NEWBASE;

        //push OLD BLOCK forward, and size = size minus length
        (*FREE_BLOCKS)[i].beginPtr = NEWBASE + SIZE_NEEDED;
        (*FREE_BLOCKS)[i].size = (*FREE_BLOCKS)[i].size - SIZE_NEEDED;
        
        CURRENT_MEMORY_POOL->memoryAvailable = CURRENT_MEMORY_POOL->memoryAvailable - SIZE_NEEDED;
        return VM_STATUS_SUCCESS;
      }
    }
    return VM_STATUS_SUCCESS;
  }


  //the memory pool to deallocate is specified by the memory parameter
  //the base of the previously allocated array is specified by pointer
  TVMStatus VMMemoryPoolDeallocate(TVMMemoryPoolID memory, void *pointer){
    //cout << "DEALLOCATING ID: " << memory << endl;
    MemPool* CURRENT_MEMORY_POOL = findMemoryPool(memory);
    //check if there is free memory before the used 
    //                  
    //        [ FREE ] [ USED ] [UNKNOWN]   ---> [ FREE        ] [UNKNOWN]   
    //                  
    vector<MemoryChunk> *FREE_BLOCKS = &(CURRENT_MEMORY_POOL->PTRS_TO_FREE_BLOCKS);


    vector<MemoryChunk> *USED_BLOCKS = &(CURRENT_MEMORY_POOL->PTRS_TO_USED_BLOCKS);
    int NUMBER_OF_FREE_BLOCKS = FREE_BLOCKS->size();
    int NUMBER_OF_USED_BLOCKS = USED_BLOCKS->size();
    MemoryChunk* memoryBlockTarget;
    void* BEGINNING_OF_USED_BLOCK;
    void* BEGINNING_OF_FREE_BLOCK;
    void* LOOK_AHEAD_POINTER;
    bool deallocateAll = false;
        //cout << "Number of free blocks: " << (FREE_BLOCKS)->size() << endl;
        unsigned int totalfree = 0;
        for(int m = 0; m < NUMBER_OF_FREE_BLOCKS; m++){
          totalfree += (*FREE_BLOCKS)[m].size;
        }
        //cout << "Total size of free blocks: " << totalfree << endl;
        //cout << "Number of used blocks: " << (USED_BLOCKS)->size() << endl;
        unsigned int totalused = 0;
        for(int m = 0; m < NUMBER_OF_USED_BLOCKS; m++){
          totalused += (*USED_BLOCKS)[m].size;
        }
        //cout << "Total size of used blocks: " << totalused << endl;

    //scan used blocks for "pointer" to deallocate
    for(int i = 0; i < NUMBER_OF_USED_BLOCKS; i++){
      BEGINNING_OF_USED_BLOCK = (*USED_BLOCKS)[i].beginPtr;
      //if the beginning of the used block what we need to deallocate
      if(BEGINNING_OF_USED_BLOCK == pointer){
        memoryBlockTarget = &((*USED_BLOCKS)[i]);
        for(int k = 0; k < NUMBER_OF_FREE_BLOCKS; k++){
          //cout << "BEGINNING OF FREE BLOCK: " << (*USED_BLOCKS)[k].beginPtr << endl;
        }
        //cout << "BEGINNING OF BLOCK TO DELETE: " << pointer << endl;

        if(NUMBER_OF_FREE_BLOCKS == 0){
          //if there are no free blocks, need to free entire chunk
          //cout << "NO FREE BLOCKS, PUSHING USED BLOCK TO FREE BLOCKS" << endl;
          (FREE_BLOCKS)->push_back((*USED_BLOCKS)[i]);
          // BELOW LINE MIGHT BE BUGGY
          CURRENT_MEMORY_POOL->memoryAvailable += (*USED_BLOCKS)[i].size;
          (*USED_BLOCKS).erase((*USED_BLOCKS).begin()+i);
          deallocateAll = true;
          break;
        }

        for(int k = 0; k < NUMBER_OF_FREE_BLOCKS; k++){
          if(((*FREE_BLOCKS)[k].beginPtr+(*FREE_BLOCKS)[k].size) == (*USED_BLOCKS)[i].beginPtr){
            //then (*FREE_BLOCKS)[k] is the MemoryChunk before the Used Block to delete
            //cout << "Found a free block right before the used block" << endl;
            (*FREE_BLOCKS)[k].size = (*FREE_BLOCKS)[k].size + (*USED_BLOCKS)[i].size;
            CURRENT_MEMORY_POOL->memoryAvailable = CURRENT_MEMORY_POOL->memoryAvailable + (*USED_BLOCKS)[i].size;
            (*USED_BLOCKS).erase((*USED_BLOCKS).begin()+i);
            //(*FREE_BLOCKS).erase((*FREE_BLOCKS).begin()+i);
          }
        }
        //cout << "Number of free blocks: " << (FREE_BLOCKS)->size() << endl;
        //cout << "Number of used blocks: " << (USED_BLOCKS)->size() << endl;
      }
    }
    if(deallocateAll == true){  
      //USED_BLOCKS->erase(pointer);
      //CURRENT_MEMORY_POOL->availableMemory += ((MemoryChunk*)pointer)->size();
    }
    /*
    for(int i = 0; i < NUMBER_OF_FREE_BLOCKS; i++){
      void* CURRENTBASE = (*FREE_BLOCKS)[i].beginPtr;
      MemoryChunk *FREE_BLOCK = &((*FREE_BLOCKS)[i]);
      void* BEGINNING_OF_USED_BLOCK = (*USED_BLOCKS)[i].beginPtr;


      if(CURRENTBASE = 
    } */
    
    NUMBER_OF_FREE_BLOCKS = FREE_BLOCKS->size();
    NUMBER_OF_USED_BLOCKS = USED_BLOCKS->size();
    totalfree = 0;
    //cout << "Number of free blocks: " << (FREE_BLOCKS)->size() << endl;
        for(int m = 0; m < NUMBER_OF_FREE_BLOCKS; m++){
          totalfree += (*FREE_BLOCKS)[m].size;
        }
        //cout << "Total size of free blocks: " << totalfree << endl;
        //cout << "Number of used blocks: " << (USED_BLOCKS)->size() << endl;
        totalused = 0;
        
        for(int m = 0; m < NUMBER_OF_USED_BLOCKS; m++){
          totalused += (*USED_BLOCKS)[m].size;
        }
        //cout << "Total size of used blocks: " << totalused << endl;

    //cout << endl;
    return VM_STATUS_SUCCESS;
  }





  Mutex* getMutex(TVMMutexID mutexId){
    for(unsigned int i = 0; i < mutexList.size(); i++ ){
      if ( mutexId == mutexList[i]->id )
        return mutexList[i];
    }
    return NULL;
  }

  TCB* getTCB(TVMThreadID id){
    for (unsigned int i = 0;i < threadList.size();i++){
      if (id == threadList[i]->id){
        return threadList[i];
      }
    }
    return NULL;
  }

  void threadSchedule(){
    TCB* oldThread = currentThread;
    //TCB* nextThread;
    // cout << "schedule " << endl;
    //cout << "current id: " << currentThread->id <<  endl;
    //cout << "readyThread: " << readyThreads.top()->id << endl;
    // cout << "size: " << readyThreads.size() << endl;
    if (readyThreads.empty() ){
      readyThreads.push(getTCB(2));
    }
    if(!readyThreads.empty( )){

      currentThread = readyThreads.top();
      currentThread->state = VM_THREAD_STATE_RUNNING;
      readyThreads.pop();
      //cout << "old thread id: " << oldThread->id << endl;
      //cout << "new thread id: " << currentThread->id << endl;

      // cout << "new id: " << currentThread->id << endl;
      if (oldThread->id != currentThread->id){
        //	cout << "switch" << endl;
        SMachineContextRef oldContext = &oldThread->context;
        SMachineContextRef newContext = &currentThread->context; 
        MachineContextSwitch(oldContext,newContext);
      }
    }
    // cout <<  "finish schedule" << endl;
  }

  void idleThread(void*){
    // cout << "idle" << endl;
    MachineResumeSignals(&sigstate);
    while(true);
    MachineResumeSignals(&sigstate);
  }

  void  callbackAlarm( void* t){
    for(unsigned int i = 0; i < sleepThreads.size();i++){
      //	cout << "ticks: " << sleepThreads[i]->vmTick << endl;
      if ( sleepThreads[i]->vmTick == 0){
        //	cout << "timeout:" << endl;
        sleepThreads[i]->state = VM_THREAD_STATE_READY;
        //cout << "pushing " << sleepThreads[i]->id << endl; 
        readyThreads.push(sleepThreads[i]);
        sleepThreads.erase(sleepThreads.begin() + i);
        threadSchedule();
      }	
      sleepThreads[i]->vmTick--;
    }
  }

  TVMStatus VMThreadSleep(TVMTick tick){
    if (tick == VM_TIMEOUT_INFINITE){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    if (tick == VM_TIMEOUT_IMMEDIATE){
      //current process yields the remainder of its processing quantum
      // to the next ready process of equla priority
    }
    else{
      currentThread->vmTick = tick;
      currentThread->state = VM_THREAD_STATE_WAITING;
      sleepThreads.push_back(currentThread);
      threadSchedule();
      //cout << "sleep thread id: " << currentThread->id << endl;		
    }
    //threadSchedule();
    return VM_STATUS_SUCCESS;
  }

  //MachineInitialize(size_t sharesize);
  //the sharesize specifies the size of the shared memory location to be used in the machine
  //the size of the shared memory will be set to an integral number of pages (4096 bytes) that covers the size of sharesize

  //MachineFileRead
  //void
  // MachineFileRead(
  // int
  // fd, 
  // void
  // *data, 
  // int
  // length, 
  // TMachineFileCallback callback, 
  // void
  // *calldata);

  // heapsize of the virtual machine is specified by heapsize
  // the heap is accessed by the applications through the VM_MEMORY_POOL_ID_SYSTEM memory pool
  // the size of shared memory space between the virtual machine and the machine is specified by the sharedsize
  TVMStatus VMStart(int tickms, TVMMemorySize heapsize, TVMMemorySize sharedsize, int argc, char *argv[]){
    dataPtr = MachineInitialize(sharedsize);

    MemoryChunk* shared = new MemoryChunk;
    shared->beginPtr = dataPtr;
    shared->size = sharedsize;
    MemPool* sharedMM = new MemPool;
    sharedMM->id = memoryPoolCount++;
    sharedMM->memSize = sharedsize;
    //cout << "Sharedsize: " << sharedsize << endl;
    sharedMM->memoryAvailable = sharedsize;
    sharedMM->base = dataPtr;
    sharedMM->deleted = false;
    sharedMM->PTRS_TO_FREE_BLOCKS.push_back(*shared);
    memPools.push_back(sharedMM);

    // allocate system memory
    void* baseMemory = new uint8_t[heapsize];
    MemoryChunk* chunk = new MemoryChunk;
    chunk->beginPtr = baseMemory;
    chunk->size = heapsize;
    MemPool* heapMM = new MemPool; 
    heapMM->id = memoryPoolCount++;
    heapMM->memSize = heapsize;
    heapMM->memoryAvailable = heapsize;
    //cout << "Heapsize: " << heapsize << endl;
    heapMM->base = baseMemory;
    heapMM->deleted = false;
    heapMM->PTRS_TO_FREE_BLOCKS.push_back(*chunk);
    memPools.push_back(heapMM); 


    // request time
    TVMMainEntry mainEntry =  VMLoadModule(argv[0]);
    if(mainEntry == NULL){
      return VM_STATUS_FAILURE;
    }

    TMachineAlarmCallback callback = callbackAlarm;
    MachineRequestAlarm(tickms*1000,callback,NULL);
    //MemoryChunk* base =  
    // allocate base of memory

    // setup information for the main and idle thread
    //main thread
    TCB* tstartBlk = new TCB;
    tstartBlk->id = threadCount++;
    tstartBlk->idref = 0;
    tstartBlk->priority = VM_THREAD_PRIORITY_NORMAL;
    tstartBlk->state = VM_THREAD_STATE_RUNNING;
    tstartBlk->mmSize = 0;
    tstartBlk->vmTick = 0;
    // idle thread
    TCB *idleB = new TCB;
    idleB->entryCB = idleThread;
    idleB->priority = VM_THREAD_PRIORITY_IDLE;
    idleB->state = VM_THREAD_STATE_READY;
    idleB->mmSize = 0x10000;
    idleB->vmTick = 0;
    idleB->id = threadCount++;
    SMachineContextRef idleContext = new SMachineContext;
    void* stackaddr = (void*)malloc(idleB->mmSize);
    MachineContextCreate( idleContext, idleB->entryCB , idleB, stackaddr, idleB->mmSize);
    idleB ->context = *idleContext;

    threadList.push_back(idleB);
    currentThread = tstartBlk;

    readyThreads.push(idleB);
    mainEntry(argc,argv);

    MachineTerminate();

    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMTickMS(int *tickmsref){
    if(tickmsref == NULL){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    *tickmsref = (currentThread->vmTick);
    return VM_STATUS_SUCCESS; 
  }

  TVMStatus VMTickCount(TVMTickRef tickref){
    if(tickref == NULL){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    return VM_STATUS_SUCCESS; 
  }

  TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
    MachineSuspendSignals(&sigstate);
    if(entry == NULL || tid == NULL ){
      return VM_STATUS_FAILURE;
    }
    TCB *threadB = new TCB;
    threadB->mmSize = memsize;
    threadB->priority = prio;
    *tid = threadCount;
    threadB->id = threadCount++;
    threadB->entryCB = entry;
    threadB->param = param;
    threadB->state = VM_THREAD_STATE_DEAD;
    threadList.push_back(threadB);
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS; 
  }

  TVMStatus VMThreadDelete(TVMThreadID thread){
    MachineSuspendSignals(&sigstate);
    bool found = false;
    vector<TCB*>::iterator iter;
    for(iter = threadList.begin();iter != threadList.end();iter++){
      if((*iter)->id == thread){
        found = true;
        threadList.erase(iter);
      }
    }
    if(!found){
      return VM_STATUS_ERROR_INVALID_ID;
    }
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS; 
  }

  void threadWrapper(void* thread ){
    MachineSuspendSignals(&sigstate);
    ((TCB*)thread)->entryCB(((TCB*)thread)->param);
    VMThreadTerminate( ((TCB*)thread)->id);
    MachineResumeSignals(&sigstate);
  }

  TVMStatus VMThreadActivate(TVMThreadID thread){
    MachineSuspendSignals(&sigstate);
    TCB* activateTCB = getTCB(thread);
    if (activateTCB != NULL){
      if (activateTCB->state != VM_THREAD_STATE_DEAD )
        return VM_STATUS_ERROR_INVALID_STATE;

      SMachineContextRef  mtContext  = new SMachineContext;
      void* stackaddr = (void*)malloc(activateTCB->mmSize);
      MachineContextCreate( mtContext, threadWrapper , activateTCB, stackaddr, activateTCB->mmSize);
      activateTCB->context = *mtContext;
      activateTCB->state = VM_THREAD_STATE_READY;
      readyThreads.push(activateTCB);

      if ( (activateTCB->priority > currentThread->priority) || (currentThread->state == VM_THREAD_STATE_WAITING)){
        threadSchedule();
      }
    }
    else{
      return VM_STATUS_ERROR_INVALID_ID;
    }

    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadTerminate(TVMThreadID thread){
    MachineSuspendSignals(&sigstate);
    TCB* terminateTCB = getTCB(thread);
    if (terminateTCB != NULL){
      if(terminateTCB == VM_THREAD_STATE_DEAD){
        return VM_STATUS_ERROR_INVALID_STATE;
      }
      terminateTCB->state = VM_THREAD_STATE_DEAD;
    }
    else{
      return VM_STATUS_ERROR_INVALID_ID;
    }
    threadSchedule();
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadID(TVMThreadIDRef threadref){
    if(threadref == NULL){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }

    *threadref = currentThread->id;
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){

    if(stateref == NULL){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    TCB* stateTCB = getTCB(thread);
    if(stateTCB != NULL){
      *stateref = stateTCB->state;
    }
    else{
      return VM_STATUS_ERROR_INVALID_ID;
    }
    return VM_STATUS_SUCCESS;	
  }

  TVMStatus VMMutexCreate(TVMMutexIDRef mutexref){
    if ( mutexref == NULL){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    Mutex* mutex =  new Mutex;
    mutex->islocked = false;
    mutex->id = mutexCount++;
    *mutexref = mutex->id;
    mutexList.push_back(mutex);
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexDelete(TVMMutexID mutex){
    bool found = false;
    for (unsigned int i = 0; i < mutexList.size();i++){
      if (mutexList[i]->id == mutex ){
        //check if held by a thread
        mutexList.erase(mutexList.begin() + i );
        found = true;
      }	
    }
    if (!found)
      return VM_STATUS_ERROR_INVALID_ID;



    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexQuery(TVMMutexID mutex, TVMThreadIDRef ownerref){
    if (ownerref == NULL){
      return VM_STATUS_ERROR_INVALID_PARAMETER;
    }
    // mutex 
    Mutex* queryMutex = getMutex(mutex);
    if (queryMutex == NULL){
      return VM_STATUS_ERROR_INVALID_ID;
    }
    if ( queryMutex->islocked ){
      return VM_THREAD_ID_INVALID;
    }
    else{
      *ownerref = queryMutex->owner->id;
    } 
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexAcquire(TVMMutexID mutex, TVMTick timeout){
    if (timeout == 0){
      return VM_STATUS_FAILURE;
    }
    Mutex* mutexAcquire = getMutex(mutex);
    if (mutexAcquire == NULL ){
      return VM_STATUS_ERROR_INVALID_ID;
    }
    if (timeout == VM_TIMEOUT_IMMEDIATE){
      if (!mutexAcquire->islocked){
        mutexAcquire->islocked = true;
        mutexAcquire->owner = currentThread;
        return VM_STATUS_SUCCESS;
      }
      else{
        return VM_STATUS_FAILURE;
      }

    }
    else if (timeout == VM_TIMEOUT_INFINITE){
      currentThread->state = VM_THREAD_STATE_WAITING;
      mutexAcquire->waiting.push(currentThread);
    }
    return VM_STATUS_SUCCESS;
  }

  TVMStatus VMMutexRelease(TVMMutexID mutex){
    Mutex* mutexRelease = getMutex(mutex);
    if (mutexRelease == NULL ){
      return VM_STATUS_ERROR_INVALID_ID;
    }
    return VM_STATUS_SUCCESS;	
  }

  volatile bool writeDone = false;
  void fileWCallback(void* thread,int result){
    MachineSuspendSignals(&sigstate);
    readyThreads.push((TCB*)thread);
    if ( ( (TCB*)thread)->priority > currentThread->priority )
      threadSchedule();
    MachineResumeSignals(&sigstate);
  }

  TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
    MachineSuspendSignals(&sigstate);
    TMachineFileCallback myfilecallback = fileWCallback; 
    int totalBytes = *length, bytes;
    void* newMemoryPool;
    VMMemoryPoolAllocate((TVMMemoryPoolID)0, 512, &newMemoryPool);
    while(totalBytes>0){  
      (totalBytes >= 512) ? bytes = 512 : bytes = totalBytes;
      memcpy(newMemoryPool, data, bytes);
      MachineFileWrite(filedescriptor, newMemoryPool, bytes, myfilecallback, currentThread);
      totalBytes = totalBytes - bytes;
      data += bytes;
      currentThread->state = VM_THREAD_STATE_WAITING;
      threadSchedule();
    }
    VMMemoryPoolDeallocate((TVMMemoryPoolID)0, &newMemoryPool);
    //memcpy(dataPtr,data,*length);
    //MachineFileWrite(filedescriptor, dataPtr, *length, myfilecallback, currentThread);
    //currentThread->state = VM_THREAD_STATE_WAITING;
    //threadSchedule();
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS;
  }

  void fileOpenCallback(void* thread , int result){
    MachineSuspendSignals(&sigstate);
    ((TCB*)thread)->result = result;
    readyThreads.push( (TCB*)thread);
    if ( ( (TCB*)thread)->priority > currentThread->priority )
      threadSchedule();
    MachineResumeSignals(&sigstate);
  }

  TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
    MachineSuspendSignals(&sigstate);
    TMachineFileCallback fOpenCallback = fileOpenCallback;
    MachineFileOpen(filename, flags,  mode, fOpenCallback, currentThread);
    currentThread->state = VM_THREAD_STATE_WAITING;
    threadSchedule();
    *filedescriptor = currentThread->result; 
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS;
  }


  void fileCloseCallback(void* thread ,int result){
    readyThreads.push( (TCB*)thread);
    if ( ( (TCB*)thread)->priority > currentThread->priority )
      threadSchedule();
  }
  TVMStatus VMFileClose(int filedescriptor){
    MachineSuspendSignals(&sigstate);
    TMachineFileCallback fCloseCallback = fileCloseCallback;
    MachineFileClose(filedescriptor, fCloseCallback, currentThread);
    currentThread->state = VM_THREAD_STATE_WAITING;
    threadSchedule();
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS;
  }  

  void fileSeekCallback(void* thread ,int result){
    readyThreads.push( (TCB*)thread);
    ((TCB*)thread)->result = result;
    if ( ( (TCB*)thread)->priority > currentThread->priority )
      threadSchedule();
  }

  TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
    MachineSuspendSignals(&sigstate);
    TMachineFileCallback fSeekCallback = fileSeekCallback;
    MachineFileSeek( filedescriptor, offset, whence, fSeekCallback, currentThread);
    threadSchedule();
    *newoffset = currentThread->result;
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS;
  }

  void fileReadCallback(void* thread ,int result){
    ((TCB*)thread)->result = result;
    readyThreads.push( (TCB*)thread);
    if ( ( (TCB*)thread)->priority > currentThread->priority )
      threadSchedule();
  }


  TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
    MachineSuspendSignals(&sigstate);
    TMachineFileCallback fReadCallback = fileReadCallback;
    int totalBytes = *length, bytes;
    void* newMemoryPool;
    VMMemoryPoolAllocate((TVMMemoryPoolID)0, 512, &newMemoryPool);
    while(totalBytes>0){  
      (totalBytes >= 512) ? bytes = 512 : bytes = totalBytes;
      MachineFileRead(filedescriptor, newMemoryPool, bytes, fReadCallback, currentThread);
      currentThread->state = VM_THREAD_STATE_WAITING;
      threadSchedule();
      memcpy(data, newMemoryPool, bytes);
      totalBytes = totalBytes - bytes;
      data+= bytes; 
    }
    VMMemoryPoolDeallocate((TVMMemoryPoolID)0, &newMemoryPool);
    //MachineFileRead(filedescriptor, dataPtr , *length, fReadCallback, currentThread);
    //currentThread->state = VM_THREAD_STATE_WAITING;
    //threadSchedule();
    //*length = currentThread->result;
    //memcpy(data,dataPtr,*length);
    MachineResumeSignals(&sigstate);
    return VM_STATUS_SUCCESS;
  }
} 
