.PHONY: erlang

RULE_NAME   := $(word 1, $(MAKECMDGOALS))
MODE  		:= $(word 2, $(MAKECMDGOALS))
JOBS  		:= $(word 3, $(MAKECMDGOALS))
PORT   		:= $(word 4, $(MAKECMDGOALS))
TIMEJOB  	:= $(word 5, $(MAKECMDGOALS))
JOBTIMEOUT  := $(word 6, $(MAKECMDGOALS))

erlang:
# 	@echo "Borrando el log anterior"
	touch ../scheduler.log && rm ../scheduler.log
	clear
	
# 	@echo "Iniciando agente de Erlang"
	erl -eval "compile:file(main), compile:file(parser), compile:file(system_init), compile:file(tcp_connection), compile:file(job_manager), main:server($(MODE),$(JOBS),$(PORT),$(TIMEJOB),$(JOBTIMEOUT))."