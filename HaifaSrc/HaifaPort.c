#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE

#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <math.h>

#define BUFFER_SIZE 55
#define MAX_SLEEP_TIME 3000 //ms
#define MIN_SLEEP_TIME 5 //ms

HANDLE suezMutex, randMutex; // mutexes
HANDLE* vessel_Semaphores; // each vessel has its own semaphore
HANDLE* vessels; // vessel Threads
HANDLE ReadFromHaifa, WriteToEilat;    // pipe for writing parent to child (Haifa -> Eilat)
HANDLE ReadFromEilat, WriteToHaifa;  // pipe for writing child to parent (Eilat -> Haifa)
char buffer[BUFFER_SIZE]; // buffer to communicate between haifa and eilat processes.
DWORD written, read;
STARTUPINFO si;
PROCESS_INFORMATION pi;

int vessels_Count; // number of vessels

/* int and clean global data */
int initGlobalData();
void cleanupGlobalData();

// vessel entering the canal on its way to Eilat
int enterCanal(int trID);

/* Main Flow */
int validate(int argc, char* argv[]); // validate the command line argument
int createEilat(); // create eilat process and necessary pipes
int sendRequest(); // send sailing request to Eilat
int readResponse(); // read Eilats response
int vesselsBack(); // wait for vessels coming back from Eilat and continue their flow here

//WINAPI vessel(PVOID) - thread function for each vessel
DWORD WINAPI vessel(PVOID); 

// helping funcs
char* getTime(); // get the current time
int calcSleepTime(); // calc random sleep time (mutex protected)

// ======== MAIN =========== 
int main(int argc, char* argv[]) {
	// validate the command line argument
	if (!validate(argc, argv)) {
		fprintf(stderr, "[%s] Haifa Port: Error validating, Exiting...\n", getTime());
		return -1;
	}

	// create Eilat process
	if (!createEilat()) {
		fprintf(stderr, "[%s] Haifa Port: Error creating Eilat proccess, Exiting...\n", getTime());
		return -1;
	}

	/* Sailing Process */
	fprintf(stderr, "[%s] Haifa Port: Starting Sailing Proccess for -%d- Vessels\n", getTime(), vessels_Count);
	// 1.Send vessels request
	// 2.Read the response
	// 3.Init global data - creates the veseels and let them start sailing
	// 4.Listen to vessels coming back from eilat
	if (!sendRequest() || !readResponse() || !initGlobalData() || !vesselsBack()) {
		fprintf(stderr, "[%s] Haifa Port: Exiting...\n", getTime());
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return -1;
	}


	// wait for all the vessel threads to finish
	WaitForMultipleObjects(vessels_Count, vessels, TRUE, INFINITE);
	fprintf(stderr, "[%s] Haifa Port: All Vessel Threads are done\n", getTime());

	// wait for child process to finish
	WaitForSingleObject(pi.hProcess, INFINITE);
	fprintf(stderr, "[%s] Haifa Port: Exiting...\n", getTime());

	// close handles and free memmory allocation
	cleanupGlobalData();
	return 1;
}


/* Main Flow*/
// validate the command line arguments
int validate(int argc, char* argv[]) {
	//validations
	if (argc == 1)
	{
		fprintf(stderr, "[%s] Haifa Port: No command line argument was provided!\n", getTime());
		return FALSE;
	}
	if (argc != 2)
	{
		fprintf(stderr, "[%s] Haifa Port: Non Single Paramater was provided!\n", getTime());
		return FALSE;
	}

	//parse the argument from cmd 
	vessels_Count = atoi(argv[1]);

	if (vessels_Count < 2 || vessels_Count > 50) {
		fprintf(stderr, "[%s] Haifa Port: Value can only be between 2 and 50 vessels!\n", getTime());
		return FALSE;
	}
	return TRUE;
}
// Create Eilat process - return TRUE/FALSE
int createEilat() {
	TCHAR ProcessName[256];

	/* security attributes to enable pipe handles inheritense. */
	SECURITY_ATTRIBUTES sa = {
		sizeof(SECURITY_ATTRIBUTES),
		NULL,
		TRUE
	};

	/* allocate memory */
	ZeroMemory(&pi, sizeof(pi));

	//create EilatPort Proccess
	/* create the #1st pipe (Haifa -> Eilat) */
	if (!CreatePipe(&ReadFromHaifa, &WriteToEilat, &sa, 0))
	{
		fprintf(stderr, "[%s] Haifa: Haifa -> Eilat Pipe Failed\n", getTime());
		return FALSE;
	}
	/* create the #2nd pipe (Eilat -> Haifa) */
	if (!CreatePipe(&ReadFromEilat, &WriteToHaifa, &sa, 0))
	{
		fprintf(stderr, "[%s] Haifa: Eilat -> Haifa Pipe Failed\n", getTime());
		return FALSE;
	}

	/* establish the START_INFO structure for the child process */
	GetStartupInfo(&si);
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

	/* redirect the standard input to the read end of the pipe */
	si.hStdInput = ReadFromHaifa;
	si.hStdOutput = WriteToHaifa;
	si.dwFlags = STARTF_USESTDHANDLES;

	/* we do not want the child to inherit the write end of the pipe */
	SetHandleInformation(WriteToEilat, HANDLE_FLAG_INHERIT, 0);

	wcscpy(ProcessName, L"EilatPort.exe");

	// Start the child process.
	if (!CreateProcess(
		NULL,				// No module name (use command line)
		ProcessName,		// No module name (use command line).
		NULL,				// Process handle not inheritable.
		NULL,				// Thread handle not inheritable.
		TRUE,				// inherite handle.
		0,					// No creation flags.
		NULL,				// Use parent's environment block.
		NULL,				// Use parent's starting directory.
		&si,				// Pointer to STARTUPINFO structure.
		&pi))				// Pointer to PROCESS_INFORMATION structure.			
	{
		fprintf(stderr, "[%s] Haifa Port: Process Creation Failed\n", getTime());
		return FALSE;
	}
	/* close the unused end of the pipe */
	CloseHandle(ReadFromHaifa);
	CloseHandle(WriteToHaifa);

	return TRUE;
}
// send request to Eilat for shipping N vessels - return TRUE/FALSE
int sendRequest() {
	fprintf(stderr, "[%s] Haifa Port: Sending 'Ship --%d-- vessels' Request\n", getTime(), vessels_Count);
	// write vessels count to Eilat Port
	sprintf(buffer, "%d", vessels_Count);
	if (!WriteFile(WriteToEilat, buffer, BUFFER_SIZE, &written, NULL)) {
		fprintf(stderr, "[%s] Haifa Port: sendRequest Error writing to pipe\n", getTime());
		return FALSE;
	}
	return TRUE;
}
// read the response from Eilat - returns TRUE/FALSE
int readResponse() {
	if (ReadFile(ReadFromEilat, buffer, BUFFER_SIZE, &read, NULL)) {
		int ves_count_approved = atoi(buffer);
		if (!ves_count_approved) {
			// buffer came back false --> the vessel_count was a prime number
			fprintf(stderr, "[%s] Haifa Port: 'Ship --%d-- vessels' response status DENIED\n", getTime(), vessels_Count);
			return FALSE;
		}
		fprintf(stderr, "[%s] Haifa Port: Got 'Ship --%d-- vessels' response, response status APPROVED\n\n", getTime(), vessels_Count);
	}
	else {
		fprintf(stderr, "[%s] Haifa Port: readResponse Error reading from pipe\n", getTime());
		return FALSE;
	}
	return TRUE;
}
// initialise the global data (mallocs + semaphores) - return TRUE/FALSE
int initGlobalData() {
	DWORD ThreadId;
	// initialize random seed for random number:
	srand((unsigned int)time(NULL));

	// === Mallocs ===
	vessel_Semaphores = (HANDLE*)malloc(vessels_Count * sizeof(HANDLE));
	vessels = (HANDLE*)malloc(vessels_Count * sizeof(HANDLE));
	if (vessel_Semaphores == NULL || vessels == NULL) {
		fprintf(stderr, "[%s] Haifa Port: initGlobalData Error Mallocing Semaphores\n", getTime());
		return FALSE;	
	}

	// === Semaphores ===
	suezMutex = CreateMutex(NULL, FALSE, NULL);
	randMutex = CreateMutex(NULL, FALSE, NULL);
	if (suezMutex == NULL || randMutex == NULL) {
		fprintf(stderr, "[%s] Haifa Port: initGlobalData Error Creating Mutexes\n", getTime());
		return FALSE;
	}

	// === Create Vessels ===
	for (int i = 1; i <= vessels_Count; i++)
	{
		vessels[i - 1] = CreateThread(NULL, 0, vessel, (int*)i, 0, &ThreadId);
		if (vessels[i - 1] == NULL) {
			fprintf(stderr, "[%s] Haifa Port: initGlobalData Error Creating Vessel %d\n", getTime(), i);
			return FALSE;
		}
	}

	return TRUE;
}
// listen to vessels coming back from eilat and free them to continue in haifa - returns TRUE/FALSE
int vesselsBack() {
	// listen to vessels coming back from Eilat Port
	for (int i = 0; i < vessels_Count; i++) {
		if (ReadFile(ReadFromEilat, buffer, BUFFER_SIZE, &read, NULL)) {
			int trID = atoi(buffer);
			if (!ReleaseSemaphore(vessel_Semaphores[trID - 1], 1, NULL)) {
				printf("[%s] Haifa Port: vesselsBack Error releasing semaphore for vessel %d!\n", getTime(), trID);
				return FALSE;
			}
		}
		else {
			fprintf(stderr, "[%s] Haifa Port: vesselsBack Error reading from pipe\n", getTime());
			return FALSE;
		}
	}
	return TRUE;
}
// clean up the global data initializes.
void cleanupGlobalData() {
	/* close handles */
	CloseHandle(WriteToEilat);
	CloseHandle(ReadFromEilat);
	CloseHandle(suezMutex);
	CloseHandle(randMutex);

	/* close thread semaphores - every thread closes its own semaphore before it finishes */

	/* free malloc */
	free(vessel_Semaphores);
	free(vessels);

	/* close remaining handles */
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}


/* Vessel Thread Function */
DWORD WINAPI vessel(PVOID ptr) {
	int trID = (int)ptr;
	int i = trID - 1;
	// create own semaphore
	vessel_Semaphores[i] = CreateSemaphore(NULL, 0, 1, NULL);
	if (vessel_Semaphores[i] == NULL) {
		fprintf(stderr, "\n[%s] Vessel %d - Error initializing own semaphore\n", getTime(), trID);
		exit(0);
	}

	// start sailing to eilat
	fprintf(stderr, "[%s] Vessel %d - starts sailing @ Haifa Port \n", getTime(), trID);

	int sleepTime = calcSleepTime();
	if (!sleepTime) {
		fprintf(stderr, "[%s] Vessel %d - done Sailing @ Haifa Port\n", getTime(), trID);
		exit(0);
	}
	Sleep(sleepTime);

	// entering canal
	if (!enterCanal(trID)) {
		CloseHandle(vessel_Semaphores[i]);
		exit(0);
	}

	// vessel arrived eilat - wait for him to finish and come back
	WaitForSingleObject(vessel_Semaphores[i], INFINITE);

	// sailing done
	fprintf(stderr, "[%s] Vessel %d - done Sailing @ Haifa Port\n", getTime(), trID);

	sleepTime = calcSleepTime();
	if (!sleepTime) {
		fprintf(stderr, "[%s] Vessel %d - done Sailing @ Haifa Port\n", getTime(), trID);
		exit(0);
	}
	Sleep(sleepTime);

	// Close unnecessary semaphore of the thread that has just finished its life cycle
	CloseHandle(vessel_Semaphores[i]);
	return 1;
}
// enter the Canal: Med sea --> Red seas.
int enterCanal(int trID) {
	fprintf(stderr, "[%s] Vessel %d - entering Canal: Med. Sea ==> Red Sea  \n", getTime(), trID);

	// mutex.P (only one thread at a time in the canal)
	WaitForSingleObject(suezMutex, INFINITE);

	// simulate passing inside the canal takes time
	int sleepTime = calcSleepTime();
	if (!sleepTime) {
		fprintf(stderr, "[%s] Vessel %d - done Sailing @ Haifa Port\n", getTime(), trID);
		exit(0);
	}
	Sleep(sleepTime);

	// notify eilat
	sprintf(buffer, "%d", trID);
	if (!WriteFile(WriteToEilat, buffer, BUFFER_SIZE, &written, NULL)) {
		fprintf(stderr, "\n[%s] vessel %d - Error writing to pipe-eilat\n", getTime(), trID);
		return FALSE;
	}

	// Free Suez after arriving Eilat
	if (!ReleaseMutex(suezMutex)) {
		fprintf(stderr, "\n[%s] vessel %d - Error Releasing SuezMutex\n", getTime(), trID);
		return FALSE;
	}
	return TRUE;
}

/* Helping Functions */
// returns the current time in [HH/MM/SS] Format
char* getTime() {
	time_t rawtime;
	struct tm* tm_info;
	time(&rawtime);
	tm_info = localtime(&rawtime);
	char* now = asctime(tm_info);
	if (now != 0)
		strftime(now, 9, "%H:%M:%S", tm_info);
	return now;
}
// calc random sleep time (protected by a mutex)
int calcSleepTime() {
	int sleepTime;

	// randMutex.P
	WaitForSingleObject(randMutex, INFINITE);
	srand((unsigned int)time(NULL));
	sleepTime = rand() % (MAX_SLEEP_TIME - MIN_SLEEP_TIME + 1) + MIN_SLEEP_TIME;
	// randMutex.V
	if (!ReleaseMutex(randMutex)) {
		fprintf(stderr, "\n[%s] Haifa: calcSleepTime() Error Releasing rand Semaphore!\n", getTime());
		return FALSE;
	}

	return sleepTime;
}