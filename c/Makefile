CC = gcc
FLAGS = -ggdb3 -Wall -Wextra -lpthread
LINKS = ./agent.c ./functions/*.c ./structures/*.c
RES_AMOUNT = 3
RES_1 = cpu
RES_2 = mem
RES_3 = gpu

.PHONY: compile run c

RULE_NAME   			:= $(word 1, $(MAKECMDGOALS))
PORT   					:= $(word 2, $(MAKECMDGOALS))
VAL_1  					:= $(word 3, $(MAKECMDGOALS))
VAL_2  					:= $(word 4, $(MAKECMDGOALS))
VAL_3  					:= $(word 5, $(MAKECMDGOALS))
TIME_PER_REQUEST 		:= $(or $(word 6, $(MAKECMDGOALS)), 0)

compile:
# 	@echo "Compilando agente C"
	$(CC) $(FLAGS) $(LINKS) -o agent

run:
# 	@echo "Iniciando agente C"
	./agent $(TIME_PER_REQUEST) $(PORT) $(RES_AMOUNT) $(RES_1) $(RES_2) $(RES_3) $(VAL_1) $(VAL_2) $(VAL_3)

c:
# 	@echo "Compilando agente C"
	$(CC) $(FLAGS) $(LINKS) -o agent
	clear
#	@echo "Iniciando agente C"
	./agent $(TIME_PER_REQUEST) $(PORT) $(RES_AMOUNT) $(RES_1) $(RES_2) $(RES_3) $(VAL_1) $(VAL_2) $(VAL_3)