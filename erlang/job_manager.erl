-module(job_manager).
-export([handler_job/8, recibir_jobs_y_armar_peticiones/4, armar_peticiones/4, wait_jobs/1]).

%=============================================== FUNCIONES SOBRE JOBS ===================================================

%Recibe la respuesta de la peticion del job enviado y maneja que hacer en cada caso, cuando termina un job, lo elimina de la tabla de Pendientes, registra su log y envia -
%- msg a wait_jobs avisando que termino.
% Recibe: JobID(string), Job(string), CantRecursos(int), Socket(int), Msg_Release(string), JobTimeuot(int en milisegundos), Pid_wait_jobs(Pid).
procesar_respuesta(JobID, Job, _CantRecursos, Socket, Msg_RELEASE, JobTimeout, Pid_wait_jobs) ->
    receive 
        {tcp_msg, Bin} -> 
            case binary_to_list(Bin) of
                "JOB_GRANTED " ++ _Rest -> 
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_GRANTED"),
                    io:format("Simulando trabajo. . .~n"),
                    timer:sleep(2000),
                    io:format("Trabajo finalizado!.~n"),
                    gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)),
                    pid_scheduler_job ! {job_terminado, JobID},
                    Pid_wait_jobs ! {ok, Socket};

                "JOB_DENIED " ++ _Rest -> 
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_DENIED"),
                    pid_scheduler_job ! {job_terminado, JobID},
                    Pid_wait_jobs ! {ok, Socket};
                
                Invalido ->
                    io:format("Formato de mensaje no esperado por el handler: ~p~n", [Invalido]),
                    pid_scheduler_job ! {job_terminado, JobID}, 
                    Pid_wait_jobs ! {ok, Socket}
            end
    after JobTimeout -> 
        borrarPendiente_and_registrarLog(JobID, Job, "POSIBLE DEADLOCK"),
        gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)),
        pid_scheduler_job ! {job_terminado, JobID}
        % volver mandar al buzon
        % pid_scheduler_job ! {JobID, Job, CantRecursos}
    end.

% Retorna: Lista de tuplas de la forma [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2}, etc] si pudo repartir el recurso        
%manda el msg al agente espera su respuesta y la maneja
handler_job(JobID, Job, CantRecursos, JobTimeout, Pid_wait_jobs, Socket, Msg_REQUEST, Msg_RELEASE) ->
    gen_tcp:send(Socket, list_to_binary(Msg_REQUEST)),
    ets:insert(pendientes, {JobID, Job, self()}),
    procesar_respuesta(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Pid_wait_jobs).

% Cada vez q recibe un job arma la peticion y crea un proceso (conectado al mismo agente) para q mande y espere la rta del job
% se llama recursivamente para seguir atendiendo jobs
% Eliminamos MapNodos de los argumentos iniciales
recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos) ->
    receive
         no_hay_mas_jobs -> 
            wait_jobs(JobsActivos);

         {job_terminado, _JobID} ->
            recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos - 1);

         {JobID, Job, CantRecursos} -> 
            io:format("[scheduler] Procesando Job ~s (~s) ~n", [JobID, Job]),
            
            % Solicitamos los nodos de forma asíncrona a C
            tcp_connection:send_map_nodes_request(Socket),
            
            % Saltamos a un estado de espera específico para capturar la respuesta de tcp_deliver
           esperar_mapa_nodos(Socket, JobTimeout, Pid_wait_jobs, JobsActivos, JobID, Job, CantRecursos)
    end.

%Esperar mapa nodos
esperar_mapa_nodos(Socket, JobTimeout, Pid_wait_jobs, JobsActivos, JobID, Job, CantRecursos) ->
    receive
        {tcp_nodes, BinList} -> 
            MapNodos = binList_to_MapNodos(BinList),

            case armar_peticiones(JobID, Job, CantRecursos, MapNodos) of
                {error, no_alcanza} -> 
                    Pid_wait_jobs ! {ok, Socket},
                    recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos);

                {Msg_REQUEST, Msg_RELEASE} -> 
                    spawn(job_manager, handler_job, [JobID, Job, CantRecursos, JobTimeout, Pid_wait_jobs, Socket, Msg_REQUEST, Msg_RELEASE]),
                    recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos + 1) % +1 JobActivo       
            end;

        % (?) Esto debería estar acá? 
        {job_terminado, _IDTerminado} ->
            esperar_mapa_nodos(Socket, JobTimeout, Pid_wait_jobs, JobsActivos - 1, JobID, Job, CantRecursos)

    after JobTimeout -> 
        io:format("[scheduler] Error: Timeout esperando nodos del tcp_deliver para el Job ~s~n", [JobID]),
        Pid_wait_jobs ! {ok, Socket},
        recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs, JobsActivos)
    end.


%Repartir entre nodos
repartir_entre_nodos(_Indice, 0, _Nodos) -> [];

repartir_entre_nodos(_Indice, _CantidadRestante, []) ->
    {error, no_alcanza};

repartir_entre_nodos(Indice, CantidadRestante, [{Host, Recursos} | Resto]) -> 
    Disponible = lists:nth(Indice, Recursos), 
    case Disponible of
        0 -> 
            repartir_entre_nodos(Indice, CantidadRestante, Resto);
        _ -> 
            Tomar = min(Disponible, CantidadRestante),
            % Evaluamos primero qué devuelve la llamada recursiva
            case repartir_entre_nodos(Indice, CantidadRestante - Tomar, Resto) of
                {error, no_alcanza} -> 
                    % Si en el fondo de la lista no alcanzó, propagamos el error directo hacia arriba
                    {error, no_alcanza};
                ResultadoExitoso -> 
                    % Si alcanzó (devolvió una lista), acoplamos el nodo actual a la cabeza
                    [{Host, Tomar} | ResultadoExitoso]
            end
    end.

%En el case busca en el mapa de nodos el primer nodo q tenga suficiente recurso segun el tipo de recurso pedido y su cant requerida
%%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
%Si encuentra en 1 devuelve {value, {Nodo, Recursos}, retornamos Nodo y Cantidad y sino tiene q buscar entre mas nodos para completar
% Recibe: %Recurso(string), Cantidad(int), MapNodos(map)
% Retorna: {error, no_alcanza} si no alcanzaron los nodos para la cantidad que requerias
% Retorna: Lista de tuplas de la forma [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2}, etc]
elegir_nodos(Recurso, Cantidad, MapNodos) -> 
    Nodos = maps:to_list(MapNodos), 
    Indice = case Recurso of 
        "cpu" -> 1;
        "mem" -> 2;
        "gpu" -> 3
    end,
    case lists:search(fun({_Host, Recursos}) -> 
        lists:nth(Indice, Recursos) >= Cantidad
    end, Nodos) of  
        {value, {Nodo, _}} -> 
            [{Nodo, Cantidad}]; 

        false -> 
            repartir_entre_nodos(Indice, Cantidad, Nodos)
    end.
    
%Funcion que devuelve el msg armado con el job de cuanto recurso le pedis a cada nodo, luego solo faltaria agregarle la peticion y el JobID.
% Recibe: Recurso(string), Cant(string), MapNodos(map)
% Retorna {error, no_alcanza} si no alcanzaron los nodos para la cantidad que requerias
% Retorna string Ej: "@Nodo:Recurso:Cant @Nodo:Recurso:Cant"
armar_msg(Recurso, Cant, MapNodos) -> 
    case elegir_nodos(Recurso, list_to_integer(Cant), MapNodos) of 
        {error, no_alcanza} -> 
            {error, no_alcanza};

        ListNodoCantidad -> %devuelve la lista de tuplas [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2} 
            ListPedidoPorNodo = [Nodo ++ ":" ++ Recurso ++ ":" ++ integer_to_list(Cantidad) || {Nodo, Cantidad} <- ListNodoCantidad ],%A cada elem de la lista, le aplica eso
            %Devuelve una lista con ["@Nodo:Recurso:Cant", "@Nodo:Recurso:Cant", etc]
            string:join(ListPedidoPorNodo, " ")%retorna un string donde separa cada elem de la lista con un " ", EJ: @Nodo:Recurso:Cant @Nodo:Recurso:Cant etc
    end.

% Crea el msg final de JOB_REQUEST 
% Recibe: JobID(string), Job(string),CantRecursos(int) MapNodos(mapa)
% Retorna: {error, no alcanza} en caso que no alcance la cantidad de nodos
% Retorna: Msg_final(string)
handler_msgs(JobID, Job, CantRecursos, MapNodos) -> %JobID(string), Job(string),CantRecursos(int) MapNodos(mapa)
    List_recursos = string:tokens(Job, ":"),
    case CantRecursos of 
        1 ->
            [Recurso1, Cant1] = List_recursos,
            case armar_msg(Recurso1, Cant1, MapNodos) of 
                {error, no_alcanza} ->  %Primero evaluamos el error, pq sino Msg al ser variable matchea cualquier cosa que llegue
                    {error, no_alcanza};
                Msg -> %Msg es un string por ej: "@Nodo:Recurso:Cant @Nodo:Recurso:Cant" etc. 
                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg,
                    Msg_final
            end;

        2 ->
            [Recurso1, Cant1, Recurso2, Cant2] = List_recursos,
            case armar_msg(Recurso1, Cant1, MapNodos) of
                {error, no_alcanza} -> 
                    {error, no_alcanza};
                Msg1 -> 
                    case armar_msg(Recurso2, Cant2, MapNodos) of
                        {error, no_alcanza} -> 
                            {error, no_alcanza};
                        Msg2 ->
                            Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg1 ++ " " ++ Msg2,
                            Msg_final
                    end
            end;

        3 ->
            [Recurso1, Cant1, Recurso2, Cant2, Recurso3, Cant3] = List_recursos,
            case armar_msg(Recurso1, Cant1, MapNodos) of
                {error, no_alcanza} -> 
                    {error, no_alcanza};
                Msg1 ->
                    case armar_msg(Recurso2, Cant2, MapNodos) of
                        {error, no_alcanza} -> 
                            {error, no_alcanza};
                        Msg2 ->
                            case armar_msg(Recurso3, Cant3, MapNodos) of
                                {error, no_alcanza} -> 
                                    {error, no_alcanza};
                                Msg3 ->
                                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg1 ++ " " ++ Msg2 ++ " " ++ Msg3,
                                    Msg_final
                            end
                    end
            end
    end.

%Arma las peticiones que enviara al agente C.
% Recibe: JobID(string), Job(string), CantRecursos(int), MapNodos(mapa)
% Retorna {error, no alcanza} si la cantidad del recurso no se puede repartir entre ninguna cantidad de nodos disponibles
% Retorna {Msg_REQUEST(string), Msg_RELEASE(string} si la cantidad del recurso SI se pudo repartir entre la cantidad de los nodos disponibles
armar_peticiones(JobID, Job, CantRecursos, MapNodos) -> %%JobID(string), Job(string), CantRecursos(int), MapNodos(map)
    case handler_msgs(JobID, Job, CantRecursos, MapNodos) of %Devuelve el msg completo para enviar si puede y sino error.
        {error, no_alcanza} ->
            {error, no_alcanza};
         Msg_REQUEST ->
            Msg_RELEASE = "JOB_RELEASE" ++ " " ++ JobID, %Liberar recurso
            {Msg_REQUEST, Msg_RELEASE}
    end.

wait_jobs(0) ->
    io:format("[scheduler] Todos los jobs finalizaron.~n"),
    cliente_pid ! fin,
    ok;

wait_jobs(JobsActivos) ->
    receive
        {job_terminado, _JobID} ->
            wait_jobs(JobsActivos - 1)
    end.

binList_to_MapNodos(BinList) ->
    ListStr = binary_to_list(BinList),
    ListSinPrefijo = parser:remover_prefijo_nodes(ListStr),
    List_nodos_separados = string:split(ListSinPrefijo, ";", all),
    parser:parsear_lista_nodos(List_nodos_separados).

% Si no existe el archivo lo crea y sino escribe al final
% Funcion que registra en un archivo log la fecha, hora, JobID , Job y respuesta del agente.
% Recibe: JobID(string), Job(string), Msg_Resultado(string)
registrar_log(JobID, Job, Msg_resultado)-> 
    {{Y,M,D}, {H,Mi,S}} = calendar:local_time(), %obtiene la fecha 
    Linea = io_lib:format("~p-~p-~p ~p:~p:~p | Job ~p | ~s | ~s~n", [Y, M, D, H, Mi, S, JobID, Job, Msg_resultado]), %Devuelve un string para usarlo, a dif de io:format que imprime directo en la consola
    file:write_file("scheduler.log", Linea , [append]). %append para q no se borre lo anterior

%Borra el Job de la tabla de pendientes y registra su log.
%Recibe: %JobID(string), Job(string), Msg(String)
borrarPendiente_and_registrarLog(JobID, Job, Msg) -> 
    ets:delete(pendientes, JobID), %lo elimino de la lista de pendientes
    registrar_log(JobID, Job, Msg).


