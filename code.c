#include "stm32f4xx.h"

#define TRIG_PIN       6
#define ECHO_PIN       7
#define RED_LED_PIN    10
#define GREEN_LED_PIN  11

volatile unsigned int adc_value = 0;
int angle = -60;
unsigned int direction = 1;
unsigned int distance = 0;
unsigned int threshold = 10;

/* Approximate microsecond delay for a 16 MHz clock */
void delay_us(unsigned int us)
{
    while (us--)
    {
        SysTick->LOAD = 16 - 1;
        SysTick->VAL = 0;
        SysTick->CTRL = 5;

        while (!(SysTick->CTRL & (1U << 16)));

        SysTick->CTRL = 0;
    }
}

void delay_ms(unsigned int ms)
{
    while (ms--)
        delay_us(1000);
}

/* USART2 transmit */
void USART2_Init(void)
{
    RCC->AHB1ENR |= (1U << 0);
    RCC->APB1ENR |= (1U << 17);

    /* PA2 and PA3 alternate function mode */
    GPIOA->MODER &= ~((3U << 4) | (3U << 6));
    GPIOA->MODER |=  ((2U << 4) | (2U << 6));

    GPIOA->AFR[0] &= ~((15U << 8) | (15U << 12));
    GPIOA->AFR[0] |=  ((7U << 8) | (7U << 12));

    /* USART2: 9600 baud, assuming 16 MHz PCLK1 */
    USART2->BRR = 0x0683;
    USART2->CR1 = (1U << 3) | (1U << 2) | (1U << 13);
}

void USART2_SendChar(char c)
{
    while (!(USART2->SR & (1U << 7)));
    USART2->DR = c;
}

void USART2_SendString(char *str)
{
    while (*str)
        USART2_SendChar(*str++);
}

void USART2_SendNumber(unsigned int num)
{
    char buffer[10];
    int i = 0;

    if (num == 0)
    {
        USART2_SendChar('0');
        return;
    }

    while (num > 0)
    {
        buffer[i++] = (num % 10) + '0';
        num /= 10;
    }

    while (i > 0)
        USART2_SendChar(buffer[--i]);
}

void USART2_SendSignedNumber(int num)
{
    if (num < 0)
    {
        USART2_SendChar('-');
        num = -num;
    }

    USART2_SendNumber((unsigned int)num);
}

/* ADC1: PA0, channel 0 */
void ADC1_Init(void)
{
    RCC->AHB1ENR |= (1U << 0);
    RCC->APB2ENR |= (1U << 8);

    GPIOA->MODER |= (3U << 0);

    ADC1->CR2 = 0;
    ADC1->SQR3 = 0;
    ADC1->SMPR2 |= (7U << 0);
    ADC1->CR2 |= (1U << 0);
}

unsigned int ADC1_Read(void)
{
    ADC1->CR2 |= (1U << 30);

    while (!(ADC1->SR & (1U << 1)));

    return ADC1->DR;
}

/* TIM1 PWM on PA8 for servo */
void Servo_Init(void)
{
    RCC->AHB1ENR |= (1U << 0);
    RCC->APB2ENR |= (1U << 0);

    GPIOA->MODER &= ~(3U << 16);
    GPIOA->MODER |=  (2U << 16);

    GPIOA->AFR[1] &= ~(15U << 0);
    GPIOA->AFR[1] |=  (1U << 0);

    TIM1->PSC = 15;
    TIM1->ARR = 19999;
    TIM1->CCR1 = 1500;

    TIM1->CCMR1 = (6U << 4) | (1U << 3);
    TIM1->CCER |= (1U << 0);
    TIM1->BDTR |= (1U << 15);
    TIM1->CR1 |= (1U << 0);
}

void Servo_SetAngle(int deg)
{
    if (deg > 60)
        deg = 60;
    else if (deg < -60)
        deg = -60;

    /* -60 to +60 degrees maps to approximately 1 ms to 2 ms */
    TIM1->CCR1 = 1500 + ((deg * 500) / 60);
}

/* HC-SR04 trigger and echo on PA6 and PA7 */
void Ultrasonic_Init(void)
{
    RCC->AHB1ENR |= (1U << 0);

    GPIOA->MODER &= ~((3U << 12) | (3U << 14));
    GPIOA->MODER |= (1U << 12);

    GPIOA->OTYPER &= ~(1U << TRIG_PIN);
    GPIOA->PUPDR &= ~(3U << 14);
}

unsigned int Read_Distance(void)
{
    unsigned int count = 0;
    unsigned int timeout = 30000;

    GPIOA->BSRR = (1U << (TRIG_PIN + 16));
    delay_us(2);
    GPIOA->BSRR = (1U << TRIG_PIN);
    delay_us(10);
    GPIOA->BSRR = (1U << (TRIG_PIN + 16));

    timeout = 30000;

    while (!(GPIOA->IDR & (1U << ECHO_PIN)))
    {
        if (--timeout == 0)
            return 0;
    }

    while (GPIOA->IDR & (1U << ECHO_PIN))
    {
        count++;
        delay_us(1);

        if (count >= 30000)
            return 0;
    }

    return count / 58;
}

/* LEDs: PA10 red, PA11 green */
void LED_Init(void)
{
    RCC->AHB1ENR |= (1U << 0);

    /* Keep both active-low LEDs OFF while configuring the pins */
    GPIOA->BSRR = (1U << RED_LED_PIN) |
                  (1U << GREEN_LED_PIN);

    GPIOA->MODER &= ~((3U << 20) | (3U << 22));
    GPIOA->MODER |=  ((1U << 20) | (1U << 22));
}

void Update_LEDs(unsigned int detected)
{
    if (detected)
    {
        /* Active-low: red ON, green OFF */
        GPIOA->BSRR = (1U << GREEN_LED_PIN) |
                      (1U << (RED_LED_PIN + 16));
    }
    else
    {
        /* Active-low: green ON, red OFF */
        GPIOA->BSRR = (1U << RED_LED_PIN) |
                      (1U << (GREEN_LED_PIN + 16));
    }
}

int main(void)
{
    unsigned int detected;

    USART2_Init();
    ADC1_Init();
    Servo_Init();
    Ultrasonic_Init();
    LED_Init();

    /* No object at startup: green ON, red OFF */
    Update_LEDs(0);

    while (1)
    {
        /* Threshold range: 2 to 50 cm */
        adc_value = ADC1_Read();
        threshold = 2 + ((adc_value * 48U) / 4095U);

        Servo_SetAngle(angle);
        delay_ms(30);

        distance = Read_Distance();

        detected = (distance > 0 && distance <= threshold);

        Update_LEDs(detected);

        USART2_SendString("Angle: ");
        USART2_SendSignedNumber(angle);

        USART2_SendString(" deg, Distance: ");
        USART2_SendNumber(distance);

        USART2_SendString(" cm, Threshold: ");
        USART2_SendNumber(threshold);

        USART2_SendString(" cm, Status: ");
        USART2_SendString(detected ? "OBJECT DETECTED\r\n"
                                   : "CLEAR / NO ECHO\r\n");

        delay_ms(20);

        /* Sweep between -60 and +60 degrees */
        if (direction)
        {
            if (angle >= 60)
                direction = 0;
            else
                angle += 5;
        }
        else
        {
            if (angle <= -60)
                direction = 1;
            else
                angle -= 5;
        }
    }
}