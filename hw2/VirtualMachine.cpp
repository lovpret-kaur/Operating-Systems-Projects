#include "VirtualMachine.h"
#include "Machine.h"
#include <iostream>
#include <queue>
#include <vector>
#include <stdlib.h>

using namespace std;
extern "C" {

volatile int TIMER;
static const int HIGH = 3;
static const int MED = 2;
static const int LOW = 1;




class TCB{
  public:
	TVMThreadID id;
	TVMThreadPriority priority;
	TVMThreadState state;
	TVMMemorySize mmSize;
	TVMStatus status;
	TVMTick vmTick;
	TVMThreadEntry entryCB;
	void* param;
	SMachineContext context;
	
};
//Contains all thread create to lookup
vector<TCB> threadList;

TVMThreadID curThreadID = NULL; // current operating thread


//PRIORITY QUEUE SETUP
//WILL RETURN HIGH PRIORITY > MED PRIORITY > LOW PRIORITY
struct LessThanByPriority{
  bool operator()(const TCB& lhs, const TCB& rhs) const{
    return lhs.priority < rhs.priority;
  }
};
typedef priority_queue<TCB, vector<TCB>, LessThanByPriority> pq;
// END PRIORITY QUEUE SETUP

pq priorThreads;
pq sleepThreads;

void threadSchedule(){

}


TVMMainEntry VMLoadModule(const char *module);
TVMStatus VMFilePrint(int filedescriptor, const char *format, ...);

void  callbackAlarm( void* t){
	TIMER--;

}

void idleThread(void*){
	while(true);
}


void threadWrapper(void* thread){
	// thread->entryCB(thread->param);
	// VMThreadTerminate(thread->id);
}

TVMStatus VMStart(int tickms, int argc, char *argv[]){
	MachineInitialize();
	// request time
	// int time = 0;
	TVMMainEntry mainEntry =  VMLoadModule(argv[0]);
	if(mainEntry == NULL){
		return VM_STATUS_FAILURE;
	}

	// cout << "TIMER:" << TIMER << endl;
	int flag = false;

	TMachineAlarmCallback callback = callbackAlarm;
	MachineRequestAlarm(tickms*1000,callback,&flag);
        // setup information for the main and idle thread
        TCB startB;
        startB.priority = VM_THREAD_PRIORITY_NORMAL;
        startB.state = VM_THREAD_STATE_WAITING;

        // SMachineContextRef startContext = new SMachineContext;
        // MachineContextSwitch(NULL,startContext);
        // startB.context = *startContext;

        
        TCB idleB;
        idleB.entryCB = idleThread;
        idleB.priority = VM_THREAD_PRIORITY_LOW;
        idleB.state = VM_THREAD_STATE_READY;
        idleB.mmSize = 0x10000;
        threadList.push_back(startB);
        threadList.push_back(idleB);



        mainEntry(argc,argv);
        MachineTerminate();
        return VM_STATUS_SUCCESS;
      
        


}

TVMStatus VMTickMS(int *tickmsref){
	if(tickmsref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	//tickmsref = 

	return VM_STATUS_SUCCESS; 
}

TVMStatus VMTickCount(TVMTickRef tickref){
	if(tickref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}

	return VM_STATUS_SUCCESS; 
}



TVMStatus VMThreadCreate(TVMThreadEntry entry, void *param, TVMMemorySize memsize, TVMThreadPriority prio, TVMThreadIDRef tid){
	if(entry == NULL || tid == NULL ){
		return VM_STATUS_FAILURE;
	}

	TCB threadB;
	threadB.mmSize = memsize;
	threadB.priority = prio;
	threadB.id = *tid;
	threadB.entryCB = entry;
	threadB.param = param;
	threadB.state = VM_THREAD_STATE_DEAD;
	threadList.push_back(threadB);
	priorThreads.push(threadB);
	
return VM_STATUS_SUCCESS; 
}

TVMStatus VMThreadDelete(TVMThreadID thread){
	for(unsigned int i = 0;i < threadList.size();i++){
        if(threadList[i].id == thread){
			// delete thread
      }
   }
}



TVMStatus VMThreadActivate(TVMThreadID thread){
	bool found = false;
	// find  thread with matching id given
	for  (unsigned int i = 0;i < threadList.size();i++){
        if(threadList[i].id == thread){

        	found = true;
        	if ( threadList[i].state != VM_THREAD_STATE_DEAD){
        		return VM_STATUS_ERROR_INVALID_STATE;
        	}
        	// activate thread
        	SMachineContextRef  mtContext  = new SMachineContext;
        	void* stackaddr = (void*)malloc(threadList[i].mmSize);
        	MachineContextCreate( mtContext, threadWrapper , &threadList[i], stackaddr, threadList[i].mmSize);\
        	threadList[i].context = *mtContext;
        	


		}
    }

    if(!found){
    	return VM_STATUS_ERROR_INVALID_ID;
    }

    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadTerminate(TVMThreadID thread){
	bool found = false;
	// find  thread with matching id given
	for(unsigned int i = 0;i < threadList.size();i++){
        if(threadList[i].id == thread){
        	// thread enters the dead state
        	found = true;
        	if(threadList[i].state == VM_THREAD_STATE_DEAD ){
        		return VM_STATUS_ERROR_INVALID_STATE;
        	}
	}
    }

    if(!found){
    	return VM_STATUS_ERROR_INVALID_ID;
    }
    return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadID(TVMThreadIDRef threadref){
	if(threadref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	//threadref = curThreadID;
	return VM_STATUS_SUCCESS;
}

TVMStatus VMThreadState(TVMThreadID thread, TVMThreadStateRef stateref){
	if(stateref == NULL){
		return VM_STATUS_ERROR_INVALID_PARAMETER;
	}
	bool found = false;
	// find  thread with matching id given
	for(unsigned int i = 0;i < threadList.size();i++){
        if(threadList[i].id == thread){
        	// delete thread
        	found = true;
		}
    }

    if(!found){
    	return VM_STATUS_ERROR_INVALID_ID;
    }
    return VM_STATUS_SUCCESS;	
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
	// put current thread to sleep
		TIMER = tick;
		while(TIMER != 0){

		}
	}

	return VM_STATUS_SUCCESS;
}




volatile bool writeDone = false;
void fileWCallback(void* a,int result){

	writeDone = true;
}

TVMStatus VMFileWrite(int filedescriptor, void *data, int *length){
    // write(filedescriptor,data,*length);
    TMachineFileCallback myfilecallback = fileWCallback;
   
    MachineFileWrite(filedescriptor, data, *length, myfilecallback, NULL);
    // cout << "write: " << calldata << endl;
    while(!writeDone);

    // }

    
    return VM_STATUS_SUCCESS;
}
volatile bool openDone = false; 
int fd;
void fileOpenCallback(void* calldata, int result){
    cout << "CB:" << result << endl;
    fd = result;
   
    openDone = true;
}
 
TVMStatus VMFileOpen(const char *filename, int flags, int mode, int *filedescriptor){
	TMachineFileCallback fOpenCallback = fileOpenCallback;

	MachineFileOpen(filename, flags,  mode, fOpenCallback, NULL);
	while(!openDone);
	*filedescriptor = fd;
	cout << "fileopen:" <<  fd << endl;


 return VM_STATUS_SUCCESS;
 
}
volatile bool closeDone = false;
void fileCloseCallback(void* calldata,int result){
	closeDone = true;
}
TVMStatus VMFileClose(int filedescriptor){
	TMachineFileCallback fCloseCallback = fileCloseCallback;
	
	MachineFileClose(filedescriptor, fCloseCallback, NULL);
	while(!closeDone);
	return VM_STATUS_SUCCESS;
}  

volatile bool seekdone = false;
int offset;
void fileSeekCallback(void* calldata,int result){
	cout << "seek:" << result << endl;
	offset = result;
	seekdone = true;
}

TVMStatus VMFileSeek(int filedescriptor, int offset, int whence, int *newoffset){
	TMachineFileCallback fSeekCallback = fileSeekCallback;
	
	MachineFileSeek( filedescriptor, offset, whence, fSeekCallback, NULL);
	while(!seekdone);
	*newoffset = offset;


    return VM_STATUS_SUCCESS;
}
volatile bool readDone = false;
int readsize; 
void fileReadCallback(void* calldata,int result){
	
	readsize = result;
	readDone = true;
}


TVMStatus VMFileRead(int filedescriptor, void *data, int *length){
	TMachineFileCallback fReadCallback = fileReadCallback;
	
	MachineFileRead(filedescriptor, data, *length, fReadCallback, NULL);
	while(!readDone);
	*length = readsize;


	return VM_STATUS_SUCCESS;
}

 
 

}
