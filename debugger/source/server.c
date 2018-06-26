// golden
// 6/12/2018
//

#include "server.h"

struct debug_server dbgsrv;

int cmd_handler(int fd, struct cmd_packet *packet) {
	if (!VALID_CMD(packet->cmd)) {
		return 1;
	}

    uprintf("[ps4debug] cmd_handler %X", packet->cmd);

    if(VALID_PROC_CMD(packet->cmd)) {
        return proc_handle(fd, packet);
    } else if(VALID_DEBUG_CMD(packet->cmd)) {
        return debug_handle(fd, packet);
    } else if(VALID_KERN_CMD(packet->cmd)) {
        return kern_handle(fd, packet);
    } else if(VALID_CONSOLE_CMD(packet->cmd)) {
        return console_handle(fd, packet);
    }

    return 0;
}

void *client_thread(void *arg) {
    int r, fd;
    struct cmd_packet packet;
    uint32_t length;
    uint8_t *data;

    fd = (uint64_t)(void *)arg;

    while(1) {
        r = net_recv_data(fd, &packet, CMD_PACKET_SIZE, 0);

        if (!r) {
			// check if disconnected
			if (errno == ECONNRESET) {
				goto error;
			}

			continue;
		}

        uprintf("[ps4debug] client_thread packet %i", fd);

        // invalid packet
		if (packet.magic != PACKET_MAGIC) {
            uprintf("[ps4debug] invalid packet magic %X!", packet.magic);
			continue;
		}

		// mismatch received size
		if (r != CMD_PACKET_SIZE) {
            uprintf("[ps4debug] invalid recieve size %i!", r);
			continue;
		}

        length = packet.datalen;
		if (length) {
			// allocate data
			data = (uint8_t *)malloc(length);
			if (!data) {
				goto error;
			}

            uprintf("recieving data length %i", length);

			// recv data
			r = net_recv_data(fd, data, length, 1);
			if (!r) {
				goto error;
			}

			// set data
			packet.data = data;
		} else {
			packet.data = NULL;
		}

        // check crc if there is one
        if(packet.crc) {
            if(packet.crc != crc32(0, data, length)) {
                goto error;
            }
        }

		// handle the packet
		r = cmd_handler(fd, &packet);

    	if (data) {
			free(data);
			data = NULL;
		}

		// check cmd handler error
		if (r) {
			goto error;
		}

        sceKernelUsleep(50000);
    }

error:
    uprintf("[ps4debug] client_thread error %i", fd);
    sceNetSocketClose(fd);

    return 0;
}

void start_server() {
    uprintf("[ps4debug] server started");

    int fd, flag;

    // reset server
    memset(&dbgsrv, 0, sizeof(struct debug_server));

    // server structure
    dbgsrv.server.sin_len = sizeof(dbgsrv.server);
    dbgsrv.server.sin_family = AF_INET;
    dbgsrv.server.sin_addr.s_addr = SERVER_IN;
    dbgsrv.server.sin_port = sceNetHtons(SERVER_PORT);
    memset(dbgsrv.server.sin_zero, 0, sizeof(dbgsrv.server.sin_zero));

    // start up server
    dbgsrv.servsock = sceNetSocket("dbgsock", AF_INET, SOCK_STREAM, 0);

    sceNetBind(dbgsrv.servsock, (struct sockaddr *)&dbgsrv.server, sizeof(dbgsrv.server));

    sceNetListen(dbgsrv.servsock, 16);

    flag = 1;
	sceNetSetsockopt(dbgsrv.servsock, SOL_SOCKET, SO_NBIO, (char *)&flag, sizeof(int));

    while(1) {
        scePthreadYield();

        fd = sceNetAccept(dbgsrv.servsock, NULL, NULL);
        if(fd > -1) {
            uprintf("[ps4debug] accepted a client");

            flag = 1;
            sceNetSetsockopt(fd, SOL_SOCKET, SO_NBIO, (char *)&flag, sizeof(int));
            
            ScePthread thread;
            scePthreadCreate(&thread, NULL, client_thread, (void *)(uint64_t)fd, "dbgclient");
        }

        sceKernelSleep(1);
    }
}
