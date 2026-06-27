-module(aux).
-export([delete_index/2, start_system/2, handler_job/7, wait_jobs/1, receive_jobs_and_create_request/4, supervisor_scheduler_jobs/4]).

%========= Funciones AUXILIARES  ===============$
%MapNodos : mapa donde key es el nodo y value lista con 3 enteros, donde cada entero representa en orden la cantidad de CPU, MEM, GPU

get_MaxResourcesCount([], ListMaximos) ->
    ListMaximos;

% Obtiene la cantidad maxima de cada recurso entre todos los nodos disponibles, sirve para armar los jobs sin que se pase del maximo general que puede obtener
% Recibe: [Nodo | Resto](Cada elemento es un string con el nodo y sus datos), ListMaximos(lista formada por 3 int)
% Si no puede hacer pattern matching sobre el nodo es pq esta mal formado termina el programa pq no podra funcionar.
% Retorna: ListMaximos(Lista de 3 int)
get_MaxResourcesCount([Nodo | Resto], ListMaximos) -> 
    List_recursos = string:tokens(Nodo, ":"), %devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, etc
    [MaxCPU, MaxMEM, MaxGPU] = ListMaximos,
    case List_recursos of 
        [_Host, _Puerto , "cpu", CantCPU,  "mem", CantMEM, "gpu", CantGPU] -> 
            SumaCPU = MaxCPU + list_to_integer(CantCPU),
            SumaMEM = MaxMEM + list_to_integer(CantMEM),
            SumaGPU = MaxGPU + list_to_integer(CantGPU),
            get_MaxResourcesCount(Resto, [SumaCPU, SumaMEM, SumaGPU]);

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
delete_index(N, Lista) ->
    {Izq, [_ | Der]} = lists:split(N - 1, Lista), 
    Izq ++ Der.  
    

% Recibe: Nodo(string)
parseNode(Nodo) ->%Nodo(string)
    [Host, Puerto, "cpu", CantCPU, "mem", CantMEM, "gpu", CantGPU] = string:tokens(Nodo, ":"),%devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem, 
    {Host ++ ":" ++ Puerto, [list_to_integer(CantCPU), list_to_integer(CantMEM), list_to_integer(CantGPU)]}.    
    % Retorna: Una tupla de la forma {Host:Puerto, [CantCPU, CantMem, CantGPU}

    
%A cada nodo que es un string lo transforma en una tupla de la forma {Host:Puerto, [CantCPU, CantMem, CantGPU}, de esta forma arma una lista con list comprehension
% y finalmente transforma la lista en un mapa
% Recibe: ListNodos(lista de strings)
parse_list_nodes(ListNodos) -> %ListNodos(list de strings)
    maps:from_list([parseNode(Nodo) || Nodo <- ListNodos]).
    %Retorna: Un map de la forma {Nodo1:Puerto => [cantCPU, cantMEM, cantGPU], Nodo2:Puerto => [cantCPU, cantMEM, cantGPU], etc}

% Retorna: Lista de tuplas de la forma [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2}, etc] si pudo repartir el recurso        
distributeResourcesToNodes(_Indice, 0, _Nodos) -> 
    [];

% Si sigue habiendo cantidad distinta de 0 y ya recorrio toda la lista entonces no alcanzó entre todos los nodos
% Retorna: {error, no alcanza} si no se puede repartir el recurso entre la cantidad q hay disponible entre los nodos
distributeResourcesToNodes(_Indice, _CantidadRestante, []) ->
    {error, no_alcanza};

% Retorna una lista con los nodos a los cuales pedirle recursos y cuanto le pide a cada uno
% Recibe: Indice(int), CantidadRestante(int), [{Host, Recursos} | Resto](lista de tuplas)
distributeResourcesToNodes(Indice, CantidadRestante, [{Host, Recursos} | Resto]) -> 
    Disponible = lists:nth(Indice, Recursos), %Busca con el indice el recurso en el nodo actual para ver su cantidad disponible
    case Disponible of
        0 -> %SI no tiene nada dispoible nos fijamos en el prox nodo(resto llama al prox nodo y de vuelta se divide entre primer elemento y resto la lista)
            distributeResourcesToNodes(Indice, CantidadRestante, Resto);
        _ -> %SI hay cantidad para tomar
            Tomar = min(Disponible, CantidadRestante),%El minimo entre cant q queres o la cantidad disponible, para no sobrepasarte
            [{Host, Tomar} | distributeResourcesToNodes(Indice, CantidadRestante - Tomar, Resto)]
    end.    %arma la lista con ese elem y para el resto d la lista llama recursivamente a la funcion

%En el case busca en el mapa de nodos el primer nodo q tenga suficiente recurso segun el tipo de recurso pedido y su cant requerida
%%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
%Si encuentra en 1 devuelve {value, {Nodo, Recursos}, retornamos Nodo y Cantidad y sino tiene q buscar entre mas nodos para completar
% Recibe: %Recurso(string), Cantidad(int), MapNodos(map)
% Retorna: {error, no_alcanza} si no alcanzaron los nodos para la cantidad que requerias
% Retorna: Lista de tuplas de la forma [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2}, etc]
choose_nodes(Recurso, Cantidad, MapNodos) -> 
    Nodos = maps:to_list(MapNodos), % convierte mapa en una lista d tuplas EJ :[{Nodo1:Puerto, [CPU, MEM, GPU]}, {Nodo2:Puerto, [CPU, MEM, GPU]}] etc
    Indice = case Recurso of % convierte el recurso pedido en un indice de la lista de recursos
        "cpu" -> 1;
        "mem" -> 2;
        "gpu" -> 3
    end,   %Busca el primer nodo q cumpla la condicion, le aplica esa funcion a cada elem de la lista Nodos.
    case lists:search(fun({_Host, Recursos}) -> lists:nth(Indice, Recursos) >= Cantidad end, Nodos) of  %Si lo encontro devuelve {value, {Nodo, Recursos}, retornamos Nodo y Cantidad 
        {value, {Nodo, _}} -> 
            [{Nodo, Cantidad}]; %encontramos 1 solo nodo q cubre toda la cantidad requerida, devolvemos la Cantidad pq el recurso ya lo sabemos

        false -> %hay q pedir entre varios nodos
            distributeResourcesToNodes(Indice, Cantidad, Nodos)%Si no lo encontro reetornamos error no hay nodo
    end.
    
%Funcion que devuelve el msg armado con el job de cuanto recurso le pedis a cada nodo, luego solo faltaria agregarle la peticion y el JobID.
% Recibe: Recurso(string), Cant(string), MapNodos(map)
% Retorna {error, no_alcanza} si no alcanzaron los nodos para la cantidad que requerias
% Retorna string Ej: "@Nodo:Recurso:Cant @Nodo:Recurso:Cant"
build_msg(Recurso, Cant, MapNodos) -> 
    case choose_nodes(Recurso, list_to_integer(Cant), MapNodos) of 
        {error, no_alcanza} -> 
            {error, no_alcanza};

        ListNodoCantidad -> %lista de tuplas [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2} 
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
            case build_msg(Recurso1, Cant1, MapNodos) of 
                {error, no_alcanza} ->  %Primero evaluamos el error, pq sino Msg al ser variable matchea cualquier cosa que llegue
                    {error, no_alcanza};
                Msg -> %Msg es un string por ej: "@Nodo:Recurso:Cant @Nodo:Recurso:Cant" etc. 
                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg,
                    Msg_final
            end;

        2 ->
            [Recurso1, Cant1, Recurso2, Cant2] = List_recursos,
            case build_msg(Recurso1, Cant1, MapNodos) of
                {error, no_alcanza} -> 
                    {error, no_alcanza};
                Msg1 -> 
                    case build_msg(Recurso2, Cant2, MapNodos) of
                        {error, no_alcanza} -> 
                            {error, no_alcanza};
                        Msg2 ->
                            Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ " " ++ Msg1 ++ " " ++ Msg2,
                            Msg_final
                    end
            end;

        3 ->
            [Recurso1, Cant1, Recurso2, Cant2, Recurso3, Cant3] = List_recursos,
            case build_msg(Recurso1, Cant1, MapNodos) of
                {error, no_alcanza} -> 
                    {error, no_alcanza};
                Msg1 ->
                    case build_msg(Recurso2, Cant2, MapNodos) of
                        {error, no_alcanza} -> 
                            {error, no_alcanza};
                        Msg2 ->
                            case build_msg(Recurso3, Cant3, MapNodos) of
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
logEvent(JobID, Job, Msg_resultado)-> 
    {{Y,M,D}, {H,Mi,S}} = calendar:local_time(), %obtiene la fecha 
    Linea = io_lib:format("~p-~p-~p ~p:~p:~p | Job ~p | ~s | ~s~n", [Y, M, D, H, Mi, S, JobID, Job, Msg_resultado]), %Devuelve un string para usarlo, a dif de io:format que imprime directo en la consola
    file:write_file("scheduler.log", Linea , [append]). %append para q no se borre lo anterior

%Borra el Job de la tabla de pendientes y registra su log.
%Recibe: %JobID(string), Job(string), Msg(String)
deletePendingJob_and_logEvent(JobID, Job, Msg) -> 
    ets:delete(pendientes, JobID), %lo elimino de la lista de pendientes
    logEvent(JobID, Job, Msg).

%Arma las peticiones que enviara al agente C.
% Recibe: JobID(string), Job(string), CantRecursos(int), MapNodos(mapa)
% Retorna {error, no alcanza} si la cantidad del recurso no se puede repartir entre ninguna cantidad de nodos disponibles
% Retorna {Msg_REQUEST(string), Msg_RELEASE(string} si la cantidad del recurso SI se pudo repartir entre la cantidad de los nodos disponibles
create_request(JobID, Job, CantRecursos, MapNodos) -> %%JobID(string), Job(string), CantRecursos(int), MapNodos(map)
    case handler_msgs(JobID, Job, CantRecursos, MapNodos) of %Devuelve el msg completo para enviar si puede y sino error.
        {error, no_alcanza} ->
            {error, no_alcanza};
         Msg_REQUEST ->
            Msg_RELEASE = "JOB_RELEASE" ++ " " ++ JobID, %Liberar recurso
            {Msg_REQUEST, Msg_RELEASE}
    end.


% Remueve el prefijo "NODES " si está presente en el string
remove_node_prefix("Nodes " ++ Resto) -> Resto;
remove_node_prefix("nodes " ++ Resto) -> Resto;
remove_node_prefix("NODES " ++ Resto) -> Resto;
remove_node_prefix(String) -> String.

%Una vez N es menor o igual a 0, envia un msg al cliente para que termine y cierra el socket
wait_jobs(N) when N =< 0 ->
    receive
        {ok, Socket} ->
            gen_tcp:close(Socket),%Cierra el socket pq ya terminaron todos los jobs
            cliente_pid ! fin
    end. 

%Funcion para esperar a que terminen los N jobs.
% Recibe N(int)
wait_jobs(N) -> %N(int)
    receive 
        {ok, _Socket} ->
            wait_jobs(N-1)
        end.
% Se intenta conectar al socket, si es exitosa, envia GET_NODES para consultar sobre los nodos activos, recibe una lista en binario de estos y crea el mapa con nodos y sus cantidades
% Recibe: Puerto(int)
% Retorna {ok, Socket, MapNodos} si fue exitoso, donde Socket(int), MapNodos(mapa donde key es el nodo y value una lista de 3 int donde cada cantidad pertenece a CPU, MEM, GPU en ese orden.)
% Retorna {error, Reason} si fallo al conectarse al socket.
connect_and_obtain_nodes(Puerto) ->
    case gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}, {active, false}]) of 
        {ok, Socket} ->
            gen_tcp:send(Socket, <<"GET_NODES">>), 
            {ok, BinList} = gen_tcp:recv(Socket, 0),

            ListStr = binary_to_list(BinList),
            ListSinPrefijo = remove_node_prefix(ListStr),

            List_nodos_separados = string:split(ListSinPrefijo, ";", all),
            MapNodos = parse_list_nodes(List_nodos_separados), %EJ: {Nodo1:Puerto => [cantCPU, cantMEM, cantGPU], Nodo2:Puerto => [cantCPU, cantMEM, cantGPU], etc}
            {ok, Socket, MapNodos};

        {error, Reason} ->
            {error, Reason}
    end.
simulate_job() ->
    io:format("Simulando trabajo. . .~n"),
    timer:sleep(2000),
    io:format("Trabajo finalizado!.~n").
%Recibe la respuesta de la peticion del job enviado y maneja que hacer en cada caso, cuando termina un job, lo elimina de la tabla de Pendientes, registra su log y envia -
%- msg a wait_jobs avisando que termino.
% Recibe: JobID(string), Job(string), CantRecursos(int), Scoket(int), Msg_Release(string), JobTimeuot(int en milisegundos), Pid_wait_jobs(Pid).
process_response(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Pid_wait_jobs) ->
    case gen_tcp:recv(Socket, 0, JobTimeout) of 
    
        {error, timeout} -> %aca fue job timeout, recibiste un recurso(o no) pero esperaste mucho para otro(o para tu primer) entonces dio error timeout la fun tcp rcv
            deletePendingJob_and_logEvent(JobID, Job, "POSIBLE DEADLOCK"),
            gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)), %mandamos release devolviendo ese job
            pid_scheduler_job ! {JobID, Job, CantRecursos}; %lo mandamos d vuelta al buzon del receive para q desp intente d nuevo
            %Aca no mandamos ok al wait jobs pq todavia no termino este job
            
        {ok, Bin} -> %Si no dio error de timeout
            case binary_to_list(Bin) of
                "JOB_GRANTED " ++ _Rest -> %Si nos dieron los recursos, simulamos el job y devolvemos
                    deletePendingJob_and_logEvent(JobID, Job, "JOB_GRANTED"),
                    simulate_job(),
                    gen_tcp:send(Socket, list_to_binary(Msg_RELEASE)),%mandamos release devolviendo ese job
                    Pid_wait_jobs ! {ok, Socket}; %avisamos q el job termino

                "JOB_DENIED " ++ _Rest -> %Nos cancelaron el job, lo borramos de la tabla de pendientes, registramos el log y avisamos que termino el job
                    deletePendingJob_and_logEvent(JobID, Job, "JOB_DENIED"),
                    Pid_wait_jobs ! {ok, Socket}
            end
    end.

%Cada vez q recibe un job arma la peticion y crea un proceso (conectado al mismo agente) para q mande y espere la rta del job
% se llama recursivamente para seguir atendiendo jobs
receive_jobs_and_create_request(Socket, MapNodos, JobTimeout, Pid_wait_jobs) ->%MapNodos:{Nodo1:Puerto => [cantCPU, cantMEM, cantGPU], Nodo2:Puerto => [cantCPU, cantMEM, cantGPU], etc}
    receive
         no_hay_mas_jobs -> 
            ok;

        {JobID, Job, CantRecursos} -> %Job = "recurso:cant:recurso:cant"
            case create_request(JobID, Job, CantRecursos, MapNodos) of
                {error, no_alcanza} -> %Si no pudimos armar las peticiones no alcanzaron los nodos disponibles para la cantidad requerida de algun recurso
                    Pid_wait_jobs ! {ok, Socket},

                {Msg_REQUEST, Msg_RELEASE} -> %Si pudimos armarlas, las enviamos al agente e insertamos en la lista de pendientes el job
                    spawn(aux, handler_job, [JobID, Job, CantRecursos, JobTimeout,Pid_wait_jobs, Socket, Msg_RELEASE]) %Recibe el job y crea un proceso q lo maneje,  LE PASAMOS MODULO AUX ESTA AHI LA FUN       
            end,
    receive_jobs_and_create_request(Socket, MapNodos, JobTimeout, Pid_wait_jobs)
    end.  

%manda el msg al agente espera su respuesta y la maneja
handler_job(JobID, Job, CantRecursos, JobTimeout, Pid_wait_jobs, Socket, Msg_RELEASE) ->
    gen_tcp:send(Socket, list_to_binary(Msg_REQUEST)),
    ets:insert(pendientes, {JobID, Job}),
    process_response(JobID, Job, CantRecursos, Socket, Msg_RELEASE, JobTimeout, Pid_wait_jobs).


% Se conecta al socket, obtiene la lista con los nodos disponibles si funciona bien o exit si da error ya que no podemos hacer nada si no obtenemos los nodos disponibles.
% Recibe: Puerto(int)
% Retorna {ok, BinList} en caso de conexion y recibimiento exitoso, donde BinList es una lista en binario de los nodos activos disponibles con sus cantidades.
% Retorna {error, Reason} en caso de conexion erronea, al conectarse o al hacer el recv
get_nodes_or_exit(Puerto)-> %packet, 2 lo q hace es q en los primeros 2 bytes pone la longitud y en lo qsigue el msg
    case  gen_tcp:connect("localhost", Puerto, [binary, {packet, 2}, {active, false}]) of %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
        {ok, Socket} ->
            gen_tcp:send(Socket, <<"GET_NODES">>), %consulto con el agente C respondera con una lista con los nodos disponoinbiles, necesito esto para armar listMax para generar los jobs
            % EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1 ; 192.168.1.11:8101:cpu:2:mem:4096
            case gen_tcp:recv(Socket, 0) of
                {ok, BinList} -> 
                    gen_tcp:close(Socket),
                    {ok, BinList};
                {error, Reason} ->
                    gen_tcp:close(Socket),
                    exit({error_al_recibir_get_nodes, Reason}) 
            end;
        {error, Reason} ->
            exit({error_al_conectar, Reason})
        
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
            unregister(pid_scheduler_job),
            supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs, Puerto, self())
    end.

% Obtiene la lista de nodos activos, crea tabla de PENDIENTES, crea y LINKEA los procesos scheduler_job y wait_job 
% Recibe: N(cantidad de jobs a crear), Puerto(int)
% Retorna: ListMaximos(lista de 3 int, formada por la suma de la cantidad de ese recurso entre todos los nodos disponibles, donde el orden de las cantidades es CPU, MEM, GPU.)
start_system(N, Puerto) -> 
    {ok, BinList} = get_nodes_or_exit(Puerto),
    
    % Usamos la nueva función acá también:
    ListSinPrefijo = remove_node_prefix(binary_to_list(BinList)),
    
    List_nodos_separados = string:split(ListSinPrefijo, ";", all),
    ListMaximos = get_MaxResourcesCount(List_nodos_separados, [0,0,0]),
    JobTimeout = 10000,
    ets:new(pendientes, [named_table, public, set]),
    Pid_wait_jobs = spawn_link(?MODULE, wait_jobs, [N]), 
    spawn_link(?MODULE, supervisor_scheduler_jobs, [JobTimeout, Pid_wait_jobs, Puerto, self()]),
    receive
        scheduler_listo -> ok %espera q se registre el scheduler asi le pueden mandar los jobs
    end,
    ListMaximos.