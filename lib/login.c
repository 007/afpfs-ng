

/*
 *  login.c
 *
 *  Copyright (C) 2006 Alex deVries
 *  Portions copyright (C) 2007 Derrik Pates
 *
 */

#include <string.h>
#include <stdlib.h>
#include <syslog.h>
#include "dsi.h"
#include "dsi_protocol.h"
#include "afp.h"
#include "utils.h"
#include "log.h"


int afp_logout(struct afp_server *server, unsigned char wait) 
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t pad;
	}  __attribute__((__packed__)) request;
	dsi_setup_header(server,&request.dsi_header,DSI_DSICommand);
	request.command=afpLogout;
	request.pad=0;
	return dsi_send(server, (char *) &request,sizeof(request),
	wait,afpLogout,NULL);
}

int afp_login_reply(struct afp_server *server, char *buf, unsigned int size,
		struct afp_rx_buffer *other) {
	struct {
		struct dsi_header header __attribute__((__packed__));
		char userauthinfo[];
	} * afp_login_reply_packet = (void *)buf;

	size -= sizeof(struct dsi_header);
	if (size > 0 && other != NULL) {
		if (size > other->maxsize)
			size = other->maxsize;
		memcpy(other->data, afp_login_reply_packet->userauthinfo, size);
	}

	return 0;
}

int afp_login(struct afp_server *server, char * ua_name, 
	char * userauthinfo, unsigned int userauthinfo_len,
	struct afp_rx_buffer *rx)
{

	char * msg;
	char * p;
	int ret;
	struct {
		struct dsi_header header __attribute__((__packed__));
		uint8_t command;
	}  __attribute__((__packed__)) * request;
	unsigned int len = 
		sizeof(*request) /* DSI Header */
		+ strlen(server->using_version->av_name) + 1 /* Version */
		+ strlen(ua_name) + 1   /* UAM */
		+ userauthinfo_len;

	msg = malloc(len);
	if (!msg) return -1;
	request = (void *) msg;
	p=msg+(sizeof(*request));

	dsi_setup_header(server,&request->header,DSI_DSICommand);
	request->command=afpLogin;
	p +=copy_to_pascal(p,server->using_version->av_name)+1;
	p +=copy_to_pascal(p,ua_name)+1;

	memcpy(p,userauthinfo,userauthinfo_len);

	ret=dsi_send(server, (char *) msg,len,1,afpLogin, (void *)rx);
	free(msg);
	
	return ret;
}


int afp_logincont(struct afp_server *server, unsigned short id,
	char * userauthinfo, unsigned int userauthinfo_len,
	struct afp_rx_buffer *rx)
{
	char *msg;
	char *p;
	int ret;
	struct {
		struct dsi_header header __attribute__((__packed__));
		uint8_t command;
		uint8_t pad;
		uint16_t id;
	} __attribute__((__packed__)) * request;
	unsigned int len =
		sizeof(*request) /* DSI header */
		+ userauthinfo_len;

	msg = malloc(len);
	if (msg == NULL)
		return -1;
	memset(msg, 0, len);
	request = (void *)msg;
	p = msg + sizeof(*request);

	dsi_setup_header(server, &request->header, DSI_DSICommand);
	request->command = afpLoginCont;
	request->id = htons(id);
	memcpy(p, userauthinfo, userauthinfo_len);

	ret = dsi_send(server, (char *)msg, len, 1, afpLoginCont, (void *)rx);
	free(msg);

	return ret;
}