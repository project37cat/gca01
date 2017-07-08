// Arduino Geiger counter 01.2
// Arduino 1.0.5


#include "LiquidCrystal.h"

#include <avr/delay.h>


#define GEIGER_TIME 75 //время измерения, для СИ29БГ 75 секунд
#define ALARM_LEVEL 40 //уровень тревоги
#define TIMER1_PRELOAD 64910 //65535-64910=625, 15625/625=25Гц
#define TIMER2_PRELOAD 0
#define HVGEN_FACT 5 // 25/5=5Гц частота подкачки преобразователя
#define TIME_FACT 25 // 25Гц/25=1Гц секундные интервалы
#define NUM_KEYS 5 //количество кнопок

LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

uint16_t rad_buff[GEIGER_TIME]; //массив секундных замеров для расчета фона
uint16_t adc_key_val[5] = { 50, 200, 400, 600, 800 }; //значения АЦП для обработки кнопок

uint32_t rad_sum; //сумма импульсов за все время
uint32_t rad_back; //текущий фон
uint32_t rad_max; //максимум фона
uint32_t rad_dose; //доза
uint8_t time_sec; //секунды //счетчики времени
uint8_t time_min; //минуты
uint8_t time_hrs; //часы
uint8_t scr_mode; //режим
uint8_t alarm; //флаг тревоги
uint8_t alarm_disable; //флаг запрета тревоги

char str_buff[17];


void conv_pump(void);
uint8_t get_key(void);
uint8_t check_keys(void);
void alarm_warning(void);


///////////////////////////////////////////////////////////////////////////////////////////////////
void setup(void) //инициализация
{
//настраиваем Timer 1
TIMSK1=0; //отключить таймер
TCCR1A=0; //OC1A/OC1B disconnected
TCCR1B=0b00000101; //предделитель 16M/1024=15625кГц
TCNT1=TIMER1_PRELOAD;

//настраиваем Timer 2
TIMSK2=0;
TCCR2A=0;
TCCR2B=0;
TCNT2=TIMER2_PRELOAD;

bitSet(DDRB,2); //pin 10 (PB2) как выход, управление подсветкой
bitSet(PORTB,2); //включаем подсветку

bitSet(DDRD,3); //pin 3 (PD3) как выход, уаравление преобразователем
bitClear(PORTD,3);

bitClear(DDRD,2); //настраиваем пин 2 (PD2) на вход, импульсы от счетчика
bitSet(PORTD,2); //подтягивающий резистор

lcd.begin(16,2);
lcd.print("Geiger Counter");
lcd.setCursor(0,1);
lcd.print("Wait a moment");

for(uint16_t k=2000; k>0; k--) //преднакачка преобразователя
	{
	conv_pump();
	_delay_ms(1);
	}

lcd.clear();

TIMSK1=0b00000001; //запускаем Timer 1
EICRA=0b00000010; //настриваем внешнее прерывание 0 по спаду
EIMSK=0b00000001; //разрешаем внешнее прерывание 0
}

///////////////////////////////////////////////////////////////////////////////////////////////////
ISR(INT0_vect) //внешнее прерывание //считаем импульсы от счетчика
{
if(rad_buff[0]!=65535) rad_buff[0]++; //нулевой элемент массива - текущий секундный замер
if(++rad_sum>999999UL*3600/GEIGER_TIME) rad_sum=999999UL*3600/GEIGER_TIME; //общая сумма импульсов

conv_pump(); //подкачка преобразователя
}


///////////////////////////////////////////////////////////////////////////////////////////////////
ISR(TIMER1_OVF_vect) //прерывание по переполнению Timer 1
{
static uint8_t cnt1;
static uint8_t cnt2;

TCNT1=TIMER1_PRELOAD;

if(++cnt2>=HVGEN_FACT) //подкачка преобразователя с заданной частотой
	{
	cnt2=0;
	conv_pump();
	}

if(++cnt1>=TIME_FACT) //расчет показаний один раз в секунду
	{
	cnt1=0;

	uint32_t tmp_buff=0;
	for(uint8_t i=0; i<GEIGER_TIME; i++) tmp_buff+=rad_buff[i]; //расчет фона мкР/ч
	if(tmp_buff>999999) tmp_buff=999999; //переполнение
	rad_back=tmp_buff;

	if(rad_back>rad_max) rad_max=rad_back; //фиксируем максимум фона

	if(rad_back>ALARM_LEVEL && alarm_disable==0) alarm=1; //превышение фона //поднимаем флаг сигнала тревоги

	for(uint8_t k=GEIGER_TIME-1; k>0; k--) rad_buff[k]=rad_buff[k-1]; //перезапись массива
	rad_buff[0]=0; //сбрасываем счетчик импульсов

	rad_dose=(rad_sum*GEIGER_TIME/3600); //расчитаем дозу

	if(time_hrs<99) //если таймер не переполнен
		{
		if(++time_sec>59) //считаем секунды
			{
			if(++time_min>59) //считаем минуты
				{
				if(++time_hrs>99) time_hrs=99; //часы
				time_min=0;
				}
			time_sec=0;
			}
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////
void conv_pump(void) //импульс на преобразователь напряжения
{
bitSet(PORTD,3);
_delay_us(10);
bitClear(PORTD,3);
}


///////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t get_key(void) //получить номер нажатой кнопки из данных АЦП
{
uint8_t key=0;
uint16_t adc_result=analogRead(0);

for (uint8_t i=0; i<NUM_KEYS; i++)
	{
	if (adc_result<adc_key_val[i])
		{
		key=i+1;
		break;
		}
	}
return key;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
uint8_t check_keys(void) //проверить клавиатуру
{
uint8_t k=0;
static uint8_t old_key;

uint8_t new_key = get_key(); //обновить состояние
if (new_key != old_key) //если состояние не равно старому - была нажата копка
	{
	_delay_ms(50); //защита от дребезга
	new_key = get_key();
	if (new_key != old_key)
		{
		old_key = new_key;
		k=new_key;
		}
	}
return k; //вернем номер кнопки 1..5, 0-кнопка не нажата
}


///////////////////////////////////////////////////////////////////////////////////////////////////
void alarm_warning(void) //выводим предупреждение
{
uint8_t cnt=0;

lcd.clear();
lcd.print("*** WARNING ***");

while(1)
	{
	sprintf(str_buff,"%6lu uR/h",rad_back);
	lcd.setCursor(0,1);
	lcd.print(str_buff);

	_delay_ms(100);

	if(++cnt>3) //мигаем подсветкой
		{
		cnt=0;
		if(bit_is_set(PORTB,2)) bitClear(PORTB,2);
		else bitSet(PORTB,2);
		}

	if(check_keys()) //если нажата любая кнопка отключаем тревогу
		{
		bitSet(PORTB,2); //включаем подсветку
		lcd.home();
		lcd.print(" ALARM DISABLE ");
		_delay_ms(2000);
		alarm_disable=1;
		alarm=0;
		lcd.clear();
		return;
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////
void loop(void) //главная
{
if(alarm) alarm_warning();


lcd.setCursor(0,0);
switch(scr_mode)
	{
	case 0:
		lcd.print("Background");
		sprintf(str_buff,"%6lu uR/h",rad_back);
		break;
	case 1:
		lcd.print("Dose");
		sprintf(str_buff,"  %6lu uR",rad_dose);
		break;
	case 2:
		lcd.print("Maximum");
		sprintf(str_buff,"%6lu uR/h",rad_max);
		break;
	case 3:
		lcd.print("Time");
		sprintf(str_buff,"   %02u:%02u:%02u",time_hrs,time_min,time_sec);
		break;
	}
lcd.setCursor(5,1);
lcd.print(str_buff);

lcd.setCursor(0,1);
if(alarm_disable) lcd.print("x"); //если тревога была отключена рисуем "x" в углу экрана
else lcd.print(" ");


switch(check_keys())
	{
	case 1: //right key
		break;
	case 2: //up key
		break;
	case 3: //down key //сброс
		switch(scr_mode)
			{
			case 0: //сбрасываем фон и флаг отключения тревоги
				for(uint8_t i=0; i<GEIGER_TIME; i++) rad_buff[i]=0;
				rad_back=0;
				alarm_disable=0;
				break;
			case 1: //сбрасываем дозу
				rad_sum=0;
				rad_dose=0;
				break;
			case 2: //сбрасываем макс. фон
				rad_max=0;
				break;
			case 3: //сброс счетчика времени
				time_hrs=time_min=time_sec=0;
				break;
			}
		break;
	case 4: //left key
		break;
	case 5: //select key //выбор режима
		if(++scr_mode>3)
		scr_mode=0;
		lcd.clear();
		break;
	}

_delay_ms(100);
}
