-module(system_init).
-export([inicializar_sistema/3, supervisor_scheduler_jobs/4]).

%=============================================== FUNCIONES SISTEMA ===================================================
%MapNodos : mapa donde key es el nodo y value lista con 3 enteros, donde cada entero representa en orden la cantidad de CPU, MEM, GPU

% Crea el proceso scheduler_jobs, si este muere captura el error y se encarga de volver a levantarlo.

% Recibe: JobTimeout(int en milisegundos), Socket, Pid_caller(int, quien inicializó el sistema y espera la confirmación)
% No retorna nada, vive siempre mientras el sistema este corriendo.
supervisor_scheduler_jobs(JobTimeout, Socket, Pid_caller, TimeJob) ->
    process_flag(trap_exit, true),
    
    % Lanzamos el scheduler por primera vez
    Pid_scheduler_job = spawn_link(main, scheduler_jobs, [JobTimeout, Socket, TimeJob]),
    register(pid_scheduler_job, Pid_scheduler_job),
    
    % Avisamos al inicializador que ya está todo montado
    Pid_caller ! scheduler_listo,
    
    % Saltamos al bucle de escucha perpetuo pasándole el PID del scheduler actual
    bucle_supervisor(JobTimeout, Socket, Pid_scheduler_job, TimeJob).

% Loop sin fin que espera señales de salida ('EXIT') del proceso scheduler_job.
% Distingue terminación normal de un crash (revive el proceso con
% un nuevo Pid), e ignora EXIT de cualquier otro proceso que no sea el scheduler actual.

% Recibe: JobTimeout(int), Socket, Pid_scheduler_job(Pid del scheduler que se está supervisando)
% No retorna nada relevante: corre indefinidamente (o hasta terminación normal del scheduler).
bucle_supervisor(JobTimeout, Socket, Pid_scheduler_job, TimeJob) ->
    receive 
        {'EXIT', Pid_scheduler_job, normal} ->
            % El scheduler terminó de procesar todo de forma limpia. 
            % El supervisor ya no es necesario, cerramos tranquilos.
            % io:format("[supervisor] Mi trabajo termino, me voy en paz.~n"),
            ok;

        {'EXIT', Pid_scheduler_job, Reason} ->
            io:format("Error provocado por: ~p. ~nRestaurando sistema...~n", [Reason]),
            
            NuevoPid = spawn_link(main, scheduler_jobs, [JobTimeout, Socket, TimeJob]),
            
            case whereis(pid_scheduler_job) of
                undefined -> ok;
                _ -> unregister(pid_scheduler_job)
            end,
            register(pid_scheduler_job, NuevoPid),
            
            % RECURSIÓN LIMPIA: Volvemos al bucle pasándole el NUEVO Pid.
            % ¡Fijate que jamás volvimos a tocar la inicialización ni clonamos nada más!
            bucle_supervisor(JobTimeout, Socket, NuevoPid, TimeJob);

        {'EXIT', _OtroPid, _Reason} ->
            % Se cayó otra cosa (por ejemplo el padre o wait_jobs)
            % Seguimos escuchando con el mismo PID de scheduler de antes
            bucle_supervisor(JobTimeout, Socket, Pid_scheduler_job, TimeJob)
    end.

% Pide el mapa de nodos al agente C de forma SÍNCRONA y bloqueante, leyendo el socket directamente (sin pasar por tcp_deliver,
% que todavía no existe en este  punto del arranque). Se usa una única vez, al inicio del sistema.

% Recibe: Socket
% Retorna: MapNodos (map de Host => [CantCPU, CantMEM, CantGPU]).
% Si falla la conexión, termina el proceso con exit({error_pidiendo_mapa_inicial, Reason}).
request_map_nodes_initial(Socket) -> 
    gen_tcp:send(Socket, <<"GET_NODES">>),
    case gen_tcp:recv(Socket, 0) of
        {ok, Data} -> 
            parser:binList_to_MapNodos(Data);
        {error, Reason} -> 
            exit({error_pidiendo_mapa_inicial, Reason})
    end.

% Convierte el mapa inicial {Host => [CPU,MEM,GPU]} en entradas atómicas por recurso,
% más una lista con el orden de los nodos, y las inserta en la tabla ETS 'recursos_nodos'.
% Esto permite después usar ets:update_counter de forma atómica al repartir jobs.
% EJ:            CLAVE          VALOR
%           { {"Nodo1", 1}   ,   4}
%           { {"Nodo1", 2}   ,   9}

% Recibe: MapNodos (map de Host => [CantCPU, CantMEM, CantGPU])
cargar_tabla_recursos(MapNodos) ->
    ListNodos = maps:to_list(MapNodos),
    lists:foreach(fun({Host, [Cpu, Mem, Gpu]}) ->
        ets:insert(recursos_nodos, {{Host, 1}, Cpu}),
        ets:insert(recursos_nodos, {{Host, 2}, Mem}),
        ets:insert(recursos_nodos, {{Host, 3}, Gpu})
    end, ListNodos),
    OrdenNodos = [Host || {Host, _} <- ListNodos],
    % Insertamos ahora en la tabla orden_nodos que es una lista con los nodos disponibles
    ets:insert(recursos_nodos, {orden_nodos, OrdenNodos}).

% Arranca todo el sistema: conecta con el agente C, crea las tablas ETS ('pendientes' para jobs en curso, 
% 'recursos_nodos' para el estado de los nodos), pide el mapa de nodos inicial y lo carga en ETS,
% levanta el supervisor del scheduler (esperando su confirmación antes de seguir), y finalmente arranca
% tcp_deliver para empezar a escuchar respuestas del agente en tiempo de ejecución.
% Recibe: Puerto(int)
% Retorna: Socket (para que quien llamó pueda usarlo, ej: manual_loop en modo manual)
inicializar_sistema(Puerto, TimeJob, JobTimeoutInit) -> 

    % Nos conectamos al agente de C
    {ok, Socket} = tcp_connection:connect_agent(Puerto),
    io:format("Conectado al servidor~n"),
    ets:new(pendientes, [named_table, public, set]),
    ets:new(recursos_nodos, [named_table, public, set]), 

    % Pedimos el mapa de nodos DIRECTO por el socket, en modo síncrono,
    % sin pasar por tcp_deliver (que todavía no existe).
    MapNodos = request_map_nodes_initial(Socket),  
    cargar_tabla_recursos(MapNodos),

    % io:format("Anter ~p~n",[JobTimeoutInit]),
    JobTimeout = 1000 * JobTimeoutInit,
    % io:format("Despues ~p~n",[JobTimeout]),
    spawn_link(system_init, supervisor_scheduler_jobs, [JobTimeout, Socket, self(), TimeJob]),
    
    receive
        scheduler_listo -> ok
    end,
    
    % Creamos el proceso tcp deliver
    spawn_link(tcp_connection, tcp_deliver, [Socket, JobTimeout]),
    Socket.

