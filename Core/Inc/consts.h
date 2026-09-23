/*
 * consts.h
 *
 *  Created on: May 21, 2024
 *      Author: pojar
 *      obsahuje definice konstant a novych datovych typu - struktur pro
 *      seznam merenych hodnot
 */

#ifndef INC_CONSTS_H_
#define INC_CONSTS_H_

#define ITEMS_LEN 20 // delka seznamu pro ukladani merenych hodnot

typedef struct{
	/* datovy typ pro ukladani merenych hodnot,
	 * ktere se vypisuji na displej - kapacita,
	 * stejnosmerne predpeti a rozsah */
    float cx_val;
    float vin_val;
    int range;
}MEAS_ITEM;

typedef struct{
	/* seznam pro ukladani jednotlivych mereni, obsahuje
	 * pole hodnot typu MEAS_ITEM, aktualni pozici v seznamu
	 * a pozici k ulozani dalsi polozky */
    MEAS_ITEM meas_arr[ITEMS_LEN];
    unsigned int act_pos;
    unsigned int save_pos;
}MEASLIST;

#define PI 3.1415926
#define SINE120_NUM_SAMPLES 1667 // pocet vzorku pro vypocet sinusovky 120 Hz
#define SINE1K_NUM_SAMPLES 200 // pocet vzorku pro vypocet sinusovky 100 Hz

#define C1_RNG1 4.6484 // kapacita eltalonu C1 merena RLC metrem (rozsah 1-10 uF)
#define C1_RNG2 49.355 // kapacita eltalonu C1 merena RLC metrem (rozsah 10-100 uF)

#define ADC_RNG 4096.0 // pocet kvantizacnich urovni AD prevodniku
#define DELIC_UIN 11.02285 // delici pomer delice pro mereni stejnosmerneho predpeti

#define ADC_CHANNELS 3 // pocet vyuzitych kanalu AD prevodniku
#define ADC_BUF_SIZE 192 // delka bufferu pro ukladani hodnot z AD prevodniku
#define SIN_GEN_DELAY 2000 // zpozdeni po spusteni DA prevodniku

enum{BUTTON1 = 1, BUTTON2, BUTTON3, BUTTON4 }; // konstanty pro tlacitka
enum{RNG1 = 1, RNG2 }; // konstanty pro rozsahy, RNG1 = 1-10 uF, RNG2 = 10-100 uF

#endif /* INC_CONSTS_H_ */
