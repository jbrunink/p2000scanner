#include <zmq.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int exitRequested = 0;

int ZMQ_MAJOR_VERSION;
int ZMQ_MINOR_VERSION;
int ZMQ_PATCH_VERSION;

const char * endpoint = "tcp://[::1]:5555";

void signalhandler(int sig)
{
        if(sig == 2)
        {
                printf("\n\nSIGINT received, requesting exit from while()\n");
                exitRequested = 1;
        }
}


int main()
{
	signal(SIGINT, signalhandler);
	zmq_version(&ZMQ_MAJOR_VERSION, &ZMQ_MINOR_VERSION, &ZMQ_PATCH_VERSION);

	printf("Client started; ZMQ version: %d.%d.%d\n", ZMQ_MAJOR_VERSION, ZMQ_MINOR_VERSION, ZMQ_PATCH_VERSION);

	void * context = zmq_ctx_new();
	void * socket = zmq_socket(context, ZMQ_SUB);
	void * optionvalue = "";
	zmq_setsockopt(socket, ZMQ_SUBSCRIBE, &optionvalue, 0);
        int ipv4only = 0;
        int socketopt = zmq_setsockopt(socket, ZMQ_IPV4ONLY, &ipv4only, sizeof(int));

	int connect = zmq_connect(socket, endpoint);
	printf("Connect: %d\n", connect);

	while(exitRequested == 0)
	{
		zmq_msg_t message;
		int rc = zmq_msg_init(&message);
		assert(rc == 0);

		rc = zmq_msg_recv(&message, socket, 0);

		if(rc == -1)
		{
			if(zmq_errno() == EAGAIN)
			{
				zmq_msg_close(&message);
				continue;
			}
			zmq_msg_close(&message);
			continue;

		} else if(rc > 0)
		{
			void * data = zmq_msg_data(&message);

			int iMsgSize = zmq_msg_size(&message);
			char * testbuffer;
			testbuffer = (char *) malloc(iMsgSize + 1);
			memcpy(testbuffer, data, iMsgSize);

			testbuffer[iMsgSize] = 0;

			printf("'%s'\n", testbuffer);
			free(testbuffer);
		}
		printf("MSG close: %d\n", zmq_msg_close(&message));
		sleep(1);
	}
	

	zmq_disconnect(socket, endpoint);
	zmq_close(socket);
	zmq_ctx_destroy(context);

	printf("Bye!\n");

}
