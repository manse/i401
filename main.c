#include "xc.h"
#include <htc.h>
#define _XTAL_FREQ 8000000
#define T1COUT 49152
#pragma config FOSC     = INTOSC
#pragma config WDTE     = OFF
#pragma config PWRTE    = OFF
#pragma config MCLRE    = ON
#pragma config CP       = OFF
#pragma config BOREN    = OFF
#pragma config CLKOUTEN = OFF
#pragma config IESO     = OFF
#pragma config FCMEN    = OFF
#pragma config WRT      = OFF
#pragma config PLLEN    = ON
#pragma config STVREN   = ON
#pragma config BORV     = 2
#pragma config LVP      = OFF

unsigned long offsetFAT;
unsigned long offsetBPB;
unsigned long offsetRoot;
unsigned long offsetData;
unsigned char numOfSectorsPerCluster;
unsigned int numOfRootEntries;
unsigned long fileSize;
unsigned char soundBuffer[128];
unsigned int clusters[128];

unsigned long gsrc;
unsigned long gdst;
int fade;
char fadeLevel;

unsigned char sec;
unsigned char min;
unsigned char hour;
unsigned char isPlay;

char get(char dt) {
	SSP1BUF = dt;
	while (SSP1IF == 0);
	SSP1IF = 0;
	return SSP1BUF;
}

void cs_select(int flag) {
	RC3 = flag;
	get(0xff);
}

char send_command(unsigned char cmd, unsigned long arg) {
	cs_select(0);
	get(0xff);
	get(cmd | 0x40);
	get(arg >> 24);
	get(arg >> 16);
	get(arg >> 8);
	get(arg);
	if (cmd == 0x00) {
		get(0x95);
	} else if (cmd == 0x8) {
		get(0x87);
	}
	int x = 0;
	while (get(0xff) != 0 && ++x < 256);
	return x >= 256 ? 1 : 0;
}

char open_sector(unsigned long sector) {
	if (send_command(0x11, sector) != 0) return 0;
	int i = 0;
	while (1) {
		if (get(0xff) == 0xfe) {
			break;
		} else if (++i >= 1000) {
			return 0;
		}
		__delay_us(1);
	}
	return 1;
}

char open_sector_with_cluster(unsigned long sector, unsigned long cluster) {
	return open_sector(offsetData + (cluster - 2) * numOfSectorsPerCluster + sector);
}

void skip(int i) {
	while (i-- > 0) get(0xff);
}

void close() {
	get(0xff);
	get(0xff);
	cs_select(0);
}

unsigned long next_cluster(unsigned long cluster) {
	open_sector(offsetFAT + (cluster >> 8));
	unsigned int position = (cluster & 0xff) << 1;
	skip(position);
	cluster = get(0xff);
	cluster |= get(0xff) << 8;
	skip(510 - position);
	close();
	if (cluster < 0x2 || 0xffff6 < cluster) return 0;
	return cluster;
}

char init_fat() {
	unsigned int rootEntries;
	unsigned char numberOfFATs;
	unsigned int reservedSectors;
	unsigned int sectorsPerFAT;

	open_sector(0);
	skip(454);                                    // #0-453
	offsetBPB  = (unsigned long) get(0xff);       // #454
	offsetBPB |= (unsigned long) get(0xff) << 8;  // #455
	offsetBPB |= (unsigned long) get(0xff) << 16; // #456
	offsetBPB |= (unsigned long) get(0xff) << 24; // #457
	skip(512 - 454 - 4);
	close();

	open_sector(offsetBPB);
	skip(13);                           // #0-12
	numOfSectorsPerCluster = get(0xff); // #13
	reservedSectors  = get(0xff);       // #14
	reservedSectors |= get(0xff) << 8;  // #15
	numberOfFATs = get(0xff);           // #16
	rootEntries  = get(0xff);           // #17
	rootEntries |= get(0xff) << 8;      // #18
	skip(3);                            // #19,20,21
	sectorsPerFAT  = get(0xff);         // #22
	sectorsPerFAT |= get(0xff) << 8;    // #23
	skip(512 - 13 - 11);
	close();

	numOfRootEntries = rootEntries;
	numOfRootEntries >>= 4;
	offsetFAT = reservedSectors + offsetBPB;
	offsetRoot = offsetBPB + reservedSectors + sectorsPerFAT * numberOfFATs;
	offsetData = (rootEntries * 32) / 512 + offsetRoot;
	return 1;
}

char init_connection() {
	__delay_ms(100);
	unsigned int i;
	RC3 = 1;
	for (i = 0; i < 10; i++) {
		get(0xFF);
	}
	if (send_command(0x0, 0) != 1) return 0;
	send_command(0x8, 0x1AA);
	for (i = 0;; i++) {
		send_command(0x37, 0);
		if (send_command(0x29, 0x40000000) == 0) {
			break;
		} else if (i > 1000) {
			return 0;
		}
		__delay_us(1);
	}
	if (send_command(0x10, 512) != 0) return 0;
	cs_select(1);

	return 1;
}

char find_voices_directory_cluster(unsigned int chara, unsigned long *cluster) {
	unsigned char cursor = 0;
	for (unsigned int i = 0; i < numOfRootEntries; i++) {
		open_sector(offsetRoot + i);
		for (unsigned int j = 0; j < 16; j++) {
			unsigned char name0 = get(0xff); //0
			unsigned char name1 = get(0xff); //1
			skip(9);                             //2-10
			unsigned char attribute = get(0xff); //11
			if (attribute == 0b10000 && name0 == 'K' && name1 == 'C') {
				skip(14);
				clusters[cursor] = get(0xff);
				clusters[cursor] |= get(0xff) << 8;
				skip(4);
				cursor++;
			} else {
				skip(20);
			}
		}
		close();
	}
	if (cursor == 0) {
		return 0;
	} else {
		*cluster = clusters[chara % cursor];
		return 1;
	}
}

unsigned long find_voice_cluster(unsigned long cluster, int hour, unsigned long *fileCluster, unsigned long *fileSize) {
	unsigned char digit1 = hour / 10 + '0';
	unsigned char digit0 = hour % 10 + '0';
	unsigned char sector = 0;
	*fileCluster = 0;
	while(1) {
		open_sector_with_cluster(sector, cluster);
		for (int i = 0; i < 512; i += 32) {
			unsigned char name0 = get(0xff);
			unsigned char name1 = get(0xff);
			if (name0 == digit1 && name1 == digit0 && *fileCluster == 0) {
				skip(24);
				*fileCluster  = get(0xff);
				*fileCluster |= get(0xff) << 8;
				*fileSize  = (unsigned long) get(0xff);
				*fileSize |= (unsigned long) get(0xff) << 8;
				*fileSize |= (unsigned long) get(0xff) << 16;
				*fileSize |= (unsigned long) get(0xff) << 24;
			} else {
				skip(30);
			}
		}
		close();
		if (*fileCluster != 0) {
			return 1;
		}
		sector++;

		if (sector == numOfSectorsPerCluster) {
			sector = 0;
			cluster = next_cluster(cluster);
			if (cluster == 0) break;
		}
	}
	return 0;
}

void play(unsigned char chara, unsigned char hour) {
	OSCCON = 0b01110000;
	unsigned long cluster;
	if (init_connection() == 0) return;
	if (init_fat() == 0) return;
	if (find_voices_directory_cluster(chara, &cluster) == 0) return;
	if (find_voice_cluster(cluster, hour, &cluster, &fileSize) == 0) return;

	unsigned char i, j, k;
	gdst = 50;
	gsrc = 0;
	fade = -256;
	fadeLevel = 0;

	DAC1CON1 = 0;
	DAC1CON0 = 0b10000000;
	OPA1CON = 0b11010010;
	TMR0 = 0;
	TMR0IF = 0;
	TMR0IE = 1;

	// cluster loop
	do {
		// sector loop
		for (k = 0; k < numOfSectorsPerCluster; k++) {
			open_sector_with_cluster(k, cluster);
			// sound buffer loop
			for (j = 0; j < 4; j++) {
				// wave loop
				for (i = 0; i < 128; i++) {
					while (gsrc >= gdst);
					soundBuffer[i] = get(0xff);
					gsrc++;
				}
			}
			close();
			if (RA2 == 0 || RC5 == 0) {
				gdst = fileSize;
				return;
			}
		}
	} while ((cluster = next_cluster(cluster)) != 0);
	return;
}

void interrupt InterTimer(void) {
	if (TMR1IF == 1) {
		TMR1L = (T1COUT & 0x00ff);
		TMR1H = (T1COUT >> 8);
		TMR1IF = 0;
		sec++;
		if (sec >= 60) {
			sec = 0;
			min++;
			if (min >= 60) {
				min = 0;
				hour++;
				if (hour >= 24) {
					hour = 0;
				}
				isPlay = 1;
			}
		}
	}
	if (TMR0IF == 1) {
		TMR0IF = 0;
		TMR0 = 83;
		int v = 0;
		if (gdst < fileSize) {
			v = soundBuffer[gdst & 0x7f];
			if (v < 0x7e || v > 0x82) {
				if (fade > -4) {
					fade = 0;
				} else {
					fade += 4;
				}
			} else if (++fadeLevel > 4) {
				fadeLevel = 0;
				if (--fade < -0xff) fade = -0xff;					
			}
			v += fade;
			if (v > 0xff) {
				v = 0xff;
			} else if (v < 0) {
				v = 0;
			}
		}
		gdst++;
		DAC1CON1 = v;
	}
}

int main(void) {
	OSCCON     = 0b00010011; // 32MHz
	ANSELA     = 0b00000000;
	ANSELC     = 0b00000000; 
	TRISA      = 0b00000100;
	WPUA       = 0b00000100;
	TRISC      = 0b00100010;
	WPUC       = 0b00100000;
	OPTION_REG = 0b00000000;
	PORTA      = 0;
	PORTC      = 0;
	PEIE       = 1;
	GIE        = 1;
	
	// SPI (SD)
	RC0PPS     = 0b00010000; // RC0 SCK/SCL
	RC4PPS     = 0b00010010; // RC4 SDO
	SSP1CON1   = 0b00110010;
	SSP1STAT   = 0b00000000;
	SSP1IF     = 0;

	// CLOCK
	T1CON      = 0b10011110;
	TMR1H      = 0;
	TMR1L      = 0;
	TMR1IF     = 0;
	TMR1IE     = 1;
	TMR1ON     = 1;
	
	sec = min = hour = 0;
	isPlay = 1;
	unsigned int chara = 0;
	
	while (1) {
		if (RA2 == 0) {
			chara++;
			min = sec = 0;
			isPlay = 1;
			while (RA2 == 0);
		} else if (RC5 == 0) {
			hour++;
			if (hour >= 24) {
				hour = 0;
			}
			min = sec = 0;
			isPlay = 1;
			while (RC5 == 0);
		}
		if (isPlay == 0) continue;

		play(chara, hour);
		OSCCON = 0b00010011;
		TMR0IE = 0;
		OPA1CON = 0;
		DAC1CON0 = 0;
		DAC1CON1 = 0;
		isPlay = 0;
	}
	
	return 0;
}
