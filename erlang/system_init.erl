-module(system_init).
-export([inicializar_sistema/2, supervisor_scheduler_jobs/4]).

%=============================================== FUNCIONES SISTEMA ===================================================
%MapNodos : mapa donde key es el nodo y value lista con 3 enteros, donde cada entero representa en orden la cantidad de CPU, MEM, GPU

% Crea el proceso scheduler_jobs, si este muere captura el error y se encarga de volver a levantarlo.
% Recibe: JobTimeout(int en milisegundos), Pid_wait_jobs(Pid), Puerto(int)
% No retorna nada, vive siempre mientras el sistema este corriendo
supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs, Socket, Pid_caller) ->
    process_flag(trap_exit, true),
    
    % Lanzamos el scheduler por primera vez
    Pid_scheduler_job = spawn_link(main, scheduler_jobs, [JobTimeout, Pid_wait_jobs, Socket]),
    register(pid_scheduler_job, Pid_scheduler_job),
    
    % Avisamos al inicializador que ya está todo montado
    Pid_caller ! scheduler_listo,
    
    % Saltamos al bucle de escucha perpetuo pasándole el PID del scheduler actual
    bucle_supervisor(JobTimeout, Pid_wait_jobs, Socket, Pid_scheduler_job).


bucle_supervisor(JobTimeout, Pid_wait_jobs, Socket, Pid_scheduler_job) ->
    receive 
        {'EXIT', Pid_scheduler_job, normal} ->
            % El scheduler terminó de procesar todo de forma limpia. 
            % El supervisor ya no es necesario, cerramos tranquilos.
            io:format("[supervisor] Mi trabajo termino, me voy en paz.~n"),
            ok;

        {'EXIT', Pid_scheduler_job, Reason} ->
            io:format("[supervisor] Mori por: ~p. ~n[supervisor] Reviviendo...~n", [Reason]),
            
            NuevoPid = spawn_link(main, scheduler_jobs, [JobTimeout, Pid_wait_jobs, Socket]),
            
            case whereis(pid_scheduler_job) of
                undefined -> ok;
                _ -> unregister(pid_scheduler_job)
            end,
            register(pid_scheduler_job, NuevoPid),
            
            % RECURSIÓN LIMPIA: Volvemos al bucle pasándole el NUEVO Pid.
            % ¡Fijate que jamás volvimos a tocar la inicialización ni clonamos nada más!
            bucle_supervisor(JobTimeout, Pid_wait_jobs, Socket, NuevoPid);

        {'EXIT', _OtroPid, _Reason} ->
            % Se cayó otra cosa (por ejemplo el padre o wait_jobs)
            % io:format("[supervisor] (~p). Ignorando.~n", [Reason]),
            
            % Seguimos escuchando con el mismo PID de scheduler de antes
            bucle_supervisor(JobTimeout, Pid_wait_jobs, Socket, Pid_scheduler_job)
    end.

% Obtiene la lista de nodos activos, crea tabla de PENDIENTES, crea y LINKEA los procesos scheduler_job y wait_job 
% Recibe: N(cantidad de jobs a crear), Puerto(int)
% Retorna: ListMaximos(lista de 3 int, formada por la suma de la cantidad de ese recurso entre todos los nodos disponibles, donde el orden de las cantidades es CPU, MEM, GPU.)
inicializar_sistema(N, Puerto) -> 

    % Nos conectamos al agente de C
    {ok, Socket} = tcp_connection:connect_agent(Puerto),

    ets:new(pendientes, [named_table, public, set]),

    % (!) Registrar este pid
    Pid_wait_jobs = spawn_link(job_manager, wait_jobs, [N]),  

    JobTimeout = 3000,
    spawn_link(system_init, supervisor_scheduler_jobs, [JobTimeout, Pid_wait_jobs, Socket, self()]),
    
    receive
        scheduler_listo -> ok
    end,
    
    % Creamos el proceso tcp deliver
    spawn_link(tcp_connection, tcp_deliver, [Socket, JobTimeout]),
    Socket.