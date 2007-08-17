
/*
 *  map.c
 *
 *  Copyright (C) 2007 Alex deVries
 *
 */

#include <string.h>
#include <stdlib.h>

#include "dsi.h"
#include "afp.h"
#include "utils.h"
#include "afp_protocol.h"
#include "log.h"

/* This is used to pass the return values back from afp_getuserinfo_reply() */
struct uidgid {
	unsigned int uid;
	unsigned int gid;
};

int afp_getuserinfo_request(struct afp_server * server, int thisuser,
	unsigned int userid, unsigned short bitmap, 
	unsigned int *newuid, unsigned int *newgid)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t thisuser;
		uint32_t userid __attribute__((__packed__)); 
		uint16_t bitmap __attribute__((__packed__)); 
	}  __attribute__((__packed__)) request;
	struct uidgid uidgid;
        int ret;

	dsi_setup_header(server,&request.dsi_header,DSI_DSICommand);
	request.command=afpGetUserInfo;
	request.thisuser=(thisuser!=0);
	request.userid=htonl(userid);
	request.bitmap=htons(bitmap);
	ret=dsi_send(server, (char *) &request,sizeof(request),
		1,afpGetUserInfo, &uidgid);

	if (bitmap & kFPGetUserInfo_USER_ID) 
		*newuid=uidgid.uid;
	if (bitmap & kFPGetUserInfo_PRI_GROUPID)
		*newgid=uidgid.gid;

	return ret;
}


int afp_getuserinfo_reply(struct afp_server *server, char * buf, unsigned int size, void *other)
{
	struct {
		struct dsi_header header __attribute__((__packed__));
		uint16_t bitmap;
		uint32_t id1;
		uint32_t id2;
	}  __attribute__((__packed__)) * reply= (void *) buf;
	struct uidgid *uidgid = other;
	unsigned short bitmap;
	
printf("id: %d\n",(unsigned int) htonl(reply->id2));
	if (size < sizeof (struct dsi_header))
		return -1;

	bitmap=ntohs(reply->bitmap);

	if (reply->header.return_code.error_code!=kFPNoErr) return -1;

	if (bitmap & kFPGetUserInfo_USER_ID) {
		uidgid->uid=ntohl(reply->id1);
		if (bitmap & kFPGetUserInfo_USER_ID)
			uidgid->gid=ntohl(reply->id2);
		return 0;
	}
	if (bitmap & kFPGetUserInfo_PRI_GROUPID) {
		uidgid->gid=ntohl(reply->id1);
	}

	return 0;
}


int afp_mapid_request(struct afp_server * server, unsigned char subfunction,
	unsigned int id, char *name)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t subfunction;
		uint32_t id;
	}  __attribute__((__packed__)) request;

        int ret;

	dsi_setup_header(server,&request.dsi_header,DSI_DSICommand);
	request.command=afpMapID;
	request.subfunction=subfunction;
	request.id=htonl(id);
	ret=dsi_send(server, (char *) &request,sizeof(request),
		1,afpMapID,(void *) name);
	return ret;
}

int afp_mapid_reply(struct afp_server *server, char * buf, unsigned int size, void *other)
{
	struct {
		struct dsi_header header __attribute__((__packed__));
		char * name ;
	}  __attribute__((__packed__)) * reply= (void *) buf;
	char * name = other;
	
	if (size < sizeof (struct dsi_header))
		return -1;

	if (reply->header.return_code.error_code!=kFPNoErr) return -1;

	copy_from_pascal_two(name,&reply->name,255);

	return 0;
}

int afp_mapname_request(struct afp_server * server, unsigned char subfunction,
	char * name, unsigned int * id)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t subfunction;
	}  __attribute__((__packed__)) * request;

	unsigned int len=sizeof(*request)+
		1 + strlen(name);
	char * msg, * nameptr;
        int ret;

        if ((msg=malloc(len)) == NULL)
                return -1;

	memset(msg,0x4b,len);

	nameptr=msg+sizeof(*request);
	request = (void *) msg;

	copy_to_pascal(nameptr,name);

	dsi_setup_header(server,&request->dsi_header,DSI_DSICommand);
	request->command=afpMapName;
	request->subfunction=subfunction;
	ret=dsi_send(server, (char *) request,len,
		1,afpMapName,(void *) id);
	free(msg);
	return ret;
}


int afp_mapname_reply(struct afp_server *server, char * buf, unsigned int size, void *other)
{
	struct {
		struct dsi_header header __attribute__((__packed__));
		uint32_t id ;
	}  __attribute__((__packed__))* reply= (void *) buf;
	unsigned int * id = (void *) other;
	
	if (size < sizeof (struct dsi_header))
		return -1;

	*id=ntohl(reply->id);
	return 0;
}

