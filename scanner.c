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

#define SYNC1	0xA6C6
#define SYNC2	0xAAAA

#define EOT1	0xAAAA
#define EOT2	0xFFFF

#define BUFSIZE 1024

int pd_i = 0;

char ob[32];

int iFlexBlock = 0;
int iFlexBlockCount = 0;
int iFlexTimer = 0;

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

// this table translates received modem status line combinations into
// the received symbol; it gives the number of modem status lines that
// are high (other than RI) for a given line status nybble
int iLineSymbols[16] = { 0, 1, 1, 2, 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3 };

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

	int nh; //geen idee wat dit is
	double aver = 0.0;

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
				printf("End of transmission detected.\n");
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
			iFlexBlockCount = 89;

			if(nOnes(iBitBuffer[0] ^ iBitBuffer[3] ^ 0xFFFF) < 2)
			{
				for(int speed = 0; speed < 8; speed++)
				{
					if((nOnes(iBitBuffer[0] ^ syncs[speed]) + nOnes(iBitBuffer[3] ^ ~syncs[speed])) < 2)
					{
						if ((speed & 0x03) == 0)
						{
						}

						iFlexTimer = 20;

						g_sps   = (speed & 0x01) ? 3200 : 1600;
						level   = (speed & 0x02) ? 4 : 2;
						printf("g_sps: %d\n", g_sps);
						printf("level: %d\n", level);
						break;

					}
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
				printf("Hier doen we error correction\n");
			}

			if(iFlexBlockCount == 0)
			{
				printf("hy");
				iFlexBlock = 11;
			}

		}

	} else
	{
		if(g_sps == 1600)
		{
			
		}
	}

}

int
open_port(void)
{
	int iFileDescriptor;

	iFileDescriptor = open("/dev/ttyUSB1", O_RDWR | O_NOCTTY);

	if(iFileDescriptor == -1)
	{
		printf("Couldn't open /dev/blabla");
		return -1;
	} else
	{
		fcntl(iFileDescriptor, F_SETFL, 0);
	}
	return iFileDescriptor;
}


int
main()
{
	printf("*** Program started. Mede mogelijk gemaakt door Peter Hunt, Rutger A. Heunks, Andreas Verhoeven!\n");
	int iFileDescriptor;
	FILE *dataFile;

	dataFile = fopen("binary-data.log", "a");
	if(dataFile == NULL)
	{
		exit(1);
	}
	
	char buffer[BUFSIZE];
	unsigned char linedatabuffer[10000];
	unsigned short freqdatabuffer[10000];
	double timing = 1.0/(1600.0*838.8e-9);	// 745.1

	double temptiming;
	unsigned int tempdata = 48;
	unsigned int tempsymbol = 49;



	int offset = 0;

	if((iFileDescriptor = open_port()) == -1)
	{
		printf("oops!");
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

	while((bytesRcvd = read(iFileDescriptor, buffer, BUFSIZE - 1)) != -1)
	{
		if(bytesRcvd == 0)
			continue;

		int bit;
		int bitarray[7];

		for(int i = 0; i < bytesRcvd; i++)
		{
			for(int j = 7; j >= 0; j--)
			{
				bit = (buffer[i] >> j) & 1;
				bitarray[j] = bit;
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

			while(temptiming >= (exc + 0.5 * timing))
			{
				temptiming = temptiming - timing;
				frame_flex(tempsymbol);
				break;
			}


			pd_i++;

			if(pd_i == 10000) pd_i = 0;
		}

		//printf("\n");

		//printf("--- End\n");
		usleep(100);
	}
}

