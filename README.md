# Manejador de Recursos Distribuidos para HPC

**R-322 — Sistemas Operativos I**  
Facultad de Ciencias Exactas, Ingeniería y Agrimensura — FCEIA, UNR

Sistema distribuido que gestiona recursos (CPUs, memoria, GPUs) en un clúster HPC simulado. Cada nodo ejecuta un agente en C (comunicaciones con epoll) y un planificador en Erlang (lógica de jobs y anti-deadlock), cooperando mediante un protocolo ASCII/TCP y descubrimiento UDP broadcast.

---

## Estructura del Proyecto

```text
/proyecto
├── test_deadlock.sh       # Script de pruebas automatizadas
├── /agente
│   ├── Makefile           # Compilación del agente C
│   └── (código .c)        # Implementación en C
├── main.erl               # Lógica del scheduler Erlang
└── aux.erl                # Funciones auxiliares Erlang

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

```bash
cd agente
make
cd ..
```

El binario del agente C queda en `./agente/agent`.

### Compilar el planificador Erlang

```bash
erlc aux.erl main.erl
```

Genera archivos `aux.beam` y `main.beam` en la carpeta raíz.

---

## Ejecución

### Ejecutar un nodo individual

**1. Arrancar el agente C:**

Desde la raíz del proyecto, ejecuta el agente:

```bash
./agente/agent <PUERTO> <N_RECURSOS> <nombre1> <nombre2> ... <cantidad1> <cantidad2> ...

# Ejemplo: Nodo con 2 CPUs, 8192 MB RAM, 0 GPUs
./agente/agent 8100 3 cpu mem gpu 2 8192 0
```

Al arrancar, el agente C:
1. Envía un `ANNOUNCE` UDP broadcast inmediatamente.
2. Espera 2 segundos para recibir anuncios de otros nodos.
3. Comienza a procesar peticiones.

**2. Arrancar el planificador Erlang (otra terminal desde la raíz del proyecto):**

```bash
erl -noshell -pa . -eval "main:server(manual, 2, 8100)" -s init stop
```

El planificador se conecta al agente C local en `localhost:8100`.

### Ejecutar múltiples nodos localmente

Para una prueba automatizada, usar el script:

```bash
bash test_deadlock.sh
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
JOB_REQUEST <job_id> <@host1:recurso:cantidad [@host2:recurso:cantidad ...]>
job_table_release <job_id>
GET_NODES
```

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

Cada job tiene un **timeout de 5 segundos**. Si el job no completa en ese tiempo:
- El scheduler Erlang detecta el posible bloqueo
- Envía `job_table_release` para liberar los recursos asignados parcialmente
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
   - Timer de 5s en Job1 expira
   - Scheduler detecta timeout
   - `job_table_release` libera los 2 CPUs en A
   - Log registra `"POSIBLE DEADLOCK"`
3. Job2 ahora obtiene las CPUs de A
4. **RESULTADO:** Sistema recupera normalidad 

---

## Script de prueba

El script `test_deadlock.sh` realiza una prueba automatizada:

```bash
bash test_deadlock.sh
```

**Qué hace:**

1. Compila agente C y módulos Erlang
2. Levanta Nodo A (puerto 8100) y Nodo B (puerto 8200)
3. Inyecta Job1 y Job2 simultáneamente para provocar deadlock
4. Espera 7 segundos (incluye timeout de 5s de los jobs)
5. Busca `"POSIBLE DEADLOCK"` en `scheduler.log`
6. Imprime resultado: `TEST: OK` o `TEST: FALLIDO`
7. Limpia procesos al finalizar con `trap`

**Salida esperada si funciona (representacion):**

```
╔════════════════════════════════════╗
║        TEST: OK                    ║
╚════════════════════════════════════╝
```

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

Bloqueo circular → Timeout de 5s → `job_table_release` → Recuperación

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
./agente/agent 8100 3 cpu mem gpu 2 8192 0 2>&1 | tee agent_A.log
```

### Planificador Erlang

Escribe en `scheduler.log`:
- `JOB_GRANTED <job_id>`
- `JOB_DENIED <job_id>`
- `POSIBLE DEADLOCK` (cuando aplica timeout y job_table_release)

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
cd agente
make
cd ..

# Compilar Erlang
erlc aux.erl main.erl

# Prueba
bash test_deadlock.sh
```

---
 
**Proyecto:** R-322 Sistemas Operativos I - TP Final  
**Equipo:** Dallas Cañari Benites - Valentino Perticarari - Alan Hergenreder - Benjamín Alomar 