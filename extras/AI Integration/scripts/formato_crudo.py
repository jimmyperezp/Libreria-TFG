"""
Definición del CSV en crudo.

Es el formato común a los dos orígenes de datos:
  - el simulador (lo genera adaptador_simulador.py),
  - las ESP32 (lo generará el grabador del puerto serie).

Una fila por ciclo de comunicación. Columnas fijas para 5 vehículos:
el coordinador (c) y cuatro vagones (v1..v4), en el orden del tren.
Los nombres van por POSICIÓN; los identificadores reales de cada
dispositivo se guardan aparte, en el archivo de composición.

Convenio para lo que no se ha recibido: celda vacía (NaN) y visto = 0.
"""

VEHICULOS = ['c', 'v1', 'v2', 'v3', 'v4']          # c = coordinador (locomotora)
VAGONES = VEHICULOS[1:]
ENLACES = [(VEHICULOS[i], VEHICULOS[i + 1]) for i in range(len(VEHICULOS) - 1)]   # adyacentes

EJES_IMU = ['acel_x_ms2', 'acel_y_ms2', 'acel_z_ms2', 'giro_x_rads', 'giro_y_rads', 'giro_z_rads']


def nombre_enlace(a, b):
    return f'enlace_{a}_{b}'


def columnas_crudo():
    cols = ['ciclo', 'tiempo_ms', 'duracion_ciclo_ms']
    for a, b in ENLACES:
        e = nombre_enlace(a, b)
        cols += [f'{e}_visto', f'{e}_distancia_m', f'{e}_potencia_rx_dbm']
    for v in VEHICULOS:
        cols += [f'{v}_{eje}' for eje in EJES_IMU]
    return cols
