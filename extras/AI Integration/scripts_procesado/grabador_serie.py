"""
Grabador del puerto serie: registra un ensayo con las placas.

Uso (VS Code): cambia ENSAYO (y PUERTO si hace falta) en la CONFIGURACIÓN de abajo y pulsa
el botón de ejecutar (Play). Las teclas de las marcas se pulsan en el terminal donde se ejecuta.

Lee lo que imprime el coordinador y guarda, en datasets_procesados/placas/<ENSAYO>/:
  crudo/crudo_<ensayo>.csv      filas RAW_DATA, en el formato en crudo (formato_crudo.py)
  crudo/marcas_<ensayo>.csv     las marcas de fase pulsadas durante el ensayo
  crudo/monitor_<ensayo>.txt    todo el monitor serie, tal cual llega (con la hora del PC)
  resumen_<ensayo>.json         fecha, descripción (vacía, para rellenarla a mano) y datos de la grabación
Y apunta el ensayo en datasets_procesados/placas/ensayos_placas.json (lo crea si no existe),
para que el exportador lo encuentre.

Si ya existe un ensayo con ese nombre, pregunta si se sobrescribe (se borra entero, también lo exportado).

Marcas de fase: durante la grabación, pulsa una de las teclas definidas en TECLAS
(por defecto: 0 = OK, 1 = LOST_INTEGRITY). La marca se guarda con el RELOJ DEL COORDINADOR:
la fase nueva empieza en el ciclo siguiente al último recibido (columna desde_ciclo).
Al empezar se pone automáticamente una marca con la fase inicial (FASE_INICIAL).

Para terminar: la tecla de salida (por defecto q) o Ctrl+C. Los archivos se escriben línea a
línea, así que no se pierde nada si el programa se corta.

Requisito: pyserial (pip install pyserial).
"""

import csv
import json
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

from formato_crudo import columnas_crudo


# ============================== CONFIGURACIÓN ==============================

ENSAYO = 'prueba_04'        # Nombre del ensayo: carpeta y archivos. Solo letras sin tildes, números, "_" y "-"
PUERTO = 'COM15'             # Puerto serie del coordinador
BAUDIOS = 115200
REINICIAR_COORDINADOR = True    # Reinicia el coordinador al abrir el puerto (como el botón EN/RST), para que la
                                # grabación empiece en el ciclo 0, con su cabecera RAW_HEADER y su calibración

FASE_INICIAL = 'OK'         # Fase con la que empieza la grabación
TECLAS = {                  # Tecla -> fase. Para añadir una fase, añade una línea
    '0': 'OK',
    '1': 'LOST_INTEGRITY',
}
TECLA_SALIR = 'q'

VER_MONITOR = False         # True: muestra todo el monitor serie. False: solo una línea de estado

# ===========================================================================

RAIZ = Path(__file__).resolve().parent.parent          # extras/AI Integration
CARPETA_PLACAS = RAIZ / 'datasets_procesados' / 'placas'
REGISTRO_PLACAS = CARPETA_PLACAS / 'ensayos_placas.json'
NOMBRE_VALIDO = re.compile(r'^[A-Za-z0-9_-]+$')

# Parámetros generales con los que se crea ensayos_placas.json la primera vez (luego se editan allí)
PARAMETROS_GENERALES_PLACAS = {
    'ciclos_descartados_inicio': 0,
    'calibracion_primer_ciclo': 0,
    'calibracion_n_ciclos': 30,     # debe coincidir con LINK_REF_SAMPLES del coordinador
    'tope_ciclos_sin_ver': 10,
    'periodo_nominal_ms': 1000,
}


PREFIJO_CABECERA = 'RAW_HEADER:'
PREFIJO_FILA = 'RAW_DATA:'


class ErrorGrabacion(Exception):
    """Situación en la que seguir grabando produciría datos inservibles."""


class Grabador:
    """Procesa las líneas del monitor serie y escribe los archivos del ensayo.
    No sabe de dónde vienen las líneas (puerto serie o archivo): así se puede probar sin placas."""

    def __init__(self, carpeta, ensayo, teclas, fase_inicial, info):
        self.carpeta = Path(carpeta)                # Carpeta del ensayo
        self.carpeta_crudo = self.carpeta / 'crudo'
        self.carpeta_crudo.mkdir(parents=True, exist_ok=True)
        self.ensayo = ensayo
        self.teclas = teclas
        self.columnas = columnas_crudo()
        self.fecha = datetime.now()

        self.f_crudo = open(self.carpeta_crudo / f'crudo_{ensayo}.csv', 'w', newline='', encoding='utf-8')
        self.f_marcas = open(self.carpeta_crudo / f'marcas_{ensayo}.csv', 'w', newline='', encoding='utf-8')
        self.f_monitor = open(self.carpeta_crudo / f'monitor_{ensayo}.txt', 'w', encoding='utf-8')
        self.w_crudo = csv.writer(self.f_crudo)
        self.w_marcas = csv.writer(self.f_marcas)
        self.w_crudo.writerow(self.columnas)
        self.w_marcas.writerow(['desde_ciclo', 'tiempo_ms_coordinador', 'fase', 'hora_pc'])

        self.info = dict(info, inicio=self.fecha.isoformat(timespec='seconds'))
        self.cabecera_validada = False
        self.ultimo_ciclo = None
        self.ultimo_tiempo_ms = None
        self.filas = 0
        self.ciclos_perdidos = 0
        self.lineas_corruptas = 0
        self.marcas = 0
        self.fase_actual = None
        self.fase_inicial = fase_inicial
        self.fase_inicial_pendiente = fase_inicial is not None

    # --- Entrada de líneas -------------------------------------------------

    def procesar_linea(self, linea):
        linea = linea.strip()
        if not linea:
            return
        self.f_monitor.write(f'{datetime.now().strftime("%H:%M:%S.%f")[:-3]}  {linea}\n')

        if linea.startswith(PREFIJO_CABECERA):
            self._cabecera(linea[len(PREFIJO_CABECERA):])
        elif linea.startswith(PREFIJO_FILA):
            self._fila(linea[len(PREFIJO_FILA):])

    def _cabecera(self, texto):
        recibidas = texto.split(',')
        if recibidas != self.columnas:
            raise ErrorGrabacion(
                'La cabecera RAW_HEADER del coordinador no coincide con formato_crudo.py.\n'
                f'  Esperadas ({len(self.columnas)}): {self.columnas}\n  Recibidas ({len(recibidas)}): {recibidas}\n'
                'El firmware y los scripts no son de la misma versión.')
        if self.filas > 0:
            # Una cabecera a mitad de grabación significa que el coordinador se ha reiniciado:
            # el reloj y el número de ciclo vuelven a 0 y las marcas dejarían de cuadrar.
            raise ErrorGrabacion('El coordinador se ha reiniciado durante la grabación. '
                                 'Lo grabado hasta aquí se conserva; empieza una grabación nueva.')
        self.cabecera_validada = True
        print('Cabecera RAW_HEADER del coordinador validada.')

    def _fila(self, texto):
        valores = texto.split(',')
        if len(valores) != len(self.columnas):
            self.lineas_corruptas += 1
            return
        try:
            ciclo = int(valores[0])
            tiempo_ms = int(valores[1])
        except ValueError:
            self.lineas_corruptas += 1
            return

        if self.ultimo_ciclo is not None:
            if ciclo <= self.ultimo_ciclo:
                raise ErrorGrabacion(f'El número de ciclo ha retrocedido ({self.ultimo_ciclo} -> {ciclo}): '
                                     'el coordinador se ha reiniciado. Empieza una grabación nueva.')
            self.ciclos_perdidos += ciclo - self.ultimo_ciclo - 1

        if self.filas == 0 and not self.cabecera_validada:
            print('AVISO: no se recibió RAW_HEADER (el grabador arrancó después que el coordinador). '
                  'Solo se ha comprobado el número de columnas.')

        self.w_crudo.writerow(valores)
        self.f_crudo.flush()
        self.ultimo_ciclo, self.ultimo_tiempo_ms = ciclo, tiempo_ms
        self.filas += 1

        if self.fase_inicial_pendiente:
            # La fase inicial cubre desde la primera fila grabada
            self._escribir_marca(ciclo, tiempo_ms, self.fase_inicial)
            self.fase_inicial_pendiente = False

    # --- Marcas -----------------------------------------------------------

    def marcar(self, tecla):
        """Pone una marca de fase. Devuelve un texto para mostrar, o None si la tecla no es de fase."""
        fase = self.teclas.get(tecla)
        if fase is None:
            return None
        if self.ultimo_ciclo is None:
            # Aún no ha llegado ninguna fila: la fase elegida pasa a ser la inicial
            self.fase_inicial, self.fase_inicial_pendiente = fase, True
            return f'Fase inicial: {fase} (empezará con la primera fila recibida)'
        # La fase nueva empieza en el ciclo siguiente al último recibido
        self._escribir_marca(self.ultimo_ciclo + 1, self.ultimo_tiempo_ms, fase)
        return f'MARCA: {fase} desde el ciclo {self.ultimo_ciclo + 1}'

    def _escribir_marca(self, desde_ciclo, tiempo_ms, fase):
        self.w_marcas.writerow([desde_ciclo, tiempo_ms, fase, datetime.now().isoformat(timespec='milliseconds')])
        self.f_marcas.flush()
        self.marcas += 1
        self.fase_actual = fase

    # --- Estado y cierre ----------------------------------------------------

    def estado(self):
        return (f'ciclo {self.ultimo_ciclo if self.ultimo_ciclo is not None else "-"} | fase {self.fase_actual or "-"} | '
                f'filas {self.filas} | perdidos {self.ciclos_perdidos} | corruptas {self.lineas_corruptas} | marcas {self.marcas}')

    def cerrar(self, motivo):
        """Cierra los archivos y escribe resumen_<ensayo>.json. Devuelve los datos de la grabación."""
        resumen = {
            'fin': datetime.now().isoformat(timespec='seconds'),
            'motivo_fin': motivo,
            'filas_crudo': self.filas,
            'ciclos_perdidos': self.ciclos_perdidos,
            'lineas_corruptas': self.lineas_corruptas,
            'marcas': self.marcas,
            'cabecera_validada': self.cabecera_validada,
        }
        self.info.update(resumen)
        for f in (self.f_crudo, self.f_marcas, self.f_monitor):
            f.close()
        escribir_json(self.carpeta / f'resumen_{self.ensayo}.json', {
            'fecha': self.fecha.strftime('%Y-%m-%d %H:%M'),
            'descripcion': '',            # Para rellenar a mano: qué se hizo en el ensayo
            'grabacion': self.info,       # El exportador añadirá después la parte "procesado"
        })
        return resumen


# --- Registro de ensayos de las placas (ensayos_placas.json) --------------------

def escribir_json(ruta, datos):
    Path(ruta).write_text(json.dumps(datos, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def leer_registro():
    """Lee ensayos_placas.json (o devuelve uno nuevo si no existe). Lanza ErrorGrabacion si está mal escrito."""
    if not REGISTRO_PLACAS.exists():
        return {'general': dict(PARAMETROS_GENERALES_PLACAS), 'ensayos': []}
    try:
        registro = json.loads(REGISTRO_PLACAS.read_text(encoding='utf-8'))
    except json.JSONDecodeError as e:
        raise ErrorGrabacion(f'{REGISTRO_PLACAS.name} no es un JSON válido (línea {e.lineno}, columna {e.colno}): '
                             f'{e.msg}. Corrígelo antes de grabar (suele ser una coma de más o de menos).')
    registro.setdefault('general', dict(PARAMETROS_GENERALES_PLACAS))
    registro.setdefault('ensayos', [])
    return registro


def registrar_ensayo(ensayo):
    """Añade (o actualiza, si ya estaba) la entrada del ensayo en ensayos_placas.json."""
    entrada = {
        'nombre': ensayo,
        'crudo': f'{ensayo}/crudo/crudo_{ensayo}.csv',
        'marcas': f'{ensayo}/crudo/marcas_{ensayo}.csv',
        'resumen': f'{ensayo}/resumen_{ensayo}.json',
    }
    try:
        registro = leer_registro()
        # Si se sobrescribió un ensayo, su entrada se sustituye (los resultados de antes ya no existen)
        registro['ensayos'] = [e for e in registro['ensayos'] if e.get('nombre') != ensayo] + [entrada]
        escribir_json(REGISTRO_PLACAS, registro)
        print(f'Ensayo apuntado en {REGISTRO_PLACAS.name}.')
    except (ErrorGrabacion, OSError) as e:
        # La grabación ya está guardada: solo falta apuntarla
        print(f'\nNO se ha podido apuntar el ensayo en {REGISTRO_PLACAS.name}: {e}\n'
              'La grabación está guardada. Añade a mano esta entrada dentro de "ensayos":\n'
              + json.dumps(entrada, indent=2, ensure_ascii=False))


def linea_de_variable(nombre):
    """Línea de este script donde se define la variable (para indicar dónde cambiarla)."""
    for n, linea in enumerate(Path(__file__).read_text(encoding='utf-8').splitlines(), start=1):
        if re.match(rf'^{nombre}\s*=', linea):
            return n
    return '?'


def confirmar_sobrescritura(carpeta, ensayo):
    """Si el ensayo ya existe, pregunta si se sobrescribe. Devuelve True para seguir (borrándolo)."""
    if not carpeta.exists() or not any(carpeta.iterdir()):
        return True
    detalles = ''
    ruta_resumen = carpeta / f'resumen_{ensayo}.json'
    try:
        r = json.loads(ruta_resumen.read_text(encoding='utf-8'))
        detalles = f" (grabado el {r.get('fecha', '?')}, {r.get('grabacion', {}).get('filas_crudo', '?')} ciclos)"
    except Exception:
        pass
    print(f"El ensayo '{ensayo}' ya existe{detalles}.")
    while True:
        respuesta = input('¿Sobrescribirlo? Se borrará entero, también lo exportado y su descripción '
                          '[1 = sí / 0 = no]: ').strip()
        if respuesta == '1':
            shutil.rmtree(carpeta)
            print('Ensayo anterior borrado.\n')
            return True
        if respuesta == '0':
            print(f'Introduzca nuevo nombre en la variable "ENSAYO" (línea {linea_de_variable("ENSAYO")}) '
                  f'de {Path(__file__).name}.')
            return False


# --- Teclado sin bloquear (Windows y Linux/macOS) -----------------------------

class Teclado:
    def __enter__(self):
        if sys.platform == 'win32':
            import msvcrt
            self._msvcrt = msvcrt
        else:
            import termios, tty
            self._fd = sys.stdin.fileno()
            self._termios = termios
            self._antes = termios.tcgetattr(self._fd)
            tty.setcbreak(self._fd)
        return self

    def tecla(self):
        """Devuelve la tecla pulsada (en minúscula) o None si no hay ninguna."""
        if sys.platform == 'win32':
            if self._msvcrt.kbhit():
                return self._msvcrt.getwch().lower()
            return None
        import select
        if select.select([sys.stdin], [], [], 0)[0]:
            return sys.stdin.read(1).lower()
        return None

    def __exit__(self, *exc):
        if sys.platform != 'win32':
            self._termios.tcsetattr(self._fd, self._termios.TCSADRAIN, self._antes)


def reiniciar_esp32(ser):
    """Reinicia la ESP32 por el puerto serie, con el mismo pulso que usa esptool ("hard reset").
    En las placas ESP32 con reinicio automático, RTS controla el pin EN (reset) y DTR el pin GPIO0."""
    ser.dtr = False     # GPIO0 en alto: al arrancar, ejecuta el programa (no entra en modo de carga)
    ser.rts = True      # EN a nivel bajo: la placa queda en reset
    time.sleep(0.2)
    ser.rts = False     # EN libre: la placa arranca
    ser.reset_input_buffer()


def commit_repositorio():
    try:
        r = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], capture_output=True, text=True,
                           cwd=Path(__file__).parent, timeout=5)
        return r.stdout.strip() or None
    except Exception:
        return None


def main():
    teclas = {k.lower(): v for k, v in TECLAS.items()}
    tecla_salir = TECLA_SALIR.lower()
    if tecla_salir in teclas:
        sys.exit(f'La tecla de salida "{tecla_salir}" también está asignada a una fase.')
    if not NOMBRE_VALIDO.match(ENSAYO):
        sys.exit(f'El nombre del ensayo "{ENSAYO}" no es válido: solo letras sin tildes, números, "_" y "-" '
                 f'(sin espacios). Cámbialo en la variable "ENSAYO" (línea {linea_de_variable("ENSAYO")}).')
    try:
        leer_registro()     # Que esté bien escrito ANTES de la prueba, no al terminarla
    except ErrorGrabacion as e:
        sys.exit(str(e))

    carpeta = CARPETA_PLACAS / ENSAYO
    if not confirmar_sobrescritura(carpeta, ENSAYO):
        return

    import serial  # pyserial
    info = {'puerto': PUERTO, 'baudios': BAUDIOS, 'commit_repositorio': commit_repositorio(), 'teclas': teclas}
    grabador = Grabador(carpeta, ENSAYO, teclas, FASE_INICIAL, info)

    print(f'Grabando "{ENSAYO}" desde {PUERTO} en {carpeta}')
    print('Teclas: ' + ', '.join(f'{k} = {v}' for k, v in teclas.items()) + f', {tecla_salir} = terminar\n')

    motivo = 'tecla de salida'
    try:
        with serial.Serial(PUERTO, BAUDIOS, timeout=0.05) as ser, Teclado() as teclado:
            if REINICIAR_COORDINADOR:
                reiniciar_esp32(ser)
                print('Coordinador reiniciado. Esperando su cabecera RAW_HEADER...')
            inicio = time.time()
            aviso_sin_datos = False
            ultimo_estado = 0.0
            while True:
                linea = ser.readline()
                if linea:
                    texto = linea.decode('utf-8', errors='replace')
                    if VER_MONITOR:
                        print(texto.rstrip())
                    grabador.procesar_linea(texto)

                t = teclado.tecla()
                if t == tecla_salir:
                    break
                if t:
                    mensaje = grabador.marcar(t)
                    if mensaje:
                        print('\n' + mensaje)

                if not aviso_sin_datos and grabador.filas == 0 and not grabador.cabecera_validada and time.time() - inicio > 5:
                    aviso_sin_datos = True
                    print('\nAVISO: en 5 s no ha llegado la cabecera del coordinador. Si la placa no se ha reiniciado '
                          'sola, pulsa su botón EN/RST ahora.')

                if not VER_MONITOR and time.time() - ultimo_estado > 0.5:
                    print('\r' + grabador.estado() + '   ', end='', flush=True)
                    ultimo_estado = time.time()
    except KeyboardInterrupt:
        motivo = 'Ctrl+C'
    except ErrorGrabacion as e:
        motivo = f'error: {e}'
        print(f'\n\nGRABACIÓN DETENIDA: {e}')
    except serial.SerialException as e:
        motivo = f'error del puerto serie: {e}'
        print(f'\n\nError con el puerto serie: {e}')

    terminar(grabador, motivo)


def terminar(grabador, motivo):
    """Cierra la grabación y, si se grabó algo, la apunta en el registro."""
    resumen = grabador.cerrar(motivo)
    print('\n\nResumen de la grabación:')
    for k, v in resumen.items():
        print(f'  {k}: {v}')
    print(f'Archivos en {grabador.carpeta}')
    if grabador.filas > 0:
        registrar_ensayo(grabador.ensayo)
    else:
        print(f'No se ha grabado ninguna fila: el ensayo NO se apunta en {REGISTRO_PLACAS.name}.')


if __name__ == '__main__':
    main()
