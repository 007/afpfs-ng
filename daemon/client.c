#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <sys/un.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <grp.h>

#include "config.h"
#include <afp.h>
#include "afpfsd.h"
#include "uams_def.h"
#include "map_def.h"
#include "libafpclient.h"
#include "afpsl.h"

#define DEFAULT_MOUNT_FLAGS (VOLUME_EXTRA_FLAGS_SHOW_APPLEDOUBLE|\
	VOLUME_EXTRA_FLAGS_NO_LOCKING | VOLUME_EXTRA_FLAGS_IGNORE_UNIXPRIVS)

static char * thisbin;

static void usage(void) 
{
	printf(
"afp_client [command] [options]\n"
"    mount [mountopts] <server>:<volume> <mountpoint>\n"
"         mount options:\n"
"         -u, --user <username> : log in as user <username>\n"
"         -p, --pass <password> : use <password>\n"
"                           If password is '-', password will be hidden\n"
"         -o, --port <portnum> : connect using <portnum> instead of 548\n"
"         -V, --volumepassword <volpass> : use this volume password\n"
"         -v, --afpversion <afpversion> set the AFP version, eg. 3.1\n"
"         -a, --uam <uam> : use this authentication method, one of:\n"
"               \"No User Authent\", \"Cleartxt Passwrd\", \n"
"               \"Randnum Exchange\", \"2-Way Randnum Exchange\", \n"
"               \"DHCAST128\", \"Client Krb v2\", \"DHX2\" \n\n"
"         -m, --map <mapname> : use this uid/gid mapping method, one of:\n"
"               \"Common user directory\", \"Login ids\"\n"
"    status: get status of the AFP daemon\n\n"
"    unmount <mountpoint> : unmount\n\n"
"    suspend <servername> : terminates the connection to the server, but\n"
"                           maintains the mount.  For laptop suspend/resume\n"
"    resume  <servername> : resumes the server connection \n\n"
"    exit                 : unmounts all volumes and exits afpfsd\n"
);
 }

static int do_exit(int argc,char **argv)
{
	struct afpfsd_connect conn;

	if (afp_sl_setup(&conn)) {
		printf("Could not connect to afpfsd.\n");
		return -1;
	}
	afp_sl_exit(&conn);

	return 0;

}

static int do_status(int argc, char ** argv) 
{
	char volumename[AFP_VOLUME_NAME_LEN];
	char servername[AFP_SERVER_NAME_LEN];
        int c;
        int option_index=0;
	int optnum;
	struct afpfsd_connect conn;
#define STATUS_TEXT_LEN 1024
	unsigned int len=STATUS_TEXT_LEN;
	char text[STATUS_TEXT_LEN];

	struct option long_options[] = {
		{"volume",1,0,'v'},
		{"server",1,0,'s'},
		{0,0,0,0},
	};

	if (afp_sl_setup(&conn)) {
		printf("Could not setup connection to afpfsd\n");
		return -1;
	}

	memset(volumename,0,AFP_VOLUME_NAME_LEN);
	memset(servername,0,AFP_SERVER_NAME_LEN);

        while(1) {
		optnum++;
                c = getopt_long(argc,argv,"v:s:",
                        long_options,&option_index);
                if (c==-1) break;
                switch(c) {
                case 'v':
                        snprintf(volumename,AFP_VOLUME_NAME_LEN,
				"%s",optarg);
                        break;
                }
        }
	afp_sl_status(&conn,volumename,servername,text,&len);

	printf(text);

        return 0;
}

static int do_resume(int argc, char ** argv) 
{
	struct afpfsd_connect conn;
	if (argc<3) {
		usage();
		return -1;
	}
	if (afp_sl_setup(&conn)) {
		printf("Could not setup connection to afpfsd\n");
		return -1;
	}

	afp_sl_resume(&conn, argv[2]);

	return 0;
}

static int do_suspend(int argc, char ** argv) 
{
	struct afpfsd_connect conn;
	if (argc<3) {
		usage();
		return -1;
	}
	if (afp_sl_setup(&conn)){
		printf("Could not setup connection to afpfsd\n");
		return -1;
	}

	afp_sl_suspend(&conn, argv[2]);
	return 0;
}

static int do_unmount(int argc, char ** argv) 
{
	struct afpfsd_connect conn;
	if (argc<2) {
		usage();
		return -1;
	}

	if (afp_sl_setup(&conn)){
		printf("Could not setup connection to afpfsd\n");
		return -1;
	}

/* FIXME: deal with mntpoint */
	afp_sl_unmount(&conn,argv[2]);

	return 0;
}

static int do_mount(int argc, char ** argv) 
{
        int c;
        int option_index=0;
	int optnum;
	struct afp_url url;
	unsigned int uam_mask=default_uams_mask();
	int map=AFP_MAPPING_UNKNOWN;
	char mountpoint[PATH_MAX];
	unsigned int volume_options;
	struct afpfsd_connect conn;
	serverid_t serverid;

	struct option long_options[] = {
		{"afpversion",1,0,'v'},
		{"volumepassword",1,0,'V'},
		{"user",1,0,'u'},
		{"pass",1,0,'p'},
		{"port",1,0,'o'},
		{"uam",1,0,'a'},
		{"map",1,0,'m'},
		{0,0,0,0},
	};
	if (argc<4) {
		usage();
		return -1;
	}
	if (afp_sl_setup(&conn)) {
		printf("Could not setup connection to afpfsd\n");
		return -1;
	}

	afp_default_url(&url);

        while(1) {
		optnum++;
                c = getopt_long(argc,argv,"a:u:m:o:p:v:V:",
                        long_options,&option_index);
                if (c==-1) break;
                switch(c) {
                case 'a':
			if (strcmp(optarg,"guest")==0) 
				uam_mask=UAM_NOUSERAUTHENT;
			else
				uam_mask=uam_string_to_bitmap(optarg);
                        break;
                case 'm':
			map=map_string_to_num(optarg);
                        break;
                case 'u':
                        snprintf(url.username,AFP_MAX_USERNAME_LEN,"%s",optarg);
                        break;
                case 'o':
                        url.port=strtol(optarg,NULL,10);
                        break;
                case 'p':
                        snprintf(url.password,AFP_MAX_PASSWORD_LEN,"%s",optarg);
                        break;
                case 'V':
                        snprintf(url.volpassword,9,"%s",optarg);
                        break;
                case 'v':
                        url.requested_version=strtol(optarg,NULL,10);
                        break;
                }
        }

	if (strcmp(url.password, "-") == 0) {
		char *p = getpass("AFP Password: ");
		if (p)
			snprintf(url.password,AFP_MAX_PASSWORD_LEN,"%s",p);
	}
	if (strcmp(url.volpassword, "-") == 0) {
		char *p = getpass("Password for volume: ");
		if (p)
			snprintf(url.volpassword,9,"%s",p);
	}

	optnum=optind+1;
	if (optnum>=argc) {
		printf("No volume or mount point specified\n");
		return -1;
	}
	if (sscanf(argv[optnum++],"%[^':']:%[^':']",
		url.servername,url.volumename)!=2) {
		printf("Incorrect server:volume specification\n");
		return -1;
	}
	if (uam_mask==0) {
		printf("Unknown UAM\n");
		return -1;
	}

	if (optnum>=argc) {
		printf("No mount point specified\n");
		return -1;
	}

	snprintf(mountpoint,255,"%s",argv[optnum++]);


	if (afp_sl_connect(&conn,&url,uam_mask,&serverid)!=
		AFP_SERVER_RESULT_OKAY) {
		printf("Cound not connect, so not proceeding with mount.\n");
		return -1;
	}

	afp_sl_mount(&conn,&url,mountpoint,map, DEFAULT_MOUNT_FLAGS);

        return 0;
}

static void mount_afp_usage(void)
{
	printf("Usage:\n     mount_afp [-o volpass=password] <afp url> <mountpoint>\n");
}

static int handle_mount_afp(int argc, char * argv[])
{
	char * urlstring, * mountpoint;
	char * volpass = NULL;
	int readonly=0;
	struct afp_url url;
	unsigned int volume_options = 0, map=0, uam_mask = default_uams_mask();
	struct afpfsd_connect conn;
	unsigned int uid = geteuid();
	unsigned int gid;
	int ret;
	static int changeuid=0;
	static int changegid=0;
	serverid_t serverid;

	if (argc<2) {
		mount_afp_usage();
		return -1;
	}
	if (strncmp(argv[1],"-o",2)==0) {
		char * p = argv[2], *q;
		char command[256];
		struct passwd * passwd;
		struct group * group;
		
		do {
			memset(command,0,256);
			
			if ((q=strchr(p,','))) 
				strncpy(command,p,(q-p));
			else 
				strcpy(command,p);

			if (strncmp(command,"volpass=",8)==0) {
				p+=8;
				volpass=p;
			} else if (strncmp(command,"user=",5)==0) {
				p=command+5;
				if ((passwd=getpwnam(p))==NULL) {
					printf("Unknown user %s\n",p);
					return -1;
				}
				uid=passwd->pw_uid;
				if (geteuid()!=uid)
					changeuid=1;
			} else if (strncmp(command,"group=",6)==0) {
				p=command+6;
				if ((group=getgrnam(p))==NULL) {
					printf("Unknown group %s\n",p);
					return -1;
				}
				gid=group->gr_gid;
				changegid=1;
			} else if (strcmp(command,"rw")==0) {
				/* Don't do anything */
			} else if (strcmp(command,"ro")==0) {
				readonly=1;
			} else {
				printf("Unknown option %s, skipping\n",command);
			}
			if (q) p=q+1;
			else p=NULL;

		} while (p);

		urlstring=argv[3];
		mountpoint=argv[4];
	} else {
		urlstring=argv[1];
		mountpoint=argv[2];
	}

	afp_default_url(&url);

	volume_options|=DEFAULT_MOUNT_FLAGS;
	if (readonly) volume_options |= VOLUME_EXTRA_FLAGS_READONLY;

	map=AFP_MAPPING_UNKNOWN;

	if (afp_parse_url(&url,urlstring,0) !=0) 
	{
		printf("Could not parse URL\n");
		return -1;
	}
	if (strcmp(url.password,"-")==0) {
		char *p = getpass("AFP Password: ");
		if (p)
			snprintf(url.password,AFP_MAX_PASSWORD_LEN,"%s",p);
	}

	if (volpass && (strcmp(volpass,"-")==0)) {
		volpass  = getpass("Password for volume: ");
	}
	if (volpass)
		snprintf(url.volpassword,9,"%s",volpass);

	if (changeuid || changegid)
		ret=afp_sl_setup_diffuser(&conn,uid,gid);
	else 
		ret=afp_sl_setup(&conn);

	if (ret) {
		printf("Could not connect to afpfsd\n");
		return -1;
	}

	if (afp_sl_connect(&conn,&url,uam_mask,&serverid)!=
		AFP_SERVER_RESULT_OKAY) {
		printf("Could not connect, so not proceeding with mount.\n");
		return -1;
	}

	afp_sl_mount(&conn,&url,mountpoint,map,volume_options);

        return 0;
}

static int handle_afp_client(int argc, char * argv[]) 
{

	if (argc<2) {
		usage();
		return -1;
	}
	if (strncmp(argv[1],"mount",5)==0) {
		return do_mount(argc,argv);
	} else if (strncmp(argv[1],"resume",6)==0) {
		return do_resume(argc,argv);
	} else if (strncmp(argv[1],"suspend",7)==0) {
		return do_suspend(argc,argv);

	} else if (strncmp(argv[1],"status",6)==0) {
		return do_status(argc,argv);

	} else if (strncmp(argv[1],"unmount",7)==0) {
		return do_unmount(argc,argv);
	} else if (strncmp(argv[1],"exit",4)==0) {
		return do_exit(argc,argv);

	} else {
		usage();
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[]) 
{
	int ret;
	thisbin=argv[0];

	if (strstr(argv[0],"mount_afp")) {
		if (handle_mount_afp(argc,argv)<0)
		return -1;
	}
	ret=handle_afp_client(argc,argv);

	return ret;
}
