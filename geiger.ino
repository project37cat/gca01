// Arduino Geiger counter 01.3
// Arduino 1.0.5
// ATmega328P 16MHz


#include "LiquidCrystal.h"

#include <avr/delay.h>


#define GEIGER_TIME 75  //время измерения, для СИ29БГ 75 секунд (1..255)
#define HVGEN_FACT  5   //частота подкачки преобразователя 5Гц (1..25)


//Timer2 ~2kHz для пищалки
#define BUZZ_FREQ_HZ    2000
#define TMR2_PRESCALER  128
#define TIMER2_2K       ((F_CPU/TMR2_PRESCALER/BUZZ_FREQ_HZ)-1)

//Timer1 25Hz для секунд
#define TMR1_INT_HZ     25
#define TMR1_PRESCALER  1024
#define TIMER1_PRELOAD  (65536-(F_CPU/TMR1_PRESCALER)/TMR1_INT_HZ)

#define TIME_FACT 25 //секундные интервалы 25Гц/25=1Гц
#define NUM_KEYS 5   //количество кнопок

//пищалка старт/стоп
#define TIMER2_START       (bitSet(TIMSK2,OCIE0A))
#define TIMER2_STOP        (bitClear(TIMSK2,OCIE0A))
#define TIMER2_IS_STOPPED  (bit_is_clear (TIMSK2,OCIE0A))


#define DDR_REG(portx)  (*(&portx-1))

//пин преобразователя
#define CONV_BIT   3
#define CONV_PORT  PORTD

#define CONV_CLR   (bitClear(CONV_PORT, CONV_BIT))
#define CONV_SET   (bitSet(CONV_PORT, CONV_BIT))
#define CONV_OUT   (bitSet((DDR_REG(CONV_PORT)), CONV_BIT))

#define CONV_INIT  CONV_CLR; CONV_OUT

//пин пищалки
#define BUZZ_BIT   1
#define BUZZ_PORT  PORTC

#define BUZZ_CLR   (bitClear(BUZZ_PORT, BUZZ_BIT))
#define BUZZ_SET   (bitSet(BUZZ_PORT, BUZZ_BIT))
#define BUZZ_OUT   (bitSet((DDR_REG(BUZZ_PORT)), BUZZ_BIT))

#define BUZZ_INIT  BUZZ_CLR; BUZZ_OUT

//пин детектора
#define DET_BIT   2
#define DET_PORT  PORTD

#define DET_SET   (bitSet(DET_PORT, DET_BIT))
#define DET_INP   (bitClear((DDR_REG(DET_PORT)), DET_BIT))

#define DET_INIT  DET_SET; DET_INP

//пин подсветки
#define LIGHT_BIT   2
#define LIGHT_PORT  PORTB

#define LIGHT_CLR   (bitClear(LIGHT_PORT, LIGHT_BIT))
#define LIGHT_SET   (bitSet(LIGHT_PORT, LIGHT_BIT))
#define LIGHT_OUT   (bitSet((DDR_REG(LIGHT_PORT)), LIGHT_BIT))

#define LIGHT_IS_SET  (bit_is_set(LIGHT_PORT,LIGHT_BIT))

#define LIGHT_INIT  LIGHT_SET; LIGHT_OUT


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

uint8_t scr=0; //флаг обновления экрана
uint8_t alarm=0; //флаг тревоги
uint8_t alarm_disable=0; //флаг запрета тревоги
uint8_t alarm_wait=0; //флаг ожидания выключения запрета
uint8_t buzz_disable=0; //флаг запрет треска пищалкой

uint8_t timer=0; //for delay
uint8_t timer_out=0; //flag

uint8_t buzz_vol = 10; //громкость треска(щелчков)  (10-50 с шагом 10)
uint8_t beep_vol = 20; //громкость тревоги  (1-50 с шагом 5)
uint8_t alarm_level = 50; //уровень тревоги uR/h  (40..250 с шагом 10)


char str_buff[18];


uint8_t s0[8] = {
  0b00000,
  0b00100,
  0b01110,
  0b01110,
  0b01110,
  0b11111,
  0b00100,
  0b00000
};

uint8_t  s1[8] = {
  0b00000,
  0b00100,
  0b10010,
  0b01010,
  0b01010,
  0b10010,
  0b00100,
  0b00000
};

uint8_t  s2[8] = {
  0b00000,
  0b11111,
  0b00000,
  0b11111,
  0b00000,
  0b11111,
  0b00000,
  0b00000
};

uint8_t  s3[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b10101
};

uint8_t  s4[8] = {
  0b00000,
  0b00001,
  0b00011,
  0b01111,
  0b01111,
  0b00011,
  0b00001,
  0b00000
};

uint8_t  s5[8] = {
  0b00000,
  0b00000,
  0b01110,
  0b01010,
  0b01010,
  0b11011,
  0b00000,
  0b00000
};

uint8_t  s6[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

uint8_t  s7[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};


void conv_pump(void);
void buzz_pulse(uint8_t time);
uint8_t get_key(void);
uint8_t check_keys(void);
void alarm_warning(void);
void menu(void);

//-------------------------------------------------------------------------------------------------
void setup(void) //инициализация
    {
    TIMSK1=0; //Timer1 interrupts disable
    TCCR1A=0; //OC1A/OC1B disconnected
    TCCR1B=0b00000101; //prescaler: 16000000/1024=15625Гц
    TCNT1=TIMER1_PRELOAD;

    TIMSK2=0; //Timer2 interrupts disable
    TCCR2A=0;
    TCCR2B=0b00000101; //prescaler: 16M/128=125000
    OCR2A=TIMER2_2K;

    CONV_INIT;
    BUZZ_INIT;
    DET_INIT;
    LIGHT_INIT;

    lcd.begin(16,2);
    lcd.print("Geiger Counter");
    lcd.setCursor(0,1);
    lcd.print("Wait a moment");

    for(uint16_t k=2000; k>0; k--) //преднакачка преобразователя
        {
        conv_pump();
        _delay_ms(1);
        if(check_keys()) break;
        }

    lcd.clear();

    lcd.createChar(0, s0);  //загружаем символы в дисплей
    lcd.createChar(1, s1);
    lcd.createChar(2, s2);
    lcd.createChar(3, s3);
    lcd.createChar(4, s4);
    lcd.createChar(5, s5);
    lcd.createChar(6, s6);
    lcd.createChar(7, s7);

    TIMSK1=0b00000001; //разрешаем прерывание по переполнению Timer1
    EICRA=0b00000010;  //настраиваем внешнее прерывание по спаду импульса на INT0
    EIMSK=0b00000001;  //разрешаем внешнее прерывание INT0
    }


//-------------------------------------------------------------------------------------------------
ISR(INT0_vect) //внешнее прерывание на пине INT0 - считаем импульсы от счетчика
    {
    if(rad_buff[0]!=65535) rad_buff[0]++; //нулевой элемент массива - текущий секундный замер
    if(++rad_sum>999999UL*3600/GEIGER_TIME) rad_sum=999999UL*3600/GEIGER_TIME; //сумма импульсов

    conv_pump(); //подкачка преобразователя

    if(buzz_disable==0) buzz_pulse(buzz_vol); //щелчок пищалкой
    }


//-------------------------------------------------------------------------------------------------
ISR(TIMER1_OVF_vect) //прерывание по переполнению Timer1 - 25Hz
    {
    static uint8_t cnt1;
    static uint8_t cnt2;

    TCNT1=TIMER1_PRELOAD;

    if(++cnt2>=25/HVGEN_FACT) //подкачка преобразователя с заданной частотой
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

        if(rad_back>=alarm_level) alarm=1; //превышение фона
        else {alarm=0; if(alarm_wait) alarm_disable=0; alarm_wait=0;}

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

        scr=0;
        }

    if(timer) //таймер для разного
        {
        if(--timer==0) timer_out=1;
        }
    }

//-------------------------------------------------------------------------------------------------
ISR(TIMER2_COMPA_vect) //прерывание Timer0 - сигнал ~2kHz для пищалки
    {
    TCNT2 = 0x00;
    buzz_pulse(beep_vol);
    }


//-------------------------------------------------------------------------------------------------
void buzz_pulse(uint8_t time) //импульс тока на пищалку, мкс (длительность - громкость)
    {
    BUZZ_SET;
    while(time-->0) _delay_us(1);
    BUZZ_CLR;
    }


//-------------------------------------------------------------------------------------------------
void conv_pump(void) //импульс на преобразователь напряжения
    {
    CONV_SET;
    _delay_us(10);
    CONV_CLR;
    }


//-------------------------------------------------------------------------------------------------
uint8_t get_key(void) //получить номер нажатой кнопки из данных АЦП
    {
    uint8_t key = 0;
    uint16_t adc_result = analogRead(0);

    for(uint8_t i=0; i<NUM_KEYS; i++)
        {
        if(adc_result<adc_key_val[i])
            {
            key=i+1;
            break;
            }
        }
    return key;
    }


//-------------------------------------------------------------------------------------------------
uint8_t check_keys(void) //проверить клавиатуру
    {
    uint8_t k=0;
    static uint8_t old_key;

    uint8_t new_key = get_key(); //обновить состояние
    if(new_key != old_key) //если состояние не равно старому - была нажата копка
        {
        _delay_ms(50); //защита от дребезга
        new_key = get_key();
        if(new_key != old_key)
            {
            old_key = new_key;
            k=new_key;
            }
        }
    return k; //вернем номер кнопки 1..5, 0-кнопка не нажата
    }


//-------------------------------------------------------------------------------------------------
void alarm_warning(void) //выводим предупреждение
    {
    uint8_t n=0;
    uint32_t rad_alrm=0;

    uint8_t bd = buzz_disable;  //запомнить настройку звуковой индикации импульсов

    buzz_disable=1;  //запретить звуковую индикацию импульсов

    lcd.clear();
    lcd.setCursor(0,1);
    lcd.print("WARNING");
    scr==0;

    while(1)
        {
        if(scr==0)
            {
            scr=1;
            if(rad_back>rad_alrm) rad_alrm=rad_back; //максимум
            sprintf(str_buff,"%6lu uR/h",rad_alrm);
            lcd.setCursor(5,0);
            lcd.print(str_buff);
            }

        //==================================================================
        if(n==0) //начало прерывистого звукового сигнала
            {
            n=1;
            timer=9; //длительность сигнала x40ms
            timer_out=0;
            TIMER2_START;
            }

        if(n==1 && timer_out==1) //начало паузы между сигналами
            {
            n=2;
            timer=4; //длительность паузы x40ms
            timer_out=0;
            TIMER2_STOP;
            }

        if(n==2 && timer_out==1) n=0; //запуск следующего цикла
        //==================================================================

        if(check_keys()==4) //если нажата кнопка left отключаем тревогу
            {
            TIMER2_STOP;
            lcd.setCursor(0,1);
            lcd.print("ALARM DISABLE");
            n=0;
            timer=35; //длительность сообщения x40ms
            timer_out=0;
            while(timer_out==0) if(check_keys()==4) break;
            lcd.clear();
            buzz_disable=bd;
            alarm_disable=1;
            scr_mode=0;
            scr=0;
            return;
            }
        }
    }


//-------------------------------------------------------------------------------------------------
void menu(void)
    {
    uint8_t n=0;

    lcd.clear();
    scr=0;

    while(1)
        {
        if(alarm && alarm_disable==0) alarm_warning();

        if(scr==0)  //+++++++++++++++++++   вывод информации на экран  +++++++++++++++++++++++++
            {
            scr=1;

            lcd.setCursor(0,1);
            lcd.write(byte(2));
            lcd.setCursor(2,1);
            sprintf(str_buff,"%01u",n+1);
            lcd.print(str_buff);

            switch(n)
                {
                case 0: sprintf(str_buff,"BUZZ VOLUME   %2u",buzz_vol); break;
                case 1: sprintf(str_buff,"ALARM VOLUME  %2u",beep_vol); break;
                case 2: sprintf(str_buff,"ALARM LEVEL  %3u",alarm_level); break;
                }
            lcd.setCursor(0,0);
            lcd.print(str_buff);
            }

        switch(check_keys())  //+++++++++++++++++++++  опрос кнопок  +++++++++++++++++++++++++++
            {
            case 1: //right key
                scr=0;
                break;
            case 2: //up key
                switch(n)
                    {
                    case 0: if(buzz_vol<50) buzz_vol+=10; break;
                    case 1: if(beep_vol<50) { if(beep_vol==1) beep_vol=0;  beep_vol+=5;} break;
                    case 2: if(alarm_level<250) alarm_level+=10; break;
                    }
                scr=0;
                break;
            case 3: //down key
                switch(n)
                    {
                    case 0: if(buzz_vol>10) buzz_vol-=10; break;
                    case 1: if(beep_vol>5) beep_vol-=5; else beep_vol=1; break;
                    case 2: if(alarm_level>40) alarm_level-=10; break;
                    }
                scr=0;
                break;
            case 4: //left key
                scr=0;
                break;
            case 5: //select key
                if(++n>2)
                    {
                    n=0;
                    lcd.clear();
                    scr=0;
                    return;
                    }
                scr=0;
            default: break;
            }
        }
    }


//-------------------------------------------------------------------------------------------------
void loop(void) //главная
    {
    if(alarm && alarm_disable==0) alarm_warning();

    if(scr==0)  //+++++++++++++++++++   вывод информации на экран  +++++++++++++++++++++++++
        {
        scr=1;  //сброс флага

        switch(scr_mode)
            {
            case 0: sprintf(str_buff,"Rate %6lu uR/h",rad_back); break;  //dose rate, uR/h
            case 1: sprintf(str_buff,"Dose   %6lu uR",rad_dose); break;  //dose, uR
            }
        lcd.setCursor(0,0);
        lcd.print(str_buff);

        switch(scr_mode)
            {
            case 0: sprintf(str_buff,"  %6lu",rad_max); break;  //peak rate
            case 1: sprintf(str_buff,"%02u:%02u:%02u",time_hrs,time_min,time_sec); break;
            }
        lcd.setCursor(8,1);
        lcd.print(str_buff);

        lcd.setCursor(0,1);
        if(alarm_disable) //если тревога запрещена
             {
             if(alarm_wait) //если ждем понижения фона
                 {
                 lcd.write(byte(3)); //значок "ожидание"
                 lcd.write(byte(3));
                 }
             else lcd.print("  ");
             }
        else
            {
            lcd.write(byte(0)); //значок "вкл. тревожная сигнализация"
            lcd.write(byte(1));
            }

        lcd.setCursor(3,1);
        if(buzz_disable) lcd.print("  ");
        else
            {
            lcd.write(byte(4));  //значок "вкл. звуковая индикация импульсов"
            lcd.write(byte(5));
            }
        }

    switch(check_keys())  //+++++++++++++++++++++  опрос кнопок  +++++++++++++++++++++++++++
        {
        case 1: //right key
            buzz_disable=!buzz_disable;
            scr=0;
            break;
        case 2: //up key //выбор режима
            if(++scr_mode>1)
            scr_mode=0;
            scr=0;
            break;
        case 3: //down key //сброс
            switch(scr_mode)
                {
                case 0: //сбрасываем фон и макс. фон
                    for(uint8_t i=0; i<GEIGER_TIME; i++) rad_buff[i]=0;
                    rad_back=0;
                    rad_max=0;
                    break;
                case 1:
                    rad_sum=0;
                    rad_dose=0;
                    time_hrs=time_min=time_sec=0;
                    break;
                }
            scr=0;
            break;
        case 4: //left key
            if(alarm) { if(alarm_disable==1) alarm_wait=!alarm_wait; }
            else alarm_disable=!alarm_disable;
            scr=0;
            break;
        case 5: //select key
            menu();
            break;
        }
    }


