.PHONY: erlang

RULE_NAME   := $(word 1, $(MAKECMDGOALS))
MODE  		:= $(word 2, $(MAKECMDGOALS))
JOBS  		:= $(word 3, $(MAKECMDGOALS))
PORT   		:= $(word 4, $(MAKECMDGOALS))
TIMEJOB  	:= $(or $(word 5, $(MAKECMDGOALS)), 1)
TIMEOUT  	:= $(or $(word 6, $(MAKECMDGOALS)), 4)

erlang:
# 	@echo "Borrando el log anterior"
	touch ../scheduler.log && rm ../scheduler.log
	clear
	
# 	@echo "Iniciando agente de Erlang"
	erl -eval "compile:file(main), compile:file(parser), compile:file(system_init), compile:file(tcp_connection), compile:file(job_manager), main:server($(MODE),$(JOBS),$(PORT),$(TIMEJOB),$(TIMEOUT))."