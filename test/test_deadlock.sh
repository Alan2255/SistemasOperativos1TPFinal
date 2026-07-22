#!/bin/bash
#
# test_deadlock.sh
# ------------------------------------------------------------------
# Prueba automatizada pedida por la consigna (TP §9.d y §6):
# levanta DOS nodos completos (agente C + planificador Erlang) en el
# mismo host, en puertos distintos, les inyecta el escenario de
# deadlock cruzado del enunciado y verifica que el sistema lo
# resuelva (mediante el orden global CPU->MEM->GPU, o mediante el
# timeout + "POSIBLE DEADLOCK" si el orden no alcanza a evitarlo).
#
# Se ejecuta desde CUALQUIER lugar: ./test/test_deadlock.sh
# ------------------------------------------------------------------

# 'set -u' hace que el script corte con error si uso una variable que
# no existe (typo en un nombre, por ejemplo), en vez de seguir con un
# valor vacío silenciosamente. Es una red de seguridad barata.
set -u

# ============================================================
# 0. UBICARNOS SIEMPRE EN LA RAIZ DEL PROYECTO
# ============================================================
# $0 es la ruta con la que se invocó este script (puede ser relativa).
# dirname "$0"      -> carpeta que contiene el script (.../test)
# cd .../test/..    -> sube un nivel: la raíz del proyecto
# Así el script funciona sin importar desde qué directorio lo llames.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT" || exit 1

# ============================================================
# 1. CONFIGURACION DEL ESCENARIO (TP §6)
# ============================================================
# Tiempo entre cada request
TIME_PER_REQUEST=1

# Puertos TCP de cada nodo. Deben estar entre 7000 y 15000
# (lo valida get_port_and_resources.c).
PUERTO_A=8100
PUERTO_B=8200

# Recursos de cada nodo, tal cual los define el enunciado:
#   Nodo A: 2 CPUs, 8192 MB RAM, 0 GPUs
#   Nodo B: 2 CPUs, 4096 MB RAM, 1 GPU
RECURSOS_A="cpu mem gpu 2 0 0"
RECURSOS_B="cpu mem gpu 0 0 1"

# Carpeta donde van a quedar los logs de esta corrida (no rompe nada
# si ya existe gracias a "-p").
LOGS_DIR="$PROJECT_ROOT/logs"
mkdir -p "$LOGS_DIR"

# El planificador Erlang loguea siempre en "../scheduler.log" tomando
# como referencia el directorio desde el que se arrancó "erl" (no la
# carpeta del .erl). Por eso más abajo SIEMPRE arrancamos erl parados
# adentro de erlang/, para que ese "../scheduler.log" caiga acá.
SCHEDULER_LOG="$PROJECT_ROOT/scheduler.log"

# Los agentes C anuncian su presencia por UDP broadcast y la IP que
# usa el resto de los nodos para identificarlos es la que ve el
# receptor con recvfrom() -- NUNCA 127.0.0.1 en un broadcast real.
# Por eso necesitamos la IP "de verdad" de esta máquina, no localhost.
# hostname -I    -> lista todas las IPs de red de la máquina
# awk '{print $1}' -> nos quedamos con la primera
LOCAL_IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
if [ -z "$LOCAL_IP" ]; then
    echo "ADVERTENCIA: no pude detectar una IP de red, uso 127.0.0.1 (puede no funcionar el broadcast UDP)."
    LOCAL_IP="127.0.0.1"
fi
echo "IP local detectada: $LOCAL_IP"

# ============================================================
# 2. LIMPIEZA / CLEANUP
# ============================================================
# Acá vamos a ir guardando los PID (identificadores de proceso) de
# todo lo que lancemos en background, para poder matarlo al final.
PIDS=()

cleanup() {
    echo ""
    echo "=== Deteniendo procesos de la prueba ==="
    # Recorremos el array de PIDs guardados y los matamos si siguen vivos.
    for pid in "${PIDS[@]:-}"; do
        # kill -0 no mata a nadie, solo pregunta "¿este PID existe?"
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
        fi
    done
    # Pequeño margen para que terminen prolijo antes de matar fuerte.
    sleep 1
    for pid in "${PIDS[@]:-}"; do
        kill -9 "$pid" 2>/dev/null
    done
}
# trap: si el script termina por EXIT (normal), o lo cortás con
# Ctrl+C (INT) o alguien lo mata con kill (TERM), se ejecuta cleanup
# de todas formas. Así nunca quedan agentes/beam.smp colgados.
trap cleanup EXIT INT TERM

# ============================================================
# 3. COMPILACION
# ============================================================
echo "=== [1/5] Compilando agente C ==="
gcc -ggdb3 -Wall -Wextra \
    ./c/agent.c ./c/functions/*.c ./c/structures/*.c \
    -lpthread -o agent
if [ $? -ne 0 ]; then
    echo "ERROR: no se pudo compilar el agente C."
    exit 1
fi
echo "OK: ./agent generado."

echo "=== [2/5] Compilando planificador Erlang ==="
erlc -o erlang erlang/*.erl
if [ $? -ne 0 ]; then
    echo "ERROR: no se pudo compilar el código Erlang."
    exit 1
fi
echo "OK: módulos .beam generados en erlang/."

rm -f "$SCHEDULER_LOG"

# ============================================================
# 4. LEVANTAR LOS DOS AGENTES C
# ============================================================
echo "=== [3/5] Levantando agentes C ==="

stdbuf -oL ./agent $TIME_PER_REQUEST "$PUERTO_A" 3 $RECURSOS_A > "$LOGS_DIR/agente_A.log" 2>&1 &
PID_AGENTE_A=$!
PIDS+=("$PID_AGENTE_A")
echo "  Nodo A en puerto $PUERTO_A (PID $PID_AGENTE_A) -> $RECURSOS_A"

stdbuf -oL ./agent $TIME_PER_REQUEST "$PUERTO_B" 3 $RECURSOS_B > "$LOGS_DIR/agente_B.log" 2>&1 &
PID_AGENTE_B=$!
PIDS+=("$PID_AGENTE_B")
echo "  Nodo B en puerto $PUERTO_B (PID $PID_AGENTE_B) -> $RECURSOS_B"

echo "  Esperando descubrimiento UDP entre nodos (4s)..."
sleep 10

if ! kill -0 "$PID_AGENTE_A" 2>/dev/null; then
    echo "ERROR: el agente A murió al iniciar. Ver $LOGS_DIR/agente_A.log"
    exit 1
fi
if ! kill -0 "$PID_AGENTE_B" 2>/dev/null; then
    echo "ERROR: el agente B murió al iniciar. Ver $LOGS_DIR/agente_B.log"
    exit 1
fi

# ============================================================
# 5. LEVANTAR LOS DOS PLANIFICADORES ERLANG E INYECTAR LOS JOBS
# ============================================================
echo "=== [4/5] Levantando schedulers e inyectando Job1 y Job2 ==="

JOBTIME=1
TIMEOUT=20

JOB1="$LOCAL_IP:$PUERTO_A:cpu:2 $LOCAL_IP:$PUERTO_B:gpu:1"
JOB2="$LOCAL_IP:$PUERTO_B:gpu:1 $LOCAL_IP:$PUERTO_A:cpu:2"

(
    cd erlang || exit 1
    erl -noshell -pa . \
        -eval "compile:file(main), compile:file(parser), compile:file(system_init), compile:file(tcp_connection), compile:file(job_manager), \
                main:server(manual, 0, $PUERTO_A, $JOBTIME, $TIMEOUT), timer:sleep(1000), cliente_pid ! {\"$JOB1\"}, timer:sleep(9000)" \
        -s init stop
) > "$LOGS_DIR/scheduler_A.log" 2>&1 &
PID_ERL_A=$!
PIDS+=("$PID_ERL_A")
echo "  Scheduler A (PID $PID_ERL_A) -> Job1: $JOB1"

(
    cd erlang || exit 1
    erl -noshell -pa . \
        -eval "compile:file(main), compile:file(parser), compile:file(system_init), compile:file(tcp_connection), compile:file(job_manager), \
                main:server(manual, 0, $PUERTO_B, $JOBTIME, $TIMEOUT), timer:sleep(1000), cliente_pid ! {\"$JOB2\"}, timer:sleep(9000)" \
        -s init stop
) > "$LOGS_DIR/scheduler_B.log" 2>&1 &
PID_ERL_B=$!
PIDS+=("$PID_ERL_B")
echo "  Scheduler B (PID $PID_ERL_B) -> Job2: $JOB2"

echo "  Esperando resolución de los jobs (8s)..."
sleep 8

# ============================================================
# 6. ANALISIS DE RESULTADOS
# ============================================================
echo "=== [5/5] Resultado ==="

if [ ! -f "$SCHEDULER_LOG" ]; then
    echo "TEST: FALLIDO (no se generó $SCHEDULER_LOG, revisá los logs en $LOGS_DIR/)"
    exit 1
fi

echo "--- Contenido de scheduler.log ---"
cat "$SCHEDULER_LOG"
echo "-----------------------------------"

CANT_ENTRADAS=$(wc -l < "$SCHEDULER_LOG")

if grep -q "POSIBLE DEADLOCK" "$SCHEDULER_LOG"; then
    echo "Se detectó un posible deadlock y el sistema lo resolvió vía timeout (liberó recursos y siguió)."
fi

if [ "$CANT_ENTRADAS" -ge 2 ]; then
    echo ""
    echo "TEST: OK  (ambos jobs terminaron resueltos, sin quedar bloqueados para siempre)"
else
    echo ""
    echo "TEST: FALLIDO  (esperaba al menos 2 entradas en el log, hay $CANT_ENTRADAS)"
    exit 1
fi
