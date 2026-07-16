-module(main).
-export([server/3, client/3, scheduler_jobs/3]).
%nodos = host 
                 
%Una vez llega a 0, termina.
generate_jobs(0) -> % cuando N es 0, termina
        pid_scheduler_job ! no_hay_mas_jobs;

% Arma el job con el jobID y la cantidd de recursos q requerira, a que nodo se lo pedira lo manejara el scheduler
% Envia por mensaje JobID(int), Job(string), CantRecursos(int) al proceso scheduler_job
% Recibe: N(int), ListMaximos(lista de 3 enteros)
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
    io:format("[job_generator] ~p ~p ~p ~n",[JobID, Job, Eleccion_recursos]),
    generate_jobs(N-1).%Ya generamos un job restamos el N de cantidad a generar y llamamos de nuevo a la funcion.

% Recibe por mensaje JobID(int), Job(string), CantRecursos(int) y crea SIN LINK un proceso que maneje este job, se vuelve a llamar recursivamente para seguir atendiendo jobs
% handler_job es creado sin link ya que si muere o le pasa algo a ese job no nos importa queremos seguir atendiendo los proximos.
% Recibe : JobTimeout(int), Pid_wait_jobs(Pid), Puerto(int)
scheduler_jobs(JobTimeout, Pid_wait_jobs, Socket)->
    job_manager:recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, 0).

%Crea y linkea el proceso client
% Recibe: Modo(atomo), N(int), Puerto(int) 
server(Modo, N, Puerto) ->
    Pid_client = spawn_link(?MODULE, client, [Modo, N, Puerto]), %Si el client muere el server se entera
    register(cliente_pid, Pid_client).

manual_loop(Socket) -> %Asi deberia quedar el string a mandar a C  JOB_REQUEST 1001 192.168.1.2:cpu:2 192.168.1.3:gpu:1
    receive     
        %Desde consola envias el JOB entero por ej: {"192.168.1.2:cpu:2 192.168.1.3:gpu:1"}.
        {Job} ->
                JobID_int = erlang:unique_integer([positive]), %genera un entero unico en toda la instancia actual del sistema(maq virtual BEAM)
                JobID = integer_to_list(JobID_int),
                Msg_REQUEST = "JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Job,
                Msg_RELEASE = "JOB_RELEASE" ++ " " ++ JobID,
                %Pasamos 0 en CantRecusos pq no importan y ademas procesar_respuesta los ignora. 
                spawn(job_manager, handler_job, [JobID, Job, 0, 3000, Socket, Msg_REQUEST, Msg_RELEASE]), %Manda el msg al agente espera su rta y la maneja
                manual_loop(Socket);
        %Terminara cuando el usuario mande cliente_pid ! fin o cuando ya generaste N jobs q le pasaste como parametro
        fin ->  ok 
    end,
        ets:delete(pendientes).%liberamos la tabla d procesos pendientes pq ya terminamos   


%Inicializa el sistema y manda a generar los N jobs y espera a q terminen
% Recibe: Modo(atomo), N(int), Puerto(int) 
client(Modo, N, Puerto) ->
    
    Socket = system_init:inicializar_sistema(N, Puerto), %Inicializar sistema, y retorna una list de 3 int con los valores maximos d cada recurso
    
    case Modo of
        random ->
            generate_jobs(N), %generara N jobs q se los enviara a scheduler de jobs
            receive 
                fin ->  ok%Cuando terminan todos los jobs se manda solo el msg fin avisando al cliente y ahora si puede finalizar.
            end,
            ets:delete(pendientes);%liberamos la tabla d procesos pendientes pq ya terminamos

        manual ->
            manual_loop(Socket); %Mismo socket que usa tcp_deliver

        _ -> 
            io:format("Los modos son: manual o random~n"),
            exit({modo_invalido, Modo})
    end.


