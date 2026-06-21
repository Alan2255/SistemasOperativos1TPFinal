#!/bin/bash

# 1. Limpieza automática: explica que esto mata todo si cancelas con Ctrl+C
trap "kill 0" EXIT

# 2. Configuración: variables fáciles de entender
PUERTO_A=8100
PUERTO_B=8200
BINARIO=./agente
LOG_A="erl_A.log"
LOG_B="erl_B.log"

# 3. Compilación: el "make" es básico
echo "Compilando..."
make clean && make
[ -f $BINARIO ] || { echo "Error: Binario no encontrado"; exit 1; }

# 4. Levantar Agentes: estructura clara
$BINARIO $PUERTO_A > agent_A.log &
$BINARIO $PUERTO_B > agent_B.log &
sleep 2 # Damos tiempo a que arranquen

# 5. Pipes: mkfifo es el "caño" para hablar con Erlang
mkfifo pipe_A pipe_B

# 6. Lanzar Erlang: explicá que '-noshell' es para correr sin consola interactiva
erl -noshell -s main server manual 0 < pipe_A > $LOG_A 2>&1 &
erl -noshell -s main server manual 0 < pipe_B > $LOG_B 2>&1 &
sleep 2

# 7. Comandos: inyección simple
echo "c(main)." > pipe_A
echo "c(main)." > pipe_B
echo "main:server(manual, 10)." > pipe_A
echo "main:server(manual, 10)." > pipe_B

# 8. Test de Deadlock: la parte que vas a defender
echo "Inyectando trabajos para forzar deadlock..."
echo 'pid_scheduler_job ! {"job1", "cpu:2:gpu:1", 2}.' > pipe_A
echo 'pid_scheduler_job ! {"job2", "gpu:1:cpu:2", 2}.' > pipe_B

sleep 6

# 9. Verificación: acá usás grep para buscar el error esperado
echo "Resultado del test:"
grep "POSIBLE DEADLOCK" $LOG_A $LOG_B && echo "Test OK: Deadlock detectado" || echo "Test fallido"