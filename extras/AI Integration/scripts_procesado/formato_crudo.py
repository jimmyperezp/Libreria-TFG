"""
Definición del CSV en crudo.

Es el formato común a los dos orígenes de datos:
  - el simulador (lo genera adaptador_simulador.py),
  - las placas (lo imprime el coordinador como RAW_DATA y lo guarda grabador_serie.py).
Si cambia, hay que cambiar también printRawHeader() / printRawRow() del coordinador
(Coordinator.ino): el grabador comprueba que la cabecera RAW_HEADER coincide con esta.

Una fila por ciclo de comunicación. Columnas fijas para 5 vehículos:
el coordinador (c) y cuatro vagones (w1..w4, de "wagon"), en el orden del tren.
Los nombres van por POSICIÓN; los identificadores reales de cada
dispositivo se guardan aparte (composición).

Columnas (nombres en inglés, iguales que en el firmware):
  cycle, time_ms, cycle_duration_ms
  Por cada enlace (vehículos consecutivos: c_w1, w1_w2, w2_w3, w3_w4):
    link_X_Y_seen, link_X_Y_distance_m
  Por cada par a dos saltos (nieto: c_w2, w1_w3, w2_w4):
    hop2_X_Z_seen, hop2_X_Z_distance_m
  Por cada vehículo (c, w1..w4):
    X_acc_x_ms2, X_acc_y_ms2

Contenido: lo que trae el informe de modo modelo (distancias con el hijo y el nieto, ax y ay de cada
nodo), más la duración del ciclo, que mide el coordinador. No hay potencia RX.
Se graba más de lo que usa el modelo (duración del ciclo, acc_y) por si entra en el futuro.

Convenio para lo que no se ha recibido: celda vacía y seen = 0.

Pares a dos saltos:
  - placas: c_w2 es la medida del coordinador con w2; w1_w3 y w2_w4, las de cada nodo con su nieto.
  - simulador: c_w2 es la distancia del coordinador a w2; w1_w3 y w2_w4 salen por resta.
"""

VEHICULOS = ['c', 'w1', 'w2', 'w3', 'w4']          # c = coordinador (locomotora); w = wagon (vagón)
VAGONES = VEHICULOS[1:]
ENLACES = [(VEHICULOS[i], VEHICULOS[i + 1]) for i in range(len(VEHICULOS) - 1)]   # consecutivos
PARES_SALTO2 = [(VEHICULOS[i], VEHICULOS[i + 2]) for i in range(len(VEHICULOS) - 2)]  # a dos saltos (nieto)

# Nombres de columna comunes (los usan todos los scripts)
COL_CICLO = 'cycle'
COL_TIEMPO = 'time_ms'
COL_DURACION = 'cycle_duration_ms'
SUF_VISTO = 'seen'
SUF_DISTANCIA = 'distance_m'
EJE_ACEL_X = 'acc_x_ms2'
EJE_ACEL_Y = 'acc_y_ms2'
EJES_IMU = [EJE_ACEL_X, EJE_ACEL_Y]


def nombre_enlace(a, b):
    return f'link_{a}_{b}'


def nombre_salto2(a, b):
    return f'hop2_{a}_{b}'


def columnas_crudo():
    cols = [COL_CICLO, COL_TIEMPO, COL_DURACION]
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        cols += [f'{e}_{SUF_VISTO}', f'{e}_{SUF_DISTANCIA}']
    for a, b in PARES_SALTO2:
        s = nombre_salto2(a, b)
        cols += [f'{s}_{SUF_VISTO}', f'{s}_{SUF_DISTANCIA}']
    for v in VEHICULOS:
        cols += [f'{v}_{eje}' for eje in EJES_IMU]
    return cols


# Archivo de marcas del grabador (marcas_<ensayo>.csv): una fila por marca de fase.
MARCA_DESDE_CICLO = 'from_cycle'                    # la fase empieza en este ciclo
MARCA_TIEMPO_COORD = 'coordinator_time_ms'          # tiempo del coordinador del último ciclo recibido
MARCA_FASE = 'phase'                                # OK, LOST_INTEGRITY...
MARCA_HORA_PC = 'pc_time'                           # hora del PC al pulsar la tecla
COLUMNAS_MARCAS = [MARCA_DESDE_CICLO, MARCA_TIEMPO_COORD, MARCA_FASE, MARCA_HORA_PC]
