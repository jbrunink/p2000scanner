/* Function prototypes */

void signalhandler(int sig);
char *trim(char *s);
void freedata(void *data, void *hint);
void writeToLog(const char* format, ...);
static void *req_socket_monitor (void *ctx);
void parseSingleMessage();
void ShowMessage();
void ConvertGroupcall(int groupbit, char *vtype, int capcode);
void SortGroupCall(int groupbit);
void AddAssignment(int assignedframe, int groupbit, int capcode);
int xsumchk(long int l);
void FlexTIME();
void show_phase_speed(int vt);
void display_show_char(int cin);
void show_address(long int l, long int l2, int bLongAddress);
void showframe(int asa, int vsa, int flex_phase);
void showblock(int blknum, int flex_phase);
void setupecc();
int bit10(int gin);
int ecd();
int nOnes(int k);
void frame_flex(char input);
void usage(void);
int open_port(void);
int daemonize();

/* End function prototypes */

struct flex_phase
{
	unsigned char phase;
	unsigned char codewordbuffer[256];
	long int frame[200];
};

/* Pre-processor */

#define SYNC1	0xA6C6
#define SYNC2	0xAAAA

#define EOT1	0xAAAA
#define EOT2	0xFFFF

#define BUFSIZE 1024

#define STAT_FLEX1600		2

#define MODE_SECURE			0
#define MODE_SHORT_INSTRUCTION		1
#define MODE_SH_TONE			2
#define MODE_STNUM			3
#define MODE_SFNUM			4
#define MODE_ALPHA			5
#define MODE_BINARY			6
#define MODE_NUNUM			7

#define MAX_STR_LEN			5120

#define MSG_CAPCODE		1
#define MSG_TIME		2
#define MSG_DATE		3
#define MSG_MODE		4
#define MSG_TYPE		5
#define MSG_BITRATE		6
#define MSG_MESSAGE		7
#define MSG_MOBITEX		8
#define MSG_TIMESTAMP		9

#define MAXIMUM_GROUPSIZE       1000
#define CAPCODES_INDEX		0

#define STAT_FLEX6400    0
#define STAT_FLEX3200    1
#define STAT_FLEX1600    2
#define STAT_POCSAG2400  3
#define STAT_POCSAG1200  4
#define STAT_POCSAG512   5
#define STAT_ACARS2400   6
#define STAT_MOBITEX     7
#define STAT_ERMES       8
#define NUM_STAT         9

#define FLEX_PHASE_A 0
#define FLEX_PHASE_B 1
#define FLEX_PHASE_C 2
#define FLEX_PHASE_D 3

/* End pre-processor */