/*
 * Multicomp MP7100  / OWON SP3051 / 3101 / 6031 / 5051
 *
 * V0.1 - March 20, 2020, Peak COVID19
 * 
 *
 * Written by Paul L Daniels (pldaniels@gmail.com)
 *
 */

#include <SDL.h>
#include <SDL_ttf.h>

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define FL __FILE__,__LINE__

/*
 * Should be defined in the Makefile to pass to the compiler from
 * the github build revision
 *
 */
#ifndef BUILD_VER 
#define BUILD_VER 000
#endif

#ifndef BUILD_DATE
#define BUILD_DATE " "
#endif

#define SSIZE 1024

#define INTERFRAME_SLEEP	200000 // 0.2 seconds

#define DATA_FRAME_SIZE 12
#define ee ""
#define uu "\u00B5"
#define kk "k"
#define MM "M"
#define mm "m"
#define nn "n"
#define pp "p"
#define dd "\u00B0"
#define oo "\u03A9"

#define MMFLAG_AUTORANGE	0b01000000

#define CMODE_USB 1
#define CMODE_SERIAL 2
#define CMODE_NONE 0

#define MEAS_VOLT "MEAS:VOLT?"
#define MEAS_CURR "MEAS:CURR?"

char SEPARATOR_DP[] = ".";

struct serial_params_s {
	char *device;
	int fd, n;
	int cnt, size, s_cnt;
	struct termios oldtp, newtp;
};


struct glb {
	uint8_t debug;
	uint8_t quiet;
	uint16_t flags;
	uint16_t error_flag;
	char *output_file;
	char *device;

	char meas_volt[20];
	char meas_curr[20];

	int usb_fhandle;

	int comms_mode;
	char *com_address;
	char *serial_parameters_string; // this is the raw from the command line
	struct serial_params_s serial_params; // this is the decoded version


	int interval;
	int font_size;
	int window_width, window_height;
	int wx_forced, wy_forced;
	SDL_Color font_color_volts, font_color_amps, background_color;
};

/*
 * A whole bunch of globals, because I need
 * them accessible in the Windows handler
 *
 * So many of these I'd like to try get away from being
 * a global.
 *
 */
struct glb *glbs;

/*
 * Test to see if a file exists
 *
 * Does not test anything else such as read/write status
 *
 */
bool fileExists(const char *filename) {
	struct stat buf;
	return (stat(filename, &buf) == 0);
}


char digit( unsigned char dg ) {

	int d;
	char g;

	switch (dg) {
		case 0x30: g = '0'; d = 0; break;
		case 0x31: g = '1'; d = 1; break;
		case 0x32: g = '2'; d = 2; break;
		case 0x33: g = '3'; d = 3; break;
		case 0x34: g = '4'; d = 4; break;
		case 0x35: g = '5'; d = 5; break;
		case 0x36: g = '6'; d = 6; break;
		case 0x37: g = '7'; d = 7; break;
		case 0x38: g = '8'; d = 8; break;
		case 0x39: g = '9'; d = 9; break;
		case 0x3E: g = 'L'; d = 0; break;
		case 0x3F: g = ' '; d = 0; break;
		default: g = ' ';
	}

	return g;
}

/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220248
  Function Name	: init
  Returns Type	: int
  ----Parameter List
  1. struct glb *g ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int init(struct glb *g) {
	g->debug = 0;
	g->quiet = 0;
	g->flags = 0;
	g->error_flag = 0;
	g->output_file = NULL;
	g->interval = 100000;
	g->device = NULL;
	g->comms_mode = CMODE_NONE;

	g->serial_parameters_string = NULL;

	g->font_size = 60;
	g->window_width = 400;
	g->window_height = 100;
	g->wx_forced = 0;
	g->wy_forced = 0;

	g->font_color_volts =  { 10, 200, 10 };
	g->font_color_amps =  { 200, 200, 10 };
	g->background_color = { 0, 0, 0 };

	return 0;
}

void show_help(void) {
	fprintf(stdout,"MP7100 Power supply display\r\n"
			"By Paul L Daniels / pldaniels@gmail.com\r\n"
			"Build %d / %s\r\n"
			"\r\n"
			" [-p <usbtmc path, ie /dev/usbtmc2>] \r\n"
			"\r\n"
			"\t-h: This help\r\n"
			"\t-d: debug enabled\r\n"
			"\t-q: quiet output\r\n"
			"\t-v: show version\r\n"
			"\t-z <font size in pt>\r\n"
			"\t-cv <volts colour, a0a0ff>\r\n"
			"\t-ca <amps colour, ffffa0>\r\n"
			"\t-cb <background colour, 101010>\r\n"
			"\t-t <interval> (sleep delay between samples, default 100,000us)\r\n"
			"\t-p <comport>: Set the com port for the meter, eg: -p /dev/ttyUSB0\r\n"
			"\t-s <[9600|4800|2400|1200]:[7|8][o|e|n][1|2]>, eg: -s 2400:8n1\r\n"
			"\r\n"
			"\texample: MP7100 -p /dev/usbtmc2\r\n"
			, BUILD_VER
			, BUILD_DATE 
			);
} 


/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220258
  Function Name	: parse_parameters
  Returns Type	: int
  ----Parameter List
  1. struct glb *g,
  2.  int argc,
  3.  char **argv ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int parse_parameters(struct glb *g, int argc, char **argv ) {
	int i;

	if (argc == 1) {
		show_help();
		exit(1);
	}

	for (i = 0; i < argc; i++) {
		if (argv[i][0] == '-') {
			/* parameter */
			switch (argv[i][1]) {

				case 'h':
					show_help();
					exit(1);
					break;

				case 'z':
					i++;
					if (i < argc) {
						g->font_size = atoi(argv[i]);
					} else {
						fprintf(stdout,"Insufficient parameters; -z <font size pts>\n");
						exit(1);
					}
					break;

				case 'p':
					/*
					 * com port can be multiple things in linux
					 * such as /dev/ttySx or /dev/ttyUSBxx
					 */
					i++;
					if (i < argc) {
						g->device = argv[i];
					} else {
						fprintf(stdout,"Insufficient parameters; -p <usb TMC port ie, /dev/usbtmc2>\n");
						exit(1);
					}
					break;

				case 'o':
					/* 
					 * output file where this program will put the text
					 * line containing the information FlexBV will want 
					 *
					 */
					i++;
					if (i < argc) {
						g->output_file = argv[i];
					} else {
						fprintf(stdout,"Insufficient parameters; -o <output file>\n");
						exit(1);
					}
					break;

				case 'd': g->debug = 1; break;

				case 'q': g->quiet = 1; break;

				case 'v':
							 fprintf(stdout,"Build %d\r\n", BUILD_VER);
							 exit(0);
							 break;

				case 't':
							 i++;
							 g->interval = atoi(argv[i]);
							 break;

				case 'c':
							 int a,b,c;
							 if (argv[i][2] == 'v') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &g->font_color_volts.r
										 , &g->font_color_volts.g
										 , &g->font_color_volts.b
										 );

							 } else if (argv[i][2] == 'a') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &g->font_color_amps.r
										 , &g->font_color_amps.g
										 , &g->font_color_amps.b
										 );

							 } else if (argv[i][2] == 'b') {
								 i++;
								 sscanf(argv[i], "%2hhx%2hhx%2hhx"
										 , &(g->background_color.r)
										 , &(g->background_color.g)
										 , &(g->background_color.b)
										 );

							 }
							 break;

				case 'w':
							 if (argv[i][2] == 'x') {
								 i++;
								 g->wx_forced = atoi(argv[i]);
							 } else if (argv[i][2] == 'y') {
								 i++;
								 g->wy_forced = atoi(argv[i]);
							 }
							 break;

				case 's':
							 i++;
							 g->serial_parameters_string = argv[i];
							 break;

				default: break;
			} // switch
		}
	}

	return 0;
}



/*
 * Default parameters are 2400:8n1, given that the multimeter
 * is shipped like this and cannot be changed then we shouldn't
 * have to worry about needing to make changes, but we'll probably
 * add that for future changes.
 *
 */
void open_port( struct glb *g ) {
#ifdef __linux__
	struct serial_params_s *s = &(g->serial_params);
	char *p = g->serial_parameters_string;
	char default_params[] = "9600:8:n";
	int r; 

	if (!p) p = default_params;

	fprintf(stdout,"Attempting to open '%s'\n", s->device);
	s->fd = open( s->device, O_RDWR | O_NOCTTY | O_NDELAY );
	if (s->fd <0) {
		perror( s->device );
	}

	fcntl(s->fd,F_SETFL,0);
	tcgetattr(s->fd,&(s->oldtp)); // save current serial port settings 
	tcgetattr(s->fd,&(s->newtp)); // save current serial port settings in to what will be our new settings
	cfmakeraw(&(s->newtp));
	
		s->newtp.c_cflag = CS8 |  CLOCAL | CREAD ; 

      if (strncmp(p, "115200:", 7) == 0) s->newtp.c_cflag |= B115200; 
		else if (strncmp(p, "57600:", 6) == 0) s->newtp.c_cflag |= B57600;
		else if (strncmp(p, "38400:", 6) == 0) s->newtp.c_cflag |= B38400;
		else if (strncmp(p, "19200:", 6) == 0) s->newtp.c_cflag |= B19200;
		else if (strncmp(p, "9600:", 5) == 0) s->newtp.c_cflag |= B9600;
		else if (strncmp(p, "4800:", 5) == 0) s->newtp.c_cflag |= B4800;
      else if (strncmp(p, "2400:", 5) == 0) s->newtp.c_cflag |= B2400; //
      else {
         fprintf(stdout,"Invalid serial speed\r\n");
         exit(1);
      }

		p = strchr(p,':');
		if (p) {
			p++;
			// we don't do anything with the data bits because
			// this PSU only accepts 8, so just jump to the next
			// field
			p = strchr(p,':');
			if (!p) {
				fprintf(stdout,"Invalid serial format string\n");
				exit(1);
			}
      }

      p++;
      if (*p == 'o') s->newtp.c_cflag |= PARODD;
      else if (*p == 'e') s->newtp.c_cflag |= PARENB;
      else if (*p == 'n') s->newtp.c_cflag &= ~(PARODD|PARENB);
      else {
         fprintf(stdout,"Invalid serial parity type '%c'\r\n", *p);
         exit(1);
      }

	s->newtp.c_iflag &= ~(IXON | IXOFF | IXANY );

	r = tcsetattr(s->fd, TCSANOW, &(s->newtp));
	if (r) {
		fprintf(stderr,"%s:%d: Error setting terminal (%s)\n", FL, strerror(errno));
		exit(1);
	}

	fprintf(stdout,"Serial port opened, FD[%d]\n", s->fd);
#endif
}

uint8_t a2h( uint8_t a ) {
	a -= 0x30;
	if (a < 10) return a;
	a -= 7;
	return a;
}

int data_read( glb *g, char *b, ssize_t s ) {
	ssize_t sz;
	if (g->comms_mode == CMODE_USB) {
		/*
		 * usb mode read
		 *
		 */
		int bp = 0;
		do {
			sz = read(g->usb_fhandle, b+bp, s -1 -bp);
			b[bp+sz] = '\0';

			if (sz == -1) {
				g->error_flag = true;
				fprintf(stdout,"Error reading data: %s\n", strerror(errno));
				snprintf(b, s, "NODATA");
				break;
			}

			bp += sz;
			if (sz == 0) break;
			if (bp >= s) break;
			usleep(1000);
		} while (sz);
		b[bp] = '\0';
		if ((bp > 0) && b[bp-1] == '\n') b[bp -1] = '\0';

	} else {
		/*
		 * serial mode read
		 *
		 */
		int bp = 0;
		ssize_t bytes_read = 0;

		do {
			char temp_char;
			bytes_read = read(g->serial_params.fd, &temp_char, 1);
			if (bytes_read) {
				b[bp] = temp_char;
				if (b[bp] == '\n') break;
				bp++;
			}
		} while (bytes_read && bp < s);
		b[bp] = '\0';
	}
	return sz;
}

int data_write( glb *g, char *d, ssize_t s ) { 
		ssize_t sz;

	if (g->comms_mode == CMODE_USB) {
		/*
		 * usb mode write
		 *
		 */
		sz = write(g->usb_fhandle, d, s); //"MEAS:VOLT?", sizeof("MEAS:VOLT?"));
		if (sz < 0) {
			g->error_flag = true;
			fprintf(stdout,"Error sending USB data: %s\n", strerror(errno));
			snprintf(d,s-1,"NODATA");
			//exit(1);
		}
	} else {
		/*
		 * serial mode write
		 *
		 */
		sz = write(g->serial_params.fd, d, s); 
		if (sz < 0) {
			g->error_flag = true;
			fprintf(stdout,"Error sending serial data: %s\n", strerror(errno));
			snprintf(d,s-1,"NODATA");
		}
	}

	return sz;
}

#ifdef __WIN32
void parse_serial_parameters( struct glb *g ) {
      char *p = g->serial_parameters_string;
      if (strncmp(p, "9600:", 5) == 0) dcbSerialParams.BaudRate = CBR_9600; // BaudRate = 9600
      else if (strncmp(p, "4800:", 5) == 0) dcbSerialParams.BaudRate = CBR_4800; // BaudRate = 4800
      else if (strncmp(p, "2400:", 5) == 0) dcbSerialParams.BaudRate = CBR_2400; // BaudRate = 2400
      else if (strncmp(p, "1200:", 5) == 0) dcbSerialParams.BaudRate = CBR_1200; // BaudRate = 1200
      else {
         wprintf(L"Invalid serial speed\r\n");
         CloseHandle(hComm);
         exit(1);
      }

      p = &(pg->serial_params[5]);
      if (*p == '7') dcbSerialParams.ByteSize = 7;
      else if (*p == '8') dcbSerialParams.ByteSize = 8;
      else {
         wprintf(L"Invalid serial byte size '%c'\r\n", *p);
         CloseHandle(hComm);
         exit(1);
      }

      p++;
      if (*p == 'o') dcbSerialParams.Parity = ODDPARITY;
      else if (*p == 'e') dcbSerialParams.Parity = EVENPARITY;
      else if (*p == 'n') dcbSerialParams.Parity = NOPARITY;
      else {
         wprintf(L"Invalid serial parity type '%c'\r\n", *p);
         CloseHandle(hComm);
         exit(1);
      }
}
#endif

/*-----------------------------------------------------------------\
  Date Code:	: 20180127-220307
  Function Name	: main
  Returns Type	: int
  ----Parameter List
  1. int argc,
  2.  char **argv ,
  ------------------
  Exit Codes	:
  Side Effects	:
  --------------------------------------------------------------------
Comments:

--------------------------------------------------------------------
Changes:

\------------------------------------------------------------------*/
int main ( int argc, char **argv ) {

	SDL_Event event;
	SDL_Surface *surface, *surface_2;
	SDL_Texture *texture, *texture_2;

	char linetmp[SSIZE]; // temporary string for building main line of text

	struct glb g;        // Global structure for passing variables around
	int i = 0;           // Generic counter
	char temp_char;        // Temporary character
	char tfn[4096];
	bool quit = false;

	glbs = &g;

	/*
	 * Initialise the global structure
	 */
	init(&g);

	/*
	 * Parse our command line parameters
	 */
	parse_parameters(&g, argc, argv);
	if (g.device == NULL) {
		fprintf(stdout,"Require valid device (ie, -p /dev/usbtmc2 )\nExiting\n");
		exit(1);
	}

	fprintf(stdout,"START\n");

	if (strstr(g.device,"usbtmc")) {
		fprintf(stdout,"\nUsing USB mode\n\n");
		fflush(stdout);
		g.comms_mode = CMODE_USB;
		snprintf(g.meas_volt,sizeof(g.meas_volt),"%s", MEAS_VOLT);
		snprintf(g.meas_curr,sizeof(g.meas_curr),"%s", MEAS_CURR);
	} else {
		fprintf(stdout,"\nUsing SERIAL mode\n\n");
		fflush(stdout);
		g.comms_mode = CMODE_SERIAL;
		g.serial_params.device = g.device;
		snprintf(g.meas_volt,sizeof(g.meas_volt),"%s\n", MEAS_VOLT);
		snprintf(g.meas_curr,sizeof(g.meas_curr),"%s\n", MEAS_CURR);
	}

	/* 
	 * check paramters
	 *
	 */
	if (g.font_size < 10) g.font_size = 10;
	if (g.font_size > 200) g.font_size = 200;

	if (g.output_file) snprintf(tfn,sizeof(tfn),"%s.tmp",g.output_file);


	if ( g.comms_mode == CMODE_SERIAL ) {
		/* 
		 * handle the serial port
		 *
		 */
		open_port( &g );

	} else {
		/*
		 * Handle the USB port
		 *
		 */

		g.usb_fhandle = open( g.device, O_RDWR );
		if (g.usb_fhandle == -1) {
			fprintf(stdout, "Error opening device [%s] : %s\n", g.device, strerror(errno));
			exit (1);
		}
	}

	/*
	 * Setup SDL2 and fonts
	 *
	 */

	SDL_Init(SDL_INIT_VIDEO);
	TTF_Init();
	TTF_Font *font = TTF_OpenFont("RobotoMono-Regular.ttf", g.font_size);
	TTF_Font *font_small = TTF_OpenFont("RobotoMono-Regular.ttf", g.font_size/4);

	/*
	 * Get the required window size.
	 *
	 * Parameters passed can override the font self-detect sizing
	 *
	 */
	TTF_SizeText(font, " 00.000V ", &g.window_width, &g.window_height);
	g.window_height *= 1.85;

	if (g.wx_forced) g.window_width = g.wx_forced;
	if (g.wy_forced) g.window_height = g.wy_forced;

	SDL_Window *window = SDL_CreateWindow("MP7100", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, g.window_width, g.window_height, 0);
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
	if (!font) {
		fprintf(stderr,"Error trying to open font :( \r\n");
		exit(1);
	}

	/* Select the color for drawing. It is set to red here. */
	SDL_SetRenderDrawColor(renderer, g.background_color.r, g.background_color.g, g.background_color.b, 255 );

	/* Clear the entire screen to our selected color. */
	SDL_RenderClear(renderer);

	/*
	 *
	 * Parent will terminate us... else we'll become a zombie
	 * and hope that the almighty PID 1 will reap us
	 *
	 */
	while (!quit) {
		char line1[1024];
		char line2[1024];
		char buf_volt[100];
		char buf_curr[100];

		char *p, *q;
		double v = 0.0;
		int end_of_frame_received = 0;
		uint8_t range;
		uint8_t dpp = 0;
		ssize_t bytes_read = 0;
		ssize_t sz;


		while (SDL_PollEvent(&event)) {
			switch (event.type)
			{
				case SDL_KEYDOWN:
					if (event.key.keysym.sym == SDLK_q) quit = true;
					break;
				case SDL_QUIT:
					quit = true;
					break;
			}
		}

		linetmp[0] = '\0';


		/*
		if (g.error_flag) {
			f = open( g.device, O_RDWR );
			if (f == -1) {
				fprintf(stdout, "Error opening device [%s] : %s\n", g.device, strerror(errno));
				sleep(1);
			} else {
				error_flag = false;
			}

		}
		*/

		sz = data_write( &g, g.meas_volt, strlen(g.meas_volt)  );
		usleep(20000);
		sz = data_read( &g, buf_volt, sizeof(buf_volt) );

		sz = data_write( &g, g.meas_curr, strlen(g.meas_curr));
		usleep(20000);
		sz = data_read( &g, buf_curr, sizeof(buf_curr));

		/*
		 *
		 * END OF DATA ACQUISITION
		 *
		 */


		//snprintf(line1, sizeof(line1)-1, "%s%s", buf_volt, error_flag?"":"V");
		snprintf(line1, sizeof(line1), "%7s%s", buf_volt, g.error_flag?"":"V");
		snprintf(line2, sizeof(line2), "%7s%s", buf_curr, g.error_flag?"":"A");
		if (g.debug) fprintf(stdout,"%s\n%s\n", line1, line2);


		{
			int texW = 0;
			int texH = 0;
			int texW2 = 0;
			int texH2 = 0;
			SDL_RenderClear(renderer);
			surface = TTF_RenderUTF8_Solid(font, line1, g.font_color_volts);
			texture = SDL_CreateTextureFromSurface(renderer, surface);
			SDL_QueryTexture(texture, NULL, NULL, &texW, &texH);
			SDL_Rect dstrect = { 0, 0, texW, texH };
			SDL_RenderCopy(renderer, texture, NULL, &dstrect);

			surface_2 = TTF_RenderUTF8_Solid(font, line2, g.font_color_amps);
			texture_2 = SDL_CreateTextureFromSurface(renderer, surface_2);
			SDL_QueryTexture(texture_2, NULL, NULL, &texW2, &texH2);
			dstrect = { 0, texH -(texH /5), texW2, texH2 };
			SDL_RenderCopy(renderer, texture_2, NULL, &dstrect);

			SDL_RenderPresent(renderer);

			SDL_DestroyTexture(texture);
			SDL_FreeSurface(surface);
			if (1) {
				SDL_DestroyTexture(texture_2);
				SDL_FreeSurface(surface_2);
			}

			if (g.error_flag) {
				sleep(1);
			} else {
				usleep(g.interval);
			}


		}


		if (g.output_file) {
			/*
			 * Only write the file out if it doesn't
			 * exist. 
			 *
			 */
			if (!fileExists(g.output_file)) {
				FILE *f;
				fprintf(stderr,"%s:%d: output filename = %s\r\n", FL, g.output_file);
				f = fopen(tfn,"w");
				if (f) {
					fprintf(f,"%s", linetmp);
					fprintf(stderr,"%s:%d: %s => %s\r\n", FL, linetmp, tfn);
					fclose(f);
					rename(tfn, g.output_file);
				}
			}
		}

	} // while(1)

	if (g.comms_mode == CMODE_USB) {
		close(g.usb_fhandle);
	}

	TTF_CloseFont(font);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();

	return 0;

}
