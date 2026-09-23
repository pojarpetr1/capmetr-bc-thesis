/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lcd.h"
#include "string.h"
#include "math.h"

#include "consts.h" // vlozi preddefinovane konstanty a datove typy

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

DAC_HandleTypeDef hdac;
DMA_HandleTypeDef hdma_dac1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

uint8_t adc_ready = 0; // signalizace dokonceni AD prevodu
uint32_t adc_buffer[ADC_BUF_SIZE]; // pole pro nacteni hodnot z AD prevodniku pomoci DMA

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_DAC_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM5_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
uint32_t sine120_table[SINE120_NUM_SAMPLES]; // diskretizovana sinusovka 120 Hz pro DA prevodnik
uint32_t sine1k_table[SINE1K_NUM_SAMPLES]; // diskretizovana sinusovka 1 kHz pro DA prevodnik

void calc_sine_table(){
	/* vypocet hodnot diskretizovane sinusovky pro DA prevodnik */
	for (int i = 0; i < SINE120_NUM_SAMPLES; ++i) {
		sine120_table[i] = (uint32_t)(((0.707*sin(i*2*PI/SINE120_NUM_SAMPLES)+1.6)*(4096/3.3)));
	}
	for (int i = 0; i < SINE1K_NUM_SAMPLES; ++i) {
		sine1k_table[i] = (uint32_t)(((1.414*sin(i*2*PI/SINE1K_NUM_SAMPLES)+1.6)*(4096/3.3)));
	}
}

void run_adc(void){
	/* cteni AD prevodniku (vsechny kanaly) a ulozeni hodnot do pameti */
	HAL_ADC_Start_DMA(&hadc1, adc_buffer, ADC_BUF_SIZE);
	while(adc_ready != 1){
		HAL_Delay(1);
	}
	adc_ready = 0;
	HAL_ADC_Stop_DMA(&hadc1);
}


int add_item(MEASLIST* mlist_ptr, float cx_val, float vin_val, int range){
	/* ulozeni dalsi polozky do seznamu namerenych hodnot */
    int idx = (mlist_ptr->save_pos)%ITEMS_LEN;
    mlist_ptr->act_pos = idx;
    mlist_ptr->meas_arr[idx].cx_val = cx_val;
    mlist_ptr->meas_arr[idx].vin_val = vin_val;
    mlist_ptr->meas_arr[idx].range = range;
    mlist_ptr->save_pos++;
    return 0;
}

void erase_mlist(MEASLIST* mlist_ptr){
	/* vynuluje promenne pozic pro ukladani a cteni v seznamu,
	 * puvodni hodnoty seznamu nejsou smazany, ale seznam se pak
	 * chova jako prazdny */
	mlist_ptr->act_pos = 0;
	mlist_ptr->save_pos = 0;
}

int read_buttons(){
	/* cteni stavu tlacitek */
	int ret = 0;
	if (!HAL_GPIO_ReadPin(BUT1_GPIO_Port, BUT1_Pin)){
		ret = BUTTON1; //stisknuto prvni tlacitko -> nove mereni
	}
	else if(!HAL_GPIO_ReadPin(BUT2_GPIO_Port, BUT2_Pin)){
		ret = BUTTON2; //stisknuto druhe tlacitko -> dalsi mereni
	}
	else if(!HAL_GPIO_ReadPin(BUT3_GPIO_Port, BUT3_Pin)){
		ret = BUTTON3; //stisknuto treti tlacitko -> posun nahoru
	}
	else if(!HAL_GPIO_ReadPin(BUT4_GPIO_Port, BUT4_Pin)){
		ret = BUTTON4; //stisknuto ctvrte tlacitko -> posun dolu
	}
	return ret;
}

void discharge_cap(void){
	/* vybiti mereneho kondenzatoru a kondenzátoru na integracnich clancich
	 * na vystupu operacnich usmernovacu */
	HAL_GPIO_WritePin(MOS1_GPIO_Port, MOS1_Pin|MOS2_Pin|MOS3_Pin, GPIO_PIN_SET);
	HAL_Delay(500);
	HAL_GPIO_WritePin(MOS1_GPIO_Port, MOS1_Pin|MOS2_Pin|MOS3_Pin, GPIO_PIN_RESET);
}

void start_sine_gen(int rng){
	/* generovani sinusovky pomoci DA prevodniku, 1 kHz nebo 120 Hz
	 * podle rozsahu */
	HAL_DAC_Stop(&hdac, DAC_CHANNEL_1);
	if(rng == RNG1){
		HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, sine1k_table, SINE1K_NUM_SAMPLES, DAC_ALIGN_12B_R);
	}
	else if(rng == RNG2){
		HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1, sine120_table, SINE120_NUM_SAMPLES, DAC_ALIGN_12B_R);
	}
}

void stop_sine_gen(void){
	/* ukonceni generovani sinusovky pomoci AD prevodniku */
	HAL_DAC_Stop_DMA(&hdac, DAC1_CHANNEL_1);
	HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
	HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
}

float calculate_cx(int rng){
	/* vypocet kapacity mereneho kondenzatoru z napeti merenych
	 * AD prevodnikem*/
	uint32_t ADC_val[] = {0,0};
	run_adc();
	HAL_Delay(10);
	for(int i = 0; i < ADC_BUF_SIZE/3; i++){
		for(int j = 0; j < 2; j++){
			ADC_val[j]+= adc_buffer[i*3+j+1];
		}
	}
	float c1_val = rng == 1 ? C1_RNG1 : C1_RNG2; // volba kapacity etalonu dle rozsahu
	float cx_val = c1_val*((float)ADC_val[0]/ADC_val[1]-1);
	return cx_val;
}

void charge_cx(void){
	/* nabiti mereneho kondenzatoru na hodnotu stejnosmerneho predpeti */
	HAL_GPIO_WritePin(REL1_GPIO_Port, REL1_Pin, GPIO_PIN_SET); // pripojeni ss napeti
	HAL_GPIO_WritePin(REL2_GPIO_Port, REL2_Pin, GPIO_PIN_SET); // nabijeni pres maly odpor
	HAL_Delay(2000);
	HAL_GPIO_WritePin(REL2_GPIO_Port, REL2_Pin, GPIO_PIN_RESET);
	HAL_Delay(20);
}

int get_range(int cx_val){
	/* automaticka volba mericiho rozsahu podle kapacity pri nulovem
	 * stejnosmernem predpeti */
	int ret = 0;
	if(1.0 <= cx_val && cx_val <= 10.0){
		ret = RNG1;
	}
	else if(10.0 < cx_val && cx_val <= 100.0){
		ret = RNG2;
	}
	return ret;
}

int disp_act(Lcd_HandleTypeDef* lcd_ptr, MEASLIST* mlist_ptr){
	/* vypsani aktualni zmerene hodnoty na displej */
	int ret = 1;
	if(mlist_ptr->save_pos != 0){
		char outstr[17];
		int idx = mlist_ptr->act_pos;
		MEAS_ITEM M_item = mlist_ptr->meas_arr[idx];
		sprintf(outstr, "C: %5.2f uF     ", M_item.cx_val);
		Lcd_cursor(lcd_ptr, 0,0);
		Lcd_string(lcd_ptr, outstr);
		Lcd_cursor(lcd_ptr, 1,0);
		sprintf(outstr, "U: %5.2f V  RNG%d", M_item.vin_val, M_item.range);
		Lcd_string(lcd_ptr, outstr);
		ret = 0;
	}
	return ret;
}

void disp_oor(Lcd_HandleTypeDef* lcd_ptr){
	// vypsani chybove hlasky na displej
	Lcd_cursor(lcd_ptr, 0,0);
	Lcd_string(lcd_ptr, "Kapacita mimo   ");
	Lcd_cursor(lcd_ptr, 1,0);
	Lcd_string(lcd_ptr, "rozsah!         ");
}

int move_up(Lcd_HandleTypeDef* lcd_ptr, MEASLIST* mlist_ptr){
	/* posun v seznamu namerenych hodnot o uroven vyse */
	if(mlist_ptr->save_pos != 0){
		int items_cnt = mlist_ptr->save_pos < ITEMS_LEN ? mlist_ptr->save_pos: ITEMS_LEN;
		int idx = mlist_ptr->act_pos;
		idx = (idx+1)%items_cnt;
		mlist_ptr->act_pos = idx;
	}
	disp_act(lcd_ptr, mlist_ptr);
	HAL_Delay(500);
    return 0;
}

int move_down(Lcd_HandleTypeDef* lcd_ptr,MEASLIST* mlist_ptr){
	/* posun v seznamu namerenych hodnot o uroven nize */
	if(mlist_ptr->save_pos != 0){
		int items_cnt = mlist_ptr->save_pos < ITEMS_LEN ? mlist_ptr->save_pos: ITEMS_LEN;
		int idx = mlist_ptr->act_pos;
		idx = (idx-1)%items_cnt < 0 ? items_cnt-1 : (idx-1)%items_cnt;
		mlist_ptr->act_pos = idx;
	}
	disp_act(lcd_ptr, mlist_ptr);
	HAL_Delay(500);
    return 0;
}

float measure_vin(void){
	/* mereni stejnosmerneho predpeti 0-30 V */
	uint32_t ADC_sum = 0;
		run_adc();
		HAL_Delay(10);
		for(int i = 0; i < ADC_BUF_SIZE/3; i++){
			ADC_sum += adc_buffer[i*3];
		}
	float ret = ADC_sum*((DELIC_UIN*3.3)/(ADC_RNG*64));
	return ret;
}

void set_range(int rng){
	/* volba kapacitniho etalonu podle aktualniho rozsahu */
	if (rng == RNG2) HAL_GPIO_WritePin(REL3_GPIO_Port, REL3_Pin, GPIO_PIN_SET); // pripoji kondenzotor 47 uf
}

void relay_off(){
	/* vypnuti vsech rele - odpojeni zdroje ss predpeti,
	 * odpojeni kapacitniho etalonu pro rozsah 10-100 uF */
	HAL_GPIO_WritePin(REL1_GPIO_Port, REL1_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(REL2_GPIO_Port, REL2_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(REL3_GPIO_Port, REL3_Pin, GPIO_PIN_RESET);
}

int measure_cx0_helper(int* range, float* cx_val){
	/* pomocna funkce pro proces mereni kapacity pri novem mereni */
	int rng = *range != 0 ? *range : RNG2;
	discharge_cap(); //vybiti vsech kondenzatoru
	set_range(rng);
	start_sine_gen(rng);
	HAL_Delay(SIN_GEN_DELAY);
	*cx_val = calculate_cx(rng);	// mereni napeti, vypocet kaapcty
	stop_sine_gen();
	relay_off();
	*range = get_range(*cx_val);
	return 0;
}

int measure_cx0(int* range, float* cx_val){
	/* proces mereni kapacity pri novem mereni, nejprve probehne mereni
	 * pri nejvyssim rozsahu 10-100 uF, pokud je kapacity nizsi, probehne
	 * jeste presnejsi mereni pri rozsahu 1-10 uF */
	measure_cx0_helper(range, cx_val);
	  if (*range == RNG1){
		  measure_cx0_helper(range, cx_val);
	  }
	return 0;
}

int measure_cx_vin(int* range, float* cx_val, float* vin_val){
	/* proces mereni pro mereni pri nastavenem stejnosmernem predpeti,
	 * pouziva se pouze pokud byl predtim automaticky zvolem vhodny rozsah */
	if (*range != 0){
		discharge_cap();
		set_range(*range);
		charge_cx();
		*vin_val = measure_vin();
		start_sine_gen(*range);
		HAL_Delay(SIN_GEN_DELAY);
		*cx_val = calculate_cx(*range);
		stop_sine_gen();
		relay_off();
	}
	return 0;

}

void disp_and_save_cx0(int* range, float* cx_val, Lcd_HandleTypeDef* lcd_ptr, MEASLIST* mlist_ptr){
	/* zobrazeni vysledku mereni na displeji a ulozeni do seznamu merenych hodnot
	 * pro 'nove mereni', pred ulozenim jsou predchozi hodnoty seznamu smazany */
	if(*range != 0){
	  erase_mlist(mlist_ptr);
	  add_item(mlist_ptr, *cx_val, 0.0, *range);
	  disp_act(lcd_ptr, mlist_ptr);
	}
	else{
	  disp_oor(lcd_ptr);  // zobrazi se chybova hlaska "kapacita mimo rozsah"
	}
}

void disp_and_save(int* range, float* cx_val,  float* vin_val, Lcd_HandleTypeDef* lcd_ptr, MEASLIST* mlist_ptr){
	/* zobrazeni vysledku mereni na displeji a ulozeni do seznamu merenych hodnot,
	 * pouziva se pro 'dalsi mereni' */
	if (*range != 0){
	  add_item(mlist_ptr, *cx_val, *vin_val, *range);
	  disp_act(lcd_ptr, mlist_ptr);
	}
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_DAC_Init();
  MX_TIM2_Init();
  MX_TIM5_Init();
  /* USER CODE BEGIN 2 */

  HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
  HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048); // nastaveni vystupni hodnoty DA prevodniku na polovinu rozsahu

  HAL_TIM_Base_Start(&htim5);
  HAL_TIM_Base_Start(&htim2);

  calc_sine_table(); // vypocet tabulky s hodnotami sinusovky pro DA prevodnik

  Lcd_PortType ports[] = { GPIOC, GPIOB, GPIOA, GPIOA }; // porty pro pripojeni LCD
  Lcd_PinType pins[] = {GPIO_PIN_7, GPIO_PIN_6, GPIO_PIN_7, GPIO_PIN_6}; // piny pro pripojeni LCD
  Lcd_HandleTypeDef lcd;
  lcd = Lcd_create(ports, pins, GPIOB, GPIO_PIN_5, GPIOB, GPIO_PIN_4, LCD_4_BIT_MODE); // inicializace LCD, prirazeni do "handle" instance

  MEASLIST mlist;	// seznam namerenych hodnot
  erase_mlist(&mlist);	// inicializace seznamu pro ukladani namerenych hodnot

  int range = 0;	// informace o aktualnim mericim rozsahu
  float cx_val;	// vysledna hodnota kapacity
  float vin_val;	// hodnota stejnosmerneho predpeti

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  /* // nacteni stavu tlacitek a provedeni akce podle toho,
	   * ktere tlacitko je stisknuto */
	  switch (read_buttons()) {
	    case BUTTON1: // nove mereni (prvni mereni nebo mereni s novym kondenzatorem)
	      measure_cx0(&range, &cx_val);
	      disp_and_save_cx0(&range, &cx_val, &lcd, &mlist);
	      break;
	    case BUTTON2: // dalsi mereni (mereni se stejnym kondenzatorem pri jinem napeti)
	    	measure_cx_vin(&range, &cx_val, &vin_val);
	    	disp_and_save(&range, &cx_val, &vin_val, &lcd, &mlist);
	      break;
	    case BUTTON3: // posun v seznamu namerenych hodnot o uroven vyse
	    	move_up(&lcd, &mlist);
	      break;
	    case BUTTON4: // posun v seznamu namerenych hodnot o uroven nize
	    	move_down(&lcd, &mlist);
	      break;
	  }

	  HAL_Delay(10);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 3;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_12;
  sConfig.Rank = 3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief DAC Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC_Init(void)
{

  /* USER CODE BEGIN DAC_Init 0 */

  /* USER CODE END DAC_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC_Init 1 */

  /* USER CODE END DAC_Init 1 */

  /** DAC Initialization
  */
  hdac.Instance = DAC;
  if (HAL_DAC_Init(&hdac) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT1 config
  */
  sConfig.DAC_Trigger = DAC_TRIGGER_T5_TRGO;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&hdac, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC_Init 2 */

  /* USER CODE END DAC_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 20-1;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 1823-1;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_ENABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM5 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM5_Init(void)
{

  /* USER CODE BEGIN TIM5_Init 0 */

  /* USER CODE END TIM5_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM5_Init 1 */

  /* USER CODE END TIM5_Init 1 */
  htim5.Instance = TIM5;
  htim5.Init.Prescaler = 21-1;
  htim5.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim5.Init.Period = 20-1;
  htim5.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim5.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim5) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim5, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim5, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM5_Init 2 */

  /* USER CODE END TIM5_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, LD2_Pin|GPIO_PIN_6|GPIO_PIN_7|REL2_Pin
                          |REL3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, REL1_Pin|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, MOS1_Pin|MOS2_Pin|MOS3_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : LD2_Pin PA6 PA7 REL2_Pin
                           REL3_Pin */
  GPIO_InitStruct.Pin = LD2_Pin|GPIO_PIN_6|GPIO_PIN_7|REL2_Pin
                          |REL3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : REL1_Pin MOS1_Pin MOS2_Pin MOS3_Pin
                           PB4 PB5 PB6 */
  GPIO_InitStruct.Pin = REL1_Pin|MOS1_Pin|MOS2_Pin|MOS3_Pin
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : BUT1_Pin BUT2_Pin */
  GPIO_InitStruct.Pin = BUT1_Pin|BUT2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PC7 */
  GPIO_InitStruct.Pin = GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : BUT3_Pin BUT4_Pin */
  GPIO_InitStruct.Pin = BUT3_Pin|BUT4_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(hadc);
  adc_ready = 1;
  HAL_ADC_Stop_DMA(&hadc1);
  /* NOTE : This function Should not be modified, when the callback is needed,
            the HAL_ADC_ConvCpltCallback could be implemented in the user file
   */
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
