-module(main).
-export([server/2, client/2, generate_jobs/2, scheduler_jobs/2]).
%nodos = host 
                        
generate_jobs(0, _ListMaximos) -> % cuando N es 0, termina
        ok;

%Arma el job con el jobID y la cantidd de recursos q requerira, a que nodo se lo pedira lo manejara el scheduler
generate_jobs(N, ListMaximos) -> %N(int), ListMaximos(lista de 3 enteros)
    [MaxCPU, MaxMEM, MaxGPU] = ListMaximos,
    JobID_int = erlang:unique_integer(), %genera un entero unico en toda la instancia actual del sistema(maq virtual BEAM)
    JobID = integer_to_list(JobID_int),
    ListRecursos = ["cpu", "mem", "gpu"],

    Eleccion_recursos = rand:uniform(3), %random entre 1 y N (inclusive), elije cuantos recursos va a pedir
    
    case Eleccion_recursos of
        1 ->
            Indice_recurso = rand:uniform(3),
            Recurso = lists:nth(Indice_recurso, ListRecursos),
            Cantidad = integer_to_list(rand:uniform((lists:nth(Indice_recurso, ListMaximos)))),%Cant random del recurso elegido de 1 hasta lo max q pueda pedir

            Job = Recurso ++ ":" ++ Cantidad, %esto crea el Job EJ : "recursorandom:numrandom"
            pid_scheduler_job ! {JobID, Job, 1};

        2 ->    
            Indice_ignorar = rand:uniform(3),
            Recurso_ignorar = lists:nth(Indice_ignorar, ListRecursos),

            Recursos_elegidos = [R || R <- ListRecursos, R =/= Recurso_ignorar], %devuelve una lista sin el recurso ignorado
            Cant_elegidas = aux:eliminar_indice(Indice_ignorar, ListMaximos), %lo hacemos asi pq de otra maner apodrias tener misma cant y no saber cual eliminar

            [Recurso1, Recurso2] = Recursos_elegidos,
            [Cant1, Cant2] = Cant_elegidas,
            Cantidad1 = integer_to_list(rand:uniform(Cant1)),
            Cantidad2 = integer_to_list(rand:uniform(Cant2)),
            
            Job = Recurso1 ++ ":" ++ Cantidad1 ++ ":" ++ Recurso2 ++ ":" ++ Cantidad2,
            pid_scheduler_job ! {JobID, Job, 2};

        3 ->
            Recurso1 = "cpu",
            Cantidad1 = integer_to_list(rand:uniform(MaxCPU)),%Cant random del recurso elegido de 1 hasta lo max q pueda pedir

            Recurso2 = "mem",
            Cantidad2 = integer_to_list(rand:uniform(MaxMEM)),

            Recurso3 = "gpu",
            Cantidad3 = integer_to_list(rand:uniform(MaxGPU)),

            Job = Recurso1 ++ ":" ++ Cantidad1 ++ ":" ++ Recurso2 ++ ":" ++ Cantidad2 ++ ":" ++  Recurso3 ++ ":" ++ Cantidad3,
            pid_scheduler_job ! {JobID, Job, 3}
        end,
    generate_jobs(N-1, ListMaximos).%Ya generamos un job restamos el N de cantidad a generar y llamamos de nuevo a la funcion.
        
%lleva la tabla de pendientes
% Recibe Lista donde cada elem es cada nodo, aca usamos spawn y no spawn_link pq si muere el handler debe seguir atendiendo otros jobs.
scheduler_jobs(JobTimeout, Pid_wait_jobs) -> %$Recibe jobs, analiza a que nodo pedirle cada recurso y este se lo manda al server
    receive
        {JobID, Job, CantRecursos} -> %Job = "recurso:cant:recurso:cant"
            spawn(aux, handler_job, [JobID, Job, CantRecursos, JobTimeout,Pid_wait_jobs]), %Recibe el job y crea un proceso q lo maneje,  LE PASAMOS MODULO AUX ESTA AHI LA FUN
            scheduler_jobs(JobTimeout, Pid_wait_jobs)%llama recursivamente scheduler para q siga recibiendo jobs
    end. 
    

server(Modo, N) ->
    Pid_client = spawn_link(?MODULE, client, [Modo, N]), %Si el client muere el server se entera
    register(cliente_pid, Pid_client).


%Inicializa el sistema y manda a generar los N jobs y espera a q terminen 
client(Modo, N) ->
    ListMaximos = aux:inicializar_sistema(N),
    case Modo of
        random ->
            generate_jobs(N, ListMaximos), %generara N jobs q se los enviara a scheduler de jobs
            receive 
                fin ->  ok%Cuando terminan todos los jobs se manda solo el msg fin avisando al cliente y ahora si puede finalizar.
            end,
            ets:delete(pendientes);%liberamos la tabla d procesos pendientes pq ya terminamos

        manual ->
            %jobs los genera el usuario creando los jobs como el quiera y mandando msg a pid_scheduler_job,
            % deben tener la forma de recursorandom:numrandom"
            %Terminara cuando el usuario mande cliente_pid ! fin o cuando ya generaste N jobs q le pasaste como parametro
            receive 
                fin ->  ok 
            end,
            ets:delete(pendientes)%liberamos la tabla d procesos pendientes pq ya terminamos
    end.



