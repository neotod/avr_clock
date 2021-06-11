// when you press interrupt keys for a setting, you must enter the 4 digit pin correctly (you can change the global variable "pin")

#include <mega32.h>
#include <alcd.h>
#include <stdbool.h>
#include <delay.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


// Voltage Reference: AREF pin
#define ADC_VREF_TYPE ((0<<REFS1) | (0<<REFS0) | (0<<ADLAR))

#define USER_BLOCK_MAX_TIME 15

#define KEYPAD_SQUARE 11
#define KEYPAD_STAR 10


void init();

void show_time();
void show_number_on_sevens(int number[], char segment_num);

void show_alarm(int x, int y);
void show_date_temp();

void set_temper_int();
void set_time_alarm_int();
void set_date_int();

void update_temper();
void update_temper_led();
void update_time_date();

void check_alarm();
void time_alarm_get_input(bool);
int login();
int keypad();

struct Time {
    int hour[2];
    int min[2];
    int sec[2];
} time;

struct Date {
    int year;
    int month;
    int day;
} date;

struct Temper {
    int min;
    int max;
    int current;
    bool negative;
} temper;

struct Alarm {
    bool on;
    struct Time atime;
} alarm;


char seg_numbers[] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // 7 segments are common cathod

volatile bool temper_int = false;
volatile bool time_int = false;
volatile bool date_int = false;

bool temper_buzz_alowed = false;
bool alarm_buzz = false;

bool user_blocked = false;
bool enable_login = true;

int buzz_numbers = 0;
int user_block_time = 0;
int pin = 1234;


// External Interrupt 0 handler: set temperature
interrupt [EXT_INT0] void ext_int0_isr(void) {
    temper_int = true;
}

// External Interrupt 1 handler: set time/alarm
interrupt [EXT_INT1] void ext_int1_isr(void) {
    if (alarm_buzz) {
        alarm_buzz = false;
        lcd_clear();
        delay_ms(50);
    }
    else
        time_int = true;
}

// External Interrupt 2 handler: set date
interrupt [EXT_INT2] void ext_int2_isr(void) {
    date_int = true;
}


// Timer 0 overflow interrupt handler: program regular routine
interrupt [TIM0_OVF] void timer0_ovf_isr(void)
{
    show_time();
    TCNT0=0x0F;
}

interrupt [TIM1_OVF] void timer1_isr(void) { // this will be called after 1 sec each time
    update_time_date();
    update_temper();
    delay_ms(10);
    update_temper_led();
    check_alarm();

    if (alarm_buzz) {
        buzz_numbers++;
        if (buzz_numbers > 60) {
            alarm_buzz = false;
            lcd_clear();
            delay_ms(10);
        }
        else {
            PORTD.6 = 1;
            delay_ms(20);
            PORTD.6 = 0;
        }
    }

    if (user_blocked) {
        user_block_time--;
        if (user_block_time == 0) {
            user_blocked = false;
        }
    }
    
    TCNT1H=0x7FFF >> 8;
    TCNT1L=0x7FFF & 0xff;
}


// Read the AD conversion result
unsigned int read_adc(unsigned char adc_input)
{
    ADMUX=adc_input | ADC_VREF_TYPE;
    // Delay needed for the stabilization of the ADC input voltage
    delay_us(10);
    // Start the AD conversion
    ADCSRA|=(1<<ADSC);
    // Wait for the AD conversion to complete
    while ((ADCSRA & (1<<ADIF))==0);
    ADCSRA|=(1<<ADIF);
    return ADCW;
}


void init() {
    // External Interrupt(s) initialization
    // INT0: On, Mode: Falling Edge
    // INT1: On, INT1 Mode: Falling Edge
    // INT2: On, INT2 Mode: Falling Edge
    GICR|=(1<<INT1) | (1<<INT0) | (1<<INT2);
    MCUCR=(1<<ISC11) | (0<<ISC10) | (1<<ISC01) | (0<<ISC00);
    MCUCSR=(0<<ISC2);
    GIFR=(1<<INTF1) | (1<<INTF0) | (1<<INTF2);
        
    //timer1 interrupt enalbe
    TIMSK = (1<<TOIE1) | (1<<TOIE0); // enable timer1, timer0 overflow interrupt
    
    // timer0 init
    TCCR0=(0<<WGM00) | (0<<COM01) | (0<<COM00) | (0<<WGM01) | (0<<CS02) | (1<<CS01) | (1<<CS00);
    TCNT0=0x0F;
                            
    // timer1 init    
    TCCR1A=(0<<COM1A1) | (0<<COM1A0) | (0<<COM1B1) | (0<<COM1B0) | (0<<WGM11) | (0<<WGM10);
    TCCR1B=(0<<ICNC1) | (0<<ICES1) | (0<<WGM13) | (0<<WGM12) | (1<<CS12) | (0<<CS11) | (0<<CS10);
    TCNT1H=0x7F;
    TCNT1L=0xFF;
    TCCR1A=(0<<COM1A1) | (0<<COM1A0) | (0<<COM1B1) | (0<<COM1B0) | (0<<WGM11) | (0<<WGM10);
    TCCR1B=(0<<ICNC1) | (0<<ICES1) | (0<<WGM13) | (0<<WGM12) | (1<<CS12) | (0<<CS11) | (0<<CS10);
    TCNT1H=0x7F;
    TCNT1L=0xFF;

    // ADC initialization
    // ADC Clock frequency: 500.000 kHz
    // ADC Voltage Reference: AREF pin
    // ADC Auto Trigger Source: ADC Stopped
    ADMUX=ADC_VREF_TYPE;
    ADCSRA=(1<<ADEN) | (0<<ADSC) | (0<<ADATE) | (0<<ADIF) | (0<<ADIE) | (1<<ADPS2) | (0<<ADPS1) | (0<<ADPS0);
    SFIOR=(0<<ADTS2) | (0<<ADTS1) | (0<<ADTS0); 
    

    // Alphanumeric LCD initialization:
    // RS - PORTC.4
    // RD - PORTC.6 (doesn't matter)
    // EN - PORTC.5
    // D4 - PORTC.0
    // D5 - PORTC.1
    // D6 - PORTC.2
    // D7 - PORTC.3
    // Characters/line: 16
    lcd_init(16);
    
    // pins initialization
    DDRA = 0b111111110; // A.0 to A.6: output, A.7 input
    DDRB = 0b11110000; // B.0 to B.2: input, B.3 to B.7 output
    DDRC = 0xFF; // C.0 to C.7: output
    DDRD = 0b11110011;
    
    PORTD = 0xFF;    
    PORTB = 0xFF;
    
    // default values init
    time.hour[0] = 1;
    time.hour[1] = 2;
    time.min[0] = 4;
    time.min[1] = 5;
    time.sec[0] = 0;
    time.sec[1] = 0;
    
    date.year = 1400;
    date.month = 3;
    date.day = 20;
    
    temper.min = 18;
    temper.max = 25;
    
    alarm.on = false; // off
    alarm.atime.hour[0] = 1;
    alarm.atime.hour[1] = 3;
    alarm.atime.min[0] = 3;
    alarm.atime.min[1] = 0;
    alarm.atime.sec[0] = 0;
    alarm.atime.sec[1] = 0;
    
    show_date_temp();
    show_alarm(0, 1);       
}

void show_date_temp() {
    char lcd_output[16];
    char temp[5];
    
    lcd_gotoxy(0, 0);

    itoa(date.year, temp);
    strcpy(lcd_output, temp);
    strcat(lcd_output, "/");
    
    itoa(date.month, temp);
    strcat(lcd_output, temp);
    strcat(lcd_output, "/");
    
    itoa(date.day, temp);
    strcat(lcd_output, temp);
    strcat(lcd_output, " ");
     
    // itoa(temper.current, temp);
    if (temper.negative)
        sprintf(temp, "-%d", temper.current);
    else
        sprintf(temp, "%d", temper.current);

    strcat(lcd_output, temp);
    strcat(lcd_output, "C");
    
    lcd_puts(lcd_output); 
}

void show_alarm(int x, int y) {
    char lcd_output[17];
    char temp[2];
    
    itoa(alarm.atime.hour[0], temp);
    strcpy(lcd_output, temp);
    itoa(alarm.atime.hour[1], temp);
    strcat(lcd_output, temp);
    
    strcat(lcd_output, ":");
    
    itoa(alarm.atime.min[0], temp);
    strcat(lcd_output, temp);
    itoa(alarm.atime.min[1], temp);
    strcat(lcd_output, temp);

    if (!alarm_buzz) {
        if (alarm.on) {
            strcat(lcd_output, ">ON");
        }
        else {
            strcat(lcd_output, ">OFF");
        }
    }
    else {
        if (alarm_buzz) {
            strcat(lcd_output, " btn2:Stop");
            delay_ms(20);
        }
    }
    
    if (x != -1 && y != -1)
        lcd_gotoxy(x, y);

    lcd_puts(lcd_output);
    delay_ms(20);

}

void show_time() {
    show_number_on_sevens(time.hour, 0);
    show_number_on_sevens(time.min, 1);
    show_number_on_sevens(time.sec, 2);
}

void show_number_on_sevens(int number[], char segment_num) {
    // number => [x, x], x=[0, 9]: number that you want to show it on your double 7 segment
    // segment_num => x, x=[0, 2] : number of the double 7 segment you want to show on something (we have 3 double 7 segments)
              
    PORTC &= (~(1<<PORTC7)); // enable ORs decoder
    
    PORTD = ((1<<4)|segment_num); // selecting from double 7segments, so our 7segment is ready to get data
    PORTA = seg_numbers[number[0]]; // sending first part of number
    
    delay_ms(1);
                                                    
    PORTD = ((1<<5)|segment_num); // selecting from double 7segments, so our 7segment is ready to get data    
    PORTA = seg_numbers[number[1]]; // sending second part of number
    
    delay_ms(1);
    
    PORTC |= (1<<PORTC7); // disabling ORs decoder
}

void update_time_date() {
    time.sec[1]++;
    
    if (time.sec[1] == 10) {
        time.sec[1] = 0;
        time.sec[0]++;
    }
        
    if (time.sec[0] == 6) { // 60 seconds
        time.sec[0] = 0;
        time.min[1]++;  
    }
    
    if (time.min[1] == 10) {
        time.min[1] = 0;
        time.min[0]++;
    }
    
    if (time.min[0] == 6) { // 60 minutes
        time.min[0] = 0;
        time.hour[1]++;
    }
    
    if (time.hour[1] == 10) {
        time.hour[1] = 0;
        time.hour[0]++;
    }

    if (time.hour[0] == 2 && time.hour[1] == 4) {
        time.hour[0] = 0;
        time.hour[0] = 0;

        date.day++;  
    }

    if (date.day == 30) {
        date.day = 0;
        date.month++;
    }

    if (date.month == 12) {
        date.month = 0;
        date.year++;
    }
}

int login() {
    // returns: 
    // 0 => login failed
    // 1 => logged in successfuly
    // 2 => success, but user will be going to main page
    // -1 => discard the whole thing

    bool change_pin    = false;
    bool take_new_pin  = false;
    int entered_inputs = 0;
    int pin_number     = 0;
    int kp_input       = -1;
    int attempts       = 3; // after 3 attempts of an invalid pin, you will be throwed out to the main page
    int lcd_x;
    char temp_number[5];
    char temp_output[17] = "";
    char temp[2];

    delay_ms(30);
    lcd_clear();
    delay_ms(10);
        
    lcd_gotoxy(0, 0);
    strcat(temp_output, "Pin: ----");

    lcd_puts(temp_output);
    delay_ms(20);
    
    lcd_gotoxy(0, 1);
    strcpy(temp_output, "*:Exit#:ChangPin");
    lcd_puts(temp_output);
    delay_ms(30);

    kp_input = -1;
    lcd_x = 5;
    while(1) {
        lcd_gotoxy(lcd_x, 0);
        lcd_puts("_");

        if (!take_new_pin) {
            strcpy(temp, "");
            sprintf(temp, "%d", attempts);

            lcd_gotoxy(15, 0);
            lcd_puts(temp);
            delay_ms(30);
        }
        
        delay_ms(30);
        kp_input = keypad();
        if (kp_input != -1) {
            if (0 <= kp_input && kp_input <= 9) {
                strcpy(temp, "");
                sprintf(temp, "%d", kp_input);

                delay_ms(10);
                
                lcd_gotoxy(lcd_x, 0);
                if (change_pin && take_new_pin) {
                    lcd_puts(temp);
                    delay_ms(30);
                }
                else {
                    lcd_puts("*");
                    delay_ms(30);
                }

                strcat(temp_number, temp);

                if (entered_inputs == 3) { // user entered 4 inputs (pin), so lets check it
                    pin_number = atoi(temp_number);
                    strcpy(temp_number, "");

                    if (change_pin && take_new_pin) { // taking new pin
                        pin = pin_number;

                        lcd_clear();
                        delay_ms(20);
                        lcd_gotoxy(0, 0);
                        lcd_puts("  Pin changed  ");
                        delay_ms(20);
                        lcd_gotoxy(0, 1);
                        lcd_puts("  Successfuly!  ");
                        delay_ms(300);

                        return 2; // success but user will be going to main page
                    }
                    
                    if (pin_number == pin) {
                        if (change_pin) { // go to next step => taking new pin
                            take_new_pin = true;
                            entered_inputs = 0;

                            lcd_clear();
                            delay_ms(10);

                            strcpy(temp_output, "NewPin: ----");
                            lcd_gotoxy(0, 0);
                            lcd_puts(temp_output);
                            delay_ms(10);

                            strcpy(temp_output, "*:Exit #:Reset");
                            lcd_gotoxy(0, 1);
                            lcd_puts(temp_output);
                            delay_ms(10);

                            lcd_x = 8;
                            continue;
                        }
                        else
                            return 1; // success in login
                    }
                    else { // wrong pin
                        attempts--;

                        lcd_gotoxy(0, 0);
                        strcpy(temp_output, "   Wrong Pin!   ");
                        lcd_puts(temp_output);
                        delay_ms(200);

                        if (attempts == 0) {
                            user_blocked = true;
                            user_block_time = USER_BLOCK_MAX_TIME;
                            return 0; // login failed and user will be blocked to enter settings for a while
                        }
                        
                        lcd_clear();
                        delay_ms(15);

                        lcd_gotoxy(0, 0);
                        if (change_pin) {
                            strcpy(temp_output, "CurntPin: ----");
                            lcd_puts(temp_output);
                            delay_ms(15);

                            lcd_gotoxy(0, 1);
                            strcpy(temp_output, "*:Exit #:Reset");
                            lcd_puts(temp_output);
                            delay_ms(15);

                            lcd_x = 10;
                        }
                        else {
                            strcpy(temp_output, "Pin: ----");
                            lcd_puts(temp_output);
                            delay_ms(15);

                            lcd_gotoxy(0, 1);
                            strcpy(temp_output, "*:Exit#:ChangPin");
                            lcd_puts(temp_output);
                            delay_ms(15);

                            lcd_x = 5;
                        }

                        entered_inputs = 0;
                        continue;
                    }
                }
                entered_inputs++;
                lcd_x++;
            }
            else if (kp_input == KEYPAD_STAR) {
                return -1; // discarding the whole thing
            }
            else if (kp_input == KEYPAD_SQUARE) {
                strcpy(temp_number, "");
                strcpy(temp, "");

                if (!change_pin) {
                    change_pin = true;

                    lcd_clear();
                    delay_ms(10);

                    lcd_gotoxy(0, 0);
                    strcpy(temp_output, "CurntPin: ----");
                    lcd_puts(temp_output);
                    delay_ms(15);

                    lcd_gotoxy(0, 1);
                    strcpy(temp_output, "*:Exit #:Reset");
                    lcd_puts(temp_output);
                    delay_ms(15);

                    lcd_x = 10;
                    attempts = 3;
                }
                else {
                    if (!take_new_pin) // user want to rewrite the pin
                        lcd_x = 10;
                    else
                        lcd_x = 8;

                    strcpy(temp_output, "----");
                        
                    lcd_gotoxy(lcd_x, 0);
                    lcd_puts(temp_output);
                    delay_ms(15);
                }

                entered_inputs = 0;
            }
        }         
    }    
}

void check_alarm() {
    bool on_time = true;

    if (time.hour[0] != alarm.atime.hour[0])
        on_time = false;
    else if (time.hour[1] != alarm.atime.hour[1])
        on_time = false;
    else if (time.min[0] != alarm.atime.min[0])
        on_time = false;
    else if (time.min[1] != alarm.atime.min[1])
        on_time = false;
    else if (time.sec[1] != alarm.atime.sec[1])
        on_time = false;
    else if (time.sec[0] != alarm.atime.sec[0])
        on_time = false;
        
    if (on_time) {
        buzz_numbers = 0;
        alarm_buzz = true;
    }
}

void time_alarm_get_input(bool alarm_input) {
    bool hour_part = true;
    int kp_input = -1;
    int lcd_x;
    int new_hour[2] = {0};
    int new_min[2] = {0};

    delay_ms(30);
    lcd_clear();
    delay_ms(20);
    
    if (!alarm_input) {
        lcd_puts("clock ");
        delay_ms(10);
    }
    else {
        lcd_puts("alarm ");
    }
        
    lcd_puts("--:--");
    delay_ms(10);
        
    lcd_gotoxy(0, 1);        
    lcd_puts("*:discard#:reset");
    delay_ms(10);
        
    delay_ms(100);
                        
    kp_input = -1;
    lcd_x = 6;        
    while(1) {
        lcd_gotoxy(lcd_x, 0);
        lcd_puts("_");

        delay_ms(50);
        kp_input = keypad();
        if (kp_input != -1) {
            if (0 <= kp_input && kp_input <= 9) {
                char temp[2];
                sprintf(temp, "%d", kp_input);
                    
                delay_ms(10);
                lcd_gotoxy(lcd_x, 0);
                lcd_puts(temp);                
                delay_ms(10);
                    
                if (hour_part) {
                    new_hour[lcd_x-6] = atoi(temp);
                    lcd_x++;
                        
                    if (lcd_x == 8) { // end of hour
                        if (new_hour[0] > 2 || (new_hour[0] == 2 && new_hour[1] != 4)) { // hour is bigger than 24
                            new_hour[0] = 0;
                            new_hour[1] = 0;
                            lcd_x = 6; // return to the begining of hour
                            lcd_gotoxy(6, 0);
                            lcd_puts("--");
                        }
                        else { // hour is valid
                            hour_part = false;
                            lcd_x++;
                        }                        
                    }                                        
                }
                else {
                    new_min[lcd_x-9] = atoi(temp);
                    lcd_x++;
                        
                    if (lcd_x == 11) { // end of minute
                        if (new_min[0] > 6 || (new_min[0] == 6 && new_min[1] != 0)) { // minute is bigger than 60
                            new_min[0] = 0;
                            new_min[1] = 0;
                            lcd_x = 9; // return to the begining of minute
                            lcd_gotoxy(9, 0);
                            lcd_puts("--");
                        }
                        else // minute is valid
                            break;
                    }                        
                        
                }            
            }
            else if (kp_input == KEYPAD_STAR) {
                return;
            }
            else if (kp_input == KEYPAD_SQUARE) {
                lcd_gotoxy(6, 0);
                lcd_puts("--:--");
                delay_ms(10);
                    
                lcd_x = 6;
                hour_part = true;                                                        
            }
        }                                                    
    }
        
    if (!alarm_input) {
        time.hour[0] = new_hour[0];
        time.hour[1] = new_hour[1];
            
        time.min[0] = new_min[0];
        time.min[1] = new_min[1];
            
        time.sec[0] = 0;
        time.sec[1] = 0;
    }
    else {
        alarm.atime.hour[0] = new_hour[0];
        alarm.atime.hour[1] = new_hour[1];
            
        alarm.atime.min[0] = new_min[0];
        alarm.atime.min[1] = new_min[1];
            
        alarm.atime.sec[0] = 0;
        alarm.atime.sec[1] = 0;
    }
        
    delay_ms(30);                    
    lcd_clear();
    delay_ms(50);
}

void set_time_alarm_int() {
    bool clock_set = true;
    int kp_input = -1;

    if (user_blocked) {
        char temp[7];
        delay_ms(10);
        lcd_clear();
        delay_ms(10);
        lcd_puts("Wait ");
        delay_ms(10);

        sprintf(temp, "%dsecs and", user_block_time);
        lcd_puts(temp);
        delay_ms(10);

        lcd_gotoxy(0, 1);
        lcd_puts("then try again");
        delay_ms(200);

        return;   
    }
    if (enable_login) {
        int result = login();
        
        if (result == 0 || result == -1 || result == 2)
            return;
    }
    
    delay_ms(20);
    lcd_clear();
    delay_ms(10);
    
    lcd_gotoxy(0, 0);
    lcd_puts("1:Clock 3:Alarm");
    delay_ms(10);
    
    lcd_gotoxy(0, 1);
    lcd_puts("*:Discard");
    delay_ms(10);
    
    while(1) {
        delay_ms(40);
        kp_input = keypad();
        if (kp_input != -1) {
            if (kp_input == 1) {
                clock_set = true;
                break;
            }
            else if (kp_input == 3) {
                clock_set = false;
                break;
            }
            else if (kp_input == KEYPAD_STAR)
                return;
        }                                                    
    }
    
    delay_ms(20);
    lcd_clear();
    delay_ms(10);
    
    lcd_gotoxy(0, 0);
    if (!clock_set) { // alarm setting part
        show_alarm(0, 0);
        
        if (alarm.on) {
            lcd_puts(" 1:OFF");
            delay_ms(10);
        }
        else {
            lcd_puts(" 1:ON");
            delay_ms(10);
        }
    
        lcd_gotoxy(0, 1);
        lcd_puts("*:Discard #:Set");
        delay_ms(10);

        
        kp_input = -1;        
        while(1) {
            delay_ms(50);
            kp_input = keypad();
            if (kp_input != -1) {
                if (kp_input == 1) {
                    if (alarm.on)
                        alarm.on = false;
                    else
                        alarm.on = true;
                        
                    show_alarm(0, 0);
                    
                    if (alarm.on) {
                        lcd_puts(" 1:OFF");
                        delay_ms(10);
                    }
                    else {
                        lcd_puts(" 1:ON");
                        delay_ms(10);
                    }
                }
                else if (kp_input == KEYPAD_STAR) {
                    return;
                }
                else if (kp_input == KEYPAD_SQUARE) {
                    break;
                }
            }                                                    
        }

        time_alarm_get_input(true);
                
    }
        
    else if (clock_set) { // clock setting part
        time_alarm_get_input(false);
    }

    lcd_gotoxy(0, 0);
    lcd_puts("Successfuly set!");
    delay_ms(200);
            
}

void set_temper_int() {    
    bool temper_min = true;
    int kp_input = -1;
    int number;
    int input_len = 0;
    char new_temper[4];

    if (user_blocked) {
        char temp[7];
        delay_ms(10);
        lcd_clear();
        delay_ms(10);
        lcd_puts("Wait ");
        delay_ms(10);

        sprintf(temp, "%dsecs and", user_block_time);
        lcd_puts(temp);
        delay_ms(10);

        lcd_gotoxy(0, 1);
        lcd_puts("then try again");
        delay_ms(200);

        return;   
    }
    if (enable_login) {
        int result = login();
        
        if (result == 0 || result == -1 || result == 2)
            return;
    }
    
    delay_ms(30);
    lcd_clear();
    delay_ms(10);
        
    lcd_gotoxy(0, 0);
    lcd_puts("min:");
    delay_ms(10);
    
    sprintf(new_temper, "%d", temper.min);
    lcd_puts(new_temper);
    delay_ms(10);
    
    lcd_gotoxy(4+strlen(new_temper), 0);    
    lcd_puts(" max:");
    delay_ms(20);
    
    sprintf(new_temper, "%d", temper.max);
    lcd_puts(new_temper);                 
    delay_ms(10);
    
    lcd_gotoxy(0, 1);
    lcd_puts("*:Discard #:Edit");
    delay_ms(10);
    
    while(1) {
        delay_ms(50);
        kp_input = keypad();
        if (kp_input != -1) {
            delay_ms(10);
            if (kp_input == KEYPAD_STAR)
                return;
            else if(kp_input == KEYPAD_SQUARE)
                break;
        }                                                    
    }
    
    strcpy(new_temper, "");
    
    delay_ms(10);                    
    lcd_clear();
    delay_ms(10);
        
    lcd_gotoxy(0, 0);
    lcd_puts("0:Min, 1:Max");
    lcd_gotoxy(0, 1);
    lcd_puts("*:Discard");
    
    while(1) {
        delay_ms(50);
        kp_input = keypad();
        if (kp_input != -1) {
            if (kp_input == 0) {
                temper_min = true;
                break;
            }
            else if (kp_input == 1) {
                temper_min = false;
                break;
            }
            else if (kp_input == KEYPAD_STAR) {
                return;
            }
        }                                                    
    }
            
    delay_ms(10);                    
    lcd_clear();
    delay_ms(10);
        
    lcd_gotoxy(0, 0);
    if (temper_min) {
        lcd_puts("Min: ");
        delay_ms(10);
    }
    else {
        lcd_puts("Max: ");
        delay_ms(10);
    }
        
    lcd_gotoxy(0, 1);
    delay_ms(10);
    lcd_puts("*:Discard #:Save");
    
    delay_ms(10);
    
    kp_input = -1;                
    while(1) {
        delay_ms(50);
        kp_input = keypad();
        if (kp_input != -1) {
            if (0 <= kp_input && kp_input <= 9) {
                char temp[2];
                sprintf(temp, "%d", kp_input);
                strcat(new_temper, temp);
                
                delay_ms(10);
                lcd_gotoxy(6, 0);
                lcd_puts(new_temper);                
                delay_ms(10);
                
                kp_input = -1;
                input_len++;
                
                delay_ms(10);
                
                if (input_len == 3) {
                    break;                       
                }            
            }
            else if (kp_input == KEYPAD_STAR) {
                return;
            }
            else if (kp_input == KEYPAD_SQUARE) {
                break;                                    
            }
        }                                                    
    }
    
    number = atoi(new_temper);
    if (temper_min) { // checking errors of input and if there is no error then save it
        if (number >= temper.max) {
            delay_ms(20);                    
            lcd_clear();
            delay_ms(10);
        
            lcd_gotoxy(0, 0);
            lcd_puts("Min can't be");
            delay_ms(10);
            lcd_gotoxy(0, 1);
            lcd_puts("bigger than max!");
            delay_ms(300);
        }
        else {
            temper.min = number;
            
            delay_ms(20);                    
            lcd_clear();
            delay_ms(10);
            
            lcd_gotoxy(0, 0);
            lcd_puts("Successfuly set!");
            delay_ms(200);
        }
    }
    else {                  
        if (number <= temper.min) {
            delay_ms(20);                    
            lcd_clear();
            delay_ms(10);
        
            lcd_gotoxy(0, 0);
            lcd_puts("Max can't be");
            delay_ms(20);
            lcd_gotoxy(0, 1);
            lcd_puts("smaller than min!");
            delay_ms(300);
        }
        else if (number > 100) {
            delay_ms(30);                    
            lcd_clear();
            delay_ms(30);
        
            lcd_gotoxy(0, 0);
            lcd_puts("Max can't be");
            delay_ms(20);
            lcd_gotoxy(0, 1);
            lcd_puts("bigger than 100");
            delay_ms(300);
        }
        else {
            temper.max = number; 
                        
            delay_ms(30);
            lcd_clear();
            delay_ms(30);
            
            lcd_gotoxy(0, 0);
            lcd_puts("Successfuly set!");
            delay_ms(200);
        }
    }
}

void set_date_int() {
    int kp_input = -1;
    int new_year, new_month, new_day;
    int lcd_x;
    char temp_number[5];
    char temp[2];

    if (user_blocked) {
        char temp[7];
        delay_ms(10);
        lcd_clear();
        delay_ms(10);
        lcd_puts("Wait ");
        delay_ms(10);

        sprintf(temp, "%dsecs and", user_block_time);
        lcd_puts(temp);
        delay_ms(10);

        lcd_gotoxy(0, 1);
        lcd_puts("then try again");
        delay_ms(200);

        return;   
    }
    if (enable_login) {
        int result = login();
        
        if (result == 0 || result == -1 || result == 2)
            return;
    }

    delay_ms(30);
    lcd_clear();
    delay_ms(10);
        
    lcd_gotoxy(0, 0);
    lcd_puts("date: ");
    delay_ms(10);

    lcd_puts("----/--/--"); // year: 6-9, month: 11-12, day: 14-15
    delay_ms(10);
    
    lcd_gotoxy(0, 1);
    lcd_puts("*:Discard#:Reset");
    delay_ms(10);

    kp_input = -1;
    lcd_x = 6;
    while(1) {
        lcd_gotoxy(lcd_x, 0);
        lcd_puts("_");
        
        delay_ms(50);
        kp_input = keypad();
        if (kp_input != -1) {
            if (0 <= kp_input && kp_input <= 9) {
                sprintf(temp, "%d", kp_input);

                delay_ms(10);
                lcd_gotoxy(lcd_x, 0);
                lcd_puts(temp);                
                delay_ms(10);

                strcat(temp_number, temp);

                if (lcd_x == 9) { // end of year part, year is always valid
                    new_year = atoi(temp_number);
                    strcpy(temp_number, "");
                    lcd_x++;
                }
                else if (lcd_x == 12) { // end of month part
                    new_month = atoi(temp_number);
                    strcpy(temp_number, "");

                    if (new_month > 12) {
                        new_month = 0;
                        lcd_x = 11;

                        lcd_gotoxy(lcd_x, 0);
                        lcd_puts("--");
                        delay_ms(10);
                        continue;
                    }
                    else { // day is valid
                        lcd_x++;
                    }
                }
                else if (lcd_x == 15) { // end of day part
                    new_day = atoi(temp_number);
                    strcpy(temp_number, "");

                    if (new_day > 30) {
                        new_day = 0;
                        lcd_x = 14;

                        lcd_gotoxy(lcd_x, 0);
                        lcd_puts("--");
                        delay_ms(10);
                        continue;
                    }
                    else { // day is valid
                        break;
                    }
                }

                lcd_x++;
            }
            else if (kp_input == KEYPAD_STAR) {
                return;
            }
            else if (kp_input == KEYPAD_SQUARE) {
                strcpy(temp_number, "");
                strcpy(temp, "");

                new_year = 0;
                new_month = 0;
                new_day = 0;

                lcd_gotoxy(6, 0);
                lcd_puts("----/--/--");
                delay_ms(10);

                lcd_x = 6;
            }
        }                                                    
    }

    date.year = new_year;
    date.month = new_month;
    date.day = new_day;

    delay_ms(30);
    lcd_clear();
    delay_ms(30);
    
    lcd_gotoxy(0, 0);
    lcd_puts("Successfuly set!");
    delay_ms(200);
}

void update_temper() {
    int input;
    input = read_adc(7);
    input = input*4.88/10;

    if (input < 0) {
        temper.negative = true;
        input *= -1;
    }
    else
        temper.negative = false;
    
    temper.current = input;
}

void update_temper_led() {
    bool min_or_max = false;

    PORTC &= (~(1<<PORTC6)); // enable leds decode
    
    delay_ms(10);

    if (temper.current < temper.min) {
        PORTD.0 = 0;
        PORTD.1 = 0;
        
        min_or_max = true;
    }
    else if (temper.current > temper.max) {
        PORTD.0 = 0;
        PORTD.1 = 1;
        
        min_or_max = true;
    }
    else {
        PORTD.0 = 1;
        PORTD.1 = 0;

        temper_buzz_alowed = true;
        min_or_max = false;
    }

    if (temper_buzz_alowed && min_or_max) {
        PORTD.6 = 1;
        delay_ms(100);
        PORTD.6 = 0;

        temper_buzz_alowed = false;
    }
    
    delay_ms(10);    
    PORTC |= (1<<PORTC6); // disable leds and NANDs decoder
}

int keypad()
{
    int i = -1;
    
    PORTB.4 = 0;
    if (PINB.0 == 0) { delay_ms(10); i = 1; }
    if (PINB.1 == 0) { delay_ms(10); i = 2; }
    if (PINB.3 == 0) { delay_ms(10); i = 3; }
    PORTB.4 = 1;
          
    PORTB.5 = 0;
    if (PINB.0 == 0) { delay_ms(10); i = 4; }
    if (PINB.1 == 0) { delay_ms(10); i = 5; }
    if (PINB.3 == 0) { delay_ms(10); i = 6; }
    PORTB.5 = 1;
          
    PORTB.6 = 0;
    if (PINB.0 == 0) { delay_ms(10); i = 7; }
    if (PINB.1 == 0) { delay_ms(10); i = 8; }
    if (PINB.3 == 0) { delay_ms(10); i = 9; }
    PORTB.6 = 1;
          
    PORTB.7 = 0; 
    if (PINB.0 == 0) { delay_ms(10); i = KEYPAD_STAR; }
    if (PINB.1 == 0) { delay_ms(10); i = 0; }
    if (PINB.3 == 0) { delay_ms(10); i = KEYPAD_SQUARE; }
    PORTB.7 = 1;
          
    return i;
}


void main(void) {
    init();
    // Global enable interrupts
    
    #asm("sei")

    while (1) {
        show_date_temp();
        delay_ms(50);
        
        show_alarm(0, 1);
        delay_ms(50);
        
        if (temper_int == true) {
            set_temper_int();
            temper_int = false;
                                
            lcd_clear();
            delay_ms(30);
        }
        else if (time_int == true) {
            set_time_alarm_int();
            time_int = false;
                                
            lcd_clear();
            delay_ms(30);
        }
        else if (date_int == true) {
            set_date_int();
            date_int = false;
                                
            lcd_clear();
            delay_ms(30);
        }
    }
}
