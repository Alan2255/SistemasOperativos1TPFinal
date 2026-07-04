╔═══════════════════════════════════════════════════════════════════════════╗
║  GUÍA DETALLADA: BASH COMMANDS & CONCEPTS PARA EL HPC TEST SUITE        ║
║  Sistemas Operativos 1 - FCEIA-UNR                                      ║
╚═══════════════════════════════════════════════════════════════════════════╝

================================================================================
TABLA DE CONTENIDOS
================================================================================
1. ESTRUCTURA GENERAL DEL SCRIPT
2. VARIABLES Y CONFIGURACIÓN
3. FUNCIONES AUXILIARES (print_*, wait_*, command_exists)
4. COMPILACIÓN (erl, gcc, make)
5. LANZAMIENTO DE PROCESOS (erl -eval, &, PID)
6. PIPES Y REDIRECCIÓN (>, >>, 2>&1, |)
7. CONTROL DE FLUJO (if, case, &&, ||, [ ])
8. MANEJO DE LOGS Y DETECCIÓN (grep, wait_for_log_entry)
9. CLEANUP Y SEÑALES (kill, pkill, trap)

================================================================================
1. ESTRUCTURA GENERAL DEL SCRIPT
================================================================================

Un bash script es un archivo de texto que contiene comandos shell ejecutados
secuencialmente. La primera línea es el SHEBANG:

    #!/bin/bash
    
    ^--- Indica al sistema que use /bin/bash para interpretar este archivo
    
EJECUCIÓN:
    chmod +x hpc_test_suite.sh  # Hacerlo ejecutable
    ./hpc_test_suite.sh          # Ejecutar

Los scripts se dividen en:
  - VARIABLES (configuración inicial)
  - FUNCIONES (código reutilizable)
  - MAIN (lógica de entrada/salida)


================================================================================
2. VARIABLES Y CONFIGURACIÓN
================================================================================

QUÉ ES UNA VARIABLE:
  Una variable almacena un valor (texto, número, ruta, etc.)

SINTAXIS:
  NOMBRE_VAR="valor"      # Asignar
  echo $NOMBRE_VAR        # Usar (con $ delante)
  echo ${NOMBRE_VAR}      # Usar (con ${} - más seguro)

EJEMPLO EN SCRIPT:
  
  PROJECT_ROOT="$(pwd)"   # $(pwd) EJECUTA el comando pwd y guarda su resultado
  ERLANG_DIR="${PROJECT_ROOT}"
  
  Explicación:
    $(cmd) = COMMAND SUBSTITUTION - ejecuta cmd y reemplaza con su output
    pwd    = print working directory - imprime la carpeta actual
    ""     = string entre comillas preserva el valor sin expansión accidental
    
TIPOS DE VARIABLES EN NUESTRO SCRIPT:

  a) Rutas:
     ERLANG_DIR="${PROJECT_ROOT}"      # Dónde están main.erl, aux.erl
     BEAM_DIR="${PROJECT_ROOT}/scheduler"  # Dónde se ponen .beam compilados
     
  b) Puertos:
     AGENT_PORT_A=8100               # Puerto del agente C A
     ERLANG_TIMEOUT_SEC=30           # Timeout en segundos
     
  c) Nombres Erlang:
     ERLANG_NODE_A="scheduler_a@localhost"  # Nombre del nodo A
     ERLANG_COOKIE="hpc_cookie"            # Cookie para que nodos se confíen
     
  d) Colores (para output bonito):
     RED='\033[0;31m'     # ANSI escape code para rojo
     GREEN='\033[0;32m'   # ANSI escape code para verde
     NC='\033[0m'         # No Color - vuelve al normal


VARIABLES ESPECIALES EN BASH:

  $#     = Cantidad de argumentos pasados al script
  $1, $2 = Primer, segundo argumento
  $@     = Todos los argumentos
  $?     = Código de salida del último comando (0 = éxito, != 0 = error)
  $!     = PID del último proceso lanzado en background (&)
  $$     = PID del script actual


================================================================================
3. FUNCIONES AUXILIARES
================================================================================

QUÉ ES UNA FUNCIÓN:
  Código reutilizable que se define una vez y se llama muchas veces

SINTAXIS:
  function_name() {
      # Cuerpo
      return 0  # 0 = éxito, otro número = error
  }
  
  # Llamar:
  function_name arg1 arg2

EJEMPLOS DEL SCRIPT:

A) print_step(), print_success(), print_error()
   ────────────────────────────────────────────
   
   print_step() {
       echo -e "${BLUE}[*]${NC} $1"
   }
   
   Explicación:
     echo    = imprime texto
     -e      = habilita interpretación de escapes (\033 para colores)
     ${BLUE} = variable BLUE (color azul ANSI)
     [*]     = símbolo literal
     $1      = primer argumento pasado a la función
     ${NC}   = vuelve al color normal
     
   Uso:
     print_step "Compilando Erlang..."
     
   Output:
     [*] Compilando Erlang...  (donde [*] aparece en azul)


B) command_exists()
   ────────────────
   
   command_exists() {
       command -v "$1" >/dev/null 2>&1
   }
   
   Explicación:
     command -v  = busca si el comando existe en el PATH
     >/dev/null  = redirige stdout (ver sección PIPES Y REDIRECCIÓN)
     2>&1        = redirige stderr también a /dev/null
     return $?   = retorna el código de salida de command -v
     
   Uso:
     if command_exists erl; then
         echo "Erlang está instalado"
     fi


C) wait_for_log_entry()
   ────────────────────
   
   wait_for_log_entry() {
       local logfile=$1          # Argumento 1
       local pattern=$2          # Argumento 2
       local timeout=$3          # Argumento 3
       local elapsed=0
       
       while [ $elapsed -lt $timeout ]; do
           if grep -q "$pattern" "$logfile"; then
               return 0          # Patrón encontrado
           fi
           sleep 1               # Esperar 1 segundo
           elapsed=$((elapsed + 1))  # Incrementar contador
       done
       
       return 1  # Timeout sin encontrar
   }
   
   Explicación:
     local     = variable local a la función (no afecta exterior)
     while [ ... ]; do ... done  = loop mientras condición es true
     [ $a -lt $b ]    = test si a < b (less than)
     grep -q pattern file  = busca pattern en file (-q = quiet, no imprime)
     $((expr))        = aritmética: suma, resta, etc.
     
   Uso:
     wait_for_log_entry "logs/deadlock.log" "POSIBLE DEADLOCK" 15
     # Espera hasta 15 segundos a que aparezca ese string


D) wait_for_port()
   ────────────────
   
   Similar a wait_for_log_entry pero para puertos:
   
   nc -z -w1 localhost 8100
   
   Explicación:
     nc           = netcat (herramienta de red)
     -z           = solo prueba conexión, no envía datos
     -w1          = timeout de 1 segundo
     localhost    = máquina local (127.0.0.1)
     8100         = puerto a probar


================================================================================
4. COMPILACIÓN (erl, gcc, make)
================================================================================

A) COMPILAR ERLANG: erl -eval
   ──────────────────────────
   
   erl -eval "
       compile:file(main, [{outdir, 'scheduler/'}]),
       compile:file(aux, [{outdir, 'scheduler/'}]),
       halt()
   " -noshell
   
   Desglose:
     erl              = inicia intérprete Erlang
     -eval "..."      = ejecuta código Erlang (descubrimiento de Bauti!)
     compile:file()   = función Erlang que compila un módulo
     [{outdir, '...'}] = opción: dónde guardar los .beam
     halt()           = termina el intérprete Erlang
     -noshell         = no abre shell interactivo (útil para scripts)
     
   Ventaja sobre -run:
     -run             = pasa argumentos como lista única (arity 1)
     -eval "..."      = permite múltiples comandos separados por coma
     
   Resultado:
     main.beam guardado en scheduler/
     aux.beam guardado en scheduler/


B) COMPILAR C: make
   ────────────────
   
   cd "$AGENT_DIR"   # Cambiar a directorio donde está Makefile
   make              # Lee Makefile y ejecuta recetas
   
   El Makefile define:
     CC = gcc
     CFLAGS = -Wall -Wextra
     
     agent:
         $(CC) $(CFLAGS) agent.c functions/*.c structures/*.c -lpthread -o agent
   
   Explicación de make:
     $(CC)    = variable make (substitute por gcc)
     *.c      = wildcard: todos los .c en ese directorio
     -lpthread = link pthread library
     -o agent = output file name
     
   Resultado:
     Se genera binario "agent" ejecutable


C) COMPILAR VERIFICACIÓN
   ──────────────────────
   
   if [ -f "${BEAM_DIR}/main.beam" ]; then
       echo "Éxito"
   else
       echo "Falló"
   fi
   
   Explicación:
     [ -f FILE ]  = test si FILE existe y es un archivo regular
     Otros tests:
       [ -d DIR ]   = test si DIR existe y es directorio
       [ -x FILE ]  = test si FILE es ejecutable
       [ -z STR ]   = test si STR es vacío
       [ -n STR ]   = test si STR no es vacío


================================================================================
5. LANZAMIENTO DE PROCESOS (&, PID, wait)
================================================================================

PROCESOS EN BACKGROUND:

A) LANZAR EN BACKGROUND:
   ─────────────────────
   
   ./agente_bin 127.0.0.1 8100 cpu 10 > logs/agente_A.log 2>&1 &
   AGENT_A_PID=$!
   
   Explicación:
     ./agente_bin ... & = el & al final envía el proceso a background
     $!                 = variable especial que almacena el PID del último bg process
     AGENT_A_PID=$!     = guardamos el PID para después matarlo si queremos
     
   Sin el &:
     El script esperaría a que termine agente_bin (bloqueo)
     
   Con el &:
     El script continúa mientras agente_bin corre en paralelo


B) GUARDAR PIDS PARA CLEANUP:
   ──────────────────────────
   
   echo "$AGENT_A_PID" > "$LOGS_DIR/.pids"   # Guardar PID A
   echo "$AGENT_B_PID" >> "$LOGS_DIR/.pids"  # Agregar PID B (append)
   echo "$ERLANG_A_PID" >> "$LOGS_DIR/.pids" # Agregar PID Erlang A
   
   Explicación:
     >  = redirección: sobrescribe archivo
     >> = redirección: agrega al archivo (append)
     
   Resultado:
     Archivo .pids contiene:
       12345
       12346
       12347
       12348


C) MATAR PROCESOS:
   ────────────────
   
   while IFS= read -r pid; do
       kill -9 "$pid" 2>/dev/null
   done < "$LOGS_DIR/.pids"
   
   Explicación:
     while ... done < file  = loop leyendo líneas del archivo
     read -r pid            = lee una línea y guarda en variable pid
     kill -9 PID            = mata proceso inmediatamente (SIGKILL)
     2>/dev/null            = ignora errores si pid ya terminó


D) MATAR MÚLTIPLES PROCESOS:
   ──────────────────────────
   
   pkill -9 -f "patrón"
   
   Explicación:
     pkill      = kill multiple processes matching pattern
     -9         = SIGKILL (mata inmediatamente)
     -f         = match full command line, not just process name


================================================================================
6. PIPES Y REDIRECCIÓN
================================================================================

A) REDIRECCIÓN DE STDOUT (1):
   ───────────────────────────
   
   ./programa > salida.txt
   
   Explicación:
     > = redirige stdout (fd 1) al archivo
     ./programa imprime normalmente a stdout
     Con >, todo va al archivo en lugar de la pantalla
     
   Resultado:
     Si ejecutas: echo "Hola" > test.txt
     Se crea test.txt con contenido "Hola"
     (nada aparece en pantalla)


B) REDIRECCIÓN DE STDERR (2):
   ───────────────────────────
   
   ./programa 2> errores.txt
   
   Explicación:
     2 = file descriptor para stderr (standard error)
     2> = redirige stderr al archivo
     
   Ejemplo:
     ls /no_existe 2> err.log
     El error "No such file" va a err.log


C) COMBINAR STDOUT Y STDERR:
   ──────────────────────────
   
   ./programa > output.log 2>&1
   
   Explicación:
     > output.log  = redirige stdout (fd 1) a output.log
     2>&1          = redirige stderr (fd 2) a donde vaya fd 1 (el archivo)
     
   Resultado:
     Tanto output como errores van al mismo archivo
     
   Alternativa moderna:
     ./programa &> output.log  (mismo efecto)


D) REDIRIGIR A /dev/null (descartar):
   ─────────────────────────────────
   
   comando > /dev/null 2>&1
   
   Explicación:
     /dev/null = archivo especial que descarta todo lo que escribes
     Útil cuando no te importan los outputs
     
   Ejemplo:
     if nc -z localhost 8100 > /dev/null 2>&1; then
         # Puerto está abierto
     fi


E) PIPES (|):
   ──────────
   
   cat logfile.txt | grep "ERROR"
   
   Explicación:
     | = pipe: envía stdout del comando izquierdo al stdin del derecho
     cat logfile.txt     = imprime archivo
     grep "ERROR"        = filtra líneas que contengan ERROR
     El pipe conecta ambos
     
   Resultado:
     Solo aparecen líneas con ERROR


F) PIPES MÚLTIPLES:
   ──────────────────
   
   cat logfile.txt | grep "ERROR" | wc -l
   
   Explicación:
     cat logfile.txt  = imprime archivo
     | grep "ERROR"   = filtra a ERROR
     | wc -l          = cuenta líneas
     
   Resultado:
     Número de líneas que contienen ERROR


G) INPUT REDIRECTION:
   ────────────────────
   
   while IFS= read -r line; do
       echo $line
   done < archivo.txt
   
   Explicación:
     < archivo.txt = redirige stdin desde el archivo
     while ... do ... done = loop lee líneas del archivo
     read -r line  = lee una línea a variable line


H) HEREDOC (documento inline):
   ────────────────────────────
   
   cat << 'EOF'
   Esto es texto multilínea
   En bash
   EOF
   
   Explicación:
     << 'EOF'  = empieza heredoc (termina en EOF)
     'EOF'     = comillas evitan expansión de variables
     EOF       = marca final (sin espacios)
     
   Alternativa (con expansión):
     cat << EOF
     La variable $VAR se expande
     EOF
     
   Uso en script:
     erl -eval "
         compile:file(main),
         compile:file(aux),
         main:server(manual, 0, 8100)
     " << 'EOF'
     EOF
     
   (Aunque -eval es más limpio que heredoc)


================================================================================
7. CONTROL DE FLUJO
================================================================================

A) IF / ELSE / ELIF:
   ──────────────────
   
   if command; then
       # Si command retorna 0 (éxito)
   elif command2; then
       # Si command2 retorna 0
   else
       # Si ninguno fue éxito
   fi
   
   Ejemplo en script:
     if [ -f "${BEAM_DIR}/main.beam" ]; then
         print_success "Erlang compilado"
     else
         print_error "Fallo compilación"
         return 1
     fi
   
   Explicación:
     [ ... ] = comando test que retorna 0 o 1
     ; then = el ; separa condición del then


B) CASE / ESAC:
   ────────────
   
   case "$variable" in
       valor1)
           # Si $variable == "valor1"
           ;;
       valor2)
           # Si $variable == "valor2"
           ;;
       *)
           # Default (cualquier otro)
           ;;
   esac
   
   Ejemplo en script:
     case "$1" in
         compile)
             do_compile
             ;;
         run_nodes)
             do_run_nodes
             ;;
         *)
             print_error "Opción desconocida"
             ;;
     esac
   
   Explicación:
     ;; = termina cada case (NO es else!)
     *) = default (como default en switch de otros lenguajes)


C) WHILE LOOP:
   ────────────
   
   while [ $counter -lt 10 ]; do
       echo $counter
       counter=$((counter + 1))
   done
   
   Explicación:
     [ $counter -lt 10 ] = test: counter < 10?
     -lt = less than
     do ... done = cuerpo del loop
     $((expr)) = aritmética bash


D) FOR LOOP:
   ──────────
   
   for i in 1 2 3; do
       echo $i
   done
   
   Alternativa (C-style):
     for ((i=0; i<10; i++)); do
         echo $i
     done


E) OPERADORES LÓGICOS:
   ──────────────────────
   
   command1 && command2   # Ejecuta command2 si command1 tuvo éxito
   command1 || command2   # Ejecuta command2 si command1 falló
   
   Ejemplo:
     do_compile && do_run_nodes
     # Compila, y solo si compilación fue ok, lanza nodos
     
     [ -f file ] || echo "File no existe"
     # Si file no existe, imprime error


F) COMPARADORES EN TESTS:
   ───────────────────────
   
   [ $a -eq $b ]   # equal
   [ $a -ne $b ]   # not equal
   [ $a -lt $b ]   # less than
   [ $a -le $b ]   # less than or equal
   [ $a -gt $b ]   # greater than
   [ $a -ge $b ]   # greater than or equal
   
   [ -f FILE ]     # FILE existe y es archivo
   [ -d DIR ]      # DIR existe y es directorio
   [ -x FILE ]     # FILE es ejecutable
   [ -z STR ]      # STR está vacío
   [ -n STR ]      # STR no está vacío


================================================================================
8. MANEJO DE LOGS Y DETECCIÓN (grep, tail, wait)
================================================================================

A) GREP (buscar en archivos):
   ────────────────────────────
   
   grep "patrón" archivo.txt
   
   Opciones comunes:
     -q = quiet (no imprime, solo retorna código)
     -n = line numbers
     -i = case insensitive
     -v = invert match (líneas que NO contienen patrón)
     -c = count (cuántas líneas)
   
   Ejemplos:
     grep "ERROR" logfile.log          # Líneas que contienen ERROR
     grep -q "DEADLOCK" log.txt && echo "Encontrado"  # Silent check
     grep "DEADLOCK" log.txt | wc -l   # Cuántos DEADLOCKs


B) TAIL (últimas líneas):
   ──────────────────────
   
   tail -20 logfile.txt    # Últimas 20 líneas
   tail -f logfile.txt     # Follow: muestra nuevas líneas en tiempo real
   
   Útil para:
     tail -f logs/scheduler.log  # Ver logs mientras el sistema corre


C) WAIT_FOR_LOG_ENTRY (función custom):
   ──────────────────────────────────────
   
   wait_for_log_entry "$logfile" "POSIBLE DEADLOCK" 15
   
   Qué hace:
     - Cada 1 segundo
     - Chequea si "POSIBLE DEADLOCK" aparece en $logfile
     - Si aparece: retorna 0 (éxito)
     - Si 15 segundos pasan sin encontrarlo: retorna 1 (error)
   
   Implementación:
     while [ $elapsed -lt $timeout ]; do
         if grep -q "$pattern" "$logfile"; then
             return 0
         fi
         sleep 1
         elapsed=$((elapsed + 1))
     done
     return 1


D) ESPERAR A PUERTO (wait_for_port):
   ─────────────────────────────────
   
   wait_for_port 8100 15
   
   Qué hace:
     - Intenta conectar al puerto 8100 cada 1 segundo
     - Si puede conectar: retorna 0
     - Si 15 segundos pasan: retorna 1


================================================================================
9. CLEANUP Y SEÑALES (kill, trap, pkill)
================================================================================

A) KILL (enviar señales a procesos):
   ──────────────────────────────────
   
   kill -9 PID          # SIGKILL (mata inmediatamente)
   kill -15 PID         # SIGTERM (termina gracefully)
   kill -0 PID          # Solo chequea si PID existe
   
   Ejemplo en script:
     if kill -0 "$pid" 2>/dev/null; then
         # PID aún existe
         kill -9 "$pid"
     fi


B) PKILL (kill por patrón):
   ──────────────────────────
   
   pkill -f "patrón"
   
   Ejemplo:
     pkill -f "erl -sname"    # Mata todos los erl que tengan ese patrón
     pkill -9 java            # Mata todos los procesos java


C) TRAP (ejecutar código al recibir señal):
   ─────────────────────────────────────────
   
   trap "cleanup; exit" EXIT INT TERM
   
   Explicación:
     trap "commands" SIGNAL
     EXIT  = cuando el script termina
     INT   = Ctrl+C
     TERM  = termination signal
   
   Uso (ideal para scripts):
     trap 'do_cleanup' EXIT
     
   Resultado:
     Si el script termina por cualquier razón, ejecuta do_cleanup
     Garantiza que no queden procesos huérfanos


================================================================================
10. EJECUCIÓN PASO A PASO
================================================================================

A) SYNTAX CHECK (sin ejecutar):
   ────────────────────────────
   
   bash -n script.sh
   
   Verifica errores de sintaxis sin ejecutar


B) DEBUG MODE (ver cada línea):
   ─────────────────────────────
   
   bash -x script.sh
   
   Muestra cada comando antes de ejecutar


C) DEBUG + INFO:
   ──────────────
   
   bash -xv script.sh
   
   -v = verbose (imprime líneas mientras se procesan)
   -x = debug (muestra expansión de variables)


================================================================================
EJEMPLO DE EJECUCIÓN PASO A PASO (./hpc_test_suite.sh compile)
================================================================================

1. Script comienza
   ↓
2. main() es llamado con "$@" = "compile"
   ↓
3. case "$1" in → case "compile" in
   ↓
4. case coincide con "compile)" → ejecuta do_compile
   ↓
5. do_compile():
   a) Verifica herramientas (command_exists erl, gcc)
   b) Crea directorios (mkdir -p)
   c) Ejecuta: erl -eval "compile:file(...)" -noshell
      → Genera main.beam, aux.beam en scheduler/
   d) Ejecuta: make en carpeta agent
      → Genera binario "agent"
   e) Retorna 0 (éxito)
   ↓
6. Script termina (exit 0)

OUTPUT ESPERADO:
  [*] === COMPILACIÓN ===
  [*] Compilando Erlang (main.erl, aux.erl)...
  [✓] Erlang compilado: main.beam, aux.beam
  [*] Compilando agente C...
  [✓] Agente C compilado: ./agent/agent
  [*] Compilación completada


================================================================================
TROUBLESHOOTING COMÚN
================================================================================

Problema: "command not found: erl"
Solución: sudo apt-get install erlang

Problema: "Permission denied"
Solución: chmod +x script.sh

Problema: Los procesos no se detienen
Solución: pkill -9 -f "erl"; pkill -9 agent

Problema: "No such file or directory"
Solución: Revisar que PROJECT_ROOT sea correcto (pwd)

Problema: Ports already in use
Solución: lsof -i :8100  (ver quién usa puerto)
         kill -9 PID


================================================================================
REFERENCIAS RÁPIDAS
================================================================================

Colores ANSI:
  \033[0;31m = Rojo
  \033[0;32m = Verde
  \033[1;33m = Amarillo
  \033[0;34m = Azul
  \033[0m    = Reset

File Descriptors (fd):
  0  = stdin (entrada estándar)
  1  = stdout (salida estándar)
  2  = stderr (error estándar)

Procesos:
  $$ = PID del script actual
  $! = PID del último background process
  $? = Código de salida del último comando

Strings:
  "..." = interpolación de variables
  '...' = literales (sin expansión)
  ${VAR} = variable con ${} es más seguro


================================================================================
FIN DE GUÍA
================================================================================
