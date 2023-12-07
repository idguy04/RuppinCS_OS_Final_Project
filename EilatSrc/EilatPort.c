#define _CRT_SECURE_NO_WARNINGS
//libraries
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <math.h>

// == defines ==
#define BUFFER_SIZE 55
#define MAX_SLEEP_TIME 3000 //ms
#define MIN_SLEEP_TIME 5 //ms
#define MAX_CARGO_WEIGHT 50 //tons
#define MIN_CARGO_WEIGHT 5 //tons

// == Main ==
HANDLE backToHaifaMutex, randMutex;
HANDLE readFromHaifa, writeToHaifa;
DWORD read, written;
CHAR buffer[BUFFER_SIZE];

int approveSailing(); // read sailing request from Haifa and approve / disapprove
int initGlobalData(); // initialise the global data
int vesselsArrival(); // listen to vessels arriving
int releaseCranes(); // release the cranes after all thread are done
int getDivisor(); // get the divisor between 2 and vessel_Count-1 (including) (divides vessel_count)
void cleanupGlobalData(); // cleanup global data (free mallocs and close handles)


// == Barrier ==
struct Barrier {
	HANDLE mutex; // mutex to protect global barrierADT
	HANDLE threadHandle; // handle for the barrier thread
	int* vesselQueue; // a queue (array) of vessels - in & out parameters for enqueue & dequeue
	int in;// last index enqueued to queue
	int out; // last index dequeued from queue
} barrierADT;

int releaseM(int M); // release M vessels from the barrier
int checkBarrier(); // check if barrier can release vessels (quay is empty && vessels inside barrier >= M)
int enterBarrier(int vesselID); // enter the barrier
DWORD WINAPI barrierHandle(PVOID);


// == Vessels ==
int vessels_Count; // numner of vessels in the system
HANDLE* vessels; // vessel threads
HANDLE* vessel_Semaphores; // each vessel thread has its own semaphore
DWORD WINAPI vessel(PVOID);
int enterQuay(int trID); // vessel enters the unloading quay
int unload(int trID, int craneID); // perform the unload action (wake up a crane to serve)
int exitQuay(int trID); // exit the unloading quay (update necessary parameters)
int enterCanal(int trID); // veseel enters the canal Red sea --> Med Sea back to haifa.


// == Unloading Quay ==
struct UnloadingQuay {
	HANDLE mutex; // mutex to protect global quayADT
	HANDLE* crane_Semaphores; // each crane has its own semaphore
	HANDLE* cranes; // crane threads
	int cranes_Count; // number of cranes
	int* servedVessels; // crane ID serves servedVessel[ID-1]
	int* cargos; // crane ID unloading cargo from cargos[ID-1]
	int vesselsInside; // number of vessel inside the quayADT
	int vesselsDone; // boolean to notify the cranes when the vessels are done
} quayADT;
DWORD WINAPI crane(PVOID); // Crane thread function
int matchCrane(int vesselID); // match a crane with a given vesselID - returns the matched crane


// == Helping Funcs == 
char* getTime(); // get current time
int getRand(int min, int max); // get random numebr between min and max (including)
int trSleep(); // make thread sleep random time using getRand()
int isPrime(int n); // checks weather a number is prime


// ======== MAIN =========== 
int main(int argc, char* argv[]) {
	// get the ends of the pipe
	readFromHaifa = GetStdHandle(STD_INPUT_HANDLE);
	writeToHaifa = GetStdHandle(STD_OUTPUT_HANDLE);

	// read message from Haifa to approve sailing
	if (!approveSailing()) {
		fprintf(stderr, "[%s] Eilat Port: Sending 'Ship --%d-- vessels' response, response status: DENIED , Exiting...\n", getTime(), vessels_Count);
		return -1;
	}
	else
		fprintf(stderr, "[%s] Eilat Port: Sending 'Ship --%d-- vessels' response, response status APPROVED \n", getTime(), vessels_Count);

	// initialise global data
	if (!initGlobalData()) {
		fprintf(stderr, "[%s] Eilat Port: Error initializing vessels or semaphores @ initGlobalData(), Exiting... \n", getTime());
		return -1;
	}

	// Listen to vessels arriving from haifa + create their threads here.
	if (!vesselsArrival()) {
		fprintf(stderr, "[%s] Eilat Port: Error with vessels arrival, Exiting... \n", getTime());
		return -1;
	}

	// wait for barrier thread to finish
	WaitForSingleObject(barrierADT.threadHandle, INFINITE);
	// wait for all vessels to finish
	WaitForMultipleObjects(vessels_Count, vessels, TRUE, INFINITE);
	fprintf(stderr, "[%s] Eilat Port: All Vessel Threads are done \n", getTime());

	// release the crane once the vessels are done
	if (!releaseCranes()) {
		fprintf(stderr, "[%s] Eilat Port: error releasing the cranes, Exiting... \n", getTime());
		return -1;
	}

	// wait for all quayADT.cranes to finish
	WaitForMultipleObjects(quayADT.cranes_Count, quayADT.cranes, TRUE, INFINITE);
	fprintf(stderr, "[%s] Eilat Port: All Crane Threads are done \n", getTime());

	fprintf(stderr, "[%s] Eilat Port: Exiting... \n", getTime());
	// close handles and free memmory allocation
	cleanupGlobalData();
	return 1;
}


/* Main Flow */
// approve haifas sailing request - returns TRUE/FALSE
int approveSailing() {
	int approved;

	if (ReadFile(readFromHaifa, buffer, BUFFER_SIZE, &read, NULL)) {
		vessels_Count = atoi(buffer); // get the vessels count from haifa
		fprintf(stderr, "[%s] Eilat Port: Got 'Ship --%d-- vessels' Request \n", getTime(), vessels_Count);
		approved = !isPrime(vessels_Count); // approve if its prime, else disapprove
		sprintf(buffer, "%d", approved);
		// send back the vessel_count verificaton 
		if (!WriteFile(writeToHaifa, buffer, BUFFER_SIZE, &written, NULL)) {
			fprintf(stderr, "[%s] Eilat Port: approveSailing() Error writing vessel_count verification from pipe \n", getTime());
			approved = FALSE;
		}
	}
	else {
		fprintf(stderr, "[%s] Eilat Port: approveSailing() Error reading from pipe (sailing apoproval) \n", getTime());
		return -1;
	}

	return approved;
}
// initialize the global data - mallocs + handles + cranes & barrier threads - return TRUE/FALSE
int initGlobalData() {
	DWORD ThreadId;

	srand((unsigned int)time(NULL)); 	// initialize random seed for random number

	quayADT.cranes_Count = getDivisor(); // init quayADT.cranes number with a random number between 2 and vessel_count-1 that devides the vessel_count
	quayADT.vesselsInside = 0;
	barrierADT.in = 0;
	barrierADT.out = 0;
	quayADT.vesselsDone = FALSE;

	// === Mallocs ===
	vessels = (HANDLE*)malloc(vessels_Count * sizeof(HANDLE)); // memory for vessels
	vessel_Semaphores = (HANDLE*)malloc(vessels_Count * sizeof(HANDLE)); // queue memory
	barrierADT.vesselQueue = (int*)malloc(vessels_Count * sizeof(int)); // semaphore memory

	quayADT.cranes = (HANDLE*)malloc(quayADT.cranes_Count * sizeof(HANDLE)); //allocate memory for the quayADT.cranes
	quayADT.crane_Semaphores = (HANDLE*)malloc(quayADT.cranes_Count * sizeof(HANDLE)); // allocate crane semaphores memory
	quayADT.servedVessels = (int*)malloc(quayADT.cranes_Count * sizeof(int)); // memmory for vessels served by quayADT.cranes
	quayADT.cargos = (int*)malloc(quayADT.cranes_Count * sizeof(int)); // memmory for cargos of vessels served by quayADT.cranes

	if (quayADT.crane_Semaphores == NULL || quayADT.servedVessels == NULL || quayADT.cargos == NULL || barrierADT.vesselQueue == NULL || vessel_Semaphores == NULL || quayADT.cranes == NULL || vessels == NULL) {
		return FALSE;
	}

	// === Mutex & semaphores init ===
	randMutex = CreateMutex(NULL, FALSE, NULL); // create random mutex for getRand() use
	backToHaifaMutex = CreateMutex(NULL, FALSE, NULL); // mutex for going back to haifa in the canal Red seas --> Med sea
	quayADT.mutex = CreateMutex(NULL, FALSE, NULL); // unloading quay ADT mutex
	if (backToHaifaMutex == NULL || randMutex == NULL || quayADT.mutex == NULL) {
		return FALSE;
	}

	// === Create Cranes ===
	for (int i = 1; i <= quayADT.cranes_Count; i++)
	{
		quayADT.cranes[i - 1] = CreateThread(NULL, 0, crane, (int*)i, 0, &ThreadId);
		if (quayADT.cranes[i - 1] == NULL) {
			return FALSE;
		}
	}

	// === Create barrier ===
	barrierADT.threadHandle = CreateThread(NULL, 0, barrierHandle, NULL, 0, &ThreadId);//creates the barrier thread
	if (barrierADT.threadHandle == NULL) {
		return FALSE;
	}

	return TRUE;
}
// listen to arriving vessels from Haifa
int vesselsArrival() {
	DWORD ThreadId;
	// Listen to vessels coming from Haifa & create them here
	for (int i = 0; i < vessels_Count; i++) {
		if (ReadFile(readFromHaifa, buffer, BUFFER_SIZE, &read, NULL)) {
			// reading success - create matching vessel thread.
			int trID = atoi(buffer);

			vessels[trID - 1] = CreateThread(NULL, 0, vessel, (int*)trID, 0, &ThreadId);

			if (vessels[trID - 1] == NULL) {
				fprintf(stderr, "[%s] Eilat Port: vessel %d creation error!, Exiting... \n", getTime(), trID);
				return FALSE;
			}
		} // else reading failed
		else {
			fprintf(stderr, "[%s] Eilat Port: error reading from pipe (incoming vessel), Exiting... \n", getTime());
			return FALSE;
		}
	}
	return TRUE;
}
// release the cranes when the main signals (vessels are done)
int releaseCranes() {
	// notify cranes that vessels are done
	quayADT.vesselsDone = TRUE;

	// release waiting cranes semaphores
	for (int i = 0; i < quayADT.cranes_Count; i++)
	{
		if (!ReleaseSemaphore(quayADT.crane_Semaphores[i], 1, NULL)) {
			fprintf(stderr, "[%s] Eilat Port: error releasing crane %d\n", getTime(), i);
			return FALSE;
		}
	}
	return TRUE;
}
// cleanup the global data (free mallocs + close handles)
void cleanupGlobalData() {
	// Handle Closing
	CloseHandle(quayADT.mutex);
	CloseHandle(backToHaifaMutex);
	CloseHandle(randMutex);

	/* close thread semaphores - every thread closes its own semaphore before it finishes */

	// memmory cleanup
	free(barrierADT.vesselQueue);
	free(vessel_Semaphores);
	free(quayADT.crane_Semaphores);
	free(quayADT.servedVessels);
	free(quayADT.cargos);
	free(quayADT.cranes);
	free(vessels);
}


/* Barrier Thread Function */
DWORD WINAPI barrierHandle(PVOID null) {
	// initialize own mutex
	barrierADT.mutex = CreateMutex(NULL, FALSE, NULL);
	if (barrierADT.mutex == NULL)
		return -1;

	// go on untill all vessels passed the barrier
	while (barrierADT.out < vessels_Count) {
		if (!checkBarrier()) {
			fprintf(stderr, "[%s] Barrier - Problems checking barrier \n", getTime());
			break;
		}
	}

	// close own semaphore before finishing
	CloseHandle(barrierADT.mutex);

	return 1;
}
// Check if need to free barrier, and free it - return TRUE if freed M vessels, FALSE otherwise.
int checkBarrier() {
	int M = quayADT.cranes_Count;

	// protect barrier.in
	WaitForSingleObject(quayADT.mutex, INFINITE);
	// protect quayADT.vesselsInside
	WaitForSingleObject(barrierADT.mutex, INFINITE);
	// calc vessels inside the barrier
	int vesselsInQueue = barrierADT.in - barrierADT.out;
	if (vesselsInQueue >= M && quayADT.vesselsInside == 0) {
		if (!releaseM(M)) {
			return FALSE;
		}
	}

	if (!ReleaseMutex(barrierADT.mutex) || !ReleaseMutex(quayADT.mutex)) {
		fprintf(stderr, "[%s] Barrier: error releasing mutex \n", getTime());
		return FALSE;
	}

	return TRUE;
}
// release M vessels from the barrier
int releaseM(int M) {
	for (int i = 0; i < M; i++) {
		int vesID = barrierADT.vesselQueue[barrierADT.out++];
		if (!ReleaseSemaphore(vessel_Semaphores[vesID - 1], 1, NULL)) { //sem.v()
			fprintf(stderr, "[%s] Eilat Port: releaseM error releasing semaphore for Vessel %d \n", getTime(), vesID);
			return FALSE;
		}
	}
	return TRUE;
}


/* Crane Thread Function */
DWORD WINAPI crane(PVOID ptr) {
	int craneID = (int)ptr;
	int i = craneID - 1;
	int vesselID;
	int cargo;
	int res = TRUE;

	// init own semaphore
	quayADT.crane_Semaphores[i] = CreateSemaphore(NULL, 0, 1, NULL);
	if (quayADT.crane_Semaphores[i] == NULL) {
		fprintf(stderr, "[%s] Crane  %d - Error creating own semaphore \n", getTime(), craneID);
		return FALSE;
	}
	// mark himself as free of work
	quayADT.servedVessels[i] = FALSE;

	while (TRUE) {
		// wait as long there is no work
		WaitForSingleObject(quayADT.crane_Semaphores[i], INFINITE);

		// check if main signals that the vessels are done
		if (quayADT.vesselsDone) {
			fprintf(stderr, "[%s] Crane  %d - done Unloading vessels \n", getTime(), craneID);
			break;
		}

		if (!trSleep())  // sleep between MIN_SLEEP_TIME and MAX_SLEEP_TIME
			exit(0);

		// get served vessel & its cargo
		vesselID = quayADT.servedVessels[i];
		cargo = quayADT.cargos[i];

		// finish unloading
		fprintf(stderr, "[%s] Crane  %d - done Unloading %d TONS from Vessel %d \n", getTime(), craneID, cargo, quayADT.servedVessels[i]);

		// notify vessel that unloading job has ended
		if (!ReleaseSemaphore(vessel_Semaphores[vesselID - 1], 1, NULL)) {
			fprintf(stderr, "[%s] Crane  %d - error releasing Sem of Vessel %d \n", getTime(), craneID, quayADT.servedVessels[i]);
			res = FALSE;
			break;
		}
		// set itself as free crane
		quayADT.servedVessels[i] = FALSE;
	}
	// close own semaphore handle
	CloseHandle(quayADT.crane_Semaphores[i]);
	return res;
}
// function to match a crane with a vessel - gets a vessel ID and returns the matched crane ID
int matchCrane(int vesselID) {
	int craneID = FALSE;
	// loop to find the first crane marked himself as free (FALSE)
	for (int i = 0; i < quayADT.cranes_Count; i++) {
		if (quayADT.servedVessels[i] == FALSE) {
			// update crane serving vessel
			quayADT.servedVessels[i] = vesselID;
			craneID = (i + 1);
			break;
		}
	}
	// return the craneID of the matched crane - FALSE if was not found.
	return craneID;
}


/* Vessel Thread Function */
DWORD WINAPI vessel(PVOID ptr) {
	int trID = (int)ptr;
	int i = (trID - 1);
	int craneID;

	// init own semaphore
	vessel_Semaphores[i] = CreateSemaphore(NULL, 0, 1, NULL);//semaphore that initializes with zero - which means that whoever does sem.p(), goes to 'wait' state immidiatly. 
	if (vessel_Semaphores[i] == NULL) {
		fprintf(stderr, "\n[%s] Vessel %d - error initialising own semaphore \n", getTime(), trID);
		return -1;
	}

	// notify arriving Eilat Port
	fprintf(stderr, "[%s] Vessel %d - arrived @ Eilat Port \n", getTime(), trID);

	if (!trSleep())  // sleep between MIN_SLEEP_TIME and MAX_SLEEP_TIME
		exit(0); 

	// enter barrier - stage 1
	if (!enterBarrier(trID))
		return -1;

	//enter Unloading Quay - stage 2 
	craneID = enterQuay(trID);
	if (!craneID)
		return -1;

	// stage 3 + stage 4 + stage 5
	if (!unload(trID, craneID) || !exitQuay(trID) || !enterCanal(trID))
		return -1;

	// close own semaphore handle
	CloseHandle(vessel_Semaphores[i]);
	return 1;
}
// stage 1 - enter the barrier and update neccessary parameters (barrierADT.in++) = vesselID
int enterBarrier(int vesselID) {
	// Get inside the Barrier - only one thread can interact with the vessel queue global resource
	WaitForSingleObject(barrierADT.mutex, INFINITE);
	fprintf(stderr, "[%s] Vessel %d - entering Barrier \n", getTime(), vesselID);

	barrierADT.vesselQueue[barrierADT.in++] = vesselID;

	if (!ReleaseMutex(barrierADT.mutex)) {
		fprintf(stderr, "[%s] Barrier: error releasing mutex \n", getTime());
		return FALSE;
	}
	// wait until barrier thread signals to go out of the barrier
	WaitForSingleObject(vessel_Semaphores[vesselID - 1], INFINITE);//wait in sem.p() until sem.v()

	return TRUE;
}
// stage 2 - enter the unloading quay & match a crane
int enterQuay(int vesselID) {
	int craneID;
	// only one vessel thread accesses global variable quayADT.vesselsInside
	WaitForSingleObject(quayADT.mutex, INFINITE);

	fprintf(stderr, "[%s] Vessel %d - entering Unloading Quay\n", getTime(), vesselID);

	// update vessel in ADT
	quayADT.vesselsInside++;

	if (!trSleep())  // sleep between MIN_SLEEP_TIME and MAX_SLEEP_TIME
		exit(0);

	// find a free crane
	craneID = matchCrane(vesselID);
	if (!craneID) {
		fprintf(stderr, "[%s] Vessel %d - Trouble finding free crane @ Quay @ Eilat \n", getTime(), vesselID);
		craneID = FALSE;
	}
	else
		fprintf(stderr, "[%s] Vessel %d - served by - Crane %d \n", getTime(), vesselID, craneID); // print the crane and vessel


	if (!ReleaseMutex(quayADT.mutex)) {
		fprintf(stderr, "[%s] Vessel %d - Trouble releasing quayADT.mutex @ Quay @ Eilat \n", getTime(), vesselID);
		craneID = FALSE;
	}

	return craneID;
}
// stage 3 - start unloading process (wake up the matched crane)
int unload(int vesselID, int craneID) {

	int cargo = getRand(MIN_CARGO_WEIGHT, MAX_CARGO_WEIGHT);
	if (!cargo) {
		fprintf(stderr, "[%s] Vessel %d - Error getting random cargo weight @ unload @ Eilat \n", getTime(), vesselID);
		return FALSE;
	}
	fprintf(stderr, "[%s] Vessel %d - cargo weight %d TONS \n", getTime(), vesselID, cargo);
	// send cargo to the matched crane and free him to work (sem.P())
	quayADT.cargos[craneID - 1] = cargo;

	// release crane to work 
	if (!ReleaseSemaphore(quayADT.crane_Semaphores[craneID - 1], 1, NULL)) {
		fprintf(stderr, "[%s] Vessel %d - Trouble Releasing Semaphore of free crane @ unload @ Eilat \n", getTime(), vesselID);
		return FALSE;
	}
	fprintf(stderr, "[%s] Vessel %d - start Unloading \n", getTime(), vesselID);

	// wait for crane to finish job and free him
	WaitForSingleObject(vessel_Semaphores[vesselID - 1], INFINITE);//wait for sem.p() until sem.v()

	return TRUE;
}
// stage 4 - exit the unloading quay and update neccessary parameters (quayADT.vesselsInside--)
int exitQuay(int vesselID) {

	if (!trSleep())  // sleep between MIN_SLEEP_TIME and MAX_SLEEP_TIME
		exit(0);

	// only one vessel thread accesses global variable quayADT.vesselsInside
	WaitForSingleObject(quayADT.mutex, INFINITE);
	fprintf(stderr, "[%s] Vessel %d - exiting Unloading Quay @ Eilat \n", getTime(), vesselID);
	// exit ADT
	quayADT.vesselsInside--;
	if (!ReleaseMutex(quayADT.mutex)) {
		fprintf(stderr, "[%s] Vessel %d - Trouble releasing quayADT.mutex @ Quay @ Eilat \n", getTime(), vesselID);
		return FALSE;
	}
	return TRUE;
}
// heading back to Haifa
int enterCanal(int trID) {
	fprintf(stderr, "[%s] Vessel %d - entering Canal: Red Sea ==> Med. Sea \n", getTime(), trID);

	// sem.P - only one will enter canal at a time
	WaitForSingleObject(backToHaifaMutex, INFINITE);

	if (!trSleep())  // sleep between MIN_SLEEP_TIME and MAX_SLEEP_TIME
		exit(0);

	// notify exiting canal
	fprintf(stderr, "[%s] Vessel %d - exiting Canal: Red Sea ==> Med. Sea \n", getTime(), trID);

	// notify Haifa that sailing back
	sprintf(buffer, "%d", trID);

	if (!WriteFile(writeToHaifa, buffer, BUFFER_SIZE, &written, NULL)) {
		fprintf(stderr, "[%s] Vessel %d - Error writing sailing back announcment from pipe \n", getTime(), trID);
		return FALSE;
	}

	// sem.P - allow the next vessel to enter canal
	if (!ReleaseMutex(backToHaifaMutex)) {
		fprintf(stderr, "[%s] Vessel %d - Error Releasing backToSuezMutex \n", getTime(), trID);
		return FALSE;
	}

	return TRUE;
}


/* Helping Functions */
// function to check whether a number is prime (need to check only up to sqrt(n) factor)
// suppose n = factor1 * factor2 --> min(factor1,factor2) <= sqrt(n),
// otherwise we get that factor1*factor2 > n and therefore n > n
int isPrime(int n) {
	for (int i = 2; i * i <= n; i++) {
		if (n % i == 0)
			return FALSE;
	}
	return TRUE;
}
// get a number between 2 to vessel_count-1 (including) that perfectly divides vessels_Count without remainder
int getDivisor() {
	int min = 2;
	int max = vessels_Count - 1;
	int divisor;

	do {
		divisor = rand() % (max - min + 1) + min;
	} while ((vessels_Count % divisor) != 0);

	return divisor;
}
//get the current time
char* getTime() {//refine
	time_t rawtime;
	struct tm* tm_info;
	time(&rawtime);
	tm_info = localtime(&rawtime);
	char* now = asctime(tm_info);
	if (now != 0)
		strftime(now, 9, "%H:%M:%S", tm_info);
	return now;
}
// get a random number between min and max (including) - protected by randMutex
int getRand(int min, int max) {
	int random;
	// randMutex.P
	WaitForSingleObject(randMutex, INFINITE);
	srand((unsigned int)time(NULL));
	random = rand() % (max - min + 1) + min;

	// randMutex.V
	if (!ReleaseMutex(randMutex)) {
		fprintf(stderr, "\n[%s] getRand: Error Releasing Semaphore!\n", getTime());
		return FALSE;
	}
	return random;
}
// thread sleep between MIN_SLEEP_TIME and MAX_SLEEP_TIME
int trSleep() {
	int sleepTime = getRand(MIN_SLEEP_TIME, MAX_SLEEP_TIME);
	if (!sleepTime) {
		fprintf(stderr, "\n[%s] trSleep: Error getting random sleep time\n", getTime());
		return FALSE;
	}
	Sleep(sleepTime);
	return TRUE;
}