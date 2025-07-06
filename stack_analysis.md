---

### **Stack Usage Analysis in FreeRTOS Tasks**

**Platform:** ARM Cortex-M3 (Stellaris LM3S811 simulated in QEMU)
**Word Size:** 4 bytes
**Minimal Stack Size (`configMINIMAL_STACK_SIZE`):** 143 words (572 bytes)
**Measurement Tool:** `uxTaskGetStackHighWaterMark()`

---

### 🛠️ **Stack Configuration at Task Creation**

The following tasks were created using `configMINIMAL_STACK_SIZE` as their initial stack allocation:

```c
xTaskCreate(vSimulateTemperatureSensorTask, "TempSensorTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
xTaskCreate(vLowPassFilterTask, "FilterTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
xTaskCreate(vDisplayGraphTask, "GraphTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 3, NULL);
xTaskCreate(vUARTReaderTask, "UARTReader", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 4, NULL);
xTaskCreate(vMonitorStackTask, "MonitorStack", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
```

---

### 📊 **Observed Stack Usage (`High Water Mark`)**

Based on multiple executions and measurements, the following values were observed using `uxTaskGetStackHighWaterMark()`:

| Task Name    | Max HWM Observed | Min HWM Observed | Stack Used (words) | Stack Used (bytes) |
| ------------ | ---------------- | ---------------- | ------------------ | ------------------ |
| TempSensor   | 141              | 100              | 43                 | 172 bytes          |
| FilterTask   | 141              | 100              | 43                 | 172 bytes          |
| GraphTask    | 141              | 100              | 43                 | 172 bytes          |
| UARTReader   | 141              | 100              | 43                 | 172 bytes          |
| MonitorStack | Not Measured     | Not Measured     | —                  | —                  |

> **Note:** `Stack Used = configMINIMAL_STACK_SIZE - HighWaterMark`

---

### 📈 **Analysis and Insights**

* All measured tasks are significantly over-allocated in terms of stack.
* Worst-case usage corresponds to approximately **30%** of the allocated stack.
* In the best case (HWM = 141), only **2 words** (\~8 bytes) are used — indicating an extremely lightweight task.

---

### 🧾 **Recommended Stack Adjustments**

To optimize memory usage, the following reductions are suggested, based on observed stack consumption plus a safety margin:

| Task Name    | Recommended Stack Size (words) | Justification         |
| ------------ | ------------------------------ | --------------------- |
| TempSensor   | 96                             | 43 used + margin      |
| FilterTask   | 96                             | Same as above         |
| GraphTask    | 96                             | Same as above         |
| UARTReader   | 96                             | Same as above         |
| MonitorStack | 64                             | Expected minimal load |

> Always validate recommendations after applying, by monitoring updated high water marks.

---

### 💡 **Memory Optimization Summary**

Applying these changes would yield the following memory savings:

* Total saved stack words: `5 × (143 - 96) = 235 words`
* Equivalent to: `235 × 4 = 940 bytes` of SRAM saved

This is particularly valuable on constrained platforms like the LM3S811.

---
