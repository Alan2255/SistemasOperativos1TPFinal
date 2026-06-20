-module(main).
-export([server/1, client/1, generate_jobs/3, scheduler_jobs/3, handler_job/7, wait_jobs/1]).
%nodos = host 

obtener_cant_maxima_recursos([], ListMaximos) ->
    ListMaximos;

%Cada elem de la lista es un nodo
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
            io:format("Nodo mal formado!~n"),
            throw(badmatch)
    end.


%Split devuelve: primera lista con los primeros k elem y la segunda lista el resto ej (3, [a,b,c,d]) devolvera
% [a,b,c] [d], como qres borrar el elemento N, haces N-1 para q el elem q qres borrar quede al inicio de la segunda lista
% entonces haces {izq, [ _ | Der]} q ignora el primer elemento y desp unis toda la lista ignorando ese elem
eliminar_indice(N, Lista) ->
    {Izq, [_ | Der]} = lists:split(N - 1, Lista), 
    Izq ++ Der.                                    

%generar trabajo el cual necesitara tantos recursos, ==== dentro de ask_for_resources =====
% %JobID comienza de 0, N es la cantidad de jobs q pedimos generar, es recursiva
%Una vez genere el job, le mandara un msg a ask_for_resources con el IDjob y el job para que se comunique con el agente C para pedir recursos
generate_jobs(_Pid, 0, _ListMaximos) -> %% cuando N es 0, termina
        ok;

%Armas el job con el job id y la cantidd de recursos q vas a pedir, a que nodo se lo pedira lo manejara el scheduler
generate_jobs(Pid, N, ListMaximos) ->
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
            Pid ! {JobID, Job, 1};

        2 ->    
            Indice_ignorar = rand:uniform(3),
            Recurso_ignorar = lists:nth(Indice_ignorar, ListRecursos),

            Recursos_elegidos = [R || R <- ListRecursos, R =/= Recurso_ignorar], %devuelve una lista sin el recurso ignorado
            Cant_elegidas = eliminar_indice(Indice_ignorar, ListMaximos), %lo hacemos asi pq de otra maner apodrias tener misma cant y no saber cual eliminar

            [Recurso1, Recurso2] = Recursos_elegidos,
            [Cant1, Cant2] = Cant_elegidas,
            Cantidad1 = integer_to_list(rand:uniform(Cant1)),
            Cantidad2 = integer_to_list(rand:uniform(Cant2)),
            
            Job = Recurso1 ++ ":" ++ Cantidad1 ++ ":" ++ Recurso2 ++ ":" ++ Cantidad2,
            Pid ! {JobID, Job, 2};

        3 ->
            Recurso1 = "cpu",
            Cantidad1 = integer_to_list(rand:uniform(MaxCPU)),%Cant random del recurso elegido de 1 hasta lo max q pueda pedir

            Recurso2 = "mem",
            Cantidad2 = integer_to_list(rand:uniform(MaxMEM)),

            Recurso3 = "gpu",
            Cantidad3 = integer_to_list(rand:uniform(MaxGPU)),

            Job = Recurso1 ++ ":" ++ Cantidad1 ++ ":" ++ Recurso2 ++ ":" ++ Cantidad2 ++ ":" ++  Recurso3 ++ ":" ++ Cantidad3,
            Pid ! {JobID, Job, 3}
        end,
    generate_jobs(Pid, N-1, ListMaximos).
        

%Recibe un nodo string con los datos y retorna una lista con nodo CANTCPU CANT MEM CANTGPU        
parsear_un_nodo(Nodo) ->   % string:toekns ":" , devuelve una lista con cda elem del nodo, ej [host, puerto, cpu, cntcpu, mem, cntmem,
    [Host, _Puerto, "cpu", CantCPU, "mem", CantMEM, "gpu", CantGPU] = string:tokens(Nodo, ":"),%luego aplicamos Patter Matching
    {Host, [list_to_integer(CantCPU), list_to_integer(CantMEM), list_to_integer(CantGPU)]}.

%aplica a cada nodo la funcion anterior, devuelve la lista con sublistas formadas por lo q devuelve la fun, para desp transformarlo en un mapa
% entonces podemos acceder a nodo tal y a la cant de su cpu,mem, gpu
parsear_lista_nodos(ListNodos) ->
    maps:from_list([parsear_un_nodo(Nodo) || Nodo <- ListNodos]).


%Si coinciden las 2 clausas por ej 0 y [] erlang toma la primera q concide asi q devolveria []
%Si ya llegamos 0, terminamos de asignar la cantidad, devuelve vacio, o sea q no agrega nada nuevo a la lista
repartir_entre_nodos(_Indice, 0, _Nodos) -> 
    [];

%SI sigue habiendo cantidad distinta de 0 y ya recorrio toda la lista entonces no alcanzó entre todos los nodos
repartir_entre_nodos(_Indice, _CantidadRestante, []) ->
    {error, no_alcanza};

%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
repartir_entre_nodos(Indice, CantidadRestante, [{Host, Recursos} | Resto]) ->
    Disponible = lists:nth(Indice, Recursos),
    case Disponible of
        0 -> %SI no tiene nada dispoible nos fijamos en el prox nodo(resto llama al prox nodo y de vuelta se divide entre primer elemento y resto la lista)
            repartir_entre_nodos(Indice, CantidadRestante, Resto);
        _ -> %SI hay cantidad para tomar
            Tomar = min(Disponible, CantidadRestante),%El minimo entre cant q queres o la cantidad disponible, para no sobrepasarte
            [{Host, Tomar} | repartir_entre_nodos(Indice, CantidadRestante - Tomar, Resto)]%Agrega el elem a la lista y llama recursivamente restando lo tomado de la cantidad
    end.    


%En el case busca en el mapa de nodos el primer nodo q tenga suficiente recurso segun el tipo de recurso pedido y su cant requerida
%%Devuelve una lista con los nodos a los cuales pedir y cuanto le pide a cada uno
%Si encuentra en 1 pasa lo de true y sino tiene q buscar entre mas nodos para completar
elegir_nodos(Recurso, Cantidad, MapNodos) ->
    Nodos = maps:to_list(MapNodos), %conviert mapa en una lista d tuplas EJ :[{Nodo1, [CPU, MEM, GPU]}, {Nodo2, [CPU, MEM, GPU]}] etc
    Indice = case Recurso of%convierte el recurso pedido en un indice de la lista de recursos
        "cpu" -> 1;
        "mem" -> 2;
        "gpu" -> 3
    end,
    case lists:search(fun({_Host, Recursos}) ->  %Busca el primer nodo q cumpla la condicion de q la cant dle rec del nodo sea mayorigual a la q necesitamos
        lists:nth(Indice, Recursos) >= list_to_integer(Cantidad) 
    end,
     Nodos) of  %Si lo encontro devuelve ok, y el nodo, reotrnamos eso 
        {value, {Nodo, _}} -> 
            [{Nodo, Cantidad}]; %encontramos 1 solo nodo q cubre toda la cantidad

        false -> %hay q pedir entre varios nodos
            repartir_entre_nodos(Indice, Cantidad, Nodos)%SI no lo encontro reetornamos error no hay nodo
    end.

%Recurso, Cant Son strings, luego para calcular transformamos Cant a int
% Te devuelve el msg armado con el job, solo faltaria agregarle el JobID.
armar_msg(Recurso, Cant, MapNodos) ->
    ListNodoCantidad = elegir_nodos(Recurso, list_to_integer(Cant), MapNodos), %devuelve la lista de tuplas [{Nodo1, CantidadTomada}, {Nodo2, CantidadTomada2} 
    ListPedidoPorNodo = ["@" ++ Nodo ++ ":" ++ Recurso ++ ":" ++ integer_to_list(Cantidad) || {Nodo, Cantidad} <- ListNodoCantidad ],%A cada elem de la lista, le aplica eso
    %Devuelve una lista con ["@Nodo:Recurso:Cant", "@Nodo:Recurso:Cant", etc]
    string:join(ListPedidoPorNodo, " "). %Arma un string donde separa cada elem de la lista con un " ", EJ: @Nodo:Recurso:Cant @Nodo:Recurso:Cant etc
  
    
handler_msgs(JobID, Job, CantRecursos, MapNodos) ->
            List_recursos = string:tokens(Job, ":"),%transforma el string job a ["recurso", "cant", "recurso", "cant"], max puede haber 3 elementos con el recurso y la cant q quiere.
            case CantRecursos of 
                1 ->
                    [Recurso1, Cant1] = List_recursos, %Sabemos q es un solo recurso, hacemos pattern matching con sus elementos
                    Msg = armar_msg(Recurso1, Cant1, MapNodos),

                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ Msg, %Ahora queda JobID @Nodo:Recurso:Cant @Nodo:Recurso:Cant etc
                    {JobID, Job, Msg_final};

                2 ->
                    [Recurso1, Cant1, Recurso2, Cant2] = List_recursos, 
                    Msg1 = armar_msg(Recurso1, Cant1, MapNodos), %Msg con el recurso que queremos, pedido entre el/los nodo/nodos disponibles
                    Msg2 = armar_msg(Recurso2, Cant2, MapNodos),

                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ Msg1 ++ Msg2,
                    {JobID, Job, Msg_final};

                3 ->
                    [Recurso1, Cant1, Recurso2, Cant2, Recurso3, Cant3] = List_recursos, 

                    Msg1 = armar_msg(Recurso1, Cant1, MapNodos), %Msg con el recurso que queremos, pedido entre el/los nodo/nodos disponibles
                    Msg2 = armar_msg(Recurso2, Cant2, MapNodos),
                    Msg3 = armar_msg(Recurso3, Cant3, MapNodos),

                    Msg_final = "JOB_REQUEST" ++ " " ++ JobID ++ Msg1 ++ Msg2 ++ Msg3,
                    {JobID, Job, Msg_final}
            end.

%Si no existe lo crea y sino escribe al final
registrar_log(JobID, Job, Msg_resultado)->
    {{Y,M,D}, {H,Mi,S}} = calendar:local_time(), %obtiene la fecha 
    Linea = io_lib:format("~p-~p-~p ~p:~p:~p | Job ~p | ~s | ~s~n", [Y, M, D, H, Mi, S, JobID, Job, Msg_resultado]), %Devuelve un string para usarlo, a dif de io:format que imprime directo en la consola
    file:write_file("scheduler.log", Linea , [append]). %append para q no se borre lo anterior

borrarPendiente_and_registrarLog(JobID, Job, Msg) ->
    ets:delete(pendientes, JobID), %lo elimino de la lista de pendientes
    registrar_log(JobID, Job, Msg).

%Con esto, cada job recibido tenemos una conex en simultaneo hablando con C
handler_job(JobID, Job, CantRecursos, MapNodos, JobTimeout, Pid_scheduler, Pid_wait_jobs) ->
    {ok, Socket} = gen_tcp:connect("localhost", 8100, [binary, {packet, 2}]),
    Msg_REQUEST = handler_msgs(JobID, Job, CantRecursos, MapNodos),%Msg es el msg completo para enviar a C
    Msg_RELEASE = "JOB_RELEASE" ++ JobID,
    gen_tcp:send(Socket, <<Msg_REQUEST>>), %envia job request pidiendo recursos a C, en binary
    ets:insert(pendientes, {JobID, Job}), %agrego job pendiente a la tabla 
        
        case gen_tcp:recv(Socket, 0, timeout) of %recibe la respuesta de C, TIMEOUT TODV No sabemos cuanto, pondriamos mas que C
            {error, timeout} -> %aca fue job timeout, recibiste un recurso(o no) pero esperaste mucho para otro(o para tu primer) entonces dio error timeout
                borrarPendiente_and_registrarLog(JobID, Job, "POSIBLE DEADLOCK"),
                gen_tcp:send(Socket, <<Msg_RELEASE>>), %mandamos release devolviendo ese job
                Pid_scheduler ! {JobID, Job, CantRecursos}; %lo mandamos d vuelta al buzon del receive para q desp intente d nuevo

            {ok, Bin} -> %Si no dio error de timeout
                case binary_to_list(Bin) of
                        "JOB_GRANTED " ++ _Rest -> %Si nos dieron los recursos, simulamos el job y devolvemos
                            borrarPendiente_and_registrarLog(JobID, Job, "JOB_GRANTED"),
                            io:format("Simulando trabajo. . .~n"),
                            timer:sleep(2000),
                            io:format("Trabajo finalizado!.~n"),
                            gen_tcp:send(Socket, <<Msg_RELEASE>>),
                            Pid_wait_jobs ! {ok}; %mandamos release devolviendo ese job

                        "JOB_DENIED " ++ _Rest -> %Aca no nos dieron nada de recursos nos cancelaron de una, cancelamos el job(no hacemos nd)
                            borrarPendiente_and_registrarLog(JobID, Job, "JOB_DENIED"),
                            Pid_wait_jobs ! {ok},
                            ok

                end

        end,

    gen_tcp:close(Socket).

%lleva la tabla de pendientes
% Recibe Lista donde cada elem es cada nodo
scheduler_jobs(JobTimeout, MapNodos, Pid_wait_jobs) -> %$Recibe jobs, analiza a que nodo pedirle cada recurso y este se lo manda al server
    receive
        {JobID, Job, CantRecursos} -> %Job = "recurso:cant:recurso:cant"
            spawn(?MODULE, handler_job, [JobID, Job, CantRecursos, MapNodos, JobTimeout, self(), Pid_wait_jobs]), %Recibe el job y crea un proceso q lo maneje
            scheduler_jobs(JobTimeout, MapNodos, Pid_wait_jobs)%llama recursivamente scheduler para q siga recibiendo jobs
    end. 
    

server(N) ->
    Pid_client = spawn_link(?MODULE, client, [N]),
    register(cliente_pid, Pid_client).


wait_jobs(N) when N =< 0 ->
    cliente_pid ! {fin};%Cuando terminan todos los jobs le mandamos msg avisando al cliente y ahora si puede finalizar.

wait_jobs(N) ->
    receive 
        _ ->
            wait_jobs(N-1)
        end.

%packet, 2 lo q hace es q en los primeros 2 bytes pone la longitud y en lo qsigue el msg
client(N) ->
    {ok, Socket} = gen_tcp:connect("localhost", 8100, [binary, {packet, 2}]), %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
    gen_tcp:send(Socket, <<"GET_NODES\n">>), %consulto con el agente C sobre las lista de nodos q hay disponibles
    %me respondera con una lista de nodos vivos en formato de texto 
    % EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1 ; 192.168.1.11:8101:cpu:2:mem:4096
    {ok, BinList} = gen_tcp:recv(Socket, 0),%por mas q diga lista lor recibo como un binario q luego transformo a string
    gen_tcp:close(Socket),
    List_nodos_separados = string:split(binary_to_list(BinList), ";", all),% devuelve lista donde cada elem es un nodo con sus atributos
    ListMaximos = obtener_cant_maxima_recursos(List_nodos_separados, [0,0,0]),
    MapNodos = parsear_lista_nodos(List_nodos_separados),
    JobTimeout = 3,
    %TABLA DE PENDIENTES: son los jobs q estan pendientes(fueron mandados y tdv no tienen rta), ets sierve para almacenar datos de forma compartida entre procesos
    ets:new(pendientes, [named_table, public, set]), %named table q la podemos llamar por su nombre, public cualq proceso puede acceder, set para q no repita
    Pid_wait_jobs = spawn_link(?MODULE, wait_jobs, [N]), %Creamos wait jobs para q cliente recien termine cuando terminen de ejecutarse todos los jobs y no teremine antes
    Pid_scheduler_job = spawn_link(?MODULE, scheduler_jobs, [JobTimeout, MapNodos, Pid_wait_jobs]),%queda esperando jobs para enviar al sv en C
    generate_jobs(Pid_scheduler_job , N, ListMaximos),%generara N jobs q se los enviara a scheduler de jobs
    receive 
        {fin} -> %Cuando terminan todos los jobs le mandamos msg avisando al cliente y ahora si puede finalizar.
            ok
    end,
    ets:delete(pendientes).%liberamos la tabla d procesos pendientes pq ya terminamos



