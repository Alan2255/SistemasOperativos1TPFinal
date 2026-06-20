-module(main).
-export([server/2, client/2, generate_jobs/2, scheduler_jobs/2, handler_job/5, wait_jobs/1, inicializar_sistema/1, supervisor_scheduler_jobs/2]).
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
generate_jobs(0, _ListMaximos) -> %% cuando N es 0, termina
        ok;

%Armas el job con el job id y la cantidd de recursos q vas a pedir, a que nodo se lo pedira lo manejara el scheduler
generate_jobs(N, ListMaximos) ->
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
            Cant_elegidas = eliminar_indice(Indice_ignorar, ListMaximos), %lo hacemos asi pq de otra maner apodrias tener misma cant y no saber cual eliminar

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
    generate_jobs(N-1, ListMaximos).
        

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


armar_msgs(JobID, Job, CantRecursos, MapNodos) ->
    Msg_REQUEST = handler_msgs(JobID, Job, CantRecursos, MapNodos),%Msg es el msg completo para enviar a C
    Msg_RELEASE = "JOB_RELEASE" ++ JobID,
    {Msg_REQUEST, Msg_RELEASE}.



%Con esto, cada job recibido tenemos una conex en simultaneo hablando con C
handler_job(JobID, Job, CantRecursos, JobTimeout, Pid_wait_jobs) ->
    case gen_tcp:connect("localhost", 8100, [binary, {packet, 2}]) of 
        {ok, Socket} ->

            gen_tcp:send(Socket, <<"GET_NODES\n">>), %consulto con el agente C, me respondera con una lista de nodos vivos en formato de texto, EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1 
            {ok, BinList} = gen_tcp:recv(Socket, 0),%por mas q diga lista lor recibo como un binario q luego transformo a string

            List_nodos_separados = string:split(binary_to_list(BinList), ";", all),% devuelve lista donde cada elem es un nodo con sus atributos
            MapNodos = parsear_lista_nodos(List_nodos_separados),
            {Msg_REQUEST, Msg_RELEASE} = armar_msgs(JobID, Job, CantRecursos, MapNodos),

            gen_tcp:send(Socket, <<Msg_REQUEST>>), %envia job request pidiendo recursos a C, en binary
            ets:insert(pendientes, {JobID, Job}), %agrego job pendiente a la tabla 
                
                case gen_tcp:recv(Socket, 0, JobTimeout) of %recibe la respuesta de C, TIMEOUT TODV No sabemos cuanto, pondriamos mas que C
                    {error, timeout} -> %aca fue job timeout, recibiste un recurso(o no) pero esperaste mucho para otro(o para tu primer) entonces dio error timeout la fun tcp rcv
                        borrarPendiente_and_registrarLog(JobID, Job, "POSIBLE DEADLOCK"),
                        gen_tcp:send(Socket, <<Msg_RELEASE>>), %mandamos release devolviendo ese job
                        pid_scheduler_job ! {JobID, Job, CantRecursos}; %lo mandamos d vuelta al buzon del receive para q desp intente d nuevo

                    {ok, Bin} -> %Si no dio error de timeout
                        case binary_to_list(Bin) of
                                "JOB_GRANTED " ++ _Rest -> %Si nos dieron los recursos, simulamos el job y devolvemos
                                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_GRANTED"),
                                    io:format("Simulando trabajo. . .~n"),
                                    timer:sleep(2000),
                                    io:format("Trabajo finalizado!.~n"),
                                    gen_tcp:send(Socket, <<Msg_RELEASE>>),%mandamos release devolviendo ese job
                                    Pid_wait_jobs ! {ok}; %avisamos q el job termino

                                "JOB_DENIED " ++ _Rest -> %Aca no nos dieron nada de recursos nos cancelaron de una, cancelamos el job(no hacemos nd)
                                    borrarPendiente_and_registrarLog(JobID, Job, "JOB_DENIED"),
                                    Pid_wait_jobs ! {ok},
                                    ok
                        end
                end,
            gen_tcp:close(Socket);
            {error, _Reason} ->
                Pid_wait_jobs ! {ok} %Avisamos q el job termino auque fue con error
    end.

%lleva la tabla de pendientes
% Recibe Lista donde cada elem es cada nodo, aca usamos spawn y no spawn_link pq si muere el handler debe seguir atendiendo otros jobs.
scheduler_jobs(JobTimeout, Pid_wait_jobs) -> %$Recibe jobs, analiza a que nodo pedirle cada recurso y este se lo manda al server
    receive
        {JobID, Job, CantRecursos} -> %Job = "recurso:cant:recurso:cant"
            spawn(?MODULE, handler_job, [JobID, Job, CantRecursos, JobTimeout,Pid_wait_jobs]), %Recibe el job y crea un proceso q lo maneje
            scheduler_jobs(JobTimeout, Pid_wait_jobs)%llama recursivamente scheduler para q siga recibiendo jobs

            %Si recibe recurso creado artificialmente, ver tdv
    end. 
    

server(Modo, N) ->
    Pid_client = spawn_link(?MODULE, client, [Modo, N]), %Si el client muere el server se entera
    register(cliente_pid, Pid_client).


wait_jobs(N) when N =< 0 ->
    cliente_pid ! fin;%Cuando terminan todos los jobs le mandamos msg avisando al cliente y ahora si puede finalizar.

wait_jobs(N) ->
    receive 
        _ ->
            wait_jobs(N-1)
        end.

%Se conecta al socket, obtiene la lista con los nodos disponibles si funciona bien o exit si da error
get_nodes_or_exit()-> %packet, 2 lo q hace es q en los primeros 2 bytes pone la longitud y en lo qsigue el msg
    case  gen_tcp:connect("localhost", 8100, [binary, {packet, 2}]) of %envio para conectarme al puerto 8100, si es exitosa devuelve ok socket
        {ok, Socket} ->
            gen_tcp:send(Socket, <<"GET_NODES\n">>), %consulto con el agente C respondera con una lista con los nodos disponoinbiles, necesito esto para armar listMax para generar los jobs
            % EJ: NODES 192.168.1.10:8100:cpu:4:mem:8192:gpu:1 ; 192.168.1.11:8101:cpu:2:mem:4096
            case gen_tcp:recv(Socket, 0) of
                {ok, BinList} -> 
                    gen_tcp:close(Socket),
                    {ok, BinList};
                {error, Reason} ->
                    gen_tcp:close(Socket),
                    exit({error_al_conectar, Reason}) %por mas q diga lista lor recibo como un binario q luego transformo a string
            end;
        {error, Reason} ->
            exit({error_al_conectar, Reason})
    end.


%Se encarga de volver a levantar el scheduler_jobs si muere
supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs) ->
    process_flag(trap_exit, true), %hace que la señales de salida q provengan de procesos linkeades no maten automaticamente al proceso sino que se transf en msg que llegan al mailbox
    Pid_scheduler_job = spawn_link(?MODULE, scheduler_jobs, [JobTimeout, Pid_wait_jobs]), %queda esperando jobs para enviar al sv en C
    register(pid_scheduler_job, Pid_scheduler_job), % lo registramos aca entonce ssi se cae lo volvemos a levantar y a registrar
    receive 
    {'EXIT', _From, _Reason} ->
        unregister(pid_scheduler_job),
        supervisor_scheduler_jobs(JobTimeout, Pid_wait_jobs) %"Busca esta funcion en el modulo actual" entonces cuando volves a compilar la busca la nueva compilacion"
    end.

%Obtiene la lista de nodos activos,crea tabla de PENDIENTES crea el proceso scheduler job y wait job para ver cuando terminar, retorna el pid de estos
inicializar_sistema(N) ->
    {ok, BinList} = get_nodes_or_exit(),
    List_nodos_separados = string:split(binary_to_list(BinList), ";", all),% devuelve lista donde cada elem es un nodo con sus atributos
    ListMaximos = obtener_cant_maxima_recursos(List_nodos_separados, [0,0,0]),
    JobTimeout = 5,
    %TABLA DE PENDIENTES: son los jobs q estan pendientes(fueron mandados y tdv no tienen rta), ets sierve para almacenar datos de forma compartida entre procesos
    ets:new(pendientes, [named_table, public, set]), %named table q la podemos llamar por su nombre, public cualq proceso puede acceder, set para q no repita
    Pid_wait_jobs = spawn_link(?MODULE, wait_jobs, [N]), %Creamos wait jobs para q cliente recien termine cuando terminen de ejecutarse todos los jobs y no teremine antes
    spawn_link(?MODULE, supervisor_scheduler_jobs, [JobTimeout, Pid_wait_jobs]),%Si se cae el scheduler job lo levanta, spawnlink para q el server se entere si muere el supervisor
    ListMaximos.

%Inicializa el sistema y manda a generar los N jobs y espera a q terminen 
client(Modo, N) ->
    ListMaximos = inicializar_sistema(N),
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
            %Terminara cuando el usuario mande pid_cliente ! fin o cuando ya generaste N jobs q le pasaste como parametro
            receive 
                fin ->  ok 
            end,
            ets:delete(pendientes)%liberamos la tabla d procesos pendientes pq ya terminamos
    end.



