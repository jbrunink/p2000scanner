/*
 *  p2000scanner: A P2000 paging decoder (1.0-BETA)
 *
 *  Based on the source of PDW (POCSAG, FLEX, ACARS, MOBITEX & ERMES Decoder)
 *
 *  Jason Petty (2001-2004) and Peter Hunt (2004-2010)
 *
 *  Modified by Jurgen Brunink and Martin Kollaard (2013)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>   	/* Standard input/output definitions */
#include <string.h>  	/* String function definitions */
#include <unistd.h>  	/* UNIX standard function definitions */
#include <fcntl.h>   	/* File control definitions */
#include <errno.h>   	/* Error number definitions */
#include <termios.h> 	/* POSIX terminal control definitions */
#include <stdio.h>      /* printf */
#include <string.h>     /* strcat */
#include <stdlib.h>     /* strtol */
#include <strings.h>
#include <time.h>       /* time_t, struct tm, time, localtime, asctime */
#include <assert.h>
#include <ctype.h>
#include <zmq.h>
#include <signal.h>
#include <pthread.h>
#include <getopt.h>
#include <json_object.h>
#include <json.h>
#include <syslog.h>
#include <sys/stat.h> /* umask */
#include <stdarg.h>	/* va_list */

#include "scanner.h"

/* Global variables */

FILE *dataFile = NULL;
FILE *logFile = NULL;

char vtype[8][9]={"SECURE", "INSTR", "SH/TONE", "StNUM", "SfNUM", "ALPHA", "BINARY", "NuNUM"};
char aGroupnumbers[16][8]={"-1", "-2", "-3", "-4", "-5", "-6", "-7", "-8", "-9", "10", "11", "12", "13", "14", "15", "16"};
char serialdevice[FILENAME_MAX];

int iMessagesCounter = 0;
int exitRequested = 0;
int bisDaemon = 1; /* default value is true */
int bFlexActive = 0;

int socketMonitorEnd = 0;

pid_t pid, sid;

char endpoint[] = "tcp://[*]:5555";

/* Global Zeromq variables */

void * socket;
void * context;

/* End global Zeromq variables */

/* End global variables */

void signalhandler(int sig)
{
	switch(sig)
	{
		case SIGINT:
			printf("SIGINT received, requesting exit from while()\n");
			fprintf(dataFile, "SIGINT received, requesting exit from while()\n");
			exitRequested = 1;
		break;
		case SIGKILL:
			printf("SIGKILL received, requesting exit from while()\n");
			fprintf(dataFile, "SIGKILL received, requesting exit from while()\n");
			exitRequested = 1;
		break;
		case SIGTERM:
			printf("SIGTERM received, requesting exit from while()\n");
			fprintf(dataFile, "SIGTERM received, requesting exit from while()\n");
			exitRequested = 1;
		break;
	}
}

char Current_MSG[10][MAX_STR_LEN];			// PH: Buffer for all message items
											// 1 = MSG_CAPCODE
											// 2 = MSG_TIME
											// 3 = MSG_DATE
											// 4 = MSG_MODE
											// 5 = MSG_TYPE
											// 6 = MSG_BITRATE
											// 7 = MSG_MESSAGE
											// 8 = MSG_MOBITEX


char Previous_MSG[2][10][MAX_STR_LEN];		// PH: Buffer for previous message items
											// PH: [8]=last filtered messagetext



int aGroupCodes[17][MAXIMUM_GROUPSIZE];
int GroupFrame[17] = { -1, -1, -1, -1, -1, -1, -1, -1,-1, -1, -1, -1, -1, -1, -1, -1, -1 };

//void * JSONData;

int FlexTempAddress;			// PH: Set to corresponding groupaddress (0-15)

int iFrameCount = 0;

unsigned char message_buffer[MAX_STR_LEN+1];// buffer for message characters

int iCurrentCycle, iCurrentFrame;	// Current flex cycle / frame

int reflex = 0;

struct flex_phase phases[4];

long int capcode;

char block[256]; //per phase
long int frame[200]; //per phase

int pd_i = 0;
unsigned int bch[1025], ecs[25];     // error correction sequence

int bFLEX_isGroupMessage = 0;

char ob[32];

int iFlexBlock = 0;
int iFlexBlockCount = 0;
int iFlexTimer = 0;

pthread_t threads[1];

int iMessageIndex = 0;

int syncs[8] = { 0x870C, 0x7B18, 0xB068, 0xDEA0, 0, 0, 0, 0x4C7C };
int g_sps = 1600;
int g_sps2 = 1600;
int level = 2;

double * rcverpointer;
double rcver[65];

// receive symbol clock tightness - lower numbers mean slower loop response
double rcv_clkt = 0.065;         // rcv loop clock tightness
double rcv_clkt_hi = 0.065;      // coarse rcv clock (sync ups only)
double rcv_clkt_fl = 0.015;      // fine rcv clock (data) - FLEX
double rcv_clkt_po = 0.025;      // fine rcv clock (data) - POCSAG/ACARS
double rcv_clkt_mb = 0.080;      // sync ups & data - Mobitex
double rcv_clkt_em = 0.030;      // sync ups & data - Ermes

int ircver = 0;

double exc = 0.0;

int flex_speed = STAT_FLEX1600;

// this table translates received modem status line combinations into
// the received symbol; it gives the number of modem status lines that
// are high (other than RI) for a given line status nybble
int iLineSymbols[16] = { 0, 1, 1, 2, 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3 };
int iConvertingGroupcall = 0;

int containsUnknownCharacters = 0;
int uncorrectableError = 0;

void freedata(void *data, void *hint)
{
	free(data);
}

void writeToLog(const char* format, ...)
{
	va_list arglist;
	va_start(arglist, format);

	char * buffer = malloc(1024 * sizeof(char));
	memset(buffer, 0, 1024 * sizeof(char));

	//vsprintf(buffer, format, arglist);
	vsnprintf(buffer, 1024, format, arglist);

	time_t currentTime = time(NULL);
	struct tm * currentTimeInfo = localtime(&currentTime);

	char sDate[50];
	char sTime[50];

	strftime(sDate, 50, "%d-%m-%Y", currentTimeInfo);
	strftime(sTime, 50, "%H:%M:%S", currentTimeInfo);

	if(logFile == NULL)
	{
			/* Do some magic.. */
	}

	fprintf(logFile, "[%s %s] %s", sDate, sTime, buffer);

	//syslog(LOG_NOTICE, "%s", buffer);

	free(buffer);

	va_end(arglist);
}

// monitoring thread
static void *req_socket_monitor (void *ctx)
{
	zmq_event_t event;
	int rc;

	void *s = zmq_socket (ctx, ZMQ_PAIR);
	assert (s);

	rc = zmq_connect (s, "inproc://monitor.req");
	assert (rc == 0);

	while (exitRequested == 0)
	{
		zmq_msg_t msg;
		zmq_msg_init (&msg);
		rc = zmq_recvmsg (s, &msg, 0);
		if (rc == -1 && zmq_errno() == ETERM)
		{
			const char * error = zmq_strerror(errno);
			writeToLog((char *)error);
			break;
		}
		const char * error = zmq_strerror(errno);
		writeToLog((char *)error);
		assert (rc != -1);
		memcpy (&event, zmq_msg_data (&msg), sizeof (event));

		switch (event.event)
		{
			case ZMQ_EVENT_CONNECTED:
				printf("Connection to remote peer: %s\n", event.data.connected.addr);
				fprintf(dataFile, "Connection to remote peer '%s' connected.\n", event.data.connected.addr);
				fflush(dataFile);
			break;
			case ZMQ_EVENT_CLOSED:
			break;
			case ZMQ_EVENT_ACCEPTED:
				printf("Connection: %s\n", event.data.accepted.addr);
				fprintf(dataFile, "Client '%s' connected.\n", event.data.accepted.addr);
				fflush(dataFile);
			break;
			case ZMQ_EVENT_DISCONNECTED:
				printf("%s disconnected.\n", event.data.disconnected.addr);
				fprintf(dataFile, "Client '%s' disconnected.\n", event.data.disconnected.addr);
				fflush(dataFile);
		}
		zmq_msg_close (&msg);
	}
	writeToLog("Exit requested (monitor loop)\n");
	//zmq_disconnect(s, "inproc://monitor.req");
	zmq_close(s);
	return NULL;
}

void parseSingleMessage()
{
	if(!iConvertingGroupcall)
	{
			message_buffer[iMessageIndex] = '\0';
			iMessageIndex = 0;
	}

	memcpy(Current_MSG[MSG_MESSAGE], message_buffer, MAX_STR_LEN);

	struct json_object * parentObject = json_object_new_object();
	struct json_object * dataObject = json_object_new_object();
	struct json_object * extraInfoObject = json_object_new_object();

	json_object_object_add(parentObject, "data", dataObject);
	json_object_object_add(parentObject, "extra", extraInfoObject);

	struct json_object * capcodeArray = json_object_new_array();

	struct json_object * textObject = json_object_new_string(Current_MSG[MSG_CAPCODE]);
	json_object_array_add(capcodeArray, textObject);

	struct json_object * typeObject = json_object_new_string(Current_MSG[MSG_TYPE]);
	json_object_object_add(dataObject, "type", typeObject);

	struct json_object * modeObject = json_object_new_string(Current_MSG[MSG_MODE]);
	json_object_object_add(dataObject, "mode", modeObject);

	struct json_object * bitrateObject = json_object_new_string(Current_MSG[MSG_BITRATE]);
	json_object_object_add(dataObject, "bitrate", bitrateObject);

	struct json_object * timestampObject = json_object_new_string(Current_MSG[MSG_TIMESTAMP]);
	json_object_object_add(dataObject, "timestamp", timestampObject);

	struct json_object * messageObject = json_object_new_string(Current_MSG[MSG_MESSAGE]);
	json_object_object_add(dataObject, "message", messageObject);

	struct json_object * isGroupMessageObject = json_object_new_boolean(0);
	json_object_object_add(extraInfoObject, "isGroupMessage", isGroupMessageObject);

	struct json_object * containsUnknownCharactersObject = json_object_new_boolean(containsUnknownCharacters);
	json_object_object_add(extraInfoObject, "containsUnknownCharacters", containsUnknownCharactersObject);
	containsUnknownCharacters = 0;

	struct json_object * uncorrectableErrorObject = json_object_new_boolean(uncorrectableError);
	json_object_object_add(extraInfoObject, "uncorrectableErrorDetected", uncorrectableErrorObject);
	uncorrectableError = 0;

	json_object_object_add(dataObject, "capcodes", capcodeArray);

	printf("JSON: %s\n", json_object_to_json_string(parentObject));

	const char * Current_MSG_JSON = json_object_to_json_string(parentObject);
	int Current_MSG_JSON_length = (int) strlen(Current_MSG_JSON);
	//printf("JSON length: %d\n", (int) strlen(testjson));

	void * jsondata = malloc(Current_MSG_JSON_length);
	memcpy(jsondata, Current_MSG_JSON, Current_MSG_JSON_length);
	
	fprintf(dataFile, "%s\n", Current_MSG_JSON);
	
	zmq_msg_t testmsg;

	zmq_msg_init_data(&testmsg, jsondata, Current_MSG_JSON_length, freedata, NULL);
	printf("Bytes sent (SINGLE): %d\n", zmq_msg_send(&testmsg, socket, 0));

	/* Freeing objects... */
	
	json_object_put(parentObject);
}

void ShowMessage()
{
	return;
	printf("\n--------\n");
	printf("Message: %s\n", Current_MSG[MSG_MESSAGE]);
	printf("Capcode: %s\n", Current_MSG[MSG_CAPCODE]);
	printf("Mode: %s\n", Current_MSG[MSG_TYPE]);
	printf("Time: %s\n", Current_MSG[MSG_TIME]);
	printf("Datum: %s\n", Current_MSG[MSG_DATE]);
	printf("\n--------\n");
}


void ConvertGroupcall(int groupbit, char *vtype, int capcode)
{
	message_buffer[iMessageIndex] = '\0';

	//char address[16]="";
	int addresses = 0;
	
	if(capcode >= 2029568 && capcode <= 2029583)
	{
		if(GroupFrame[groupbit] == iCurrentFrame)
		{
			for(int nCapcode = 1; nCapcode <= aGroupCodes[groupbit][CAPCODES_INDEX]; nCapcode++)
			{
				addresses += aGroupCodes[groupbit][nCapcode];
			}
			addresses += aGroupCodes[groupbit][CAPCODES_INDEX] << 25;
			//printf("address: %i\n", addresses);

			iConvertingGroupcall=groupbit+1;

			printf("GROUP%s\n", aGroupnumbers[iConvertingGroupcall-1]);

			strcpy(Current_MSG[MSG_TYPE], "GROUP");
			memcpy(Current_MSG[MSG_MESSAGE], message_buffer, MAX_STR_LEN);

			
			/* JSON meuk */

			struct json_object * parentObject = json_object_new_object();
			struct json_object * dataObject = json_object_new_object();
			struct json_object * extraInfoObject = json_object_new_object();
			json_object_object_add(parentObject, "data", dataObject);
			json_object_object_add(parentObject, "extra", extraInfoObject);

			struct json_object * capcodeArray = json_object_new_array();

			for (int nCapcode=1; nCapcode <= aGroupCodes[groupbit][CAPCODES_INDEX]; nCapcode++)
			{
				if(aGroupCodes[groupbit][nCapcode] == 9999999)
				{
					strcpy(Current_MSG[MSG_CAPCODE], "???????");
				} else
				{
					sprintf(Current_MSG[MSG_CAPCODE], "%07i", aGroupCodes[groupbit][nCapcode]);
				}

				struct json_object * textObject = json_object_new_string(Current_MSG[MSG_CAPCODE]);
				json_object_array_add(capcodeArray, textObject);
				//json_object_put(textObject);
				ShowMessage();

			}

			sprintf(Current_MSG[MSG_CAPCODE], "%07i", capcode);
			strcpy(Current_MSG[MSG_TYPE], vtype);

			struct json_object * textObject = json_object_new_string(Current_MSG[MSG_CAPCODE]);
			json_object_array_add(capcodeArray, textObject);
			//json_object_put(textObject);

			struct json_object * typeObject = json_object_new_string(Current_MSG[MSG_TYPE]);
			json_object_object_add(dataObject, "type", typeObject);
			//json_object_put(typeObject);

			struct json_object * modeObject = json_object_new_string(Current_MSG[MSG_MODE]);
			json_object_object_add(dataObject, "mode", modeObject);
			//json_object_put(typeObject);

    	    struct json_object * bitrateObject = json_object_new_string(Current_MSG[MSG_BITRATE]);
       		json_object_object_add(dataObject, "bitrate", bitrateObject);

			struct json_object * timestampObject = json_object_new_string(Current_MSG[MSG_TIMESTAMP]);
			json_object_object_add(dataObject, "timestamp", timestampObject);
			//json_object_put(typeObject);

			struct json_object * messageObject = json_object_new_string(Current_MSG[MSG_MESSAGE]);
			json_object_object_add(dataObject, "message", messageObject);
			//json_object_put(typeObject);

			struct json_object * isGroupMessageObject = json_object_new_boolean(1);
			json_object_object_add(extraInfoObject, "isGroupMessage", isGroupMessageObject);
			//json_object_put(typeObject);

			struct json_object * containsUnknownCharactersObject = json_object_new_boolean(containsUnknownCharacters);
			json_object_object_add(extraInfoObject, "containsUnknownCharacters", containsUnknownCharactersObject);
			//json_object_put(typeObject);
			containsUnknownCharacters = 0;

			struct json_object * uncorrectableErrorObject = json_object_new_boolean(uncorrectableError);
			json_object_object_add(extraInfoObject, "uncorrectableErrorDetected", uncorrectableErrorObject);
			uncorrectableError = 0;

			json_object_object_add(dataObject, "capcodes", capcodeArray);
			//json_object_put(capcodeArray);

			fprintf(dataFile, "%s\n",json_object_to_json_string(parentObject));

			printf("JSON: %s\n", json_object_to_json_string(parentObject));

//zeromq
			const char * Current_MSG_JSON = json_object_to_json_string(parentObject);
			int Current_MSG_JSON_length = (int) strlen(Current_MSG_JSON);
			//printf("JSON length: %d\n", (int) strlen(testjson));

			void * jsondata = malloc(Current_MSG_JSON_length);
			memcpy(jsondata, Current_MSG_JSON, Current_MSG_JSON_length);

			zmq_msg_t testmsg;

			zmq_msg_init_data(&testmsg, jsondata, Current_MSG_JSON_length, freedata, NULL);
			printf("Bytes sent (GROUPMESSAGE): %d\n", zmq_msg_send(&testmsg, socket, 0));

// end zeromq


			/* Freeing objects... */
			json_object_put(parentObject);

			iMessagesCounter++;
			ShowMessage();
			memset(aGroupCodes[groupbit], 0, sizeof(int) * MAXIMUM_GROUPSIZE);

			GroupFrame[groupbit] = -1;
			iConvertingGroupcall=0;		// PH: Reset for next groupmessage
		}
	}
	iMessageIndex = 0;
}

void SortGroupCall(int groupbit)	// PH: Sort aGroupCodes[groupbit]
{
	for (int nCapcode=1; nCapcode <= aGroupCodes[groupbit][CAPCODES_INDEX]; nCapcode++)
	{
		int min, j;

		for (min=nCapcode, j=nCapcode+1; aGroupCodes[groupbit][j] > 0; j++)
		{
			if (aGroupCodes[groupbit][j] < aGroupCodes[groupbit][min]) min = j;
		}

		int tmp=aGroupCodes[groupbit][nCapcode];

		aGroupCodes[groupbit][nCapcode] = aGroupCodes[groupbit][min];
		aGroupCodes[groupbit][min] = tmp;  // swap them
	}
}

void AddAssignment(int assignedframe, int groupbit, int capcode)
{
	//printf("Assignment capcode: %d\n", capcode);
	if ((GroupFrame[groupbit] != assignedframe) &&
		(GroupFrame[groupbit] != -1) &&
		(aGroupCodes[groupbit][CAPCODES_INDEX]))
	{
		/*if (groupbit < 16) */
	}

	if (aGroupCodes[groupbit][CAPCODES_INDEX] < MAXIMUM_GROUPSIZE)
	{
		aGroupCodes[groupbit][CAPCODES_INDEX]++;
		aGroupCodes[groupbit][aGroupCodes[groupbit][CAPCODES_INDEX]] = capcode;

		GroupFrame[groupbit] = assignedframe;
	}
	if (iMessageIndex)
	{	
		message_buffer[iMessageIndex] = 0;		// terminate the buffer string
		iMessageIndex = 0;
	}
}

// checksum check for BIW and vector type words
// returns: 0 if word passes test; 1 if test failed
int xsumchk(long int l)
{
		// was word already marked as bad?
		if (l > 0x3fffffl) return(1);

		// 4 bit checksum is made by summing up remaining part of word
		// in 4 bit increments, and taking the 4 lsb and complementing them.
		// Therefore: if we add up the whole word in 4 bit chunks the 4 lsb
		// bits had better come out to be 0x0f

		int xs = (int) (l        & 0x0f);
		xs += (int) ((l>> 4) & 0x0f);
		xs += (int) ((l>> 8) & 0x0f);
		xs += (int) ((l>>12) & 0x0f);
		xs += (int) ((l>>16) & 0x0f);
		xs += (int) ((l>>20) & 0x01);

		xs = xs & 0x0f;

		if (xs == 0x0f)
		{
				//CountBiterrors(0);
				return(0);
		}
		else
		{
				//CountBiterrors(1);
				return(1);
		}
}


void FlexTIME()
{

	//fprintf(dataFile, "FlexTIME() called\n");

	int year, month, day, hour, minutes;

	int i;
	//char temp[MAX_PATH];
	//char szFlexTIME[128];

	//float frame_seconds= (iCurrentFrame & 0x1f) * 1.875;
	//int seconds		 = frame_seconds;

	static int FLEX_time=0, FLEX_date=0;
	//static int count = 0;

	int bTime = 0, bDate = 0;

//	OUTPUTDEBUGMSG((("Frame[0] = 0x%08X\n"), frame[0]));		
//	OUTPUTDEBUGMSG((("Priority addresses %d\n"), (frame[0] >> 4) & 0xF));		
//	OUTPUTDEBUGMSG((("End Block %d\n"), (frame[0] >> 8) & 0x3));		
//	OUTPUTDEBUGMSG((("Vector %d\n"), (frame[0] >> 10) & 0x7));		
//	OUTPUTDEBUGMSG((("Frame Id %d\n"), (frame[0] >> 16) & 0x7));		

	for (i=0; i<=((frame[0] >> 8) & 0x03); i++)
	{
		if(xsumchk(frame[i]) != 0)
		{
			//printf("CRC error in BIW[%d]! (0x%08X)\n", i, frame[i]);
			return;
		}
		if(i)
		{
			switch((frame[i] >> 4) & 0x07)
			{
				case 0:
					printf((("frame[i]: Type == SSID/Local ID.s (i8-i0)(512) & Coverage Zones (c4-c0)(32)\n")));		
					break;
				case 1:
					frame[i] >>= 7;
					year = (frame[i] & 0x1F) + 1994;
					//printf("year: %lu\n", (frame[i] & 0x1F) + 1994);
					frame[i] >>= 5;
					day = frame[i] & 0x1F;
					//recFlexTime.wDay = frame[i] & 0x1F;
					frame[i] >>= 5;
					month = (frame[i] & 0xF);
					//recFlexTime.wMonth = (frame[i] & 0xF);
					bDate = 1;
					FLEX_date=1;
					//printf((("BIW DATE: %d-%d-%d\n"), recFlexTime.wDay, recFlexTime.wMonth, recFlexTime.wYear));		
					break;
				case 2:
					frame[i] >>= 7;
					hour = frame[i] & 0x1F;
					//recFlexTime.wHour = frame[i] & 0x1F;
					frame[i] >>= 5;
					minutes = frame[i] & 0x3F;
					//recFlexTime.wMinute = frame[i] & 0x3F;
					frame[i] >>= 6;
					//seconds = seconds;
					//recFlexTime.wSecond = seconds;
					bTime = 1;
					FLEX_time=1;
					//printf((("BIW TIME: %02d:%02d:%02d\n"), recFlexTime.wHour, recFlexTime.wMinute, recFlexTime.wSecond));
					break;
				case 5:
					printf((("frame[i]: Type == System Information (I9-I0. A3-A0) - related to NID roaming\n")));		
					break;
				case 7:
					printf((("frame[i]: Type == Country Code & Traffic Management Flags (c9-c0, T3-T0)\n")));		
					break;
				case 6:
				case 3:
				case 4:
					printf((("frame[i]: Type == Reserved\n")));		
					break;
			}
		}
	}
}

void show_phase_speed(int vt, int flex_phase)
{
	switch(flex_speed)
	{
		default:
		case STAT_FLEX1600:
			sprintf(Current_MSG[MSG_BITRATE], "1600");
			break;

		case STAT_FLEX3200:
			sprintf(Current_MSG[MSG_BITRATE], "3200");
			break;

		case STAT_FLEX6400:
			sprintf(Current_MSG[MSG_BITRATE], "6400");
			break;
	}

	switch(flex_phase)
	{
		default:
		case FLEX_PHASE_A:
			sprintf(Current_MSG[MSG_MODE], "FLEX-A");
			break;
		case FLEX_PHASE_B:
			sprintf(Current_MSG[MSG_MODE], "FLEX-B");
			break;
		case FLEX_PHASE_C:
			sprintf(Current_MSG[MSG_MODE], "FLEX-C");
			break;
		case FLEX_PHASE_D:
			sprintf(Current_MSG[MSG_MODE], "FLEX-D");
			break;
	}

	if (vt == MODE_SHORT_INSTRUCTION)
	{
		strcpy(Current_MSG[MSG_TYPE], " GROUP ");	// PH: Add "GROUP" in stead of "INSTR"
	}
	else
	{
		strcpy(Current_MSG[MSG_TYPE], vtype[vt]);	// Add flex format.
	}
}

void display_show_char(int cin)
{
	if (cin == '\n')
	{
	}
	else if (cin > 127)
	{
		cin = '?';	// PH: Display a questionmark instead of 'unknown' characters
		containsUnknownCharacters = 1;
	}
	else if ((cin >  0  && cin < 32 && cin != 10) &&
			 (cin != 23 && cin != 4))
	{
		cin = '?';	// PH: Display a questionmark instead of 'unknown' characters
		containsUnknownCharacters = 1;
	}
	message_buffer[iMessageIndex] = cin;

	if (iMessageIndex < MAX_STR_LEN-1) iMessageIndex++;
} // end of display_show_char

void show_address(long int l, long int l2, int bLongAddress)
{
//	long int capcode;

	if(!bLongAddress)
	{
		//printf("capcode long\n");
		capcode = (l & 0x1fffffl) - 32768l;
	} else
	{
		capcode = (l2 & 0x1fffffl) ^ 0x1fffffl;
		capcode = capcode << 15;
		capcode = capcode + 2068480l + (l & 0x1fffffl);
	}

	if ((l > 0x3fffffl) || (l2 > 0x3fffffl) || (capcode < 0))
	{
		strcpy(Current_MSG[MSG_CAPCODE], bLongAddress ? "?????????" : "???????");
		capcode=9999999;
	} else
	{
		/*if(bLongAddress)
		{
			printf("capcode: %09li\n", capcode);
		} else
		{
			printf("capcode: %07li\n", capcode);
		}
		//printf("capcode: %07li\n", capcode);
		*/
		sprintf(Current_MSG[MSG_CAPCODE], bLongAddress ? "%09li" : "%07li", capcode);
		//printf("capcode: %s\n", Current_MSG[MSG_CAPCODE]);
	}

	if(capcode >= 2029568 && capcode <= 2029583)
	{
		//printf("This is a groupmessage.\n");
		bFLEX_isGroupMessage = 1;
	} else
	{
		iMessagesCounter++;
		bFLEX_isGroupMessage = 0;
	}

	time_t currentTime = time(NULL);
	struct tm * currentTimeInfo;

	currentTimeInfo = localtime(&currentTime);

	strftime(Current_MSG[MSG_DATE], 50, "%d-%m-%Y", currentTimeInfo);
	strftime(Current_MSG[MSG_TIME], 50, "%H:%M:%S", currentTimeInfo);
	sprintf(Current_MSG[MSG_TIMESTAMP], "%d", (int) currentTime);
}

void showframe(int asa, int vsa, int flex_phase)
{
	int bLongAddress = 0;
	FlexTempAddress = -1;

	int iFragmentNumber, iAssignedFrame;

	if(xsumchk(phases[flex_phase].frame[0]) == 0)
	{
		for(int j = asa; j < vsa; j++)
		{
			long int cc2 = phases[flex_phase].frame[j] & 0x1fffffl;

			// check for long addresses (bLongAddress indicates long address)
			if (cc2 < 0x008001l) bLongAddress=1;
			else if ((cc2 > 0x1e0000l) && (cc2 < 0x1f0001l)) bLongAddress=1;
			else if (cc2 > 0x1f7FFEl) bLongAddress=1;

			int vb = vsa + j - asa;
			int vt = (phases[flex_phase].frame[vb] >> 4) & 0x07;

			if(xsumchk(phases[flex_phase].frame[vb]) != 0)
			{
				continue;
			}

			switch(vt)
			{
				default:
					continue;

					show_address(phases[flex_phase].frame[j], phases[flex_phase].frame[j+1], bLongAddress);
					strcpy(Current_MSG[MSG_TYPE], vtype[vt]);
					break;

				case MODE_ALPHA:
				case MODE_SECURE:
					show_address(phases[flex_phase].frame[j], phases[flex_phase].frame[j+1], bLongAddress);
					show_phase_speed(vt, flex_phase);
					int w1, w2, k, c= 0;
					//int iFragmentNumber, iAssignedFrame;
					long int cc;
					//long int cc2, cc3;
					// get start and stop word numbers
					w1 = phases[flex_phase].frame[vb] >> 7;
					w2 = w1 >> 7;
					w1 = w1 & 0x7f;
					w2 = (w2 & 0x7f) + w1 - 1;

					// get message fragment number (bits 11 and 12) from first header word
					// if != 3 then this is a continued message
					if (!bLongAddress)
					{
						iFragmentNumber = (int) (phases[flex_phase].frame[w1] >> 11) & 0x03;
						w1++;
					}
					else
					{
						iFragmentNumber = (int) (phases[flex_phase].frame[vb+1] >> 11) & 0x03;
						w2--;
					}

					for (k=w1; k<=w2; k++)				// dump all message characters onto screen
					{
						/*if (frame[k] > 0x3fffffl) display_color(&Pane1, COLOR_BITERRORS);
						else display_color(&Pane1, COLOR_MESSAGE);*/

						// skip over header info (depends on fragment number)
						if ((k > w1) || (iFragmentNumber != 0x03))
						{
							c = (int) phases[flex_phase].frame[k] & 0x7fl;
							if (c != 0x03)
							{
								//printf("%s", c);
								display_show_char(c);
							}
						}

						cc = (long) phases[flex_phase].frame[k] >> 7;
						c = (int) cc & 0x7fl;

						if (c != 0x03)
						{
							//printf("%s", c);
							display_show_char(c);
						}

						cc = (long) phases[flex_phase].frame[k] >> 14;
						c = (int) cc & 0x7fl;

						if (c != 0x03)
						{
							//printf("%s", c);
							display_show_char(c);
						}
					}

					if (iFragmentNumber < 3)	// Change last 0 of bitrate into fragmentnumber
					{
						//Current_MSG[MSG_BITRATE][3] = '1' + iFragmentNumber;
					}		

					printf("\n");

					break;

				case MODE_SHORT_INSTRUCTION:
					show_address(phases[flex_phase].frame[j], phases[flex_phase].frame[j+1], bLongAddress);	// show address
					if(bFLEX_isGroupMessage) 
					{
						//printf("");
						continue;
					}
					show_phase_speed(vt, flex_phase);
					iAssignedFrame  = (phases[flex_phase].frame[vb] >> 10) & 0x7f;	// Frame with groupmessage
					FlexTempAddress = (phases[flex_phase].frame[vb] >> 17) & 0x7f;	// Listen to this groupcode
					break;
			}

			if(vt == MODE_SHORT_INSTRUCTION || bFLEX_isGroupMessage)
			{
				if(bFLEX_isGroupMessage)
				{
					ConvertGroupcall(capcode-2029568, vtype[vt], capcode);
				} else
				{
					AddAssignment(iAssignedFrame, FlexTempAddress, capcode);
				}
			} else {
				parseSingleMessage();
				ShowMessage();
			}

			if(bLongAddress) j++;

		}
	}

}

/*
// checksum check for BIW and vector type words
// returns: 0 if word passes test; 1 if test failed
int xsumchk(long int l)
{
	// was word already marked as bad?
	if (l > 0x3fffffl) return(1);

	// 4 bit checksum is made by summing up remaining part of word
	// in 4 bit increments, and taking the 4 lsb and complementing them.
	// Therefore: if we add up the whole word in 4 bit chunks the 4 lsb
	// bits had better come out to be 0x0f

	int xs = (int) (l	 & 0x0f);
	xs += (int) ((l>> 4) & 0x0f);
	xs += (int) ((l>> 8) & 0x0f);
	xs += (int) ((l>>12) & 0x0f);
	xs += (int) ((l>>16) & 0x0f);
	xs += (int) ((l>>20) & 0x01);

	xs = xs & 0x0f;

	if (xs == 0x0f)
	{
		//CountBiterrors(0);
		return(0);
	}
	else
	{
		//CountBiterrors(1);
		return(1);
	}
}
*/

// format a received frame
void showblock(int blknum, int flex_phase)
{
	int j, k, err, asa, vsa;
	long int cc;
	static int last_frame;
	int bNoMoreData=0;	// Speed up frame processing

	for (int i=0; i<8; i++)	// format 32 bit frame into output buffer to do error correction
	{
		for (j=0; j<32; j++)
		{
			k = (j*8) + i;
			ob[j] = phases[flex_phase].codewordbuffer[k];
		}

		err = ecd();		// do error correction
		//CountBiterrors(err);

		k = (blknum << 3) + i;

		cc = 0x0000l;

		for (j=0; j<21; j++)
		{
			cc = cc >> 1;
			if (ob[j] == 0) cc ^= 0x100000l;
		}

		if (err == 3) 
		{
			cc ^= 0x400000l; // flag uncorrectable errors
			uncorrectableError = 1;
		}

		phases[flex_phase].frame[k] = cc;
	}
	if ((flex_speed == STAT_FLEX1600) && ((cc == 0x0000l) || (cc == 0x1fffffl)))
	{
		bNoMoreData = 1;	// Speed up frame processing
	}

	vsa = (int) ((phases[flex_phase].frame[0] >> 10) & 0x3f);		// get word where vector  field starts (6 bits)
	asa = (int) ((phases[flex_phase].frame[0] >> 8)  & 0x03) + 1;	// get word where address field starts (2 bits)

	if (blknum == 0)
	{
		if (vsa == asa)					// PH: Assuming no messages in current frame,
		{
			//fprintf(dataFile, "Empty message...\n");
			//bEmpty_Frame=true;
		}
		else
		{
			//bEmpty_Frame=false;
		}

		FlexTIME();

		/*if (!bFlexTIME_detected && !bFlexTIME_not_used)
		{
			FlexTIME();
		}
		else if ((iCurrentFrame == 0) && (last_frame == 127))
		{
			if (Profile.FlexTIME)
			{
				FlexTIME();
			}
		}*/

		if((iCurrentFrame == 0) && (last_frame == 127))
		{
			FlexTIME();
		}

		last_frame = iCurrentFrame;
		//printf("iCurrentFrame: %d\n", iCurrentFrame);
	}
	// show messages in frame if last block was processed and we're not in reflex mode
	else if (((blknum == 10) || bNoMoreData) /*&& !bReflex*/)
	{
		showframe(asa, vsa, flex_phase);
		if (bNoMoreData) iFlexBlock=1;
	}
}

void setupecc()
{
	unsigned int srr, j, k;

	// calculate all information needed to implement error correction
	srr = 0x3B4;

	for (int i=0; i<=20; i++)
	{
		ecs[i] = srr;
		if ((srr & 0x01) != 0) srr = (srr >> 1) ^ 0x3B4;
		else                   srr = srr >> 1;
	}

	// bch holds a syndrome look-up table telling which bits to correct
	// first 5 bits hold location of first error; next 5 bits hold location
	// of second error; bits 12 & 13 tell how many bits are bad
	for (int i=0; i<1024; i++) bch[i] = 0;

	for (int n=0; n<=20; n++)	// two errors in data
	{
		for (int i=0; i<=20; i++)
		{
			j = (i << 5) + n;
			k = ecs[n] ^ ecs[i];
			bch[k] = j + 0x2000;
		}
	}

	// one error in data
	for (int n=0; n<=20; n++)
	{
		k = ecs[n];
		j = n + (0x1f << 5);
		bch[k] = j + 0x1000;
	}

	// one error in data and one error in ecc portion
	for (int n=0; n<=20; n++)
	{
		for (int i=0; i<10; i++)  // ecc screwed up bit
		{
			k = ecs[n] ^ (1 << i);
			j = n + (0x1f << 5);
			bch[k] = j + 0x2000;
		}
	}

	// one error in ecc
	for (int n=0; n<10; n++)
	{
		k = 1 << n;
		bch[k] = 0x3ff + 0x1000;
	}

	// two errors in ecc
	for (int n=0; n<10; n++)
	{
		for (int i=0; i<10; i++)
		{
			if (i != n)
			{
				k = (1 << n) ^ (1 << i);
				bch[k] = 0x3ff + 0x2000;
			}
		}
	}
}

int bit10(int gin)
{
	int k=0;

	for (int i=0; i<10; i++)
	{
		if ((gin & 0x01) != 0) k++;
		gin = gin >> 1;
	}
	return(k);
}

int ecd()
{
	int synd, b1, b2, i;
	int errors=0, parity=0;

	int ecc = 0x000;
	int acc = 0;

	// run through error detection and correction routine

	for (i=0; i<=20; i++)
	{
		if (ob[i] == 1)
		{
			ecc = ecc ^ ecs[i];
			parity = parity ^ 0x01;
		}
	}

	for (i=21; i<=30; i++)
	{
		acc = acc << 1;
		if (ob[i] == 1) acc = acc ^ 0x01;
	}

	synd = ecc ^ acc;

	if (synd != 0) // if nonzero syndrome we have error
	{
		if (bch[synd] != 0) // check for correctable error
		{
			b1 = bch[synd] & 0x1f;
			b2 = bch[synd] >> 5;
			b2 = b2 & 0x1f;

			if (b2 != 0x1f)
			{
				ob[b2] = ob[b2] ^ 0x01;
				ecc = ecc ^ ecs[b2];
			}

			if (b1 != 0x1f)
			{
				ob[b1] = ob[b1] ^ 0x01;
				ecc = ecc ^ ecs[b1];
			}
			errors = bch[synd] >> 12;
		}
		else errors = 3;

		if (errors == 1) parity = parity ^ 0x01;
	}

	// check parity ....
	parity = (parity + bit10(ecc)) & 0x01;

	if (parity != ob[31]) errors++;

	if (errors > 3) errors = 3;

	//CountBiterrors(errors);

	return(errors);
}

int nOnes(int k)
{
	int kt=0;

	if (k == 0) return(0);

	for (int i=0; i<=15; i++)
	{
		if ((k & 0x0001) != 0) kt++;
		k = k >> 1;
	}
	return(kt);
}

void
frame_flex(char input)
{
	static short int iBitBuffer[4] = { 0, 0, 0, 0 };
	static int cy, fr;
	static int hbit, iCodeWordCount;

	int nh;
	double aver = 0.0;
	int ihd;
	int hd = 0;

	for(int i = 0; i < 3; i++)
	{
		iBitBuffer[i] = iBitBuffer[i] << 1;
		if(iBitBuffer[i+1] & 0x8000)
		{
			iBitBuffer[i] |= 0x0001;
		}
	}

	iBitBuffer[3] = iBitBuffer[3] << 1;

	if(input < 2)
	{
		iBitBuffer[3] |= 0x0001;
	}

	if(iFlexBlock == 0)
	{

		if(iFlexTimer)
		{
			if (nOnes(iBitBuffer[2] ^ EOT1) + nOnes(iBitBuffer[3] ^ EOT2) == 0)	// End of transmission?
			{
				printf("\nMessages counter: %d\n", iMessagesCounter);
				iFlexTimer = 0;
				return;
			}
		}

		nh = nOnes(iBitBuffer[1] ^ SYNC1) + nOnes(iBitBuffer[2] ^ SYNC2);
		
		if(nh == 32)
		{
			if(((iBitBuffer[0] ^ iBitBuffer[3]) & 0xFFFF) == 0xFFFF)
			{
				iBitBuffer[0] ^= 0xFFFF;
				iBitBuffer[3] ^= 0xFFFF;
				nh = 0;
				printf("Setting zero..\n");
			}
		}

		if(nh < 2)
		{
			/* Reset phase strucs */
			memset(&phases[FLEX_PHASE_A], 0, 4 * sizeof(struct flex_phase));
			phases[FLEX_PHASE_A].phase = 'A';
			phases[FLEX_PHASE_B].phase = 'B';
			phases[FLEX_PHASE_C].phase = 'C';
			phases[FLEX_PHASE_D].phase = 'D';

			iFlexBlockCount = 89;

			if(nOnes(iBitBuffer[0] ^ iBitBuffer[3] ^ 0xFFFF) < 2)
			{
				int speed;
				for(speed = 0; speed < 8; speed++)
				{
					if((nOnes(iBitBuffer[0] ^ syncs[speed]) + nOnes(iBitBuffer[3] ^ ~syncs[speed])) < 2)
					{
						if ((speed & 0x03) == 0)
						{
							/* FLEX-1600 */
							flex_speed = STAT_FLEX1600;
						} else if((speed & 0x03) == 0x03)
						{
							/* FLEX-6400 */
							flex_speed = STAT_FLEX6400;

						} else
						{
							flex_speed = STAT_FLEX3200;
						}

						iFlexTimer = 20;
						bFlexActive = 1;

						g_sps   = (speed & 0x01) ? 3200 : 1600;
						level   = (speed & 0x02) ? 4 : 2;
						reflex = (speed & 0x04) ? 1 : 0;
						//printf("g_sps: %d\n", g_sps);
						//printf("level: %d\n", level);
						//printf("reflex: %d\n", reflex);
						break;

					}
				}
				if(speed == 8)
				{
					/* Found sync */
					printf("UNKNOWN SYNC HEADER : %hX %hX %hX %hX", iBitBuffer[0], iBitBuffer[1], iBitBuffer[2], iBitBuffer[3]);
					return;
				}
			} else
			{
				printf("returning\n");
				return;
			}

			for(int j = 0; j < 64; j++)
			{
				aver = aver + rcver[j];
			}

			aver *= 0.015625;
			aver *= 0.5;
			exc  += aver;

		}

		if(iFlexBlockCount > 0)
		{
			iFlexBlockCount--;

			/* This fills the FIW (Frame Information Word) codeword, with frame and cycle number */

			if((iFlexBlockCount < 72) && (iFlexBlockCount > 39))
			{
				if(input < 2)
				{
					ob[71-iFlexBlockCount] = 1;
				} else
				{
					ob[71-iFlexBlockCount] = 0;
				}
			} else if(iFlexBlockCount == 39)
			{
				int cer = ecd();

				if(cer < 2)
				{
					for(ihd = 4; ihd < 8; ihd++)
					{
						hd = hd >> 1;
						if(ob[ihd] == 1)
						{
							hd ^= 0x08;
						}
					}
					cy = (hd & 0x0f) ^ 0x0f;
					//printf("Cycle: %d\n", cy);
					iCurrentCycle = cy;
	
					for(ihd = 8; ihd <= 14; ihd++)
					{
						hd = hd >>1;
						if(ob[ihd] == 1)
						{
							hd ^= 0x40;
						}
					}
					fr = (hd & 0x7f) ^ 0x7f;
					//printf("Frame: %d\n", fr);
					iCurrentFrame = fr;
				}

			}

			if(iFlexBlockCount == 0)
			{
				iFlexBlock = 11;
				iCodeWordCount = 0;
				hbit = 0;
			}

		}

	} else
	{
		if(g_sps == 1600)
		{
			if(input < 2)
			{
				block[iCodeWordCount] = 1;
				phases[FLEX_PHASE_A].codewordbuffer[iCodeWordCount] = 1;
			}
			else
			{
				block[iCodeWordCount] = 0;
				phases[FLEX_PHASE_A].codewordbuffer[iCodeWordCount] = 0;
			}

			if(level == 4)
			{
				if((input == 0) || (input == 3))
				{
					phases[FLEX_PHASE_B].codewordbuffer[iCodeWordCount] = 1;
				} else
				{
					phases[FLEX_PHASE_B].codewordbuffer[iCodeWordCount] = 0;	
				}
			}
			iCodeWordCount++;
		} else
		{
			if(hbit == 0)
			{
				if(input < 2)
				{
					block[iCodeWordCount] = 1;
					phases[FLEX_PHASE_A].codewordbuffer[iCodeWordCount] = 1;
				} else
				{
					block[iCodeWordCount] = 0;
					phases[FLEX_PHASE_A].codewordbuffer[iCodeWordCount] = 0;
				}

				if(level == 4)
				{
					if((input == 0) || (input == 3))
					{
						phases[FLEX_PHASE_B].codewordbuffer[iCodeWordCount] = 1;
					} else
					{
						phases[FLEX_PHASE_B].codewordbuffer[iCodeWordCount] = 0;
					}
				}

				hbit++;
			} else
			{
				if(input < 2)
				{
					phases[FLEX_PHASE_C].codewordbuffer[iCodeWordCount] = 1;
				} else
				{
					phases[FLEX_PHASE_C].codewordbuffer[iCodeWordCount] = 0;
				}

				if(level == 4)
				{
					if((input == 0) || (input == 3))
					{
						phases[FLEX_PHASE_D].codewordbuffer[iCodeWordCount] = 1;
					} else
					{
						phases[FLEX_PHASE_D].codewordbuffer[iCodeWordCount] = 0;
					}
				}

				hbit = 0;
				iCodeWordCount++;
			}
		}
		
		if(iCodeWordCount == 256)
		{
			iCodeWordCount = 0;

			showblock((11-iFlexBlock), FLEX_PHASE_A);

			if(level == 4)
			{
				showblock((11-iFlexBlock), FLEX_PHASE_B);
			}

			if(g_sps == 3200)
			{
				showblock((11-iFlexBlock), FLEX_PHASE_C);

				if(level == 4)
				{
					showblock((11-iFlexBlock), FLEX_PHASE_D);
				}
			}

			iFlexBlock--;
			if(iFlexBlock == 0)
			{
				bFlexActive = 0;
				iFrameCount++;
				//printf("This is the end! Frame ID: %d\n", iFrameCount);
			}
		}

	}

}


void 
usage(void)
{
	puts("usage: scanner -d DEVICE [-h]");
}

int
open_port(void)
{
	int iFileDescriptor;

	iFileDescriptor = open(serialdevice, O_RDONLY | O_NOCTTY);

	if(iFileDescriptor == -1)
	{
		printf("Couldn't open %s", serialdevice);
		return -1;
	} else
	{
		fcntl(iFileDescriptor, F_SETFL, 0);
	}
	return iFileDescriptor;
}

int
daemonize()
{
	/* Source: http://www.danielhall.me/2010/01/writing-a-daemon-in-c/ */

	/* Clone ourselves to make a child */
	pid = fork();

	/* If the pid is less than zero, something went wrong when forking */
	if(pid < 0)
	{
		exit(EXIT_FAILURE);
	}

	/* If the pid we got back was greater than zero, then the clone was successful and we are the parent. */
	if(pid > 0)
	{
		exit(EXIT_SUCCESS);
	}

	/* If execution reaches this point we are the child */
	/* Set the umask to zero */
	umask(0);

	/* Open a connection to the syslog server */
	openlog("p2000scanner",LOG_NOWAIT|LOG_PID,LOG_USER); 

	/* Sends a message to the syslog daemon */
	syslog(LOG_NOTICE, "Successfully started daemon\n");

	/* Try to create our own process group */
	sid = setsid();
	if(sid < 0)
	{
		syslog(LOG_ERR, "Could not create process group\n");
		exit(EXIT_FAILURE);
	}

	/* Change the current working directory */
	if ((chdir("/")) < 0)
	{
		syslog(LOG_ERR, "Could not change working directory to /\n");
		exit(EXIT_FAILURE);
	}

	/* Close the standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	return EXIT_SUCCESS;

}

int
main(int argc, char **argv)
{
	extern char *optarg;

	static struct option longopts[] = {
		{"device", required_argument, 0, 'd'},
		{"daemonize", no_argument, 0, 'D'},
		{"help", no_argument, 0, 'h'}
	};

	char shortopts[] = "d:D:h";

	if(argc<=1)
	{
		usage();
		exit(1);
	}

	int option;

	while((option=getopt_long(argc, argv, shortopts, longopts, NULL)) != -1)
	{
		/* We've got some parameters, so don't daemonize, unless specifically requested */
		bisDaemon = 0;

		switch(option)
		{
			case 'd':
				strncpy(serialdevice, optarg, FILENAME_MAX-1);
			break;
			case 'D':
				bisDaemon = 1;
			break;
			case 'h':
				usage();
				exit(1);
			break;
			default:
				/* Test! */
				//puts("Found unknown switch");
				usage();
				exit(1);
			break;
		}
	}

	
	signal(SIGINT, signalhandler);
	signal(SIGTERM, signalhandler);
	signal(SIGKILL, signalhandler);

	/* Initialize Zeromq */

	context = zmq_ctx_new();
	socket = zmq_socket (context, ZMQ_PUB);

	int ipv4only = 0;
	zmq_setsockopt(socket, ZMQ_IPV4ONLY, &ipv4only, sizeof(int));

	zmq_bind(socket, endpoint);

	printf("Bound to %s\n", endpoint);

	/* Create a thread and initialize socket and bind monitor */

	zmq_socket_monitor(socket, "inproc://monitor.req", ZMQ_EVENT_ALL);

	setupecc();

	int iFileDescriptor;

	logFile = fopen("log.txt", "a");
	if(logFile == NULL)
	{
		printf("Cannot open logfile: %s\n", strerror(errno));
		exit(1);
	}

	setvbuf(logFile, NULL, _IOLBF, 0);

	writeToLog("Program started\n");

	dataFile = fopen("binary-data.log", "a");
	if(dataFile == NULL)
	{
		exit(1);
	}

	if(setvbuf(dataFile, NULL, _IOLBF, 0))
	{
		printf("Something went wrong with setting some buffer options.\n");
		exit(1);
	}

	pthread_create(&threads[0], NULL, req_socket_monitor, context);

	char buffer[BUFSIZE];
	unsigned char linedatabuffer[10000];
	unsigned short freqdatabuffer[10000];
	double timing = 1.0/(1600.0*838.8e-9);	// 745.1

	double temptiming;
	unsigned int tempdata = 48;
	unsigned int tempsymbol = 49;
	unsigned int pd_lcw = 0;
	signed int pd_dinc = 0;

	int offset = 0;

	if((iFileDescriptor = open_port()) == -1)
	{
		exit(1);
	}
	struct termios newtio;
	bzero(&newtio, sizeof(newtio));    

	newtio.c_cflag = B19200 | CS8 | CLOCAL | CREAD;
	newtio.c_oflag &= ~OPOST;
	
	cfsetispeed(&newtio, B19200);
	cfsetospeed(&newtio, B19200);

	tcflush(iFileDescriptor, TCIFLUSH);
	tcsetattr(iFileDescriptor,TCSANOW,&newtio);
	
	int bytesRcvd;
	

	/*
	 * Let's initialize some stuff!
	 *
	 */

	for(int i = 0; i < 64; i++)
	{
		rcver[i] = 0.0;
	}

	while((bytesRcvd = read(iFileDescriptor, buffer, BUFSIZE - 1)) != -1 && exitRequested == 0)
	{

		if(bytesRcvd == 0)
			continue;

		int bit;

		for(int i = 0; i < bytesRcvd; i++)
		{
			for(int j = 7; j >= 0; j--)
			{
				bit = (buffer[i] >> j) & 1;
				//bitarray[j] = bit;
				linedatabuffer[offset] = bit << 4;
				//printf("bit placed %d\n", linedatabuffer[offset]);
				freqdatabuffer[offset++] = timing;
				if(offset >= 10000)
				{
					offset = 0;
				}
			}
		}

		while(pd_i != offset)
		{
			/* Get frequency data */
			temptiming = freqdatabuffer[pd_i];
			/* Get corresponding linedata and convert it back to zeros and ones */
			tempdata = linedatabuffer[pd_i] >> 4;
			/* Convert it to symbols */
			tempsymbol = iLineSymbols[tempdata];

			// if two level interface: force symbol to be either 0 or 3 and
			// process as if 4 level interface were used. Phases B & D then
			// always get a long stream of 0's and don't produce any output.

			if(tempsymbol > 0)
			{
				tempsymbol = 3;
			}

			tempsymbol ^= 0x03;

/*			if(!bFlexActive && ((tempsymbol >> 1) != pd_lcw))
			{
				
			} else
			{
				pd_dinc += freqdatabuffer[pd_i];
				printf("No POCSAG\n");
			}
*/


			while(temptiming >= (exc + 0.5 * timing))
			{
				temptiming = temptiming - timing;
				//printf("--- Start\n");
				frame_flex(tempsymbol);
				//printf("--- End\n");
				break;
			}


			pd_i++;

			if(pd_i == 10000) pd_i = 0;
		}
		sleep(1);
	}

	fflush(NULL);

	pthread_detach(threads[0]);

	printf("ZMQ unbind: %d\n", zmq_unbind(socket, endpoint));
	printf("ZMQ close: %d\n", zmq_close(socket));
	printf("ZMQ context: %d\n", zmq_ctx_destroy(context));
	

	printf("Bye!\n");

	fclose(logFile);
	fclose(dataFile);

	exit(EXIT_SUCCESS);

}
