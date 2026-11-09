# Ensayos completos del simulador

Aquí va una carpeta por cada ensayo del simulador, tal como la genera el simulador
(por ejemplo `decoupling_w2_150s/`). De cada ensayo, los scripts usan `config.json`,
`coord/rx_coord.csv` y `coord/imu_crd.csv`.

El contenido de esta carpeta **no se sube a Git** (pesa mucho): solo este archivo.
Para procesar un ensayo, añádelo a `datasets_procesados/simulador/ensayos_simulador.json`
(ver `guia_uso.md`).


## Estructura de los datasets

Cada ejecución genera una carpeta con la siguiente estructura:

```
Datasets/<nombre_sesion>/
├── config.json              # Parámetros exactos usados en la simulación
├── coord/                   # Datos del nodo coordinador (locomotora)
│   ├── tx_coord.csv         # Mensajes enviados por la locomotora
│   ├── rx_coord.csv         # Mensajes recibidos por la locomotora
│   └── gnss_coord.csv       # Medidas GNSS de la locomotora
├── wagon_0/                 # Datos del nodo del vagón 0
│   ├── imu_c0.csv           # Medidas IMU del vagón 0
│   ├── tx_c0.csv            # Mensajes enviados por el vagón 0
│   ├── rx_c0.csv            # Mensajes recibidos por el vagón 0
│   ├── gnss_c0.csv          # Medidas GNSS del vagón 0
│   └── uwb_c0.csv           # Medidas UWB del vagón 0 (distancia al coordinador)
├── wagon_1/ ... wagon_N/    # Idem para el resto de vagones
└── ltd_log/
    ├── dyn.csv              # Estado dinámico completo (s, v, a, jerk, fuerzas)
    └── couplers.csv         # Fuerzas de acoplamiento entre vagones 
```

### Formato de los ficheros CSV principales

**`dyn.csv`** — un registro por tick de simulación, por vehículo:


| Campo | Descripción |
|-------|-------------|
| `timestamp` | Tiempo de simulación (s) |
| `id` | Identificador (`L_0` = locomotora, `V_i` = vagón i) |
| `s` | Posición en el arco de vía (m) |
| `v` | Velocidad longitudinal (m/s) |
| `a` | Aceleración longitudinal (m/s²) |
| `jerk` | Derivada de la aceleración (m/s³) |
| `F_trac` | Fuerza de tracción (N) |
| `F_brake` | Fuerza de freno (N) |
| `F_coupler` | Fuerza neta de acoplamiento (N) |
| `F_res` | Resistencias totales (Davis + pendiente + curva) (N) |

**`imu_cX.csv`** — medidas de la IMU sintética:

| Campo | Descripción |
|-------|-------------|
| `frame` | Frame CARLA |
| `timestamp` | Tiempo de simulación (s) |
| `ax_meas` / `ay_meas` / `az_meas` | Aceleración medida (m/s²) |
| `gx_meas` / `gy_meas` / `gz_meas` | Velocidad angular medida (rad/s) |

**`tx_cX.csv`** / **`rx_cX.csv`** — mensajes V2X enviados/recibidos:

| Campo | Descripción |
|-------|-------------|
| `source_id` / `dest_id` | Identificadores origen y destino |
| `msg_type` | Tipo de mensaje (`data` o `ACK`) |
| `seq` | Número de secuencia |
| `timestamp` | Tiempo de simulación al envío (s) |
| `ax`..`gz` | Datos IMU incluidos en el mensaje |
| `lat`, `lon`, `alt` | Datos GNSS incluidos en el mensaje |
| `uwb` | Distancia UWB incluida en el mensaje |

---

## Datasets disponibles

Los datasets de la carpeta `Datasets/` siguen la convención de nombres `<fecha>_<índice>` o nombres descriptivos para sesiones especiales:

| Nombre | Descripción aproximada |
|--------|----------------------|
| Datasets_def/brake_300s | Simulación de frenado a los 300s  |
| Datasets_def/decoupling_w2_150s | Simulación con fallo instantáneo del acople 2 a los 150 s |
| Datasets_def/decoupling_prefail30s_t100s | Simulación con pre-fallo de acople a los 100 s (antes de llegar a la velocidad nominal)|

