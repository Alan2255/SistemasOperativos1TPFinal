#!/bin/bash

# --- Configuración ---
NODOS_PUERTOS=(8081 8082)
LOG_FILE="test_deadlock.log"

# Crear log vacío para evitar errores de grep
touch $LOG_FILE

echo "=== Iniciando compilación ==="
make -C agente
erlc aux.erl main.erl
echo "=== Compilación finalizada ==="

# Función para limpiar
cleanup() {
    echo -e "\n=== Deteniendo procesos ==="
    pkill -f "agente/agent"
    pkill -f "beam.smp" # Proceso real de Erlang
    exit
}
trap cleanup SIGINT SIGTERM

# --- Levantamiento del Sistema ---
echo "=== Iniciando Nodos (C + Erlang) ==="
# Argumentos: <puerto> <n_recursos> <nombres...> <cantidades...>
for PORT in "${NODOS_PUERTOS[@]}"; do
    echo "Levantando nodo en puerto $PORT"
    
    # IMPORTANTE: Corregí la ruta a ./agente/agent
    ./agente/agent $PORT 3 cpu mem gpu 4 16 2 & 
    
    sleep 3 # Esperar a que el agente C inicie
    
    erl -noshell -eval "main:server(random, 5, $PORT)" &
    sleep 2
done

echo "=== Sistema en ejecución. Esperando estabilización (10s) ==="
sleep 10

# --- Ejecución del test ---
echo "=== Enviando carga de trabajo ==="
erl -noshell -eval "main:client(random, 10, 8081), init:stop()."

# --- Análisis ---
echo "=== Análisis de logs ==="
if [ -f "$LOG_FILE" ]; then
    if grep -q "DEADLOCK" $LOG_FILE; then
        echo "¡ALERTA: Se detectó un Deadlock!"
    else
        echo "Ejecución finalizada sin bloqueos detectados."
    fi
else
    echo "El archivo de log no fue creado por el agente."
fi

cleanup