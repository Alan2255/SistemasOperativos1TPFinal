-module(main).
-export([server/5, client/5, scheduler_jobs/3]).
%nodos = host 


% Genera N jobs con recursos aleatorios (1, 2 o 3 tipos de recurso, cantidades random),
% mandándoselos uno por uno a pid_scheduler_job. Se llama a sí misma recursivamente
% hasta agotar N, y al llegar a 0 avisa que no hay más jobs por generar.

% Recibe: N(int, cuántos jobs quedan por generar)
% No retorna nada relevante: envia mensajes {JobID, Job, CantRecursos} o no_hay_mas_jobs al final) al proceso 
% registrado como pid_scheduler_job.
generate_jobs(0) -> % cuando N es 0, termina
        pid_scheduler_job ! no_hay_mas_jobs;

generate_jobs(N) -> 
    JobID_int = erlang:unique_integer([positive]), %genera un entero unico en toda la instancia actual del sistema(maq virtual BEAM)
    JobID = integer_to_list(JobID_int),
    ListRecursos = ["cpu", "mem", "gpu"],
    CantRandom = 10,
    Eleccion_recursos = rand:uniform(3), %random entre 1 y N (inclusive), elije cuantos recursos va a pedir
    
    case Eleccion_recursos of
        1 ->
            Indice_recurso = rand:uniform(3),
            Recurso = lists:nth(Indice_recurso, ListRecursos),
            Cantidad = integer_to_list(rand:uniform(CantRandom)),%Cant random del recurso elegido de 1 hasta lo max q pueda pedir

            Job = Recurso ++ ":" ++ Cantidad, %esto crea el Job EJ : "recursorandom:numrandom"
            pid_scheduler_job ! {JobID, Job, 1};

         2 ->    
             Indice_ignorar = rand:uniform(3),
             Recurso_ignorar = lists:nth(Indice_ignorar, ListRecursos),

             Recursos_elegidos = [R || R <- ListRecursos, R =/= Recurso_ignorar], %devuelve una lista sin el recurso ignorado
             %Cant_elegidas = parser:eliminar_indice(Indice_ignorar, ListRecursos), %lo hacemos asi pq de otra maner apodrias tener misma cant y no saber cual eliminar

             [Recurso1, Recurso2] = Recursos_elegidos,
             Cantidad1 = integer_to_list(rand:uniform(CantRandom)),
             Cantidad2 = integer_to_list(rand:uniform(CantRandom)),
            
             Job = Recurso1 ++ ":" ++ Cantidad1 ++ ":" ++ Recurso2 ++ ":" ++ Cantidad2,
             pid_scheduler_job ! {JobID, Job, 2};

         3 ->
             Recurso1 = "cpu",
             Cantidad1 = integer_to_list(rand:uniform(CantRandom)),%Cant random del recurso elegido de 1 hasta lo max q pueda pedir

             Recurso2 = "mem",
             Cantidad2 = integer_to_list(rand:uniform(CantRandom)),

             Recurso3 = "gpu",
             Cantidad3 = integer_to_list(rand:uniform(CantRandom)),

             Job = Recurso1 ++ ":" ++ Cantidad1 ++ ":" ++ Recurso2 ++ ":" ++ Cantidad2 ++ ":" ++  Recurso3 ++ ":" ++ Cantidad3,
             pid_scheduler_job ! {JobID, Job, 3}
        end,
    io:format("JOB REQUEST ~p ~p ~n",[JobID, Job]),
    generate_jobs(N-1).%Ya generamos un job restamos el N de cantidad a generar y llamamos de nuevo a la funcion.

% Punto de entrada del proceso scheduler: arranca el contador de jobs activos en 0 y llama a job_manager:recibir_jobs_y_armar_peticiones, 
% que es el loop real que corre durante toda la vida del sistema. Existe como función separada para que 
% supervisor_scheduler_jobs pueda spawnear el proceso con spawn_link
% Recibe: JobTimeout(int, miliseg), Socket, TimeJob(int).
scheduler_jobs(JobTimeout, Socket, TimeJob)->
    job_manager:recibir_jobs_y_armar_peticiones(Socket, JobTimeout, 0, TimeJob).

% Crea y linkea el proceso client, y lo registra como cliente_pid para que
% otros procesos puedan mandarle mensajes por nombre.
% Recibe: Modo(atomo: random|manual), N(int, cantidad de jobs en modo random), Puerto(int), TimeJob(int, en segundos), JobTimeout(int, en segundos).
server(Modo, N, Puerto, TimeJob, JobTimeoutInit) ->
    Pid_client = spawn_link(?MODULE, client, [Modo, N, Puerto, TimeJob, JobTimeoutInit]), %Si el client muere el server se entera
    register(cliente_pid, Pid_client).

% Espera que el usuario mande jobs armados a mano desde la consola, uno por uno, 
% spawneando un handler_job por cada uno. 
% Termina al recibir 'fin', momento en el que limpia las tablas ETS usadas durante la ejecución.
% Recibe: Socket, TimeJob(int)
manual_loop(Socket, TimeJob, JobTimeoutInit) -> %Asi deberia quedar el string a mandar a C  JOB_REQUEST 1001 192.168.1.2:cpu:2 192.168.1.3:gpu:1
    receive     
        %Desde consola envias el JOB entero por ej: {"192.168.1.2:cpu:2 192.168.1.3:gpu:1"}.
        {Job} ->
                JobID_int = erlang:unique_integer([positive]), %genera un entero unico en toda la instancia actual del sistema(maq virtual BEAM)
                JobID = integer_to_list(JobID_int),
                Msg_REQUEST = "JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Job,
                Msg_RELEASE = "JOB_RELEASE" ++ " " ++ JobID,
                %Pasamos 0 en CantRecusos pq no importan y ademas procesar_respuesta los ignora. 
                spawn(job_manager, handler_job, [JobID, Job, 0, 1000 * JobTimeoutInit, Socket, Msg_REQUEST, Msg_RELEASE, [], TimeJob]), %Manda el msg al agente espera su rta y la maneja
                manual_loop(Socket, TimeJob, JobTimeoutInit);
        % Terminará cuando el usuario mande cliente_pid ! fin.
        fin ->  ok 
    end,
        ets:delete(pendientes),%liberamos la tabla d procesos pendientes pq ya terminamos   
        ets:delete(recursos_nodos).

% ===================================== MODO MANUAL =================================================
% N sigue siendo un argumento obligatorio de server/3/client/3 (porque la firma es fija para los dos modos), 
% pero en modo manual no se usa para nada, así que se puede pasar cualquier valor, típicamente 0.


% Inicializa el sistema completo y según el Modo, arranca la generación automática
% de N jobs (random) o el loop de carga manual (manual). Al terminar, limpia las
% tablas ETS usadas durante la ejecución.

% Recibe: Modo(atomo: random|manual), N(int, cantidad de jobs en modo random), Puerto(int), TimeJob(int), JobTimeoutInit(int).
% Corre hasta que la ejecución completa termine (fin en modo random, o 'fin' recibido en manual_loop en modo manual).
client(Modo, N, Puerto, TimeJob, JobTimeoutInit) ->
    
    Socket = system_init:inicializar_sistema(Puerto, TimeJob, JobTimeoutInit), %Inicializar sistema, retorna el socket
    
    case Modo of
        random ->
            generate_jobs(N), % Generara N jobs q se los enviara a scheduler de jobs
            receive 
                fin ->  ok % Cuando terminan todos los jobs se manda solo el msg fin avisando al cliente y ahora si puede finalizar.
            end,
            ets:delete(pendientes),% Liberamos la tabla d procesos pendientes.
            ets:delete(recursos_nodos); %Liberamos la tabla de recursos_nodos.

        manual ->
            manual_loop(Socket, TimeJob, JobTimeoutInit); % Mismo socket que usa tcp_deliver

        _ -> 
            io:format("Los modos son: manual o random~n"),
            exit({modo_invalido, Modo})
    end.


