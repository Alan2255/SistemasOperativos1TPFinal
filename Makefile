RES_AMOUNT = 3
RES_1 = cpu
RES_2 = mem
RES_3 = gpu
VAL_1 = 32
VAL_2 = 256
VAL_3 = 48

# Ejecuta siempre
.PHONY: compile_c run_c c erlang

# 1. Capturamos los argumentos de la línea de comandos
TIPO_COMPILACION := $(word 1, $(MAKECMDGOALS))
ARGS_PROGRAMA    := $(wordlist 2, $(words $(MAKECMDGOALS)), $(MAKECMDGOALS))

# 2. Truco para que 'make' no se queje de que '8080' no es una regla válida
$(eval $(ARGS_PROGRAMA):;@:)

compile_c:
# 	@echo "Compilando agente C"
	gcc -ggdb3 -Wall -Wextra -lpthread \
		./c/agent.c \
		./c/functions/*.c \
		./c/structures/*.c\
		-o agent

run_c:
# 	@echo "Iniciando agente C"
	./agent $(ARGS_PROGRAMA) $(RES_AMOUNT) $(RES_1) $(RES_2) $(RES_3) $(VAL_1) $(VAL_2) $(VAL_3)

c:
# 	@echo "Compilando agente C"
	gcc -ggdb3 -Wall -Wextra -lpthread \
		./c/agent.c \
		./c/functions/*.c \
		./c/structures/*.c\
		-o agent
#	@echo "Iniciando agente C"
	./agent $(ARGS_PROGRAMA) $(RES_AMOUNT) $(RES_1) $(RES_2) $(RES_3) $(VAL_1) $(VAL_2) $(VAL_3)

erlang:
# 	@echo "Iniciando agente Erlang"
	erl -eval "compile:file('erlang/main'), \
		compile:file('erlang/parser'), \
		compile:file('erlang/system_init'), \
		compile:file('erlang/tcp_connection'), \
		compile:file('erlang/job_manager'), \
		main:server(random,15,$(ARGS_PROGRAMA))."