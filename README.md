# Manejador de Recursos Distribuidos para HPC

**R-322 — Sistemas Operativos I**  
Facultad de Ciencias Exactas, Ingeniería y Agrimensura — FCEIA, UNR

Sistema distribuido que gestiona recursos (CPUs, memoria, GPUs) en un clúster HPC simulado. Cada nodo ejecuta un agente en C (comunicaciones con epoll) y un planificador en Erlang (lógica de jobs y anti-deadlock), cooperando mediante un protocolo ASCII/TCP y descubrimiento UDP broadcast.

---

## Estructura del Proyecto

```text
/proyecto
├── Makefile                    # Compila el agente C (target `compile_c` / `c`)
├── agent                       # Binario del agente C (se genera al compilar)
├── scheduler.log                # Log del planificador (se genera en runtime)
├── /c
│   ├── agent.c                 # main() del agente
│   ├── consts.h                # Constantes (puertos, timeouts, límites)
│   ├── Makefile                # Compilación standalone del agente (uso opcional)
│   ├── /functions               # epoll, sockets, parseo de comandos (handlers.c es el core)
│   └── /structures              # hash table, tabla de agentes/jobs/reservas, fds
├── /erlang
│   ├── main.erl                 # Arranque del cliente/scheduler (server/3, modos manual|random)
│   ├── job_manager.erl          # Arma pedidos, timeout anti-deadlock, logging
│   ├── system_init.erl          # Conexión inicial al agente C, GET_NODES, supervisor
│   ├── tcp_connection.erl       # Conexión TCP local y reparto de respuestas
│   └── parser.erl               # Parseo de la respuesta NODES
├── /test
│   └── test_deadlock.sh         # Script de prueba automatizada (entregable oficial)
└── /diagramas                   # Diagramas de secuencia (casos 1, 2 y 3)
```

---

---

## Tabla de contenidos

1. [Arquitectura](#arquitectura)
2. [Requisitos](#requisitos)
3. [Compilación](#compilación)
4. [Ejecución](#ejecución)
5. [Protocolo de comunicación](#protocolo-de-comunicación)
6. [Estrategia anti-deadlock](#estrategia-anti-deadlock)
7. [Script de prueba](#script-de-prueba)
8. [Diagrama de secuencia](#diagrama-de-secuencia)
9. [Integrantes y roles](#integrantes-y-roles)
10. [Problemas conocidos](#problemas-conocidos)

---

## Arquitectura

Cada nodo tiene dos procesos que se comunican localmente:

```
┌────────────────────────────────────┐
│              NODO                  │
│                                    │
│  ┌─────────────────────────────┐   │
│  │  Planificador (Erlang)      │   │
│  │  - Generación de jobs       │   │
│  │  - Monitoreo con timeouts   │   │
│  │  - Detección de deadlocks   │   │
│  └──────────────┬──────────────┘   │
│                 │ TCP localhost    │
│  ┌──────────────▼──────────────┐   │
│  │    Agente C (epoll)         │   │
│  │  - Epoll para I/O           │   │
│  │  - Comunicación TCP/UDP     │   │
│  │  - Tablas de reservas/jobs  │   │
│  └──────────────┬──────────────┘   │
└─────────────────┼──────────────────┘
                  │ TCP (red) + UDP broadcast
         Otros nodos del clúster
```

**Puertos:**
- Agente C: puerto configurable (típicamente 8100, 8200, etc)
- Erlang → Agente C: conexión TCP local en el mismo puerto
- Descubrimiento: UDP broadcast en puerto 12529

---

## Requisitos

| Dependencia | Versión mínima | Notas |
|-------------|---------------|-------|
| GCC         | 11+           | con soporte C11 |
| Make        | 4.0+          | para compilar agente C |
| Erlang/OTP  | 25+           | para planificador |
| Linux       | kernel 2.6+   | `epoll` es Linux-only |

** Nota importante:** El sistema usa `epoll`, por lo que **solo funciona en Linux**. No es compatible con macOS ni Windows de forma nativa.

---

## Compilación

### Compilar el agente C

Desde la raíz del proyecto:

```bash
gcc -ggdb3 -Wall -Wextra ./c/agent.c ./c/functions/*.c ./c/structures/*.c -lpthread -o agent
```

(Es lo mismo que hace el target `compile_c` del `Makefile` de la raíz.)

El binario queda en `./agent`.

### Compilar el planificador Erlang

```bash
erlc -o erlang erlang/*.erl
```

Compila los 5 módulos (`main`, `job_manager`, `system_init`, `tcp_connection`, `parser`) y deja los `.beam` dentro de `erlang/`.

---

## Ejecución

### Ejecutar un nodo individual

**1. Arrancar el agente C:**

Desde la raíz del proyecto, ejecuta el agente ya compilado:

```bash
./agent <PUERTO> <N_RECURSOS> <nombre1> <nombre2> ... <cantidad1> <cantidad2> ...

# Ejemplo: Nodo con 2 CPUs, 8192 MB RAM, 0 GPUs, escuchando en el puerto 8100
./agent 8100 3 cpu mem gpu 2 8192 0
```

El puerto debe estar entre 7000 y 15000. Al arrancar, el agente C:
1. Envía un `ANNOUNCE` UDP broadcast inmediatamente.
2. Espera 2 segundos para recibir anuncios de otros nodos.
3. Comienza a procesar peticiones.

**2. Arrancar el planificador Erlang (otra terminal, parado DENTRO de `erlang/`):**

```bash
cd erlang
erl -noshell -pa . -eval "main:server(manual, 0, 8100), timer:sleep(infinity)"
```

> **Importante:** hay que ejecutar `erl` parado en la carpeta `erlang/`, no en la raíz.
> El módulo `job_manager` escribe el log en la ruta relativa `../scheduler.log`,
> así que si arrancás `erl` desde otro lado el log te va a aparecer en un lugar
> inesperado (o directamente falla al escribir).

El planificador se conecta al agente C local en `localhost:8100`. En modo `manual`
podés inyectar un job a mano desde la misma sesión de `erl`, mandándole un mensaje
al proceso registrado `cliente_pid`:

```erlang
cliente_pid ! {"127.0.0.1:8100:cpu:2 127.0.0.1:8200:gpu:1"}.
```

El otro modo disponible es `random` (`main:server(random, N, Puerto)`), que genera
automáticamente `N` jobs con recursos aleatorios.

### Ejecutar múltiples nodos localmente (prueba de deadlock)

Para levantar dos nodos completos y probar el escenario de deadlock del enunciado (§6)
de forma automática, usar el script (desde la raíz del proyecto):

```bash
./test/test_deadlock.sh
```

---

## Protocolo de comunicación

### Entre agentes C (TCP, ASCII, terminado en `\n`)

| Mensaje | Dirección | Descripción |
|---------|-----------|-------------|
| `RESERVE <job_id> <recurso> <cantidad>` | A → B | Solicitar un recurso |
| `GRANTED <job_id>` | B → A | Recurso concedido |
| `DENIED <job_id>` | B → A | Recurso denegado (sin disponibilidad) |
| `RELEASE <job_id> <recurso> <cantidad>` | A → B | Liberar un recurso |

### Interfaz local Erlang ↔ Agente C (TCP localhost con packet length prefix)

**Comandos de Erlang al agente C:**

```
JOB_REQUEST <job_id> <host1:puerto1:recurso:cantidad [host2:puerto2:recurso:cantidad ...]>
JOB_RELEASE <job_id>
GET_NODES
```

> Nota: cada destino se especifica como `ip:puerto:recurso:cantidad` (sin el prefijo
> `@` que muestra el enunciado como ejemplo — el parseo en `handlers.c` no lo espera).
> `job_table_release` es el nombre de la función interna en C que libera la entrada
> de la tabla de jobs; el comando que viaja por la red es `JOB_RELEASE`.

**Respuestas del agente C a Erlang:**

```
JOB_GRANTED <job_id>
JOB_DENIED <job_id>
JOB_TIMEOUT <job_id>
NODES <host>:<puerto>:cpu:<cant>:mem:<cant>:gpu:<cant>;...
```

### Descubrimiento UDP broadcast

```
ANNOUNCE <puerto> cpu:<cantidad> mem:<cantidad> gpu:<cantidad>
```

- Se envía cada ~5 segundos y al iniciar.
- La **dirección IP de cada nodo se extrae de `recvfrom()`**, no del payload.
- Un nodo se considera caído si no anuncia durante 15 segundos.

---

## Estrategia anti-deadlock

El sistema implementa una estrategia **hibrida de tolerancia a deadlocks**:

### 1. Prevención mediante Ordenamiento Global

Se impone un **orden global de adquisición de recursos**: todos los jobs solicitan los recursos siempre en el mismo orden (CPU → MEM → GPU). Esto rompe la condición de espera circular que caracteriza a los deadlocks.

### 2. Detección y Recuperación mediante Timeouts

Cada job tiene un **timeout de 3 segundos** (`JobTimeout = 3000` ms, hardcodeado en
`erlang/system_init.erl`). Si el job no completa en ese tiempo:
- El scheduler Erlang detecta el posible bloqueo
- Envía `JOB_RELEASE` para liberar los recursos asignados parcialmente
- Registra `"POSIBLE DEADLOCK"` en `scheduler.log`
- Reencola el job para reintentar más tarde

### Ejemplo paso a paso: Deadlock circular entre dos nodos

**Escenario inicial:**
- Nodo A: 2 CPUs, 8192 MB RAM, 0 GPUs
- Nodo B: 2 CPUs, 4096 MB RAM, 1 GPU

**Job1 (desde A):** Solicita `2 CPUs (A) + 1 GPU (B)`  
**Job2 (desde B):** Solicita `1 GPU (B) + 2 CPUs (A)`

**Sin la estrategia (DEADLOCK):**
1. Agente A concede 2 CPUs a Job1 ✓
2. Agente B concede 1 GPU a Job2 ✓
3. Job1 espera GPU de B (bloqueado por Job2)
4. Job2 espera CPUs de A (bloqueado por Job1)
5. **RESULTADO:** Interbloqueo indefinido 

**Con la estrategia (EVITADO/RESUELTO):**
1. Ambos jobs aplican el orden global (CPU antes que GPU)
2. Si ocurriera bloqueo por timing extremo:
   - Timer de 3s en Job1 expira
   - Scheduler detecta timeout
   - `JOB_RELEASE` libera los 2 CPUs en A
   - Log registra `"POSIBLE DEADLOCK"`
3. Job2 ahora obtiene las CPUs de A
4. **RESULTADO:** Sistema recupera normalidad 

---

## Script de prueba

El script `test/test_deadlock.sh` corre la prueba automatizada pedida por la
consigna. Se ejecuta desde la raíz del proyecto (o desde cualquier lado, se
ubica solo):

```bash
./test/test_deadlock.sh
```

**Qué hace:**

1. Compila el agente C (`gcc ...`) y los módulos Erlang (`erlc -o erlang erlang/*.erl`).
2. Detecta la IP de red de la máquina (necesaria porque el descubrimiento UDP
   identifica a cada nodo por la IP real, no por `127.0.0.1`).
3. Levanta Nodo A (puerto 8100, `cpu:2 mem:8192 gpu:0`) y Nodo B (puerto 8200,
   `cpu:2 mem:4096 gpu:1`), y espera 4s a que se descubran entre sí por UDP.
4. Levanta el scheduler Erlang de cada nodo en modo `manual` e inyecta:
   - **Job1** (desde A): `cpu:2` de A, luego `gpu:1` de B.
   - **Job2** (desde B): `gpu:1` de B, luego `cpu:2` de A (orden cruzado a
     propósito, tal como describe el enunciado §6).
5. Espera 8 segundos (cubre el timeout de 3s del job + margen).
6. Imprime `scheduler.log` completo y valida que haya al menos 2 entradas
   (una por job) — si aparece `POSIBLE DEADLOCK`, avisa que se resolvió por
   timeout; si no aparece, los jobs se resolvieron directamente gracias al
   orden global de recursos.
7. Imprime `TEST: OK` o `TEST: FALLIDO`.
8. Limpia todos los procesos lanzados (agentes C y VMs de Erlang) al
   finalizar, incluso si el script se corta a la mitad (`trap ... EXIT INT TERM`).

---

## Diagrama de secuencia

Los diagramas se encuentran en `diagramas/`:

### Caso 1 — Job exitoso
![Caso 1](diagramas/caso1_job_exitoso.drawio.png)

Secuencia normal: Job solicitado → Recursos concedidos → Job completado → Recursos liberados

### Caso 2 — Job denegado
![Caso 2](diagramas/caso2_job_denegado.drawio.png)

Recurso no disponible: Job rechazado → Notificación al scheduler

### Caso 3 — Deadlock y resolución
![Caso 3](diagramas/caso3_deadlock.drawio.png)

Bloqueo circular → Timeout de 3s → `JOB_RELEASE` → Recuperación

---

## Integrantes y roles

| Rol | Responsable | Responsabilidad | Tecnología |
|-----|-------------|-----------------|------------|
| Rol 1 | Dallas | Servidor C con epoll, sockets TCP/UDP, buffers, manejo de conexiones | C, epoll, TCP, UDP |
| Rol 2 | Alan | Estructuras de recursos, colas, asignación/liberación, tabla de jobs | C, estructuras de datos |
| Rol 3 | Valen | Planificador Erlang, generación de jobs, anti-deadlock, timeouts | Erlang |
| Rol 4 | Benja | Scripts de prueba, documentación, diagramas, testing, integración | Bash, documentación |

---

## Logs y debugging

### Agente C

Escribe en `stdout`. Para guardar:

```bash
./agent 8100 3 cpu mem gpu 2 8192 0 2>&1 | tee agent_A.log
```

### Planificador Erlang

Escribe en `scheduler.log` (en la raíz del proyecto, siempre que `erl` se haya
arrancado parado dentro de `erlang/` — ver nota en la sección Ejecución):
- `JOB_GRANTED <job_id>`
- `JOB_DENIED <job_id>`
- `POSIBLE DEADLOCK` (cuando se aplica el timeout de 3s y se libera el job)

Ver logs:

```bash
tail -f scheduler.log
```



## Compilación desde cero

```bash
# Clonar
git clone https://github.com/Alan2255/SistemasOperativos1TPFinal
cd SistemasOperativos1TPFinal

# Compilar C
gcc -ggdb3 -Wall -Wextra ./c/agent.c ./c/functions/*.c ./c/structures/*.c -lpthread -o agent

# Compilar Erlang
erlc -o erlang erlang/*.erl

# Prueba automatizada (dos nodos + escenario de deadlock)
./test/test_deadlock.sh
```

---

## Limitaciones conocidas

- El comando `JOB_STATUS <job_id>` que menciona el enunciado como interfaz
  local Erlang↔C no está implementado; el agente solo reconoce `JOB_REQUEST`,
  `JOB_RELEASE` y `GET_NODES`.
- El `Makefile` de la raíz tiene un target `erlang` que hace `cd erlang` en
  una línea propia del recipe — como cada línea de un target de Make corre en
  una subshell distinta, ese `cd` no tiene efecto en la línea siguiente. Por
  eso, para compilar y correr Erlang, usar los comandos de esta guía
  directamente en vez de `make erlang`.

---
 
**Proyecto:** R-322 Sistemas Operativos I - TP Final  
**Equipo:** Dallas Cañari Benites - Valentino Perticarari - Alan Hergenreder - Benjamín Alomar 