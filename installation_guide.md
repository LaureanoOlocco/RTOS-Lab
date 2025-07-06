## **Installation & Execution Guide for the LM3S811 FreeRTOS Project**

### ✅ **Requirements**

Before starting, ensure the following are installed on your Linux system:

* `qemu-system-arm`
* `gcc-arm-none-eabi`
* `make`

Install them using:

```bash
sudo apt update
sudo apt install qemu-system-arm gcc-arm-none-eabi make
```
---

### 🚀 **How to Compile & Run the Project**

1. **Open a terminal.**

2. **Navigate to the correct directory:**

```bash
cd Demo/CORTEX_LM3S811_GCC
```

3. **Make the script executable (only once):**

```bash
chmod +x run_qemu.sh
```

4. **Run the script with sudo:**

```bash
sudo ./run.sh
```

This script will:

* Clean previous builds (`make clean`)
* Recompile the firmware (`make`)
* Launch QEMU with the compiled image using:

  ```bash
  qemu-system-arm -machine lm3s811evb -kernel gcc/RTOSDemo.axf -serial stdio
  ```

5. **Interacting with the Program:**

* The program will print UART messages to your terminal.
* You can interact by typing values (e.g. for changing the filter window size).
* Press **Enter** to send input.

---

### 🛑 **Exiting the program**

Press `Ctrl+C` to quit the program.
