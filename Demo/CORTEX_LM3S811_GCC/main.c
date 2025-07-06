/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include <string.h>

/* Environment includes. */
#include "DriverLib.h"

/*====================== UART & Timer ========================*/
#define mainBAUD_RATE               115200     // Baud rate for serial communication
#define TIMER_LOAD_VALUE            1500       // Initial load value for the timer

/*====================== Booleans ========================*/
#define TRUE                        1          // Boolean value for true

/*====================== Timing Delays ========================*/
#define DELAY_10_MS                 10         // Delay of 10 milliseconds
#define DELAY_100_MS                100        // Delay of 100 milliseconds
#define DELAY_5_SECONDS             5000       // Delay of 5 seconds

/*====================== Buffer Sizes ========================*/
#define BUFFER_SIZE                 50         // General string formatting buffer
#define BUFFER_SIZE_STATS           128        // For task statistics output
#define BUFFER_SIZE_TEMP            16         // Temporary string formatting
#define INPUT_BUFFER_SIZE           10         // UART input buffer

/*====================== Temperature Data ========================*/
#define MIN_TEMPERATURE             15         // Minimum temperature (°C)
#define TEMPERATURE_RANGE           20         // Expected range (35 - 15 °C)
#define MAX_HEIGHT                  15         // Max height for temperature graph

/*====================== Display Config ========================*/
#define DISPLAY_BUFFER_OFFSET       96         // Number of display columns
#define IMAGE_HEIGHT_PAGES          2          // Display height in pages (8px per page)
#define IMAGE_X_START               4          // X coordinate for drawing image
#define IMAGE_Y_START               4          // Y coordinate for drawing image
#define Y_AXIS_HEIGHT               16         // Height of the Y axis
#define X_AXIS_POSITION             15         // Row for X axis (bottom row)
#define PIXEL_ON                    1          // Pixel ON state

/*====================== PRNG Settings ========================*/
#define MULTIPLIER                  1103515245 // LCG multiplier
#define INCREMENT                   12345      // LCG increment
#define SHIFT_BITS                  16         // Bits to shift result
#define RESULT_MASK                 0x7FFF      // Mask for 15-bit output
#define BASE_DECIMAL                10         // Decimal base for number conversion

/*====================== Queue Config ========================*/
#define QUEUE_LENGTH                10         // Queue length for temperature values

/*====================== Filter Parameters ========================*/
#define MAX_WINDOW_SIZE             100         // Max filter window size
#define MIN_WINDOW_SIZE             10          // Min filter window size

/*====================== FreeRTOS Stack Sizes ========================*/
#define STACK_SIZE_TEMP_SENSOR      96         // Stack for temperature task
#define STACK_SIZE_FILTER           96         // Stack for filtering task
#define STACK_SIZE_GRAPH            96         // Stack for graph task
#define STACK_SIZE_UART_READER      96         // Stack for UART command task
#define STACK_SIZE_MONITOR_STACK    64         // Stack for monitoring task

/* Global variables */
QueueHandle_t xRawTemperatureQueue;
QueueHandle_t xFilteredDataQueue;
unsigned long ulHighFrequencyTimerTicks;

TaskHandle_t xTempSensorHandle = NULL;
TaskHandle_t xFilterHandle = NULL;
TaskHandle_t xGraphHandle = NULL;
TaskHandle_t xUARTReaderHandle = NULL;
unsigned char ucDisplayBuffer[96 * 2] = {0}; // 96 columns, 2 pages (16px height)
volatile int filter_window_size = 5;        // Window size (last N samples)

/* Function prototypes */
void Timer0IntHandler( void );
void prvSetupTimer( void );
void vUARTSetup(void);
unsigned long ulGetHighFrequencyTimerTicks( void );
void vUARTSend(const char *string);
void formatString(char *buffer, const char *prefix, int value, const char *suffix);
void setPixel(int x, int y, int on);
int pseudorandom(void);
int stringToInt(const char *str);
void formatTaskStats(char *buffer, TaskStatus_t *task, uint32_t totalRunTime);
void vBusyTask(void *pvParameters);
char *utoa(unsigned int value, char *str, int base);

/* Task prototypes */
void vSimulateTemperatureSensorTask(void *pvParameters);
void vLowPassFilterTask(void *pvParameters);
void vDisplayGraphTask(void *pvParameters);
void vUARTReaderTask(void *pvParameters);
void vMonitorStackTask(void *pvParameters);
void vTopLikeTask(void *pvParameters);

/**
 * @brief Handles stack overflow detection in FreeRTOS tasks.
 *
 * This function is called automatically when FreeRTOS detects a stack overflow 
 * in any task. It sends a status character via UART and enters an infinite loop 
 * to halt execution, preventing further issues caused by the overflow.
 *
 * @param xTask Handle to the task that experienced the stack overflow.
 * @param pcTaskName Pointer to the name of the task that overflowed (null-terminated string).
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    UARTCharNonBlockingPut(UART0_BASE, 'S');
    while (TRUE);
}

void vUART_ISR(void)
{}

/**
 * @brief Configures and initializes Timer0 for a 32-bit periodic timer.
 *
 * This function sets up Timer0 by enabling the required peripheral, enabling
 * interrupts, configuring the timer in 32-bit mode, loading the initial timer
 * value, registering the interrupt handler, and enabling the timer. The timer
 * is configured to trigger an interrupt upon timeout.
 *
 * @note Ensure that the `Timer0IntHandler` function is implemented to handle
 * the timer interrupts properly.
 */
void prvSetupTimer( void )
{
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	IntMasterEnable();
	TimerIntEnable(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	TimerConfigure(TIMER0_BASE,TIMER_CFG_32_BIT_TIMER);
	TimerLoadSet(TIMER0_BASE, TIMER_A, TIMER_LOAD_VALUE);
	TimerIntRegister(TIMER0_BASE,TIMER_A, Timer0IntHandler);
	TimerEnable(TIMER0_BASE,TIMER_A);
}

/**
 * @brief Retrieves the current high-frequency timer tick count.
 *
 * This function returns the value of the `ulHighFrequencyTimerTicks` variable,
 * which represents the number of ticks counted by the high-frequency timer.
 *
 * @return The current high-frequency timer tick count as an unsigned long.
 */
unsigned long ulGetHighFrequencyTimerTicks(void)
{
	return ulHighFrequencyTimerTicks;
}

int main(void)
{
    // Initialize UART and enable UART0 RX interrupts
    vUARTSetup();

    // Create queue for raw (unfiltered) temperature values
    xRawTemperatureQueue = xQueueCreate(QUEUE_LENGTH, sizeof(int));
    if (xRawTemperatureQueue == NULL) {
        vUARTSend("Error: Failed to create raw temperature queue.\n");
        for (;;);  // Critical failure — can't continue without this queue
    }

    // Create queue for filtered temperature values
    xFilteredDataQueue = xQueueCreate(QUEUE_LENGTH, sizeof(int));
    if (xFilteredDataQueue == NULL) {
        vUARTSend("Error: Could not create queue for filtered values.\n");
        for (;;);  // Critical failure
    }

    vUARTSend("Starting...\n");

    // Initialize display (fast mode: 400 kbps)
    OSRAMInit(TRUE);
    OSRAMDisplayOn();

    // -------------------------------
    //         Create FreeRTOS Tasks
    // -------------------------------

    xTaskCreate(
        vSimulateTemperatureSensorTask,    // Task function
        "TempSensorTask",                  // Task name
        STACK_SIZE_TEMP_SENSOR,            // Stack size
        NULL,                              // Parameters
        tskIDLE_PRIORITY + 1,              // Priority
        NULL                               // Task handle
    );

    xTaskCreate(
        vLowPassFilterTask,
        "FilterTask",
        STACK_SIZE_FILTER,
        NULL,
        tskIDLE_PRIORITY + 2,
        NULL
    );

    xTaskCreate(
        vDisplayGraphTask,
        "GraphTask",
        STACK_SIZE_GRAPH,
        NULL,
        tskIDLE_PRIORITY + 3,
        NULL
    );

    xTaskCreate(
        vBusyTask,
        "BusyTask",
        128,  // Small stack, adjust if needed
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    BaseType_t result = xTaskCreate(
        vUARTReaderTask,
        "UARTReader",
        STACK_SIZE_UART_READER,
        NULL,
        tskIDLE_PRIORITY + 4,
        NULL
    );

    if (result != pdPASS) {
        vUARTSend("UARTReaderTask couldn't be created.\n");
    }

    xTaskCreate(
        vMonitorStackTask,
        "MonitorStack",
        STACK_SIZE_MONITOR_STACK,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    xTaskCreate(
        vTopLikeTask,
        "TopTask",
        configMINIMAL_STACK_SIZE,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL
    );

    // Start FreeRTOS scheduler (will not return unless something goes wrong)
    vTaskStartScheduler();

    // Should never reach here
    for (;;);

    return 0;
}
/* ------------------------------------- Tasks --------------------------------------------- */

/**
 * @brief Simulates a temperature sensor task
 *
 * This task generates pseudo-random temperature values between 15 and 35 degrees Celsius. 
 * The task runs continuously in an infinite loop. 
 * It operates at a frequency of 10 Hz (every 100 milliseconds).
 *
 * @param pvParameters Pointer to task-specific parameters (not used in this implementation).
 *                     Included to satisfy the task function prototype requirements.
 */
void vSimulateTemperatureSensorTask(void *pvParameters)
{
    (void)pvParameters; // Avoid compiler warnings for unused parameter

    while (TRUE)
    {
        int temperature = MIN_TEMPERATURE + (pseudorandom() % (TEMPERATURE_RANGE + 1));  // Between 15 and 35 degrees Celsius

        char buffer[BUFFER_SIZE];

        if (xQueueSend(xRawTemperatureQueue, &temperature, portMAX_DELAY) != pdPASS) {
            vUARTSend("Error: temperature read couldn't be sent through the queue\n");
        }

        // Delay task to achieve a frequency of 10 Hz (100 ms)
        vTaskDelay(pdMS_TO_TICKS(DELAY_100_MS));
    }
}

/**
 * @brief FreeRTOS task that applies a moving average (low-pass) filter
 *        to raw temperature readings and outputs filtered values.
 *
 * This task continuously receives temperature readings from a queue,
 * maintains a circular buffer of the most recent N samples,
 * and computes the average (filtered value) to reduce noise.
 * The window size N can be changed dynamically (e.g., via UART).
 *
 * @param pvParameters Unused (required by FreeRTOS task signature).
 */
void vLowPassFilterTask(void *pvParameters)
{
    (void)pvParameters;  // Avoid unused parameter warning

    // Copy initial window size from global variable
    int window_size = filter_window_size;

    // Allocate memory for circular buffer (max size)
    int *buffer = pvPortMalloc(MAX_WINDOW_SIZE * sizeof(int));
    if (buffer == NULL) {
        vUARTSend("Error: could not allocate memory for filter buffer.\n");
        vTaskSuspend(NULL); // Suspend this task
    }

    int index = 0;   // Current write position in circular buffer
    int sum = 0;     // Accumulated sum of values in window
    int count = 0;   // Actual number of valid samples used in the average

    while (TRUE)
    {
        int new_window_size = filter_window_size;

        // If user changed filter size (e.g., via UART), reset buffer and counters
        if (new_window_size != window_size) {
            window_size = new_window_size;
            index = 0;
            sum = 0;
            count = 0;
            memset(buffer, 0, MAX_WINDOW_SIZE * sizeof(int));
            vUARTSend("\nFilter window size updated.\n");
        }

        int raw_temperature;

        // Wait for next raw temperature reading from queue
        if (xQueueReceive(xRawTemperatureQueue, &raw_temperature, portMAX_DELAY) == pdPASS) {

            // Subtract old value at current position from sum
            sum -= buffer[index];

            // Store new value in buffer and update sum
            buffer[index] = raw_temperature;
            sum += raw_temperature;

            // Move to next position (circularly)
            index = (index + 1) % window_size;

            // Update count if buffer is not yet full
            if (count < window_size) {
                count++;
            }

            // Compute filtered average value
            int filtered_value = sum / count;

            // Send filtered result to next processing stage
            if (xQueueSend(xFilteredDataQueue, &filtered_value, portMAX_DELAY) != pdPASS) {
                vUARTSend("\nError: failed to send filtered value to queue.\n");
            }
        }
    }
}
/**
 * @brief Task to render a real-time temperature graph on the display.
 *
 * This task listens to the filtered temperature data queue, maps the values
 * to a displayable height range, and updates a scrolling graph. It also
 * draws reference axes and overlays the current temperature as text.
 *
 * @param pvParameters Unused parameter, required by FreeRTOS task signature.
 */
void vDisplayGraphTask(void *pvParameters)
{
    (void)pvParameters;
    int graphIndex = 0;
    char buffer[BUFFER_SIZE];

    while (TRUE)
    {
        int value;
        if (xQueueReceive(xFilteredDataQueue, &value, portMAX_DELAY) == pdPASS)
        {
            // We scale the value from 15-35°C to a range of 0-15 (display height)
            int y = (value - MIN_TEMPERATURE) * MAX_HEIGHT / TEMPERATURE_RANGE;
            if (y < 0) y = 0;
            if (y > MAX_HEIGHT) y = MAX_HEIGHT;

            // Shift buffer to the left
            for (int i = 0; i < DISPLAY_BUFFER_OFFSET - 1; i++) {
                ucDisplayBuffer[i] = ucDisplayBuffer[i + 1];
                ucDisplayBuffer[i + DISPLAY_BUFFER_OFFSET] = ucDisplayBuffer[i + 1 + DISPLAY_BUFFER_OFFSET];
            }

            // Clear last column
            ucDisplayBuffer[DISPLAY_BUFFER_OFFSET - 1] = 0;
            ucDisplayBuffer[(DISPLAY_BUFFER_OFFSET - 1) + DISPLAY_BUFFER_OFFSET] = 0;

            // Set the new value in the last column
            setPixel(DISPLAY_BUFFER_OFFSET - 1, MAX_HEIGHT - y, PIXEL_ON); // Inverted because the Y axis starts at the top.

            // Draw Y axis (column 0)
            for (int i = 0; i < Y_AXIS_HEIGHT; i++) {
                setPixel(0, i, PIXEL_ON);
            }

            // Draw X axis (bottom row)
            for (int i = 0; i < DISPLAY_BUFFER_OFFSET; i++) {
                setPixel(i, X_AXIS_POSITION, PIXEL_ON);
            }

            OSRAMClear();
            OSRAMImageDraw(ucDisplayBuffer, IMAGE_X_START, IMAGE_Y_START, DISPLAY_BUFFER_OFFSET,IMAGE_HEIGHT_PAGES);

            // Show the numeric value above the graph
            formatString(buffer, "T: ", value, "C");
            OSRAMStringDraw(buffer, IMAGE_X_START, IMAGE_Y_START);
        }
        vTaskDelay(pdMS_TO_TICKS(DELAY_100_MS));
    }
}
/**
 * @brief Task for reading and processing UART input.
 *
 * This FreeRTOS task continuously monitors the UART interface, receiving 
 * and processing incoming characters. It supports numeric input for configuring 
 * a filter window size, provides user feedback via UART, and handles invalid input gracefully.
 *
 * @param pvParameters Pointer to task-specific parameters (unused in this implementation).
 */
void vUARTReaderTask(void *pvParameters) {
    (void)pvParameters;

    char c;
    char inputBuffer[INPUT_BUFFER_SIZE];
    int inputIndex = 0;

    for (;;) {
        if (UARTCharsAvail(UART0_BASE)) {
            c = UARTCharGet(UART0_BASE);  // Reads an available character (blocks if none, but we already checked with UARTCharsAvail)
            UARTCharPut(UART0_BASE, c);   // Direct echo to UART

            if (c >= '0' && c <= '9') {
                if (inputIndex < sizeof(inputBuffer) - 1) {
                    inputBuffer[inputIndex++] = c;
                } else {
                    inputIndex = 0;
                    vUARTSend("\nVery long entry. Try again.\r\n");
                }
            } else if (c == '\r' || c == '\n') {
                vUARTSend("\r\n");
                inputBuffer[inputIndex] = '\0';

                if (inputIndex > 0) {
                    int newN = stringToInt(inputBuffer);
                    if (newN >= MIN_WINDOW_SIZE && newN <= MAX_WINDOW_SIZE) {
                        filter_window_size = newN;
                        vUARTSend("\n Filter now N = ");
                        vUARTSend(inputBuffer);
                        vUARTSend("\r\n");
                    } else {
                        vUARTSend("\n Invalid N (10-100).\r\n");
                    }
                } else {
                    vUARTSend("\n Empty buffer.\r\n");
                }
                inputIndex = 0;
            } else {
                inputIndex = 0;
                vUARTSend("\n Non numeric character.\r\n");
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(DELAY_10_MS));  // Avoid saturating the CPU if there is no data
        }
    }
}

/**
 * @brief Monitors the stack usage of various FreeRTOS tasks.
 *
 * This FreeRTOS task periodically retrieves and reports the stack high water mark 
 * for multiple tasks, providing insights into stack consumption and potential overflow risks.
 *
 * @param pvParameters Pointer to task-specific parameters (unused in this implementation).
 */
void vMonitorStackTask(void *pvParameters)
{
    (void)pvParameters;
    char buffer[BUFFER_SIZE];

    while (TRUE)
    {
        UBaseType_t stackTemp = uxTaskGetStackHighWaterMark(xTempSensorHandle);
        UBaseType_t stackFilter = uxTaskGetStackHighWaterMark(xFilterHandle);
        UBaseType_t stackGraph = uxTaskGetStackHighWaterMark(xGraphHandle);
        UBaseType_t stackUART = uxTaskGetStackHighWaterMark(xUARTReaderHandle);

        vUARTSend("\nStack High Water Marks:\n");

        formatString(buffer, "TempSensor HWM: ", stackTemp, "\n");
        vUARTSend(buffer);

        formatString(buffer, "FilterTask HWM: ", stackFilter, "\n");
        vUARTSend(buffer);

        formatString(buffer, "GraphTask HWM: ", stackGraph, "\n");
        vUARTSend(buffer);

        formatString(buffer, "UARTReader HWM: ", stackUART, "\n");
        vUARTSend(buffer);

        vTaskDelay(pdMS_TO_TICKS(DELAY_5_SECONDS)); // every 5 seconds
    }
}

/**
 * @brief Monitors system tasks, tracks free heap space, and logs task statistics.
 *
 * This task periodically retrieves system task statuses, monitors heap usage,
 * and reports statistics via UART. If the number of tasks increases, it dynamically
 * resizes the task status array.
 *
 * @param pvParameters Unused parameter, maintained for FreeRTOS compliance.
 */
void vTopLikeTask(void *pvParameters)
{
    TaskStatus_t *pxTaskStatusArray;
    static UBaseType_t uxMaxTasks = 0;

    UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime;
    char buffer[BUFFER_SIZE_STATS];  

    while (TRUE)
    {
        uxArraySize = uxTaskGetNumberOfTasks();

        utoa(xPortGetFreeHeapSize(), buffer, BASE_DECIMAL);
        vUARTSend("\nFree Heap: ");
        vUARTSend(buffer);
        vUARTSend("\n");

        if (uxArraySize > uxMaxTasks)
        {
            // Resize only if there are more tasks than before
            if (pxTaskStatusArray != NULL)
                vPortFree(pxTaskStatusArray);

            pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));

            if (pxTaskStatusArray != NULL)
                uxMaxTasks = uxArraySize;
            else
            {
                vUARTSend(" Could not allocate memory for pxTaskStatusArray\n");
                vTaskDelay(pdMS_TO_TICKS(DELAY_5_SECONDS));
                continue;
            }
        }

        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);

        vUARTSend("\nTask Stats:\n");

        for (x = 0; x < uxArraySize; x++)
        {
            formatTaskStats(buffer, &pxTaskStatusArray[x], ulTotalRunTime);
            vUARTSend(buffer);
        }

        vTaskDelay(pdMS_TO_TICKS(DELAY_5_SECONDS));
    }
}

/* ------------------------------------- Functions --------------------------------------------- */

/**
 * @brief Interrupt handler for Timer0 time-out event.
 *
 * This function is triggered when Timer0 reaches its time-out condition.
 * It clears the interrupt flag for Timer0 and increments the 
 * `ulHighFrequencyTimerTicks` counter, which tracks high-frequency timer ticks.
 */
void Timer0IntHandler( void )
{
	TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
	ulHighFrequencyTimerTicks++;
}

/**
 * @brief Configures and initializes UART0 for communication.
 *
 * This function enables the UART0 peripheral, sets its configuration parameters
 * (baud rate, word length, stop bits, and parity), enables UART interrupts, and 
 * sets the interrupt priority. It also globally enables the UART0 interrupt to 
 * ensure proper operation.
 */
void vUARTSetup(void)
{
    IntMasterEnable();
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    UARTConfigSet(UART0_BASE, mainBAUD_RATE, (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));
    UARTIntDisable(UART0_BASE, UART_INT_RX | UART_INT_RT);
    UARTIntClear(UART0_BASE, UART_INT_RX | UART_INT_RT);
    UARTEnable(UART0_BASE);
}

/**
 * @brief Sends a null-terminated string via UART0.
 *
 * This function transmits a string character by character using UART0. It
 * loops through each character of the provided null-terminated string and
 * sends it until the string is fully transmitted.
 *
 * @param string Pointer to the null-terminated string to be sent.
 */
void vUARTSend(const char *string) {
    while (*string) {
        UARTCharPut(UART0_BASE, *string++);
    }
}

/**
 * @brief Formats a string combining prefix, integer value, and suffix.
 *
 * Converts the given integer to a string and appends it between the given prefix and suffix.
 *
 * @param buffer Output string buffer (must be large enough).
 * @param prefix Text to prepend (e.g., "T: ").
 * @param value Integer value to convert and insert.
 * @param suffix Text to append (e.g., "C").
 */
void formatString(char *buffer, const char *prefix, int value, const char *suffix)
{
    char *ptr = buffer;

    // Copy prefix
    while (*prefix)
        *ptr++ = *prefix++;

    // Convert integer value to string (manual itoa)
    char tempBuffer[10];
    char *tempPtr = tempBuffer + sizeof(tempBuffer) - 1;
    *tempPtr = '\0';

    int temp = value;
    do {
        *--tempPtr = (temp % BASE_DECIMAL) + '0';
        temp /= BASE_DECIMAL;
    } while (temp > 0);

    // Copy numeric string
    while (*tempPtr)
        *ptr++ = *tempPtr++;

    // Copy suffix
    while (*suffix)
        *ptr++ = *suffix++;

    *ptr = '\0';  // Null-terminate final string
}

/**
 * @brief Set or clear a pixel at (x, y) in the display buffer.
 *
 * The display is organized in pages, each page representing 8 vertical pixels.
 * This function modifies the appropriate bit in the display buffer.
 *
 * @param x X-coordinate (column).
 * @param y Y-coordinate (row).
 * @param on 1 to turn pixel on, 0 to turn it off.
 */
void setPixel(int x, int y, int on) {
    if (x < 0 || x >= DISPLAY_BUFFER_OFFSET || y < 0 || y >= Y_AXIS_HEIGHT) return;
    int page = y / (Y_AXIS_HEIGHT/2);
    int bit = y % (Y_AXIS_HEIGHT/2);
    if (on)
        ucDisplayBuffer[x + (page * DISPLAY_BUFFER_OFFSET)] |= (1 << bit);
    else
        ucDisplayBuffer[x + (page * DISPLAY_BUFFER_OFFSET)] &= ~(1 << bit);
}

/**
 * @brief Generates a 15-bit pseudo-random number using an internal seed (LCG).
 *
 * This function produces a pseudo-random number using a Linear Congruential Generator (LCG)
 * with a persistent internal seed. No external state is needed.
 *
 * @return A 15-bit pseudo-random integer in the range [0, 32767].
 *
 * @note This function is deterministic for the initial seed and produces the same sequence
 *       on each run unless the code is modified to seed it differently.
 */
int pseudorandom(void)
{
    static unsigned int seed = 6789;  // Internal persistent seed
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) & 0x7FFF;
}

/**
 * @brief Converts a numeric string to an integer.
 *
 * This function parses a null-terminated string containing numeric characters ('0' to '9') 
 * and converts it into an integer value. If the string contains any non-numeric characters, 
 * the function returns an error code (-1).
 *
 * @param str Pointer to the null-terminated string representing a numeric value.
 * @return The converted integer value if the input is valid, or -1 if the string contains non-numeric characters.
 */
int stringToInt(const char *str) {
    int value = 0;
    while (*str) {
        if (*str < '0' || *str > '9') {
            return -1; // Error: non-valid character
        }
        value = value * 10 + (*str - '0');
        str++;
    }
    return value;
}

/**
 * @brief Formats and stores task statistics in a buffer.
 *
 * This function generates a formatted string containing details about a FreeRTOS task,
 * including its name, CPU usage percentage, available stack space, and current state.
 * The formatted output is stored in the provided buffer for further transmission or logging.
 *
 * @param buffer Pointer to a character array where the formatted statistics will be stored.
 * @param task Pointer to the TaskStatus_t structure containing information about the task.
 * @param totalRunTime Total runtime of all tasks, used to compute CPU usage percentage.
 */
void formatTaskStats(char *buffer, TaskStatus_t *task, uint32_t totalRunTime)
{
    char temp[16];
    uint32_t cpu = 0;
    buffer[0] = '\0'; // Clear output buffer

    // CPU usage
    if (totalRunTime > 0) {
        cpu = (task->ulRunTimeCounter * 100UL) / totalRunTime;
    }

    // Nombre con padding fijo (10 caracteres, ajustable)
    strcat(buffer, "Name: ");
    strcat(buffer, task->pcTaskName);

    int nameLen = strlen(task->pcTaskName);
    for (int i = nameLen; i < 9; i++) {
        strcat(buffer, " ");
    }

    strcat(buffer, " | CPU: ");
    utoa(cpu, temp, 10);
    strcat(buffer, temp);
    strcat(buffer, "%");

    // Padding para CPU (< 3 cifras + %)
    int cpuDigits = (cpu < 10) ? 1 : (cpu < 100) ? 2 : 3;
    for (int i = cpuDigits + 1; i < 5; i++) { // +1 por '%'
        strcat(buffer, " ");
    }

    strcat(buffer, "| Stack Free: ");
    utoa(task->usStackHighWaterMark, temp, 10);
    strcat(buffer, temp);

    strcat(buffer, " | State: ");
    utoa(task->eCurrentState, temp, 10);
    strcat(buffer, temp);
    strcat(buffer, "\n");
}

/**
 * @brief Converts an unsigned integer to a string representation in the specified base.
 *
 * This function converts a given unsigned integer into a null-terminated string
 * using the specified numerical base (between 2 and 16). The conversion is done
 * in reverse order, and the result is stored in the provided buffer.
 *
 * @param value The unsigned integer to convert.
 * @param str Pointer to a character array where the converted string will be stored.
 * @param base Numerical base for conversion (valid range: 2 to 16).
 * @return Pointer to the resulting string buffer.
 */
char *utoa(unsigned int value, char *str, int base) {
    char *ptr = str;
    char *ptr1 = str;
    char tmp_char;
    unsigned int tmp_value;

    // Only valid bases
    if (base < 2 || base > 16) {
        // Invalid base, return empty string
        *str = '\0';
        return str;
    }

    // Convert number to string in reverse order.
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789ABCDEF"[tmp_value % base];
    } while (value);

    // End string
    *ptr-- = '\0';

    // Invert string (because we built it backwards)
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}
void vBusyTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        volatile int x = 0;
        for (int i = 0; i < 100000; i++) {
            x += i;
        }
    }
}