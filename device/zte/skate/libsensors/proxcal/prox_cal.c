#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <math.h>
#include "../taos_common.h"

#define TAOS_DEV	"/dev/taos"
int fd;

void fail(){
    printf("Something went wrong. Aborting");
    ioctl(fd, TAOS_IOCTL_PROX_OFF);
    close(fd);

    exit(EXIT_FAILURE);
}

void writeCfg(struct taos_cfg *cfg){
    FILE *file;
    file = fopen("/data/misc/prox_data.txt","w");
    if(file==NULL) fail();
    else{
        fprintf(file,"%u,",cfg->prox_threshold_hi);
	fprintf(file,"%u,",cfg->prox_threshold_lo);
	fprintf(file,"%u,",cfg->prox_int_time);
	fprintf(file,"%u,",cfg->prox_adc_time);
	fprintf(file,"%u,",cfg->prox_wait_time);
	fprintf(file,"%u,",cfg->prox_intr_filter);
	fprintf(file,"%u,",cfg->prox_config);
	fprintf(file,"%u,",cfg->prox_pulse_cnt);
	fprintf(file,"%u\n",cfg->prox_gain);
	fclose(file);
    }
}

void help(){
    printf("ZTE Blade Taos proximity sensor calibration program\n");
    printf("CyanogenMod Project\n");
    printf("Author: Tom Giordano\n\n");
    printf("-c : Calibrate proximity sensor and write data to file\n");
    printf("-d : Display current proximity calibration data\n\n");
}

int main(int argc, char **argv){

    struct taos_cfg cfg;
    int rv = 0;
    char *argv_value = NULL;
    int argc_c;
    char *p;
    fd = open(TAOS_DEV, O_RDONLY);

    const char *delim = "#;,\\n";
    struct taos_prox_info prox_info;

    if (argc==1) help();

    while ((argc_c = getopt(argc, argv, "cdh")) != -1){
        switch(argc_c){
	  /* read currently set calibration values and display them */
	case 'd':

	    rv = ioctl(fd, TAOS_IOCTL_CONFIG_GET, &cfg);

	    printf("prox_theshold_hi = %u\n", cfg.prox_threshold_hi);
	    printf("prox_theshold_lo = %u\n", cfg.prox_threshold_lo);
	    printf("prox_int_time = %u\n", cfg.prox_int_time);
	    printf("prox_adc_time = %u\n", cfg.prox_adc_time);
	    printf("prox_wait_time = %u\n", cfg.prox_wait_time);
	    printf("prox_intr_filter = %u\n", cfg.prox_intr_filter);
	    printf("prox_config = %u\n", cfg.prox_config);
	    printf("prox_pulse_cnt = %u\n", cfg.prox_pulse_cnt);
	    printf("prox_gain = %u\n", cfg.prox_gain);

	    break;

	    /* Calibrate prox sensor and write values to file */
	case 'c':

	    rv = ioctl(fd, TAOS_IOCTL_PROX_ON);         // Turn on prox sensor
	    if (rv)
	        fail();
	    sleep(1);                                   // Give it a second to turn on
	    rv = ioctl(fd, TAOS_IOCTL_PROX_CALIBRATE);  //Calibrate sensor
	    if (rv)
	        fail();
	    rv = ioctl(fd, TAOS_IOCTL_PROX_OFF);        // Turn off prox sensor
	    if (rv)
	        fail();

	    rv = ioctl(fd, TAOS_IOCTL_CONFIG_GET, &cfg); // Get new settings
	    if (rv)
	        fail();

	    writeCfg(&cfg);
	    printf("Calibrated proximity sensor\n");

	    break;

	case 'h':
	    printf("ZTE Blade Taos proximity sensor calibration program\n");
	    printf("Author: Tom Giordano\n\n");
	    printf("-c : Calibrate proximity sensor and write data to file\n");
	    printf("-d : Display current proximity calibration data\n\n");
	    break;
	}
    }

    close(fd);
    exit(EXIT_SUCCESS);
}


