#!/bin/bash

#============================================================================
# HPC Resource Manager - Test Suite
# Rol 4: Sistemas Operativos 1 - FCEIA-UNR
#
# Propósito: Compilar, lanzar dos nodos Erlang, inyectar jobs para
#           triggear deadlock, y detectar "POSIBLE DEADLOCK" en logs.
#
# Uso: ./hpc_test_suite.sh [opción]
#      Opciones: compile, run_nodes, test_deadlock, cleanup
#============================================================================

# ============================================================================
# CONFIGURACIÓN INICIAL
# ============================================================================
# Colores para output legible
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Paths
PROJECT_ROOT="$(pwd)"
ERLANG_DIR="${PROJECT_ROOT}"                    # main.erl, aux.erl en raíz
AGENT_DIR="${PROJECT_ROOT}/agent"              # C agent en carpeta agent/
BEAM_DIR="${PROJECT_ROOT}/scheduler"            # .beam compilados aquí
LOGS_DIR="${PROJECT_ROOT}/logs"                # Logs de ejecución

# Puertos y configuración
# ⚠️  LIMITACIÓN CONOCIDA: Puerto TCP 8100 está hardcodeado en agent.c
#     Esto impide ejecutar 2 agentes en la misma máquina localmente.
#     Solución futura: parametrizar puerto en agent.c
AGENT_PORT_A=8100
AGENT_PORT_B=8101                              # Intenta B, pero verifica agent.c

# Erlang config
ERLANG_COOKIE="hpc_cookie"
ERLANG_NODE_A="scheduler_a@localhost"
ERLANG_NODE_B="scheduler_b@localhost"
ERLANG_TIMEOUT_SEC=30                          # Timeout para conexiones TCP en Erlang

# Job config para deadlock
# El deadlock ocurre cuando:
#   - Nodo A hace RESERVE por cpu:10 en Nodo B
#   - Nodo B hace RESERVE por mem:10 en Nodo A
#   - Ambos quedan en timeout (5 segundos según anti-deadlock en aux.erl)
JOB_TIMEOUT_MS=5000
NUM_JOBS=2                                      # Pocos jobs para test rápido

# ============================================================================
# FUNCIONES AUXILIARES
# ============================================================================

# Print helper: con prefijo y color
print_step() {
    echo -e "${BLUE}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

# Verificar si un comando existe
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Esperar a que un archivo contenga una cadena (útil para logs)
wait_for_log_entry() {
    local logfile=$1
    local pattern=$2
    local timeout=$3  # segundos
    local elapsed=0

    print_step "Esperando por pattern '$pattern' en $logfile (timeout: ${timeout}s)"
    
    while [ $elapsed -lt $timeout ]; do
        if grep -q "$pattern" "$logfile" 2>/dev/null; then
            print_success "Patrón encontrado: '$pattern'"
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    
    print_error "Timeout: patrón '$pattern' no encontrado en $timeout segundos"
    return 1
}

# Esperar a que un puerto esté disponible
wait_for_port() {
    local port=$1
    local timeout=$2  # segundos
    local elapsed=0

    print_step "Esperando puerto $port (timeout: ${timeout}s)"
    
    while [ $elapsed -lt $timeout ]; do
        if nc -z -w1 localhost "$port" 2>/dev/null; then
            print_success "Puerto $port disponible"
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    
    print_error "Timeout: puerto $port no disponible en $timeout segundos"
    return 1
}

# ============================================================================
# FUNCIÓN: COMPILAR
# ============================================================================
do_compile() {
    print_step "=== COMPILACIÓN ==="
    
    # Verificar herramientas
    if ! command_exists erl; then
        print_error "Erlang no instalado. Instala: apt-get install erlang"
        return 1
    fi
    
    if ! command_exists gcc; then
        print_error "GCC no instalado. Instala: apt-get install build-essential"
        return 1
    fi
    
    # Crear directorios
    mkdir -p "$BEAM_DIR" "$LOGS_DIR"
    
    # ========== COMPILAR ERLANG ==========
    # Compilamos main.erl y aux.erl a .beam en la carpeta scheduler/
    print_step "Compilando Erlang (main.erl, aux.erl)..."
    
    cd "$ERLANG_DIR" || return 1
    
    # Usamos erl -eval para compilar ambos módulos en una pasada
    erl -eval "
        compile:file(main, [{outdir, '${BEAM_DIR}'}]),
        compile:file(aux, [{outdir, '${BEAM_DIR}'}]),
        halt()
    " -noshell
    
    if [ -f "${BEAM_DIR}/main.beam" ] && [ -f "${BEAM_DIR}/aux.beam" ]; then
        print_success "Erlang compilado: main.beam, aux.beam"
    else
        print_error "Fallo compilación Erlang"
        return 1
    fi
    
    # ========== COMPILAR C ==========
    # Compilamos el agente C
    print_step "Compilando agente C..."
    
    cd "$AGENT_DIR" || return 1
    make clean >/dev/null 2>&1
    
    if make; then
        print_success "Agente C compilado: ./agent/agent"
        # Copiar como agente_bin para evitar conflicto con carpeta agent/
        cp agent agente_bin
        print_success "Copiado a agente_bin (evita conflicto de nombre)"
    else
        print_error "Fallo compilación C"
        return 1
    fi
    
    print_success "Compilación completada"
    return 0
}

# ============================================================================
# FUNCIÓN: LANZAR NODOS
# ============================================================================
do_run_nodes() {
    print_step "=== LANZAMIENTO DE NODOS ==="
    
    # ========== INICIAR AGENTES C EN BACKGROUND ==========
    # Cada agente escucha en su puerto TCP y hace UDP broadcast
    print_step "Iniciando agentes C..."
    
    cd "$AGENT_DIR" || return 1
    
    # Agente A en puerto 8100
    print_step "Agente A en puerto $AGENT_PORT_A"
    ./agente_bin 127.0.0.1 $AGENT_PORT_A cpu 10 mem 5 gpu 2 \
        > "$LOGS_DIR/agente_A.log" 2>&1 &
    AGENT_A_PID=$!
    print_success "Agente A PID: $AGENT_A_PID"
    
    sleep 1  # Pequeña pausa para que inicie
    
    # Agente B en puerto 8101
    # ⚠️  NOTA: Si agent.c tiene puerto hardcodeado, esto fallará.
    #     Verifica agent.c línea ~50 donde se define FIXED_PORT
    print_step "Agente B en puerto $AGENT_PORT_B"
    ./agente_bin 127.0.0.1 $AGENT_PORT_B cpu 8 mem 4 gpu 1 \
        > "$LOGS_DIR/agente_B.log" 2>&1 &
    AGENT_B_PID=$!
    print_success "Agente B PID: $AGENT_B_PID"
    
    sleep 2  # Esperar UDP discovery (se repite cada 30 segundos según ANNOUNCE_SEC)
    
    # ========== INICIAR NODOS ERLANG ==========
    # Los nodos Erlang se conectan via TCP a los agentes C
    print_step "Iniciando servidores Erlang (modo manual para control de jobs)..."
    
    # Nodo A: escucha conexiones Erlang distribuidas en puerto 12000
    print_step "Nodo A: $ERLANG_NODE_A"
    erl -sname scheduler_a \
        -setcookie "$ERLANG_COOKIE" \
        -pa "$BEAM_DIR" \
        -eval "
            % Compilamos (por si acaso)
            c(main),
            c(aux),
            % Iniciamos servidor en modo manual, 0 jobs, puerto 8100
            main:server(manual, 0, 8100),
            % El nodo seguirá corriendo hasta que termine el proceso cliente
            % Ver: aux:inicializar_sistema que conecta a agentes C
            timer:sleep(infinity)
        " \
        > "$LOGS_DIR/scheduler_A.log" 2>&1 &
    ERLANG_A_PID=$!
    print_success "Erlang A PID: $ERLANG_A_PID"
    
    sleep 1
    
    # Nodo B: idéntico al A
    print_step "Nodo B: $ERLANG_NODE_B"
    erl -sname scheduler_b \
        -setcookie "$ERLANG_COOKIE" \
        -pa "$BEAM_DIR" \
        -eval "
            c(main),
            c(aux),
            main:server(manual, 0, 8101),
            timer:sleep(infinity)
        " \
        > "$LOGS_DIR/scheduler_B.log" 2>&1 &
    ERLANG_B_PID=$!
    print_success "Erlang B PID: $ERLANG_B_PID"
    
    sleep 2  # Esperar que ambos inicien
    
    # Guardar PIDs para cleanup
    echo "$AGENT_A_PID" > "$LOGS_DIR/.pids"
    echo "$AGENT_B_PID" >> "$LOGS_DIR/.pids"
    echo "$ERLANG_A_PID" >> "$LOGS_DIR/.pids"
    echo "$ERLANG_B_PID" >> "$LOGS_DIR/.pids"
    
    print_success "Nodos iniciados. Logs en $LOGS_DIR/"
    return 0
}

# ============================================================================
# FUNCIÓN: TEST DEADLOCK
# ============================================================================
do_test_deadlock() {
    print_step "=== TEST DEADLOCK ==="
    
    # ========== INYECTAR JOBS MANUALMENTE ==========
    # Enviamos comandos Erlang a ambos nodos para que inyecten jobs
    # que causen RESERVE cruzadas (deadlock)
    
    print_step "Inyectando jobs para triggerear deadlock..."
    
    # Job 1: Nodo A pide cpu a Nodo B
    print_step "Job 1: Nodo A -> Nodo B (cpu:10)"
    echo "cliente_pid ! {job_1, 'cpu:10', 1}." | erl \
        -sname client_a \
        -setcookie "$ERLANG_COOKIE" \
        -noshell \
        -eval "
            % Conectamos al nodo A
            rpc:call(scheduler_a@localhost, erlang, register, [test_sender, self()]),
            rpc:call(scheduler_a@localhost, pid_scheduler_job, !, [\{job_1, 'cpu:10', 1\}]),
            timer:sleep(100),
            halt()
        " 2>/dev/null
    
    sleep 1
    
    # Job 2: Nodo B pide mem a Nodo A
    print_step "Job 2: Nodo B -> Nodo A (mem:10)"
    echo "cliente_pid ! {job_2, 'mem:10', 1}." | erl \
        -sname client_b \
        -setcookie "$ERLANG_COOKIE" \
        -noshell \
        -eval "
            rpc:call(scheduler_b@localhost, erlang, register, [test_sender, self()]),
            rpc:call(scheduler_b@localhost, pid_scheduler_job, !, [\{job_2, 'mem:10', 1\}]),
            timer:sleep(100),
            halt()
        " 2>/dev/null
    
    print_success "Jobs inyectados"
    
    # ========== DETECTAR DEADLOCK ==========
    # El anti-deadlock en aux.erl detecta timeout (5 segundos) y loguea "POSIBLE DEADLOCK"
    print_step "Esperando detección de deadlock (5-10 segundos)..."
    
    # Buscamos "POSIBLE DEADLOCK" en scheduler.log
    # Nota: La variable está en PID_scheduler_jobs.log, que Erlang crea automático
    wait_for_log_entry "$LOGS_DIR/scheduler_A.log" "POSIBLE DEADLOCK" 15
    DEADLOCK_FOUND=$?
    
    if [ $DEADLOCK_FOUND -eq 0 ]; then
        print_success "¡DEADLOCK DETECTADO!"
        echo ""
        print_step "Extrayendo líneas de deadlock:"
        grep "POSIBLE DEADLOCK" "$LOGS_DIR/scheduler_A.log" | head -5
    else
        print_warning "Deadlock NO detectado en el timeout esperado"
        print_step "Revisando logs:"
        tail -20 "$LOGS_DIR/scheduler_A.log"
    fi
    
    return 0
}

# ============================================================================
# FUNCIÓN: CLEANUP
# ============================================================================
do_cleanup() {
    print_step "=== CLEANUP ==="
    
    print_step "Deteniendo procesos..."
    
    if [ -f "$LOGS_DIR/.pids" ]; then
        while IFS= read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                print_step "Matando PID $pid"
                kill -9 "$pid" 2>/dev/null
                sleep 0.5
            fi
        done < "$LOGS_DIR/.pids"
        rm -f "$LOGS_DIR/.pids"
    fi
    
    # Matar cualquier erl restante (puede quedar si algo falló)
    pkill -9 -f "erl -sname" 2>/dev/null
    
    print_success "Cleanup completado"
    return 0
}

# ============================================================================
# MAIN: PARSEAR ARGUMENTOS
# ============================================================================
main() {
    if [ $# -eq 0 ]; then
        print_error "Uso: $0 [opción]"
        echo ""
        echo "Opciones:"
        echo "  compile              Compilar Erlang + C"
        echo "  run_nodes            Lanzar nodos (requiere compile previo)"
        echo "  test_deadlock        Inyectar jobs y detectar deadlock"
        echo "  cleanup              Detener todos los procesos"
        echo "  all                  Ejecutar todos los pasos en orden"
        echo ""
        echo "Ejemplo completo:"
        echo "  $0 all"
        exit 1
    fi
    
    case "$1" in
        compile)
            do_compile
            ;;
        run_nodes)
            do_run_nodes
            ;;
        test_deadlock)
            do_test_deadlock
            ;;
        cleanup)
            do_cleanup
            ;;
        all)
            print_step "Ejecutando pipeline completo..."
            do_compile && \
            do_run_nodes && \
            do_test_deadlock && \
            do_cleanup
            
            if [ $? -eq 0 ]; then
                print_success "Pipeline completado exitosamente"
            else
                print_error "Pipeline falló en algún paso"
                do_cleanup
                exit 1
            fi
            ;;
        *)
            print_error "Opción desconocida: $1"
            exit 1
            ;;
    esac
}

# Ejecutar
main "$@"
