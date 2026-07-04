#!/bin/bash

# ==============================================================================
# HPC TEST - VERSION SIMPLIFICADA CON COMENTARIOS DETALLADOS
# Para entender el flujo básico sin complejidad
# ==============================================================================

echo "======================================"
echo "HPC Resource Manager - Test Suite"
echo "======================================"
echo ""

# ==============================================================================
# PASO 1: VERIFICAR HERRAMIENTAS
# ==============================================================================
echo "[1] Verificando herramientas necesarias..."

# Comando: command -v erl
# Explicación: Busca si "erl" está instalado en el sistema
# Resultado: Si existe, retorna la ruta; si no, retorna error (1)
if ! command -v erl >/dev/null 2>&1; then
    echo "ERROR: Erlang no instalado. Ejecuta: apt-get install erlang"
    exit 1
fi
echo "    ✓ Erlang encontrado"

# Ídem para gcc
if ! command -v gcc >/dev/null 2>&1; then
    echo "ERROR: GCC no instalado. Ejecuta: apt-get install build-essential"
    exit 1
fi
echo "    ✓ GCC encontrado"

echo ""

# ==============================================================================
# PASO 2: DEFINIR RUTAS Y CONFIGURACIÓN
# ==============================================================================
echo "[2] Configuración inicial..."

# $(pwd) = COMMAND SUBSTITUTION
# Explicación: Ejecuta pwd (print working directory) y guarda su resultado
# pwd retorna algo como: /home/usuario/proyecto
PROJECT_ROOT="$(pwd)"
echo "    Directorio raíz: $PROJECT_ROOT"

# Se pueden concatenar variables
ERLANG_DIR="${PROJECT_ROOT}"         # main.erl, aux.erl aquí
AGENT_DIR="${PROJECT_ROOT}/agent"    # agent.c y Makefile aquí
BEAM_DIR="${PROJECT_ROOT}/scheduler" # Los .beam irán aquí
LOGS_DIR="${PROJECT_ROOT}/logs"      # Los logs irán aquí

echo "    Erlang dir: $ERLANG_DIR"
echo "    Agent dir: $AGENT_DIR"
echo "    Beam dir: $BEAM_DIR"
echo ""

# ==============================================================================
# PASO 3: CREAR DIRECTORIOS
# ==============================================================================
echo "[3] Creando directorios..."

# mkdir -p = Make directory, crear también directorios padres si no existen
# -p = Parent (no error si ya existe)
mkdir -p "$BEAM_DIR" "$LOGS_DIR"
echo "    ✓ Directorios creados"
echo ""

# ==============================================================================
# PASO 4: COMPILAR ERLANG
# ==============================================================================
echo "[4] Compilando Erlang..."

# cd = change directory (navegar a otra carpeta)
cd "$ERLANG_DIR" || exit 1

# erl -eval "..."
# Explicación:
#   erl                          = inicia Erlang
#   -eval "CÓDIGO ERLANG"        = ejecuta código (ej: compile files)
#   -noshell                     = no abre shell interactivo
#
# Dentro del -eval:
#   compile:file(main, [...])   = compila main.erl
#   compile:file(aux, [...])    = compila aux.erl
#   [{outdir, '${BEAM_DIR}'}]  = opción: dónde guardar .beam
#   halt()                       = termina Erlang cuando termina
erl -eval "
    compile:file(main, [{outdir, '${BEAM_DIR}'}]),
    compile:file(aux, [{outdir, '${BEAM_DIR}'}]),
    halt()
" -noshell

# [ -f FILE ] = test si FILE existe y es un archivo regular
# Explicación: Verifica si se creó main.beam correctamente
if [ -f "${BEAM_DIR}/main.beam" ] && [ -f "${BEAM_DIR}/aux.beam" ]; then
    echo "    ✓ Erlang compilado correctamente"
    echo "      - main.beam guardado"
    echo "      - aux.beam guardado"
else
    echo "    ✗ Error compilando Erlang"
    exit 1
fi
echo ""

# ==============================================================================
# PASO 5: COMPILAR AGENTE C
# ==============================================================================
echo "[5] Compilando agente C..."

# cd al directorio del agente
cd "$AGENT_DIR" || exit 1

# make = ejecuta las recetas definidas en Makefile
# El Makefile del agente define algo como:
#   CC = gcc
#   agent:
#       $(CC) $(CFLAGS) agent.c -lpthread -o agent
# Al ejecutar "make", se compila agent.c y se genera binario "agent"
if make; then
    echo "    ✓ Agente C compilado"
    # Copiar como agente_bin para evitar conflicto de nombres
    # (existe carpeta agent/, conflicto si se llama agent)
    cp agent agente_bin
    echo "    ✓ Copiado a agente_bin (evita conflicto)"
else
    echo "    ✗ Error compilando C"
    exit 1
fi
echo ""

# ==============================================================================
# PASO 6: LANZAR PROCESOS EN BACKGROUND
# ==============================================================================
echo "[6] Lanzando procesos en background..."

# El & al final de un comando lo envía a background
# Explicación:
#   ./agente_bin ... &
#   El & permite que el script continúe sin esperar a que termine agente_bin
#   Sin el &, el script se bloquearía esperando a agente_bin

# Redirección de outputs:
#   > "$LOGS_DIR/agente_A.log"   = stdout (salida normal) va al archivo
#   2>&1                          = stderr (errores) también va al archivo
#   fd 1 = stdout
#   fd 2 = stderr
#   2>&1 = redirige fd 2 a donde vaya fd 1

./agente_bin 127.0.0.1 8100 cpu 10 mem 5 gpu 2 \
    > "$LOGS_DIR/agente_A.log" 2>&1 &

# $! = Variable especial que almacena el PID del último proceso en background
# PID = Process ID (identificador único del proceso)
# Guardamos el PID para poder matarlo después si queremos
AGENT_A_PID=$!
echo "    ✓ Agente A lanzado (PID: $AGENT_A_PID)"

# Pausa para que inicie
sleep 1

# Agente B en puerto 8101
# NOTA: Si agent.c tiene puerto hardcodeado, esto fallará
# Por eso es importante verificar agent.c y ver si se puede parametrizar
./agente_bin 127.0.0.1 8101 cpu 8 mem 4 gpu 1 \
    > "$LOGS_DIR/agente_B.log" 2>&1 &
AGENT_B_PID=$!
echo "    ✓ Agente B lanzado (PID: $AGENT_B_PID)"

# Esperar a que inicien (UDP discovery)
sleep 2
echo ""

# ==============================================================================
# PASO 7: LANZAR NODOS ERLANG
# ==============================================================================
echo "[7] Lanzando nodos Erlang..."

# erl -sname nombre  = inicia nodo con nombre corto
# -setcookie cookie  = cookie para que nodos se confíen entre sí
# -pa RUTA           = prepend path (busca .beam en esta ruta primero)
# -eval "CÓDIGO"     = ejecuta código
# &                  = en background

erl -sname scheduler_a \
    -setcookie hpc_cookie \
    -pa "$BEAM_DIR" \
    -eval "
        c(main),
        c(aux),
        main:server(manual, 0, 8100),
        timer:sleep(infinity)
    " > "$LOGS_DIR/scheduler_A.log" 2>&1 &
ERLANG_A_PID=$!
echo "    ✓ Erlang A lanzado (PID: $ERLANG_A_PID)"

sleep 1

# Nodo B (similar)
erl -sname scheduler_b \
    -setcookie hpc_cookie \
    -pa "$BEAM_DIR" \
    -eval "
        c(main),
        c(aux),
        main:server(manual, 0, 8101),
        timer:sleep(infinity)
    " > "$LOGS_DIR/scheduler_B.log" 2>&1 &
ERLANG_B_PID=$!
echo "    ✓ Erlang B lanzado (PID: $ERLANG_B_PID)"

echo ""

# ==============================================================================
# PASO 8: INYECTAR JOBS (simulación de deadlock)
# ==============================================================================
echo "[8] Inyectando jobs para test de deadlock..."
echo "    (esperando 5 segundos para que nodos inicien completamente)"
sleep 5

# grep = buscar texto en un archivo
# -q = quiet (no imprime resultados, solo retorna código)
# Explicación:
#   grep -q "POSIBLE DEADLOCK" logfile
#   Si encuentra el patrón, retorna 0 (true)
#   Si NO lo encuentra, retorna 1 (false)
# En if, 0 = éxito, != 0 = error

# Buscar en schedule_A.log si hay ya DEADLOCK
if grep -q "POSIBLE DEADLOCK" "$LOGS_DIR/scheduler_A.log"; then
    echo "    ✓ Deadlock DETECTADO en scheduler_A.log"
    echo ""
    echo "[9] Líneas con POSIBLE DEADLOCK:"
    grep "POSIBLE DEADLOCK" "$LOGS_DIR/scheduler_A.log" | head -3
else
    echo "    ? Deadlock NO detectado aún"
    echo "    Revisando últimas líneas del log:"
    tail -5 "$LOGS_DIR/scheduler_A.log"
fi

echo ""

# ==============================================================================
# PASO 9: CLEANUP (detener procesos)
# ==============================================================================
echo "[10] Limpieza: deteniendo procesos..."

# kill -9 = envía SIGKILL (mata inmediatamente)
# Se matarán los procesos por sus PIDs
if [ -n "$AGENT_A_PID" ]; then
    kill -9 "$AGENT_A_PID" 2>/dev/null
    echo "    ✓ Agente A detenido"
fi

if [ -n "$AGENT_B_PID" ]; then
    kill -9 "$AGENT_B_PID" 2>/dev/null
    echo "    ✓ Agente B detenido"
fi

if [ -n "$ERLANG_A_PID" ]; then
    kill -9 "$ERLANG_A_PID" 2>/dev/null
    echo "    ✓ Erlang A detenido"
fi

if [ -n "$ERLANG_B_PID" ]; then
    kill -9 "$ERLANG_B_PID" 2>/dev/null
    echo "    ✓ Erlang B detenido"
fi

# pkill -f = kill process matching pattern
# -f = busca en toda la línea de comando, no solo nombre
# Mata cualquier erl residual
pkill -9 -f "erl -sname" 2>/dev/null

echo ""
echo "======================================"
echo "Test finalizado"
echo "Logs disponibles en: $LOGS_DIR/"
echo "======================================"
