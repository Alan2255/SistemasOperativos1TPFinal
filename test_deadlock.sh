#!/bin/bash
# Limpieza automática
trap "kill 0" EXIT

# ============================================
# CONFIGURACIÓN
# ============================================
PUERTO_A=8100
PUERTO_B=8200
CPU_A=2
RAM_A=8192
GPU_A=0
CPU_B=2
RAM_B=4096
GPU_B=1
BINARIO=./agente
LocalHost=127.0.0.1
LIMIT_INTENTOS=6

# ============================================
# PASO 1: Compilación
# ============================================
echo "Compilando C y Erlang..."
make clean && make
[ -f $BINARIO ] || { echo "Error: Binario no encontrado"; exit 1; }

# ============================================
# PASO 2: Levantar Agentes C
# ============================================
echo "Levantando agentes C..."
$BINARIO $PUERTO_A $CPU_A $RAM_A $GPU_A > agent_A.log 2>&1 &
AGENT_A_PID=$!

$BINARIO $PUERTO_B $CPU_B $RAM_B $GPU_B > agent_B.log 2>&1 &
AGENT_B_PID=$!

sleep 2

# ============================================
# PASO 3: Preparar archivos de jobs
# ============================================
cat > jobs_A.txt << 'EOF'
{"job1", "cpu:2:gpu:1", 2}.
EOF

cat > jobs_B.txt << 'EOF'
{"job2", "gpu:1:cpu:2", 2}.
EOF

# ============================================
# PASO 4: Lanzar Schedulers (con -pa scheduler/)
# ============================================
echo "Lanzando schedulers Erlang..."
erl -noshell -pa scheduler/ -s main server manual 2 $PUERTO_A jobs_A.txt > erlang_A.log 2>&1 &
ERLANG_A_PID=$!

erl -noshell -pa scheduler/ -s main server manual 2 $PUERTO_B jobs_B.txt > erlang_B.log 2>&1 &
ERLANG_B_PID=$!

echo "Esperando timeout (aprox 6s)..."
sleep 6

# ============================================
# PASO 5: Verificación
# ============================================
echo ""
echo "=== RESULTADO DEL TEST ==="
if grep -q "POSIBLE DEADLOCK" erlang_A.log || grep -q "POSIBLE DEADLOCK" erlang_B.log; then
    echo "✓ Deadlock detectado y resuelto mediante timeout"
    echo "Test: OK"
else
    echo "✗ No se detectó deadlock"
    echo "Test: FALLIDO"
fi

echo ""
echo "Logs disponibles:"
echo "  - agent_A.log / agent_B.log"
echo "  - erlang_A.log / erlang_B.log"

# ============================================
# LIMPIEZA (automática con trap)
# ============================================