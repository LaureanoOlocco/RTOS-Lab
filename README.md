# Proyecto FreeRTOS - Sistema de Monitoreo de Temperatura

## Descripción
Este proyecto implementa un sistema de monitoreo de temperatura en tiempo real utilizando FreeRTOS (versión 10.5.1) en un microcontrolador Cortex-M3 (LM3S811). El sistema simula la lectura de sensores de temperatura y procesa los datos en tiempo real.

## Características Principales
- Sistema operativo en tiempo real (RTOS) FreeRTOS
- Monitoreo de temperatura con rango de 0°C a 30°C
- Muestreo de temperatura a 10Hz
- Visualización en pantalla LCD
- Comunicación UART a 19200 baudios
- Cálculo de promedios de temperatura
- Sistema de estadísticas en tiempo de ejecución

## Estructura del Proyecto
```
.
├── Demo/
│   └── CORTEX_LM3S811_GCC/
│       ├── main.c              # Punto de entrada de la aplicación
│       ├── FreeRTOSConfig.h    # Configuración de FreeRTOS
│       └── ...                 # Otros archivos de configuración
└── Source/
    ├── tasks.c                 # Gestión de tareas
    ├── queue.c                 # Sistema de colas
    ├── timers.c                # Temporizadores
    └── ...                     # Otros componentes de FreeRTOS
```

## Tareas del Sistema
1. **vSensorTask**: 
   - Genera valores de temperatura cada 100ms (10Hz)
   - Rango de temperatura: 0°C a 30°C
   - Cambios aleatorios de ±1°C

2. **vDisplayTask**:
   - Muestra la temperatura actual en la pantalla LCD
   - Actualiza la visualización cada 500ms

3. **vAvgTask**:
   - Calcula el promedio de temperatura
   - Actualiza el promedio cada 1000ms

4. **vUARTTask**:
   - Maneja la comunicación serial
   - Configuración: 19200 baudios

5. **vTopTask**:
   - Monitoreo general del sistema
   - Muestra estadísticas de ejecución

## Configuración del Sistema
- Frecuencia del CPU: 20MHz
- Tick del sistema: 1ms
- Heap disponible: 5000 bytes
- Niveles de prioridad: 5
- Tamaño de pila por tarea: 100 palabras

## Requisitos
- Compilador GCC para ARM
- FreeRTOS 10.5.1
- DriverLib para LM3S811
- Hardware compatible con Cortex-M3

## Compilación y Ejecución
El proyecto utiliza un script de shell para la compilación y ejecución. Para compilar y ejecutar el proyecto:

```bash
./run.sh
```

Este script automatiza el proceso de compilación y ejecución del proyecto.

## Licencia
Este proyecto utiliza FreeRTOS, que está licenciado bajo la licencia MIT.

## Notas
- El sistema está optimizado para el microcontrolador LM3S811
- La configuración puede ser ajustada en FreeRTOSConfig.h
- Los parámetros de tiempo pueden ser modificados según necesidades específicas