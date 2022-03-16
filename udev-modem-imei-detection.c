#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/sysmacros.h>
#include <fcntl.h> // Contains file controls like O_RDWR
#include <dirent.h> // Contains diretor controls
#include <errno.h> // Error integer and strerror() function
#include <termios.h> // Contains POSIX terminal control definitions
#include <unistd.h> // write(), read(), close()
#include <libgen.h>
#include <syslog.h>

#define DIRNAME "/dev/serial/by-imei"

#define MAX_ERROR_MSG 0x1000

#define RX_BUF_SIZE 1024

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof((arr)[0]))

#define IMEI_SIZE 15

enum {
    GSM_OK,
    GSM_CMD_NOT_SUPP,
    GSM_NO_CARRIER,
    GSM_ERROR,
};


//char *SerialPort = "/dev/ttyUSB2";

char *UDEV_ACTION;
char *UDEV_SUBSYSTEM;
char *UDEV_DEVNAME;
char *UDEV_ID_USB_INTERFACE_NUM;

int set_interface_attribs (int, int, int);
void set_blocking (int, int);

static int compile_regex (regex_t *, const char *);
int serial_check_reply(char *);

char *trimwhitespace(char *);

int GSM_get_IMEI(char *, char *);

int serial_read(int, char *, size_t, uint32_t);
int GSM_AT(int, char *, size_t, uint32_t);
int GSM_ATZ(int, char *, size_t, uint32_t);
int GSM_ATE0(int, char *, size_t, uint32_t);
int GSM_ATCGSN(int, char *, size_t, char *, uint32_t);

int MODEM_SYMLINK(char *);
int udev_add_DirCheck(char *);
int udev_add_SymlinkCheck(char *);

void print_help(void);

int main(int argc, char **argv) 
{
    
    char *rx_buf; //, tx_buf[128];
    //uint32_t rx_bytes, rx_timeout = 0, tx_bytes = 0;
    //struct termios tty;

    int ret;

    rx_buf = (char *) malloc(RX_BUF_SIZE+1);

    int errno;

    openlog(basename(argv[0]), LOG_CONS, LOG_KERN | LOG_SYSLOG);

    UDEV_ACTION    = getenv("ACTION");
    UDEV_SUBSYSTEM = getenv("SUBSYSTEM");
    UDEV_DEVNAME   = getenv("DEVNAME");
    UDEV_ID_USB_INTERFACE_NUM = getenv("ID_USB_INTERFACE_NUM");

    if ( !UDEV_ACTION ) {
	syslog(LOG_ERR, "UDEV_ACTION not defined.");
	print_help();
	exit(EXIT_FAILURE);
    }

    if ( !UDEV_SUBSYSTEM ) {
	syslog(LOG_ERR, "UDEV_SUBSYSTEM not defined.");
	print_help();
	exit(EXIT_FAILURE);
    }

    if ( !UDEV_DEVNAME ) {
	syslog(LOG_ERR, "UDEV_DEVNAME not defined.");
	print_help();
	exit(EXIT_FAILURE);
    }

/*
    syslog(LOG_INFO, "ACTION    = \"%s\"\n", UDEV_ACTION);
    syslog(LOG_INFO, "SUBSYSTEM = \"%s\"\n", UDEV_SUBSYSTEM);
    syslog(LOG_INFO, "DEVNAME   = \"%s\"\n", UDEV_DEVNAME);
*/

    /*
     * In case we have to add the device
     * Then we will check the IMEI
     */
    if ( !strcmp(UDEV_ACTION, "add") )
    {
	int fdSerial = open(UDEV_DEVNAME, O_RDWR | O_NOCTTY | O_SYNC);

	// Check for errors
	if (fdSerial < 0) {
	    printf("Error %i opening %s: %s\n", errno, UDEV_DEVNAME, strerror(errno));
	    return EXIT_FAILURE;
	}

	set_interface_attribs(fdSerial, B9600, 0);	// 9600,8,N,1
	set_blocking(fdSerial, 0);			// set non-blocking

	char IMEI[IMEI_SIZE + 1];

	if (GSM_AT(fdSerial, rx_buf, RX_BUF_SIZE, 500))
	{
	    /*
	     * There is no modem for this DEVNAME, then exit and do nothing
	     */
	    ret = EXIT_SUCCESS;
	    goto __end;
	}
	if (GSM_ATZ(fdSerial, rx_buf, RX_BUF_SIZE, 500))
	{
	    syslog(LOG_ERR, "ATZ not replied back with OK for device \"%s\"", UDEV_DEVNAME);
	    ret = EXIT_FAILURE;
	    goto __end;
	}
	if (GSM_ATE0(fdSerial, rx_buf, RX_BUF_SIZE, 500))
	{
	    syslog(LOG_ERR, "ATE0 not replied back with OK for device \"%s\"", UDEV_DEVNAME);
	    ret = EXIT_FAILURE;
	    goto __end;
	}
	/*
	 * In case we can get the IMEI for this modem
	 * we will create a symlink to /dev/serial/by-imei/usb-IMEI-ID_USB_INTERFACE_NUM
	 */
	if (GSM_ATCGSN(fdSerial, rx_buf, RX_BUF_SIZE, IMEI, 500))
	{
	    syslog(LOG_ERR, "AT+CGSN not replied back with OK for device \"%s\"", UDEV_DEVNAME);
	    ret = EXIT_FAILURE;
	    goto __end;
	}

	close(fdSerial);
	ret = EXIT_SUCCESS;
	goto __end;
    }

    if ( !strcmp(UDEV_ACTION, "remove") )
    {
	printf("Action \"%s\" detected.\n", UDEV_ACTION);
	/*
	 * Search through /dev/serial/by-imei and readlink for each symlink.
	 * In case the symlink points to UDEV_DEVNAME, unlink it.
	 */
	int ret;
	struct stat sb;
	char path[PATH_MAX];
	char path_rl[PATH_MAX];

	//printf("NAME_MAX = %u\n", NAME_MAX);
	//printf("PATH_MAX = %u\n", PATH_MAX);

	ret = lstat(DIRNAME, &sb);

	/*
	 * In case DIRNAME is a director
	 * search inside of it.
	 * Otherwise, just exit.
	 */
	if ( (sb.st_mode & S_IFMT) == S_IFDIR )
	{
	    struct dirent *de;  // Pointer for directory entry
	    DIR *dr = opendir(DIRNAME);
	    if (dr == NULL)
	    {
		syslog(LOG_ERR, "Couldn't open dir \"%s\". opendir exit with \"%s\".", DIRNAME, strerror(errno));
	    }
	    else
	    {
		while ((de = readdir(dr)) != NULL)
		{
		    //ret = lstat();
		    memset(path, 0, PATH_MAX);
		    strcpy(path, DIRNAME);
		    strcat(path, "/");
		    strncat(path, de->d_name, NAME_MAX);
		    printf("%s\n", de->d_name);
		    printf("%s\n", path);

		    /* Just in case if "path" is a symlink we will read the pointing path */
		    ret = lstat(path, &sb);
		    if (ret == 0)
		    {
			if ( (sb.st_mode & S_IFMT) == S_IFLNK )
			{
			    memset(path_rl, 0, PATH_MAX);
			    ret = readlink(path, path_rl, PATH_MAX-1);
			    if (ret > 0)
			    {
				//printf("Symlink \"%s\" is pointing to \"%s\"\n", path, path_rl);
				if (!strcmp(path_rl, UDEV_DEVNAME))
				{
				    /*
				     * The pointing path of symlink is the same as UDEV_DEVNAME 
				     * Unlink the symlink.
				     */
				    ret = unlink(path);
				}
			    }
			}
		    }
		}
		closedir(dr);
	    }
	}
	ret = EXIT_SUCCESS;
	goto __end;
    }

    syslog(LOG_ERR, "Unknown udev action '%s' detected. Must be 'add' or 'remove'.", UDEV_ACTION);
    ret = EXIT_FAILURE;

__end:
    closelog();
    return ret;

}


int
set_interface_attribs (int fd, int speed, int parity)
{
    struct termios tty;
    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0)
    {
//        printf("Error %d from tcgetattr", errno);
	printf("Error %i from tcgetattr on %s: %s\n", errno, UDEV_DEVNAME, strerror(errno));
        return -1;
    }

    cfsetospeed (&tty, speed);
    cfsetispeed (&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    // disable IGNBRK for mismatched speed tests; otherwise receive break
    // as \000 chars
    tty.c_iflag &= ~IGNBRK;         // disable break processing
    tty.c_lflag = 0;                // no signaling chars, no echo,
                                        // no canonical processing
    tty.c_oflag = 0;                // no remapping, no delays
    tty.c_cc[VMIN]  = 0;            // read doesn't block
    tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl

    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
                                        // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
    tty.c_cflag |= parity;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr (fd, TCSANOW, &tty) != 0)
    {
//        error_message ("error %d from tcsetattr", errno);
	printf("Error %i from tcsetattr on %s: %s\n", errno, UDEV_DEVNAME, strerror(errno));
        return -1;
    }
    return 0;
}

void
set_blocking (int fd, int should_block)
{
    struct termios tty;
    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0)
    {
//        error_message ("error %d from tggetattr", errno);
	printf("Error %i from tcgetattr on %s: %s\n", errno, UDEV_DEVNAME, strerror(errno));
        return;
    }

    tty.c_cc[VMIN]  = should_block ? 1 : 0;
    tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout

    if (tcsetattr (fd, TCSANOW, &tty) != 0)
//        error_message ("error %d setting term attributes", errno);
	printf("Error %i from tcsetattr on %s: %s\n", errno, UDEV_DEVNAME, strerror(errno));

}

/* Compile the regular expression described by "regex_text" into
   "r". */

static int compile_regex (regex_t * r, const char * regex_text)
{
    int status = regcomp (r, regex_text, REG_EXTENDED|REG_NEWLINE);
    if (status != 0) {
    char error_message[MAX_ERROR_MSG];
    regerror (status, r, error_message, MAX_ERROR_MSG);
        printf ("Regex error compiling '%s': %s\n",
                 regex_text, error_message);
        return 1;
    }
    return 0;
}

int GSM_Check_Reply(char *buffer)
{
    regex_t r;

    int match;

    /* "P" is a pointer into the string which points to the end of the
       previous match. */
    //const char * p = to_match;
    /* "N_matches" is the maximum number of matches allowed. */
    //const int n_matches = 10;
    /* "M" contains the matches found. */
    //regmatch_t m[n_matches];

    //printf("RX buf:\n\"%s\"\n", buffer);
    compile_regex (&r, "OK");
    match = regexec (&r, buffer, 0, NULL, 0);
    
    if (!match) {
        regfree (&r);
	return GSM_OK;
    }

    compile_regex (&r, "COMMAND NOT SUPPORT");
    match = regexec (&r, buffer, 0, NULL, 0);

    if (!match) {
        regfree (&r);
	return GSM_CMD_NOT_SUPP;
    }

    compile_regex (&r, "ERROR");
    match = regexec (&r, buffer, 0, NULL, 0);

    if (!match) {
        regfree (&r);
	return GSM_ERROR;
    }

    compile_regex (&r, "NO CARRIER");
    match = regexec (&r, buffer, 0, NULL, 0);

    if (!match) {
        regfree (&r);
	return GSM_NO_CARRIER;
    }

    return GSM_CMD_NOT_SUPP;
}

int GSM_get_IMEI(char *buffer, char *imei)
{
    regex_t r;

    const char * regex_text;

    int ret;

    //printf("Search for IMEI\n");
    /* "P" is a pointer into the string which points to the end of the
       previous match. */
    //const char * p = to_match;
    /* "N_matches" is the maximum number of matches allowed. */
    //const int n_matches = 1;
    /* "M" contains the matches found. */
    regmatch_t pmatch[1];
    
    regoff_t    off, len;

    regex_text = "([[:digit:]]{15})";
//    regex_text = "([A-Z]{15})";
//    compile_regex (&r, "([[:digit:]]{15}).*");

    int status = regcomp (&r, regex_text, REG_EXTENDED | REG_NEWLINE);
    if (status != 0) {
	char error_message[MAX_ERROR_MSG];
	regerror(status, &r, error_message, MAX_ERROR_MSG);
        syslog(LOG_ERR, "Regex error compiling '%s': %s\n",
                 regex_text, error_message);
        return -1;
    }

    ret = regexec(&r, buffer, ARRAY_SIZE(pmatch), pmatch, 0);
    if ( !ret )
    {
//	printf("IMEI found.\n");

	off = pmatch[0].rm_so;
	len = pmatch[0].rm_eo - pmatch[0].rm_so;

	memset(imei, 0, IMEI_SIZE + 1);
	if (len > IMEI_SIZE)
	{
	    syslog(LOG_ERR, "returned IMEI is longer than maximum allowed characters. IMEI should have exaclty %u characters.", IMEI_SIZE);
	    return -1;
	}
	strncpy(imei, buffer + off, len);

/*
	printf("offset = %jd; length = %jd\n", (intmax_t) off,
                       ((intmax_t)len)-1);
*/

	//printf("ARRAY_SIZE(pmatch) = %lu\n", ARRAY_SIZE(pmatch));
	//printf("REG_NOMATCH %d\n", REG_NOMATCH);
	//syslog(LOG_INFO, "Got the modem IMEI: %s\n", imei);
	regfree (&r);
	return GSM_OK;
    }

    return GSM_CMD_NOT_SUPP;
}

char *trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}


/*
 *
 *
 */
int serial_read(int fd, char *rx_buf, size_t rx_buf_size, uint32_t timeout)
{

    int rx_bytes, rx_bytes_total;
    char rx_buf_local[rx_buf_size];

    uint32_t rx_timeout = 0;

    memset(rx_buf,0,rx_buf_size);
    memset(rx_buf_local,0,rx_buf_size);

    //printf("read return %ld\n", read(fd, NULL, 0));

    /*
    * Wait few microseconds and the exit.
    */
    do
    {
	rx_bytes = read(fd, rx_buf_local, rx_buf_size);
	//printf("rx_bytes = %lu\n", rx_bytes);
	if(rx_bytes > 0) {
	    //printf("Read %u: \"%s\"\n", rx_bytes, rx_buf);
	    strcat(rx_buf, rx_buf_local);
	    rx_bytes_total+=rx_bytes;
	}
	usleep(100);
	rx_timeout++;
	//printf("Timeout  = %lu\n", rx_timeout);
	//printf("read return %ld\n", read(fd, NULL, 0));
	memset(rx_buf_local,0,rx_buf_size);
    } while ( rx_bytes );

    return rx_bytes_total;
}

int GSM_AT(int fd, char *rx_buf, size_t rx_buf_size, uint32_t timeout)
{
    /*
     *  Send AT command to check if there is a modem
     *  We should expect an OK
     */
    char tx_buf[128];
    int rx_bytes, tx_bytes;

    //printf("Send AT\n");
    strcpy(tx_buf, "AT\r\n");
    tx_bytes = write(fd, tx_buf, strlen(tx_buf));
    tcdrain(fd);

    if (tx_bytes == 0)
	return GSM_ERROR;

    rx_bytes = serial_read(fd, rx_buf, RX_BUF_SIZE, 10);

    if (rx_bytes == 0)
	return GSM_ERROR;

    rx_buf = trimwhitespace(rx_buf);
    //printf("Received data: \"%s\"\n", rx_buf);

    //sleep(1);
    if (GSM_Check_Reply(rx_buf) == GSM_OK) {
	//printf("AT is OK\n");
	return GSM_OK;
    }

    return GSM_ERROR;
}

int GSM_ATZ(int fd, char *rx_buf, size_t rx_buf_size, uint32_t timeout)
{
    
    char tx_buf[128];
    int rx_bytes, tx_bytes;

    //printf("Send ATZ\n");
    strcpy(tx_buf, "ATZ\r\n");
    tx_bytes = write(fd, tx_buf, strlen(tx_buf));
    tcdrain(fd);

    if (tx_bytes == 0)
	return GSM_ERROR;

    rx_bytes = serial_read(fd, rx_buf, RX_BUF_SIZE, 10);

    if (rx_bytes == 0)
	return GSM_ERROR;

    rx_buf = trimwhitespace(rx_buf);
    //printf("Received data: \"%s\"\n", rx_buf);

    //sleep(1);
    if (GSM_Check_Reply(rx_buf) == GSM_OK) {
	//printf("ATZ is OK\n");
	return GSM_OK;
    }

    return GSM_ERROR;
}

int GSM_ATE0(int fd, char *rx_buf, size_t rx_buf_size, uint32_t timeout)
{
    
    char tx_buf[128];
    int rx_bytes, tx_bytes;

    //printf("Send ATE0\n");
    strcpy(tx_buf, "ATE0\r\n");
    tx_bytes = write(fd, tx_buf, strlen(tx_buf));
    tcdrain(fd);

    if (tx_bytes == 0)
	return GSM_ERROR;

    rx_bytes = serial_read(fd, rx_buf, RX_BUF_SIZE, 10);

    if (rx_bytes == 0)
	return GSM_ERROR;

    //rx_buf = trimwhitespace(rx_buf);
    //printf("Received data: \"%s\"\n", rx_buf);

    //sleep(1);

    if (GSM_Check_Reply(rx_buf) == GSM_OK) {
	//printf("ATE0 is OK\n");
	return GSM_OK;
    }

    return GSM_ERROR;
}

int GSM_ATCGSN(int fd, char *rx_buf, size_t rx_buf_size, char *IMEI, uint32_t timeout)
{
    
    char tx_buf[128];
    int rx_bytes, tx_bytes; //, ret;

    //printf("Send AT+CGSN\n");
    strcpy(tx_buf, "AT+CGSN\r\n");
    tx_bytes = write(fd, tx_buf, strlen(tx_buf));
    tcdrain(fd);

    if (tx_bytes == 0)
	return GSM_ERROR;

    rx_bytes = serial_read(fd, rx_buf, RX_BUF_SIZE, 10);

    if (rx_bytes == 0)
	return GSM_ERROR;

    rx_buf = trimwhitespace(rx_buf);
    //printf("Received data: \"%s\"\n", rx_buf);

    //sleep(1);

    if (GSM_Check_Reply(rx_buf) == GSM_OK) {
	//printf("AT+CGSN is OK\n");
	if (GSM_get_IMEI(rx_buf, IMEI) == GSM_OK)
	{
	    if ( MODEM_SYMLINK(IMEI) == -1)
		return GSM_ERROR;
	}
	return GSM_OK;
    }

    return GSM_ERROR;
}

int MODEM_SYMLINK(char *imei)
{
    int ret;

    char devName[32];
    char path[1024];
    //char* dirname = "/dev/serial/by-imei/";

    ret = udev_add_DirCheck(DIRNAME);

    /* 
     * In case we couldn't create the director under /dev/serial/
     * we have to stop here.
     */

    if (ret)
	return -1;

    syslog(LOG_INFO, "Modem IMEI is \"%s\"", imei);

    if ( UDEV_ID_USB_INTERFACE_NUM )
    {
	/*
	 * Let's check if we can create a symlink in format usb-IMEI-ID_USB_INTERFACE_NUM
	 */
	memset(devName, 0, 32);
	snprintf(path, 1024, "%s/usb-%s-%s", DIRNAME, imei, UDEV_ID_USB_INTERFACE_NUM);

	/*
	 *  udev_add_SymlinkCheck will return 0 if path doesn't exists, 1 if exists and -1 if there is an error.
	 */
	ret = udev_add_SymlinkCheck(path);
	if (ret == -1)
	    return -1;
	
	if (ret == 0)
	{
	    /*
	     *  Symlink path doesn't exist. So we can create it.
	     */
	    int ret = symlink(UDEV_DEVNAME, path);
	    if (ret == 0)
	    {
		syslog(LOG_INFO, "Create a symlink \"%s\" which points to \"%s\"", path, UDEV_DEVNAME);
		return 0;
	    }
	    else
	    {
		syslog(LOG_ERR, "Couln't create a symlink \"%s\" pointing to \"%s\"", path, UDEV_DEVNAME);
		return -1;
	    }
	}

	/*
	 * The path exists and SymlinkCheck is returning the type of the path
	 */
	/*
	printf("File type: ");
	switch (ret)
	{
	   case S_IFBLK:  printf("block device\n");            break;
	   case S_IFCHR:  printf("character device\n");        break;
	   case S_IFDIR:  printf("directory\n");               break;
	   case S_IFIFO:  printf("FIFO/pipe\n");               break;
	   case S_IFLNK:  printf("symlink\n");                 break;
	   case S_IFREG:  printf("regular file\n");            break;
	   case S_IFSOCK: printf("socket\n");                  break;
	   default:       printf("unknown?\n");                break;
	}
	*/
	/*
	 * Delete the old path and create a new symlink.
	 *
	 */
	ret = unlink(path);
	if (ret != 0)
	{
	    syslog(LOG_ERR, "Unlink returned error \"%s\" when deleting path \"%s\"", strerror(errno), path);
	    return -1;
	}

	/*
	 *  Symlink path doesn't exist. So we can create it.
	 */
	int ret = symlink(UDEV_DEVNAME, path);
	if (ret == 0)
	{
	    syslog(LOG_INFO, "Create a symlink \"%s\" which points to \"%s\"", path, UDEV_DEVNAME);
	    return 0;
	}
	else
	{
	    syslog(LOG_ERR, "Couln't create a symlink \"%s\" pointing to \"%s\"", path, UDEV_DEVNAME);
	    return -1;
	}
    }

    return 0;
}


int udev_add_DirCheck(char *dirname)
{
    
    int ret;
    struct stat sb;

    ret = lstat(dirname, &sb);

//    printf("stat = %i\n", ret);

    if ( ret == 0)
    {
	/*
	 * dirname exist. Let's see what type is it.
	 */
/*
	printf("Path %s exist. Let's see the paramenters.\n", dirname);
	printf("File type: ");
	switch (sb.st_mode & S_IFMT)
	{
	   case S_IFBLK:  printf("block device\n");            break;
	   case S_IFCHR:  printf("character device\n");        break;
	   case S_IFDIR:  printf("directory\n");               break;
	   case S_IFIFO:  printf("FIFO/pipe\n");               break;
	   case S_IFLNK:  printf("symlink\n");                 break;
	   case S_IFREG:  printf("regular file\n");            break;
	   case S_IFSOCK: printf("socket\n");                  break;
	   default:       printf("unknown?\n");                break;
	}
*/
/*
	printf("Mode: %lo (octal)\n",
		((uint64_t)sb.st_mode & ~S_IFMT) );

	printf("Mode: %lu (decimal)\n",
		((uint64_t)sb.st_mode & ~S_IFMT) );

	printf("stroul(\"775\", NULL, 8) = %ld\n", strtoul("755", NULL, 8));
*/
	if ( (sb.st_mode & S_IFMT) == S_IFDIR )
	{
	    /* Is a directory, then check the permissions. We should have 0755. */
	    if ( ((uint64_t)sb.st_mode & ~S_IFMT) == strtoul("755", NULL, 8) )
	    {
		//syslog(LOG_INFO, "Director %s has correct permisions.\n", dirname);
		return 0;
	    }
	    else
	    {
		/* Let's try to change the permissions to 0755. */
		int newPerm = (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		ret = chmod(dirname, newPerm);
		//printf("New permission is %o\n",  newPerm);
		if (ret == 0)
		{
		    //printf("Dir %s has new permission set to 0755.\n", dirname);
		    return 0;
		}
		else
		{
		    syslog(LOG_ERR, "Couldn't set permission to 0755 for path \"%s\"", dirname);
		    return -1;
		}
	    }
	}
	else
	{
	    /*
	     * In case this dirname is not a DIR, then we have to delete all and create it.
	     * We shouldn't expect a file under /dev/serial/ with name by-imei.
	     */
	    //syslog(LOG_INFO, "Path %s is not a directory.\n", dirname);
	    //syslog(LOG_INFO, "Delete %s and create it as directory.\n", dirname);
	    ret = remove(dirname);
	    if (ret == 0)
	    {
		goto __createDir;
	    }
	}
	return 0;
    }
    else
    {
	if (errno == ENOENT) {
	    /*
	     * Path doesnt' exist. Let's create it
	     */
	    goto __createDir;
	}
	syslog(LOG_ERR, "lstat for path \"%s\" returns error \"%s\".\n", dirname, strerror(errno));
	return -1;
    }
    /* In case the path doesn't exist, then is ok. We can crete the dir. */

__createDir:
    //syslog(LOG_INFO, "path %s doesn't exist. Creating directory.", dirname);
    ret = mkdir(dirname,0755);
    if (ret == 0)
    {
	syslog(LOG_INFO, "Create path %s.", dirname);
	return 0;
    }
    else
    {
	syslog(LOG_ERR, "Creating path %s.", dirname);
	return -1;
    }
}


int udev_add_SymlinkCheck(char *SymLinkPath)
{
    int ret;
    struct stat sb;

    ret = lstat(SymLinkPath, &sb);

    //printf("lstat = %i\n", ret);

    if ( ret == 0)
    {
/*
	printf("Path %s exist. Let's see its the paramenters.\n", SymLinkPath);
	printf("File type: ");
	switch (sb.st_mode & S_IFMT)
	{
	   case S_IFBLK:  printf("block device\n");            break;
	   case S_IFCHR:  printf("character device\n");        break;
	   case S_IFDIR:  printf("directory\n");               break;
	   case S_IFIFO:  printf("FIFO/pipe\n");               break;
	   case S_IFLNK:  printf("symlink\n");                 break;
	   case S_IFREG:  printf("regular file\n");            break;
	   case S_IFSOCK: printf("socket\n");                  break;
	   default:       printf("unknown?\n");                break;
	}
*/
	return (int)(sb.st_mode & S_IFMT);
    }
    else
    {
	if (errno == ENOENT) {
	    /*
	     * Path doesnt' exist. Let's create it
	     */
	    //syslog(LOG_INFO, "Path \"%s\" doesn't exists.", SymLinkPath);
	    goto __symlink_not_exists;
	}
	syslog(LOG_ERR, "lstat returned error \"%s\" when checking path \"%s\"", strerror(errno), SymLinkPath);
	return -1;
    }

__symlink_not_exists:
    return 0;
}


void print_help(void) {
    printf("Usage: this software should be run from udev daemon. Check /etc/udev/rules.d/85-usb_modem-by-imein.rules file.\n");
}
