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
void showframe(int asa, int vsa);
void showblock(int blknum);
void setupecc();
int bit10(int gin);
int ecd();
int nOnes(int k);
void frame_flex(char input);
void usage(void);
int open_port(void);
int daemonize();
