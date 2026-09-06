/********************************** (C) COPYRIGHT *******************************
 * File Name          : sss_i2c_hwsl_lib_v1.h
 * Author             : vantr
 * Description        : Библиотека аппаратного ведомого для шины I2C
 *********************************************************************************
 * Copyright (c) 2026 Vantr Universal Co., Ltd.
 *******************************************************************************/
/* ----------------------------------------------------------------------------
// Описываем пакет данных так, как НАМ удобно
typedef struct {
   float temperature;   // 4 байта
   uint16_t adc_ch1;    // 2 байта
   uint16_t adc_ch2;    // 2 байта
   uint8_t status;      // 1 байт
} __attribute__((packed)) SensorData_t; // Сжимаем без пропусков памяти

SensorData_t my_sensor; // Создаем живую переменную-структуру

И при старте программы вы один раз привязываете эту структуру к вашей библиотеке I2C Slave:

int main(void) {
    // ... инициализация чипа ...

    // Забиваем структуру стартовыми данными
    my_sensor.temperature = 24.5f;
    my_sensor.adc_ch1 = 1024;
    my_sensor.adc_ch2 = 2048;
    my_sensor.status = 0x01;

    // 🌟 Привязываем структуру к I2C Slave библиотеке!
    // Передаем адрес структуры и её размер. Библиотека сама поймет, что там 9 байт.
    I2C_Slave_Link_Data(&my_sensor, sizeof(my_sensor));

    while(1) {
        // В основном цикле вы просто обновляете данные внутри структуры:
        my_sensor.temperature = Read_Internal_Die_Temp();
        my_sensor.adc_ch1 = Get_ADC_Raw(1);

        // Больше НИКАКИХ буферов, сбросов и ручной нарезки!
        // Мастер в любой момент дернет шину, прерывание залезет прямо в эту структуру
        // и отдаст Мастеру самые свежие, актуальные байты!
    }
}
*/
// -----------------------------------------------------------------------------
#include "sss_i2c_hwsl_lib_v1.h"
#include "ch32v00x.h"
// -----------------------------------------------------------------------------
// Внутренние переменные библиотеки (скрыты от main.c)
static volatile uint8_t *i2c_sl_reg_ptr = NULL;  // Универсальный указатель на данные
static volatile uint16_t i2c_sl_reg_size = 0;    // Размер привязанных данных в байтах
// Адрес чтения из ведомого (адрес внешней памяти?)
static volatile Int32x i2c_read_addr32 = {0};    // Адрес чтения/записи из/в структуру
static volatile uint8_t i2c_read_addr32_ptr = 0; // Указатель для операций с адресом
// -----------------------------------------------------------------------------
#pragma region Logs
// -----------------------------------------------------------------------------
#ifdef I2C_SLAVE_LOG_ENABLED
#define LOG_SIZE 32

    uint8_t debug_log[LOG_SIZE];
volatile uint8_t log_idx = 0;

// Функция для записи коротких меток (вместо printf)
void log_event (uint8_t event_id) {

    if (log_idx < LOG_SIZE) {
        debug_log[log_idx++] = event_id;
    }
}
// -----------------------------------------------------------------------------
uint8_t log_getSize() { return log_idx; }
// -----------------------------------------------------------------------------
uint8_t log_getItem(uint8_t index) { return debug_log[index]; }
// -----------------------------------------------------------------------------
void log_reset() {

    log_idx = 0;
    for (int i_temp = 0; i_temp < LOG_SIZE; i_temp++) { debug_log[i_temp] = 0; }
}
#endif
// -----------------------------------------------------------------------------
#pragma endregion
// -----------------------------------------------------------------------------
// Реализация функции привязки памяти
void I2C_Slave_Link_Data (void *data_ptr, uint16_t data_size) {
    // Принудительно приводим любой указатель (void*) к байтовому (uint8_t*),
    // чтобы прерывание могло монотонно шагать по нему байт за байтом
    i2c_sl_reg_ptr = (volatile uint8_t *)data_ptr;
    i2c_sl_reg_size = data_size;
    i2c_read_addr32.ivalue = 0;
}
// -----------------------------------------------------------------------------
// Накопленный 32-битный адрес, принятый от мастера или UINT32_MAX
uint32_t I2C_Slave_READ_ADDR() {

    return i2c_read_addr32.ivalue;
}
// -----------------------------------------------------------------------------
// Накопленный 32-битный адрес, принятый от мастера был завершен?
uint32_t I2C_Slave_READ_ADDR_IsComplete() {

    return (i2c_read_addr32_ptr == 5);
}
// -----------------------------------------------------------------------------
// Установка флага Ack
void i2c_set_Ack() {

    I2C1->CTLR1 |= I2C_CTLR1_ACK;
}
// -----------------------------------------------------------------------------
// Установка флага NAck
void i2c_set_NAck() {

    I2C1->CTLR1 &= ~I2C_CTLR1_ACK;
}
// -----------------------------------------------------------------------------
// Инициализация режима I2C Slave
// address:     наш 7-битный адрес на шине
// bus_speed:   скорость шины
void i2c_slave_Init(uint16_t address, uint32_t bus_speed) {

    GPIO_InitTypeDef GPIO_InitStructure = {0};
    I2C_InitTypeDef I2C_InitStructure = {0};

    // 1. Тактирование
    RCC_APB2PeriphClockCmd (I2C_SDA_PORT.rcc, ENABLE);
    RCC_APB2PeriphClockCmd (I2C_SCL_PORT.rcc, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    //GPIO_PinRemapConfig (GPIO_PartialRemap_I2C1, ENABLE);
    //GPIO_PinRemapConfig(GPIO_FullRemap_I2C1, ENABLE);

    // 2. Настройка пинов (PC1 - SDA)
    GPIO_InitStructure.GPIO_Pin = I2C_SDA_PORT.pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD; // Альтернативная функция, открытый сток
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init (I2C_SDA_PORT.port, &GPIO_InitStructure);

    // 2. Настройка пинов (PC2 - SCL)
    GPIO_InitStructure.GPIO_Pin = I2C_SCL_PORT.pin;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_OD;  // Альтернативная функция, открытый сток
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_30MHz;
    GPIO_Init (I2C_SCL_PORT.port, &GPIO_InitStructure);

    // 3. Конфигурация I2C Slave
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_OwnAddress1 = address;       // Ваш 7-битный адрес
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_InitStructure.I2C_ClockSpeed = bus_speed;      // Частота шины
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_Init(I2C1, &I2C_InitStructure);

    NVIC_InitTypeDef NVIC_InitStructure = {0};

    // 4. Настройка прерывания по СОБЫТИЯМ (Event)
    NVIC_InitStructure.NVIC_IRQChannel = I2C1_EV_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // Приоритет 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 5. Настройка прерывания по ОШИБКАМ (Error)
    NVIC_InitStructure.NVIC_IRQChannel = I2C1_ER_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // Ошибки обычно приоритетнее
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 6. Включаем прерывания по событиям (EVT) и ошибкам (ERR)
    I2C_ITConfig(I2C1, I2C_IT_EVT | I2C_IT_ERR, ENABLE);

    // Если нужен прием/передача данных через прерывания (TXE/RXNE), также включите IT_BUF
    I2C_ITConfig(I2C1, I2C_IT_BUF, ENABLE);

    // 7. Включение периферии
    I2C_Cmd(I2C1, ENABLE);

    I2C_AcknowledgeConfig (I2C1, ENABLE);
    i2c_set_Ack();
}
// -----------------------------------------------------------------------------
// Атрибут для быстрой обработки прерываний на RISC-V (WCH)
void I2C1_EV_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

// Прерывание для обработки событий шины I2C
void I2C1_EV_IRQHandler (void) {

    // Log: d5 a1 d5 88 d5 88 d5 ff d5 ee d5 ee ef

    /*  Если вы видите d5 a1 ... ff — запись работает.
        Если вы видите ee ee ef — чтение работает.
        Появление ef в конце чтения — это нормальное поведение
        аппаратного I2C в режиме ведомого, сообщающее, что мастер
        больше не хочет данных.
    */

    uint16_t s1 = I2C1->STAR1;
    uint16_t s2 = I2C1->STAR2;

    // Превентивный сброс аппаратных ошибок шины
    if (s1 & (I2C_STAR1_BERR | I2C_STAR1_OVR | I2C_STAR1_AF)) {
        I2C1->STAR1 &= ~(I2C_STAR1_BERR | I2C_STAR1_OVR | I2C_STAR1_AF);
    }

#ifdef I2C_SLAVE_LOG_ENABLED
    if (log_idx < LOG_SIZE)
        debug_log[log_idx++] = 0xD5;
#endif
    // ------------------------------------------------------------------------
    // Шаг 1: Обработка совпадения адреса (ADDR) - одинаково для всех назначений драйвера
    if (s1 & I2C_STAR1_ADDR) {
        if (s2 & I2C_STAR2_TRA) {
            // Мастер хочет ЧИТАТЬ из нас (TRA=1)

            if (i2c_read_addr32_ptr != 5) {

                // Имитация установленного адреса
                i2c_read_addr32_ptr = 5;

                i2c_read_addr32.ivalue = 0;
            }

            // Если память успешно привязана и размер корректен
            if (i2c_sl_reg_ptr != NULL && i2c_sl_reg_size > 0 && i2c_read_addr32.ivalue < i2c_sl_reg_size) {

                uint8_t first_byte = i2c_sl_reg_ptr[i2c_read_addr32.ivalue++];
                I2C1->DATAR = first_byte;

#ifdef I2C_SLAVE_LOG_ENABLED
                if (log_idx < LOG_SIZE) {
                    debug_log[log_idx++] = 0xEE;
                    debug_log[log_idx++] = first_byte;  // Запишет реальный первый байт в ваш лог
                }
#endif
            } else {
                I2C1->DATAR = 0x00;  // Страховка: если ничего не привязали — шлем нули
            }
        } else {
            // Мастер хочет ЗАПИСЫВАТЬ в нас (TRA=0)
#ifdef I2C_SLAVE_LOG_ENABLED
            if (log_idx < LOG_SIZE)
                debug_log[log_idx++] = 0xA1;
#endif
            // Указатель в ноль. Это важно!
            i2c_read_addr32_ptr = 0;

            // Холостой пинок аппаратному автомату шины CH32 для старта приема данных
            volatile uint8_t dummy __attribute__ ((unused)) = I2C1->DATAR;
        }
        return;
    }
    // ------------------------------------------------------------------------
    // Шаг 2: Прием последующих байтов от Мастера (RXNE) — Пакетная Запись в Слейв
    if (s1 & I2C_STAR1_RXNE) {
        
        uint8_t received_data = I2C1->DATAR;  // Чтение DATAR автоматически отпускает SCL

#ifdef I2C_SLAVE_LOG_ENABLED
        if (log_idx < LOG_SIZE) {
            debug_log[log_idx++] = 0x88;
            debug_log[log_idx++] = received_data;  // Логируем принятый байт
        }
#endif

        // Принят первый байт - это команда!
        if (i2c_read_addr32_ptr == 0) {

            if (received_data == I2C_SLAVE_ADDR32) {

                i2c_read_addr32_ptr++;
            }

            if (received_data == I2C_Slave_READ) {

                // Иммитация полностью принятого адреса
                i2c_read_addr32_ptr = 5;
            }

            return;
        }

        // Если память привязана...
        if (i2c_sl_reg_ptr != NULL) {

            //printf ("byte_read: 0x%02x\r\n", (unsigned int)received_data);

            // Продолжается прием адреса
            if (i2c_read_addr32_ptr < 5) {
                // 🌟 МАГИЯ ЗАПИСИ: Порционный прием 32-битного адреса чтения
                i2c_read_addr32.cvalue[i2c_read_addr32_ptr++ - 1] = received_data;

                if (i2c_read_addr32_ptr == 5) {

                    // 32-битный адрес передан полностью!
                    //printf ("addr_read: 0x%08x\r\n", (unsigned int)i2c_read_addr32.ivalue);
                }

                return;
            }

            // Адрес был принят, пишем в массив данных
            if (i2c_read_addr32_ptr == 5) {

                // 🌟 МАГИЯ ЗАПИСИ: Запись в массив
                // Если память привязана и мы не вышли за лимит размера структуры
                if (i2c_sl_reg_ptr != NULL && i2c_read_addr32.ivalue < i2c_sl_reg_size) {

                    i2c_sl_reg_ptr[i2c_read_addr32.ivalue++] = received_data;
                }

                return;
            }
        }

        return;
    }
    // ------------------------------------------------------------------------
    // Шаг 3: Передача последующих байтов Мастеру (TXE/BTF) — Пакетное Чтение из Слейва
    if ((s2 & I2C_STAR2_TRA) && !(s1 & I2C_STAR1_ADDR)) {
        if (s1 & (I2C_STAR1_TXE | I2C_STAR1_BTF)) {

            // Если адрес не был установлен...
            if (i2c_read_addr32_ptr != 5) { 

                i2c_read_addr32.ivalue = 0;

                // Иммитация полностью принятого адреса
                i2c_read_addr32_ptr = 5;
            }

            // Если память привязана и мы не вышли за лимит размера структуры
            if (i2c_sl_reg_ptr != NULL && i2c_read_addr32.ivalue < i2c_sl_reg_size) {

                // 🌟 МАГИЯ ПЕРЕДАЧИ: Вытаскиваем байт прямо из живой памяти структуры
                uint8_t data_tmp = i2c_sl_reg_ptr[i2c_read_addr32.ivalue++];
                I2C1->DATAR = data_tmp;
#ifdef I2C_SLAVE_LOG_ENABLED
                if (log_idx < LOG_SIZE) {
                    debug_log[log_idx++] = 0xED;
                    debug_log[log_idx++] = data_tmp;  // Логируем отправленный байт
                }
#endif
            } else {
                I2C1->DATAR = 0x00;  // Данные структуры закончились, а Мастер всё просит
            }
        }
        return;
    }
    // ------------------------------------------------------------------------
    // Шаг 4: Очистка STOPF (Мастер завершил запись)
    if (s1 & I2C_STAR1_STOPF) {
        I2C1->CTLR1 = I2C1->CTLR1;  // Чистый сброс по документации
#ifdef I2C_SLAVE_LOG_ENABLED
        if (log_idx < LOG_SIZE)
            debug_log[log_idx++] = 0xFF;
#endif
        return;
    }
    // ------------------------------------------------------------------------
    // Шаг 5: Мастер прислал NACK или произошел сбой ACK
    if (s1 & I2C_STAR1_AF) {
        // ПРАВИЛЬНЫЙ СБРОС ДЛЯ CH32:
        // Очищаем бит AF (и превентивно BERR/OVR, чтобы шину не заклинило наглухо)
        I2C1->STAR1 = (uint16_t)~(I2C_STAR1_AF | I2C_STAR1_BERR | I2C_STAR1_OVR);
#ifdef I2C_SLAVE_LOG_ENABLED
        if (log_idx < LOG_SIZE)
            debug_log[log_idx++] = 0xAF;
#endif
        return;  // Обязательно выходим, так как транзакция чтения Мастером завершена!
    }
    // ------------------------------------------------------------------------
}
// ----------------------------------------------------------------------------
void I2C1_ER_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
// Прерывание для обработки ошибок передачи шины I2C
void I2C1_ER_IRQHandler (void) {
#ifdef I2C_SLAVE_LOG_ENABLED
    if (log_idx < LOG_SIZE)
        debug_log[log_idx++] = 0xEF;
#endif
    if (I2C1->STAR1 & I2C_STAR1_AF) {
        I2C1->STAR1 &= ~I2C_STAR1_AF;  // Сброс AF. Это нормальный конец Master Read.
    }

    // Сброс других возможных ошибок
    I2C1->STAR1 &= ~(I2C_STAR1_BERR | I2C_STAR1_ARLO | I2C_STAR1_OVR);
}
// -----------------------------------------------------------------------------