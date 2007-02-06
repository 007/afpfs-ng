
/*
 *  files.c
 *
 *  Copyright (C) 2006 Alex deVries
 *
 */


#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <pthread.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <syslog.h>

#include "dsi.h"
#include "afp.h"
#include "utils.h"
#include "afp_protocol.h"
#include "log.h"

/* afp_setfileparms, afp_setdirparms and afpsetfiledirparms are all remarkably
   similiar.  We abstract them to afp-setparms_lowlevel. */

static int afp_setparms_lowlevel(struct afp_volume * volume,
	unsigned int dirid, const char * pathname, unsigned short bitmap,
	struct afp_file_info *fp, char command)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t pad;
		uint16_t volid;
		uint32_t dirid;
		uint16_t bitmap;
	}  __attribute__((__packed__)) *request_packet;
	struct afp_server * server=volume->server;
	unsigned short result_bitmap=0;
	unsigned int len = sizeof(*request_packet) + 
		sizeof_path_header(server)+strlen(pathname) + 
		200 +  /* This is the max size of a data block */
		1; /* In case we're not on an even boundary */

	char * msg, * pathptr, *p;
	int ret;

	if ((msg=malloc(len))==NULL) 
		return -1;
	pathptr= msg+sizeof(*request_packet);
	p = pathptr + sizeof_path_header(server)+strlen(pathname);

	if (((uint64_t) p) & 0x1) p++;	/* Make sure we're on an even boundary */
	bzero(msg,len);
	request_packet = (void *) msg;
	dsi_setup_header(server,&request_packet->dsi_header,DSI_DSICommand);
	request_packet->command=command;
	request_packet->pad=0;
	request_packet->volid=htons(volume->volid);
	request_packet->dirid=htonl(dirid);
	request_packet->bitmap=htons(bitmap);
	copy_path(server,pathptr,pathname,strlen(pathname));
	unixpath_to_afppath(server,pathptr);

	if (bitmap & kFPAttributeBit) {
		/* Todo: 
		The spec says: "The following parameters may be set or cleared:
		Attributes (all attributes except DAlreadyOpen,
		RAlreadyOpen, and CopyProtect)".  This should be checked.
		*/

		*p=htons(fp->attributes);
		result_bitmap|=kFPAttributeBit;
		p+=2;
	}

	if (bitmap & kFPCreateDateBit) {
		unsigned int * date = (void *) p;
		*date = AD_DATE_FROM_UNIX(fp->creation_date);
		result_bitmap|=kFPCreateDateBit;
		p+=4;
	}
	if (bitmap & kFPModDateBit) {
		unsigned int * date = (void *) p;
		*date = AD_DATE_FROM_UNIX(fp->modification_date);
		result_bitmap|=kFPModDateBit;
		p+=4;

	}
	if (bitmap & kFPBackupDateBit) {
		unsigned int * date = (void *) p;
		*date = AD_DATE_FROM_UNIX(fp->backup_date);
		p+=4;
		result_bitmap|=kFPBackupDateBit;
	}
	if (bitmap & kFPFinderInfoBit) {
		result_bitmap|=kFPFinderInfoBit;
		bcopy(fp->finderinfo,p,32);
		p+=32;;
	}
	if (bitmap & kFPUnixPrivsBit) {
		struct afp_unixprivs * t_unixprivs = (void *) p;
		bcopy(&fp->unixprivs,t_unixprivs,sizeof(struct afp_unixprivs));
		/* Convert the different components */
		t_unixprivs->uid=htonl(t_unixprivs->uid);
		t_unixprivs->gid=htonl(t_unixprivs->gid);
		t_unixprivs->permissions=htonl(t_unixprivs->permissions);
		t_unixprivs->ua_permissions=htonl(t_unixprivs->ua_permissions);
		
		p+=sizeof(struct afp_unixprivs);
		result_bitmap|=kFPUnixPrivsBit;
		

	}

	ret=dsi_send(server, (char *) msg,p-msg,1, command,NULL);

	free(msg);

	return ret;
}

int afp_setfileparms(struct afp_volume * volume,
	unsigned int dirid, const char * pathname, unsigned short bitmap,
	struct afp_file_info *fp)
{
	return afp_setparms_lowlevel(volume,dirid,pathname,bitmap,
		fp,afpSetFileParms);
}

int afp_setfiledirparms(struct afp_volume * volume,
	unsigned int dirid, const char * pathname, unsigned short bitmap,
	struct afp_file_info *fp)
{
	return afp_setparms_lowlevel(volume,dirid,pathname,bitmap,
		fp,afpSetFileDirParms);
}

int afp_setdirparms(struct afp_volume * volume,
	unsigned int dirid, const char * pathname, unsigned short bitmap,
	struct afp_file_info *fp)
{
	return afp_setparms_lowlevel(volume,dirid,pathname,bitmap,
		fp,afpSetDirParms);
}

int afp_delete(struct afp_volume * volume,
	unsigned int dirid, char * pathname)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t pad;
		uint16_t volid;
		uint32_t dirid;
	}  __attribute__((__packed__)) *request_packet;
	struct afp_server * server = volume->server;
	unsigned int len = sizeof(*request_packet)+sizeof_path_header(server)+strlen(pathname);
	char * pathptr, *msg;
	int ret=0;
	if ((msg=malloc(len))==NULL) {
		LOG(AFPFSD,LOG_ERR,
			"Out of memory\n");
		return -1;
	};
	pathptr = msg + (sizeof(*request_packet));
	request_packet=(void *) msg;

	dsi_setup_header(server,&request_packet->dsi_header,DSI_DSICommand);
	request_packet->command=afpDelete;
	request_packet->pad=0;
	request_packet->volid=htons(volume->volid);
	request_packet->dirid=htonl(dirid);
	copy_path(server,pathptr,pathname,strlen(pathname));
	unixpath_to_afppath(server,pathptr);

	ret=dsi_send(server, (char *) request_packet,len,1, afpDelete ,NULL);

	free(msg);
	
	return ret;
}


int afp_readext(struct afp_volume * volume, unsigned short forkid, 
		uint64_t offset, 
		uint64_t count,
		struct afp_rx_buffer * rx)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t pad;
		uint16_t forkrefnum;
		uint64_t offset;
		uint64_t reqcount;
	}  __attribute__((__packed__)) readext_packet;

	dsi_setup_header(volume->server,&readext_packet.dsi_header,DSI_DSICommand);
	readext_packet.command=afpReadExt;
	readext_packet.pad=0x0;
	readext_packet.forkrefnum=htons(forkid);
	readext_packet.offset=hton64(offset);
	readext_packet.reqcount=hton64(count);
	return dsi_send(volume->server, (char *) &readext_packet,
		sizeof(readext_packet), 1, afpReadExt, (void *) rx);
}

int afp_readext_reply(struct afp_server *server, char * buf, unsigned int size, struct afp_rx_buffer * rx)
{
	struct dsi_header * header = (void *) buf;
	char * ptr = buf + sizeof(struct dsi_header);
	unsigned int rx_quantum = server->rx_quantum;

	size-=sizeof(struct dsi_header);

	if (size>rx_quantum) {
		LOG(AFPFSD,LOG_ERR,
			"This is definitely weird, I guess I'll just drop %d bytes\n",size-rx_quantum);
		size=rx_quantum;
	}
	bcopy(ptr,rx->data,size);
	rx->size=size;
	rx->errorcode=ntohl(header->return_code.error_code);
	return 0;
}

int afp_getfiledirparms_reply(struct afp_server *server, char * buf, unsigned int size,
	void * other)
{
	struct {
		struct dsi_header header __attribute__((__packed__));
		uint16_t filebitmap;
		uint16_t dirbitmap;
		uint8_t isdir;
		uint8_t pad;
	}  __attribute__((__packed__)) * reply_packet = (void *) buf;
	struct afp_file_info * filecur = other;

	if  (reply_packet->header.return_code.error_code) 
		return reply_packet->header.return_code.error_code;

	if (size<sizeof(*reply_packet)) 
		return -1;

	parse_reply_block(server, 
		buf + (sizeof(*reply_packet)), 
		size,
		reply_packet->isdir, 
		ntohs(reply_packet->filebitmap), 
		ntohs(reply_packet->dirbitmap), 
		filecur);
	filecur->isdir=reply_packet->isdir;

	return 0;
}


int afp_getfiledirparms(struct afp_volume *volume, unsigned int did, unsigned int filebitmap, unsigned int dirbitmap, char * pathname,
	struct afp_file_info *fpp)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t pad;
		uint16_t volid;
		uint32_t did;
		uint16_t file_bitmap;
		uint16_t directory_bitmap;
	}  __attribute__((__packed__)) * getfiledirparms;
	struct afp_server * server = volume->server;
	int ret = 0;
	char *path, * msg;
	unsigned int len;

	if (!pathname) return -1;

	len = sizeof(*getfiledirparms)+sizeof_path_header(server)+strlen(pathname);

	if ((msg = malloc(len))==NULL) 
		return -1;

	path = msg + (sizeof(*getfiledirparms));
	getfiledirparms=(void *) msg;

	dsi_setup_header(server,&getfiledirparms->dsi_header,DSI_DSICommand);
	getfiledirparms->command=afpGetFileDirParms;
	getfiledirparms->pad=0;
	getfiledirparms->volid=htons(volume->volid);
	getfiledirparms->did=htonl(did);
	getfiledirparms->file_bitmap=htons(filebitmap);
	getfiledirparms->directory_bitmap=htons(dirbitmap);
	copy_path(server,path,pathname,strlen(pathname));
	unixpath_to_afppath(server,path);

	ret=dsi_send(server, (char *) getfiledirparms,len,1, afpGetFileDirParms,(void *) fpp);

	free(msg);
	
	return ret;
}

int afp_createfile(struct afp_volume * volume, unsigned char flag, 
	unsigned int did, 
	char * pathname)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t flag;
		uint16_t volid;
		uint32_t did;
	}  __attribute__((__packed__)) * request_packet;
	char * path, *msg;
	struct afp_server * server=volume->server;
	unsigned int len = sizeof(*request_packet)+
		sizeof_path_header(server)+strlen(pathname);
	int ret;

	if ((msg = malloc(len))==NULL) 
		return -1;
	path = msg + (sizeof(*request_packet));
	request_packet =(void *) msg;
	dsi_setup_header(server,&request_packet->dsi_header,DSI_DSICommand);

	request_packet->command=afpCreateFile;
	request_packet->flag=flag;
	request_packet->volid=htons(volume->volid);
	request_packet->did=htonl(did);
	copy_path(server,path,pathname,strlen(pathname));
	unixpath_to_afppath(server,path);

	ret=dsi_send(server, (char *) request_packet,len,1, 
		afpCreateFile,NULL);

	free(msg);
	
	return ret;
}


int afp_writeext(struct afp_volume * volume, unsigned short forkid,
	uint64_t offset, uint64_t reqcount, 
	unsigned int data_size, char * data,uint64_t * written)
{
	struct {
		struct dsi_header dsi_header __attribute__((__packed__));
		uint8_t command;
		uint8_t flag;
		uint16_t forkid;
		uint64_t offset;
		uint64_t reqcount;
	}  __attribute__((__packed__)) * request_packet;
	struct afp_server * server = volume->server;

	unsigned int len = sizeof(*request_packet)+
		reqcount;
	int ret;
	char * dataptr, * msg;
	if ((msg = malloc(len))==NULL) 
		return -1;

	request_packet =(void *) msg;
	dataptr=msg+(sizeof(*request_packet));
	bcopy(data,dataptr,reqcount);
	dsi_setup_header(server,&request_packet->dsi_header,DSI_DSIWrite);
	request_packet->dsi_header.return_code.data_offset=htonl(sizeof(*request_packet)-sizeof(struct dsi_header));
	/* For writing data, set the offset correctly */
	request_packet->command=afpWriteExt;
	request_packet->flag=0;  /* we'll always do this from the start */
	request_packet->forkid=htons(forkid);
	request_packet->offset=hton64(offset);
	request_packet->reqcount=hton64(reqcount);
	ret=dsi_send(server, (char *) request_packet,len,1, 
		afpWriteExt,(void *) written);

	free(msg);
	
	return ret;
}


int afp_writeext_reply(struct afp_server *server, char * buf, unsigned int size,
	uint64_t * written)
{
	struct {
		struct dsi_header header __attribute__((__packed__));
		uint64_t written;
	}  __attribute__((__packed__)) * reply_packet = (void *) buf;

	if (size<sizeof(*reply_packet)) 
		*written=0;
	else 
		*written=ntoh64(reply_packet->written);

	return 0;
}

