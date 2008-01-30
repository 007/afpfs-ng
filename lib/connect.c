/*
 *  connect.c
 *
 *  Copyright (C) 2007 Alex deVries
 *
 */

#include <signal.h>
#include <errno.h>
#include <string.h>

#include "afp.h"
#include "dsi.h"
#include "utils.h"
#include "uams_def.h"
#include "codepage.h"
#include "users.h"
#include "libafpclient.h"
#include "server.h"



static int get_address(void * priv, const char * hostname, unsigned int port,
                struct sockaddr_in * address)
{
	struct hostent *h;
	h= gethostbyname(hostname);
	if (!h) {
		log_for_client(priv,AFPFSD,LOG_ERR,
		"Could not resolve %s.\n",hostname);
		goto error;
	}

	bzero(address,sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_port = htons(port);
	memcpy(&address->sin_addr,h->h_addr,h->h_length);
	return 0;
error:
	return -1;
}



struct afp_server * afp_server_full_connect (void * priv, struct afp_connection_request *req)
{
	int ret;
	struct sockaddr_in address;
	struct afp_server  * s=NULL;
	struct afp_server  * tmpserver;
	char signature[AFP_SIGNATURE_LEN];
	unsigned char versions[SERVER_MAX_VERSIONS];
	unsigned int uams;
	char machine_type[AFP_MACHINETYPE_LEN];
	char server_name[AFP_SERVER_NAME_LEN];
        char server_name_utf8[AFP_SERVER_NAME_UTF8_LEN];
	unsigned int rx_quantum;

	if (get_address(priv,req->url.servername, req->url.port,&address)<0) 
		goto error;

	if ((s=find_server_by_address(&address))) goto have_server;

	if ((tmpserver=afp_server_init(&address))==NULL) goto error;

	if ((ret=afp_server_connect(tmpserver,1))<0) {
		log_for_client(priv,AFPFSD,LOG_ERR,
			"Could not connect, %s\n",strerror(-ret));
		afp_server_remove(tmpserver);
		afp_server_remove(tmpserver);
		goto error;
	}
	loop_disconnect(tmpserver);

	bcopy(&tmpserver->versions,&versions,SERVER_MAX_VERSIONS);
	uams=tmpserver->supported_uams;
	bcopy(&tmpserver->signature,signature,AFP_SIGNATURE_LEN);

	bcopy(&tmpserver->machine_type,machine_type,AFP_MACHINETYPE_LEN);
	bcopy(&tmpserver->server_name,server_name,AFP_SERVER_NAME_LEN);
	bcopy(&tmpserver->server_name_utf8,server_name_utf8,
		AFP_SERVER_NAME_UTF8_LEN);
	rx_quantum=tmpserver->rx_quantum;

	afp_server_remove(tmpserver);

	s=find_server_by_signature(signature);

	if (!s) {
		s = afp_server_init(&address);

		if (afp_server_connect(s,0) !=0) {
			log_for_client(priv,AFPFSD,LOG_ERR,
				"Could not connect to server error: %s\n",
				strerror(errno));
			goto error;
		}

		if ((afp_server_complete_connection(priv,
			s,&address,(unsigned char *) &versions,uams,
			req->url.username, req->url.password, 
			req->url.requested_version, req->uam_mask))==NULL) {
			goto error;
		}
		s->supported_uams=uams;
		bcopy(signature,s->signature,AFP_SIGNATURE_LEN);
		bcopy(server_name,s->server_name,AFP_SERVER_NAME_LEN);
                bcopy(server_name_utf8,s->server_name_utf8,
                        AFP_SERVER_NAME_UTF8_LEN);
		bcopy(machine_type,s->machine_type,AFP_MACHINETYPE_LEN);
		s->rx_quantum=rx_quantum;
	} 
have_server:
	convert_utf8dec_to_utf8pre(s->server_name_utf8,
		strlen(s->server_name_utf8),
		s->server_name_precomposed, AFP_SERVER_NAME_UTF8_LEN);

	/* Figure out if we're using netatalk */
	if (strcmp(s->machine_type,"Netatalk")==0)
		s->server_type=AFPFS_SERVER_TYPE_NETATALK;
	else if (strcmp(s->machine_type,"Airport")==0)
		s->server_type=AFPFS_SERVER_TYPE_AIRPORT;
	else if (strcmp(s->machine_type,"Macintosh")==0)
		s->server_type=AFPFS_SERVER_TYPE_MACINTOSH;
	else 
		s->server_type=AFPFS_SERVER_TYPE_UNKNOWN;

	return s;
error:
	if ((s) && (!something_is_mounted(s))) { /* FIXME */
		afp_server_remove(s);
	}
	return NULL;
}

