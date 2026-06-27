-module(aux).
-export([eliminar_indice/2, inicializar_sistema/2, handler_job/8, wait_jobs/1, recibir_jobs_y_armar_peticiones/3, supervisor_scheduler_jobs/4, conectar_y_obtener_nodos/1, get_map_nodes/1, tcp_deliver/2]).

%========= Funciones AUXILIARES  ===============$
%MapNodos : mapa donde key es el nodo y value lista con 3 enteros, donde cada entero representa en orden la cantidad de CPU, MEM, GPU


obtener_cant_maxima_recursos([], ListMaximos) ->
    ListMaximos;

% Obtiene la cantidad maxima de cada recurso entre todos los nodos disponibles, sirve para armar los jobs sin que se pase del maximo general que puede obtener
% Recibe: [Nodo | Resto](Cada elemento es un string con el nodo y sus datos), ListMaximos(lista formada por 3 int)
% Si no puede hacer pattern matching sobre el nodo es pq esta mal formado termina el programa pq no podra funcionar.
% Retorna: ListMaximos(Lista de 3 int)
obtener_cant_maxima_recursos([Nodo | Resto], ListMaximos) -> 
    List_recursos = string:tokens(Nodo, ":"), %devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, etc
    [MaxCPU, MaxMEM, MaxGPU] = ListMaximos,
    case List_recursos of 
        [_Host, _Puerto , "cpu", CantCPU,  "mem", CantMEM, "gpu", CantGPU] -> 
            SumaCPU = MaxCPU + list_to_integer(CantCPU),
            SumaMEM = MaxMEM + list_to_integer(CantMEM),
            SumaGPU = MaxGPU + list_to_integer(CantGPU),
            obtener_cant_maxima_recursos(Resto, [SumaCPU, SumaMEM, SumaGPU]);

        _ ->
            %si no pudo asignar es pq esta mal el nodo
            io:format("Nodo mal formado! ~n"),
            throw(badmatch) %Si mandaron datos erroneos desde C, terminamos el programa porque no podra funcionar
    end.
          
%Split devuelve: primera lista con los primeros k elem y la segunda lista el resto ej lists:split(3, [a,b,c,d]) devolvera
% [a,b,c] [d], como queres borrar el elemento N, haces N-1 para q el elem q queres borrar quede al inicio de la segunda lista
% entonces haces {izq, [ _ | Der]} q ignora el primer elemento y desp unis toda la lista ignorando ese elem
% Recibe: N(int), Lista(Lista de 3 int)
% Retorna: La lista con el elemento eliminado
eliminar_indice(N, Lista) ->
    {Izq, [_ | Der]} = lists:split(N - 1, Lista), 
    Izq ++ Der.  
    
% string:toekns ":" , devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, 
% Recibe: Nodo(string)
% Retorna: Una tupla de la forma {Host, [CantCPU, CantMem, CantGPU}

parsear_un_nodo(Nodo) ->%Nodo(string)
    [Host, Puerto, "cpu", CantCPU, "mem", CantMEM, "gpu", CantGPU] = string:tokens(Nodo, ":"),
    {Host ++ ":" ++ Puerto, [list_to_integer(CantCPU), list_to_integer(CantMEM), list_to_integer(CantGPU)]}.    

    
%A cada nodo que es un string lo transforma en una tupla de la forma {Host, [CantCPU, CantMem, CantGPU}, de esta forma arma una lista con list comprehension
% y finalmente transforma la lista en un mapa
% Recibe: ListNodos(lista de strings)
% Retorna: Un mapa de la forma {Nodo1 => [cantCPU, cantMEM, cantGPU], Nodo2 => [cantCPU, cantMEM, cantGPU], etc}
parsear_lista_nodos(ListNodos) -> %ListNodos(list de strings)
    maps:from_list([parsear_un_nodo(Nodo) || Nodo <- ListNodos]).


% Retorna: Lista de tuplas de la forma [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2}, etc] si pudo repartir el recurso        
repartir_entre_nodos(_Indice, 0, _Nodos) -> 
    [];

% SI sigue habiendo cantidad distinta de 0 y ya recorrio toda la lista entonces no alcanzó entre todos los nodos
% Retorna: {error, no alcanza} si no se puede repartir el recurso entre la cantidad q hay disponible entre los nodos
repartir_entre_nodos(_Indice, _CantidadRestante, []) ->
    {error, no_alcanza};

%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
% Recibe: Indice(int), CantidadRestante(int), [{Host, Recursos} | Resto](lista de tuplas)
repartir_entre_nodos(Indice, CantidadRestante, [{Host, Recursos} | Resto]) -> 
    Disponible = lists:nth(Indice, Recursos), %Busca con el dice el recurso en el nodo actual para ver su cantidad disponible
    case Disponible of
        0 -> %SI no tiene nada dispoible nos fijamos en el prox nodo(resto llama al prox nodo y de vuelta se divide entre primer elemento y resto la lista)
            repartir_entre_nodos(Indice, CantidadRestante, Resto);
        _ -> %SI hay cantidad para tomar
            Tomar = min(Disponible, CantidadRestante),%El minimo entre cant q queres o la cantidad disponible, para no sobrepasarte
            [{Host, Tomar} | repartir_entre_nodos(Indice, CantidadRestante - Tomar, Resto)]%Agrega el elem a la lista y llama recursivamente restando lo tomado de la cantidad
    end.    

%En el case busca en el mapa de nodos el primer nodo q tenga suficiente recurso segun el tipo de recurso pedido y su cant requerida
%%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
%Si encuentra en 1 devuelve {value, {Nodo, Recursos}, retornamos Nodo y Cantidad y sino tiene q buscar entre mas nodos para completar
% Recibe: %Recurso(string), Cantidad(int), MapNodos(map)
% Retorna: {error, no_alcanza} si no alcanzaron los nodos para la cantidad que requerias
% Retorna: Lista de tuplas de la forma [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2}, etc]
elegir_nodos(Recurso, Cantidad, MapNodos) -> 
    Nodos = maps:to_list(MapNodos), % convierte mapa en una lista d tuplas EJ :[{Nodo1, [CPU, MEM, GPU]}, {Nodo2, [CPU, MEM, GPU]}] etc
    Indice = case Recurso of % convierte el recurso pedido en un indice de la lista de recursos
        "cpu" -> 1;
        "mem" -> 2;
        "gpu" -> 3
    end,
    case lists:search(fun({_Host, Recursos}) -> %Busca el primer nodo q cumpla la condicion de q la cant del recurso del nodo sea mayorigual a la q necesitamos
        lists:nth(Indice, Recursos) >= Cantidad
    end,
     Nodos) of  %Si lo encontro devuelve {value, {Nodo, Recursos}, retornamos Nodo y Cantidad 
        {value, {Nodo, _}} -> 
            [{Nodo, Cantidad}]; %encontramos 1 solo nodo q cubre toda la cantidad requerida, devolvemos la Cantidad pq el recurso ya lo sabemos

        false -> %hay q pedir entre varios nodos
            repartir_entre_nodos(Indice, Cantidad, Nodos)%Si no lo encontro reetornamos error no hay nodo
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


% Remueve el prefijo "NODES " si está presente en el string
remover_prefijo_nodes("NODES " ++ Resto) -> Resto;
remover_prefijo_nodes(String) -> String.

%Una vez N es menor o igual a 0, envia un msg al cliente para que termine y cierra el socket
wait_jobs(N) -> 
    case N of
        N when N =< 0 ->
            receive
                {ok, Socket} ->
                    gen_tcp:close(Socket),
                    cliente_pid ! fin
            end;
        N when N > 0 ->  
            receive 
                {ok, _Socket} -> wait_jobs(N-1)
            end
    end.

%Funcion para esperar a que terminen los N jobs.
% Recibe N(int)
% wait_jobs(N) -> %N(int)
    
% Se intenta conectar al socket, si es exitosa, envia GET_NODES para consultar sobre los nodos activos, recibe una lista en binario de estos y crea el mapa con nodos y sus cantidades
% Recibe: Puerto(int)
% Retorna {ok, Socket, MapNodos} si fue exitoso, donde Socket(int), MapNodos(mapa donde key es el nodo y value una lista de 3 int donde cada cantidad pertenece a CPU, MEM, GPU en ese orden.)
% Retorna {error, Reason} si fallo al conectarse al socket.
conectar_y_obtener_nodos(Puerto) ->
    case gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}, {active, false}]) of 
        {ok, Socket} ->
            gen_tcp:send(Socket, <<"GET_NODES">>), 
            {ok, BinList} = gen_tcp:recv(Socket, 0),

            ListStr = binary_to_list(BinList),
            ListSinPrefijo = remover_prefijo_nodes(ListStr),

            List_nodos_separados = string:split(ListSinPrefijo, ";", all),
            MapNodos = parsear_lista_nodos(List_nodos_separados),
            {ok, Socket, MapNodos};

        {error, Reason} ->
            {error, Reason}
    end.

%Recibe la respuesta de la peticion del job enviado y maneja que hacer en cada caso, cuando termina un job, lo elimina de la tabla de Pendientes, registra su log y envia -
%- msg a wait_jobs avisando que termino.
% Recibe: JobID(string), Job(string), CantRecursos(int), Scoket(int), Msg_Release(string), JobTimeuot(int en milisegundos), Pid_wait_jobs(Pid).
% (!) _CantRecursos no se usa momentaneamente porque no tenemos buzon para volver a mandar ante timeout
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
                    Pid_wait_jobs ! {ok, Socket};

                "JOB_DENIED " ++ _Rest -> 
                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_DENIED"),
                    Pid_wait_jobs ! {ok, Socket};
                
                Invalido ->
                    io:format("Formato de mensaje no esperado por el handler: ~p~n", [Invalido]),
                    Pid_wait_jobs ! {ok, Socket}
            end
    after JobTimeout -> 
        borrarPendiente_and_registrarLog(JobID, Job, "POSIBLE DEADLOCK"),
        gen_tcp:send(Socket, list_to_binary(Msg_RELEASE))
        % volver mandar al buzon
        % pid_scheduler_job ! {JobID, Job, CantRecursos}
    end.

%Cada vez q recibe un job arma la peticion y crea un proceso (conectado al mismo agente) para q mande y espere la rta del job
% se llama recursivamente para seguir atendiendo jobs
% Eliminamos MapNodos de los argumentos iniciales
recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs) ->
    receive
         no_hay_mas_jobs -> 
            ok;

        {JobID, Job, CantRecursos} -> % Entra un nuevo Job
            
            % 1. Solicitamos el mapa de nodos actualizado AL MOMENTO para este Job especifico
            case get_map_nodes(Socket) of
                {ok, MapNodos} ->
                    % 2. Intentamos armar las peticiones con la foto actual de la red
                    case armar_peticiones(JobID, Job, CantRecursos, MapNodos) of
                        {error, no_alcanza} -> 
                            Pid_wait_jobs ! {ok, Socket};

                        {Msg_REQUEST, Msg_RELEASE} -> 
                            spawn(aux, handler_job, [JobID, Job, CantRecursos, JobTimeout, Pid_wait_jobs, Socket, Msg_REQUEST, Msg_RELEASE])       
                    end;
                
                {error, timeout_esperando_nodos} ->
                    io:format("[scheduler] Error: No se pudo obtener el mapa de nodos para el Job ~s~n", [JobID]),
                    Pid_wait_jobs ! {ok, Socket}
            end,
            
            % 3. Volvemos a iterar para esperar el proximo Job
            recibir_jobs_y_armar_peticiones(Socket, JobTimeout, Pid_wait_jobs)
    end.  

%manda el msg al agente espera su respuesta y la maneja
handler_job(JobID, Job, CantRecursos, JobTimeout, Pid_wait_jobs, Socket, Msg_REQUEST, Msg_RELEASE) ->
    gen_tcp:send(Socket, list_to_binary(Msg_REQUEST)),
    ets:insert(pendientes, {JobID, Job, self()}),
    procesar_respuesta(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Pid_wait_jobs).


% Reparte los tcp que llegan desde el agente C a los procesos erlang
tcp_deliver(Socket, JobTimeout) ->
    case gen_tcp:recv(Socket, 0) of
        {ok, Bin} -> 
            Str = binary_to_list(Bin),
            case Str of
                "NODES " ++ _Resto ->
                    % Alguien pidió los nodos actualizados. 
                    % Se los enviamos directo al proceso registrado pid_scheduler_job
                    case whereis(pid_scheduler_job) of
                        undefined -> 
                            io:format("[tcp_deliver] Alerta: Llego NODES pero pid_scheduler_job no esta registrado.~n");
                        PidScheduler -> 
                            PidScheduler ! {tcp_nodes, Bin}
                    end;

                _ ->
                    % Si no es un mensaje de NODES, asumimos que es un JOB_GRANTED/DENIED
                    case string:tokens(Str, " ") of
                        [_Comando, JobID | _Resto] ->
                            case ets:lookup(pendientes, JobID) of
                                [{JobID, _Job, PidHandler}] ->
                                    PidHandler ! {tcp_msg, Bin};
                                [] ->
                                    io:format("[tcp_deliver] Alerta: Llego respuesta para JobID ~s sin proceso.~n", [JobID])
                            end;
                        _ ->
                            io:format("[tcp_deliver] Paquete invalido: ~p~n", [Str])
                    end
            end,
            tcp_deliver(Socket, JobTimeout);

        {error, closed} -> io:format("[tcp_deliver] Conexion cerrada.~n");
        {error, Reason} -> io:format("[tcp_deliver] Error: ~p~n", [Reason])
    end.


% Crea el proceso scheduler_jobs, si este muere captura el error y se encarga de volver a levantarlo.
% Recibe: JobTimeout(int en milisegundos), Pid_wait_jobs(Pid), Puerto(int)
% No retorna nada, vive siempre mientras el sistema este corriendo
supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs, Puerto, Pid_caller) ->
    process_flag(trap_exit, true),
    Pid_scheduler_job = spawn_link(main, scheduler_jobs, [JobTimeout, Pid_wait_jobs, Puerto]),
    register(pid_scheduler_job, Pid_scheduler_job),
    Pid_caller ! scheduler_listo,
    receive 
        {'EXIT', _From, _Reason} ->
            io:format("[supervisor_scheduler_jobs] Me fui a mi casa chau ~n"),
            % Si el proceso murio entonces ya no esta regitrado, no hace falta unregister
            % unregister(pid_scheduler_job),
            supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs, Puerto, self())
    end.

% Devuelve el socket del agente
connect_agent(Puerto) ->
    case  gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}, {active, false}]) of %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
        {ok, Socket} -> {ok, Socket};
        {error, Reason} -> exit({error_al_conectar, Reason})
    end.

% Devuelve la maxima cantidad de recursos disponibles al momento
get_max_resources(Socket) ->
    % io:format("get_max_resources ~p ~n", [Socket]),
    gen_tcp:send(Socket, <<"GET_NODES">>), 
        case gen_tcp:recv(Socket, 0) of
            {ok, BinList} -> 
                ListSinPrefijo = remover_prefijo_nodes(binary_to_list(BinList)),
                List_nodos_separados = string:split(ListSinPrefijo, ";", all),
                ListMaximos = obtener_cant_maxima_recursos(List_nodos_separados, [0,0,0]),
                ListMaximos;
            {error, Reason} ->
                exit({error_al_recibir_get_nodes, Reason})
        end.

% Devuelve un mapa de nodos
get_map_nodes(Socket) ->
    % 1. Solicitamos la informacion al agente C de forma asincrona
    case gen_tcp:send(Socket, <<"GET_NODES">>) of 
        ok -> ok;
        {error, Reason} -> exit({error_send_get_map_nodes, Reason})
    end,

    % Espera mensaje de TCP deliver 
    receive 
        {tcp_nodes, BinList} -> 
            ListStr = binary_to_list(BinList),
            ListSinPrefijo = remover_prefijo_nodes(ListStr),
            List_nodos_separados = string:split(ListSinPrefijo, ";", all),
            MapNodos = parsear_lista_nodos(List_nodos_separados),
            {ok, MapNodos}
    after 5000 -> 
        % Timeout de seguridad por si el agente C se tilda o cae
        {error, timeout_esperando_nodos}
    end.

% Obtiene la lista de nodos activos, crea tabla de PENDIENTES, crea y LINKEA los procesos scheduler_job y wait_job 
% Recibe: N(cantidad de jobs a crear), Puerto(int)
% Retorna: ListMaximos(lista de 3 int, formada por la suma de la cantidad de ese recurso entre todos los nodos disponibles, donde el orden de las cantidades es CPU, MEM, GPU.)
inicializar_sistema(N, Puerto) -> 

    % Nos conectamos al agente de C
    {ok, Socket} = connect_agent(Puerto),
    
    % Obtenemos por unica vez la cantidad maxima de recursos 
    % (esto esta dudoso por el tema del tiempo y de la actualizacion de las tablas, quiza
    % podriamos llamar a una funcion get_resources_available() cada vez que estemos por generar
    % algo pero en realidad creo que esto va hacer race condition con el tcp_deliver y 
    % probablemente terminemos pidiendo recursos full random desconociendo la capacidad total
    ListMaximos = get_max_resources(Socket),

    % Creamos el proceso tcp deliver
    JobTimeout = 10000,
    spawn_link(fun() -> tcp_deliver(Socket, JobTimeout) end),

    ets:new(pendientes, [named_table, public, set]),
    Pid_wait_jobs = spawn_link(?MODULE, wait_jobs, [N]), 
    spawn_link(?MODULE, supervisor_scheduler_jobs, [JobTimeout, Pid_wait_jobs, Socket, self()]),
    receive
        scheduler_listo -> ok
    end,
    ListMaximos.