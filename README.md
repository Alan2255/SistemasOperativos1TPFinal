# Manejador de Recursos Distribuidos para HPC

---

# Uso

## C

Desde c/:

### Compilar el agente de C
```bash
make compile
```

### Correr el agente de C
```bash
make run <puerto> <cantidad_cpu> <cantidad_mem> <cantidad_gpu> <tiempo_entre_cada_request*>
```


### Compilar y luego correr el agente de C
```bash
make c <puerto> <cantidad_cpu> <cantidad_mem> <cantidad_gpu> <tiempo_entre_cada_request*>
```
\* El último parámetro es opcional

## Erlang
Desde erlang/:

### Compilar y correr cliente de Erlang
```bash
make erlang <modo> <cantidad_jobs> <puerto> <timepo_de_ejecucion_job*> <job_request_timeout*>
```
\* Los últimos dos parámetros son opcionales

Hay dos modos: manual y random

En el modo manual el argumento <cant_jobs> no se toma en cuenta y el programa termina cuando se manda:
```erlang
cliente_pid ! fin.
```

Ejemplo de job request desde el modo manual
```erlang
cliente_pid ! {"127.0.0.1:8100:cpu:2 127.0.0.1:8200:gpu:1"}.
```

El otro modo disponible es `random` que genera automáticamente `N` jobs con recursos aleatorios.

### Prueba de deadlock

Para probar el escenario de deadlock del enunciado:

Desde test/
```bash
./test_deadlock.sh
```
---

## Estructura del Proyecto

```text
/proyecto
├── scheduler.log                # Log del planificador (se genera en runtime)
├── /c
│   ├── Makefile                 # Makefile para compilar y correr c
│   ├── agent.c                  # main() del agente
│   ├── consts.h                 # Constantes (puertos, timeouts, límites)
│   ├── /functions               # epoll, sockets, parseo de comandos (handlers.c es el core)
│   └── /structures              # hash table, tabla de agentes/jobs/reservas, fds
├── /erlang
│   ├── Makefile                 # Makefile para compilar y correr Erlang
│   ├── main.erl                 # Arranque del cliente/scheduler (server/3, modos manual|random)
│   ├── job_manager.erl          # Arma pedidos, timeout anti-deadlock, logging
│   ├── system_init.erl          # Conexión inicial al agente C, GET_NODES, supervisor
│   ├── tcp_connection.erl       # Conexión TCP local y reparto de respuestas
│   └── parser.erl               # Parseo de la respuesta NODES
├── /test
│   └── test_deadlock.sh         # Script de prueba automatizada (entregable oficial)
└── /informe
│   └── informe.pdf              # Informe del proyecto
```

---

## Requisitos

| Dependencia | Versión mínima | Notas |
|-------------|---------------|-------|
| GCC         | 11+           | con soporte C11 |
| Make        | 4.0+          | para compilar agente C |
| Erlang/OTP  | 25+           | para planificador |
| Linux       | kernel 2.6+   | `epoll` es Linux-only |

** Nota importante:** El sistema usa `epoll`, por lo que **solo funciona en Linux**. No es compatible con macOS ni Windows de forma nativa

---

## Roles

| Integrante | Responsabilidad | Tecnología |
|-------------|-----------------|------------|
| Dallas | Servidor C con epoll, sockets TCP/UDP, buffers, manejo de conexiones | C, epoll, TCP, UDP |
| Alan | Estructuras de recursos, colas, asignación/liberación, tabla de jobs | C, estructuras de datos |
| Valen | Planificador Erlang, generación de jobs, anti-deadlock, timeouts | Erlang |
| Benja | Scripts de prueba, documentación, diagramas, testing, integración | Bash, documentación |

**R-322 — Sistemas Operativos I**  
Facultad de Ciencias Exactas, Ingeniería y Agrimensura — FCEIA, UNR
